/**
 * 摩柿小说 —— 网络请求封装（统一 direct / bridge 双通道）
 * 统一注入鉴权头、统一错误处理；返回 Promise，页面层以 .then/.catch 使用。
 *
 * 通道选择（config.transport）：
 *   - 'direct'：本机 @system.fetch（官方支持明细里手环 9 Pro 标「不支持」）
 *   - 'bridge'：经 ESP32 网桥（bridge.js，FetchBridge 协议）代为联网
 * 上层 request()/getText() 与业务代码完全无感。
 */
import fetch from '@system.fetch'
import { request as bridgeRequest } from './bridge.js'
import config from './config.js'
import { getToken } from './session.js'

/** 按配置拼装请求头；auth=true 时附带 Bearer 令牌 */
function buildHeader(auth) {
  const header = { 'Content-Type': 'application/json' }
  if (auth) {
    const token = getToken()
    if (token) {
      // 待核实：鉴权头名与前缀均在 config.auth 中，按实测改
      header[config.auth.header] = config.auth.scheme + ' ' + token
    }
  }
  return header
}

/** 按点路径从对象取值，如 'data.token'；path 为空则返回 obj 本身 */
export function pickByPath(obj, path) {
  if (!path) return obj
  return path.split('.').reduce((acc, key) => (acc == null ? undefined : acc[key]), obj)
}

/**
 * 底层收发：统一返回 { code, headers, data }。
 * data 在 direct+json 模式下是已解析对象，其余为字符串。
 */
function sendRaw({ url, method, headers, body, asJson }) {
  if (config.transport === 'bridge') {
    // 网桥通道：bridge.js 已完成握手/分片/解码，body 是 UTF-8 字符串
    return bridgeRequest({ url: url, method: method, headers: headers, body: body || '' })
      .then((r) => {
        let data = r.body
        if (asJson && typeof data === 'string' && data) {
          try {
            data = JSON.parse(data)
          } catch (e) {
            // 非 JSON 时保留原字符串
          }
        }
        return { code: r.status, headers: r.headers, data: data }
      })
      .catch((err) => {
        // 网桥整包 body 超限（固件 BR_HTTP_MAX_BODY，约128KB）→ 清晰中文提示。
        // 注意：此上限仅作用于 bridge 路径；direct 路径不受此限（direct 受本机 @system.fetch 可用性限制）。
        if (err && err.isOversize) {
          throw new Error((config.bridge && config.bridge.oversizeTip) || '文件过大，超出网桥整包下载上限')
        }
        throw err
      })
  }
  // 直连通道
  return new Promise((resolve, reject) => {
    fetch.fetch({
      url: url,
      method: method,
      data: body,
      header: headers,
      responseType: asJson ? 'json' : 'text',
      success: (res) => resolve({ code: res.code, headers: res.headers, data: res.data }),
      fail: (err, code) => reject(new Error('网络请求失败 code=' + code + ' msg=' + err))
    })
  })
}

/**
 * 通用 JSON 请求
 * @param {string} path 接口路径（相对 apiBase）或完整 http(s) URL
 * @param {object} opts { method, data(对象), auth }
 * @returns {Promise<any>} 解析后的 JSON 对象
 */
export function request(path, opts) {
  const options = opts || {}
  const method = options.method || 'GET'
  const auth = options.auth !== false
  const url = /^https?:\/\//.test(path) ? path : config.apiBase + path
  const body = options.data == null ? undefined : JSON.stringify(options.data)

  return sendRaw({ url, method, headers: buildHeader(auth), body, asJson: true })
    .then((res) => {
      if (res.code >= 200 && res.code < 300) return res.data
      throw new Error('HTTP ' + res.code)
    })
}

/**
 * 拉取纯文本资源（用于下载 TXT）。
 * @param {string} url 下载地址：完整 http(s) URL，或相对路径（如 /dl/xxx.txt）。
 *        §8.5：download_url 实测为相对路径 /dl/<编码路径>，此处自动按 apiBase 拼接。
 * @returns {Promise<string>} TXT 文本内容
 */
export function getText(url) {
  // 相对路径统一解析为「基址 + 路径」，兼容 /dl/... 与 /downloads/... 两种形态
  const fullUrl = /^https?:\/\//.test(url) ? url : config.apiBase + url
  return sendRaw({ url: fullUrl, method: 'GET', headers: buildHeader(true), body: undefined, asJson: false })
    .then((res) => {
      if (res.code >= 200 && res.code < 300) {
        return typeof res.data === 'string' ? res.data : String(res.data || '')
      }
      throw new Error('HTTP ' + res.code)
    })
}
