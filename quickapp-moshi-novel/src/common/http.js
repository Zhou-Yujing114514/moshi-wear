/**
 * 摩柿小说 —— 网络请求封装（基于 @system.fetch）
 * 统一注入鉴权头、统一错误处理；返回 Promise，页面层以 .then/.catch 使用。
 *
 * 注意：官方支持明细中，小米手环 9 Pro 对 @system.fetch 标注为「不支持」，
 *       本模块按文档 API 编写，真实可用性需真机验证（见 README 风险清单）。
 */
import fetch from '@system.fetch'
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

  return new Promise((resolve, reject) => {
    fetch.fetch({
      url: url,
      method: method,
      data: body,
      header: buildHeader(auth),
      responseType: 'json',
      success: (res) => {
        if (res.code >= 200 && res.code < 300) {
          resolve(res.data)
        } else {
          reject(new Error('HTTP ' + res.code))
        }
      },
      fail: (err, code) => {
        reject(new Error('网络请求失败 code=' + code + ' msg=' + err))
      }
    })
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
  return new Promise((resolve, reject) => {
    fetch.fetch({
      url: fullUrl,
      method: 'GET',
      header: buildHeader(true),
      responseType: 'text',
      success: (res) => {
        if (res.code >= 200 && res.code < 300) {
          resolve(typeof res.data === 'string' ? res.data : String(res.data || ''))
        } else {
          reject(new Error('HTTP ' + res.code))
        }
      },
      fail: (err, code) => {
        reject(new Error('下载失败 code=' + code + ' msg=' + err))
      }
    })
  })
}
