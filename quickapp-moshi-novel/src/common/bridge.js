/**
 * 摩柿小说 —— ESP32 网桥通道适配层（FetchBridge over interconnect）
 * ----------------------------------------------------------------------------
 * 背景：官方支持明细里「小米手环 9 Pro」对 @system.fetch 标注为「不支持」。
 * 本层把 HTTP 请求包装成 FetchBridge JSON 信封，经互联通道发给 ESP32 网桥，
 * 由网桥代为发 HTTP，再把响应按同一协议解回来。与固件 esp32-firmware/main/qaic.c
 * 的收发格式逐字段对齐（见 shared/vela-interconnect-protocol-notes.md §3~§9）。
 *
 * 固定接口签名：
 *   request(opts) -> Promise<{ status, headers, body }>
 *     opts: { url, method, headers, body, raw }
 *     返回: status(number)、headers(object)、body(string，已 UTF-8 解码)
 *
 * 与固件契约（qaic.c 实测）：
 *   - 握手：{"tag":"__hs__","count":0,"caps":{...}} → 收 count:1 → 回 count:2 → 完成
 *   - 请求：{"tag":"fetch","id":"..","url":"..","options":{method,headers,body,raw,followRedirects}}
 *   - 单消息响应：{"tag":"fetch","id":"..","resp":{ok,status,statusText,headers,body,raw,bodyEncoding}}
 *   - 分片响应：resp.chunked=true → 收若干 {"tag":"fetch-chunk","id","seq","total","data"}
 *     每收一片按序回 {"tag":"fetch-ack","id","ack":<下一个缺失连续序号>}
 *
 * 本端能力声明（只声明自己能解的，便于协商走最简路径）：
 *   支持 text/base64；压缩只认 none（deflate/lz4 未在手环侧实现，待真机）。
 * ----------------------------------------------------------------------------
 */
import config from './config.js'

// 本端 caps（§3.3）：声明 chunk/ack，但压缩只支持 none、流(stream)暂不启用
const LOCAL_CAPS = {
  version: 4,
  chunk: true,
  maxChunkSize: 4096,
  encodings: ['text', 'base64'],
  compressions: ['none'],
  ack: true,
  ackWindow: 4,
  stream: false
}

/* ============================================================================
 * 发送 / 接收原语（收敛于此，仅这两个函数与真实互联 API 耦合）
 * ----------------------------------------------------------------------------
 * 【待真机确证】Vela 手环侧 @system.interconnect 的确切模块名与调用形式
 * （send/onMessage/open 等）无法从公开文档确证（见 README「待真机确证」）。
 * 真机联调时只需改写下面两个函数的函数体，上层协议逻辑无需任何改动。
 * 候选接入点（勿直接假定，需真机对照 SDK 头文件/类型定义确认）：
 *   - sendToBridge：用互联 API 把 JSON 字符串发给网桥，例如
 *       interconnect.send({ pkgName, payloadText: text })  // 形式待定
 *   - onBridgeMessage：注册互联 API 的接收回调，收到 JSON 文本后调
 *       _dispatchIncoming(text)                            // 形式待定
 * ==========================================================================*/
function sendToBridge(msgObj) {
  const text = JSON.stringify(msgObj)
  // TODO(待真机确证): 替换为真实互联发送调用；下面仅占位打印
  console.info('[bridge:待真机确证] send -> ' + text)
}

let bridgeListener = null
function onBridgeMessage(cb) {
  bridgeListener = cb
  // TODO(待真机确证): 注册真实互联接收回调，收到 JSON 文本后调 cb(JSON.parse(text))
}

/** 真机原语收到 JSON 文本后，调用本入口把它分发给协议状态机 */
export function _dispatchIncoming(text) {
  let msg
  try {
    msg = JSON.parse(text)
  } catch (e) {
    return
  }
  if (bridgeListener) bridgeListener(msg)
}

/* ================= 纯 JS 编解码（无第三方代码） ================= */
// base64(RFC4648) -> 字节数组；与固件 codec.c 的 base64 字母表一致
function base64ToBytes(b64) {
  const T = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/'
  const s = String(b64).replace(/=+$/, '')
  const out = []
  let bits = 0
  let bitLen = 0
  for (let i = 0; i < s.length; i++) {
    const v = T.indexOf(s[i])
    if (v < 0) continue
    bits = (bits << 6) | v
    bitLen += 6
    if (bitLen >= 8) {
      bitLen -= 8
      out.push((bits >> bitLen) & 0xff)
    }
  }
  return out
}

// 字节数组 -> UTF-8 字符串
function bytesToUtf8(bytes) {
  let str = ''
  let i = 0
  while (i < bytes.length) {
    const b = bytes[i++]
    let code
    if (b < 0x80) {
      code = b
    } else if (b < 0xE0) {
      code = ((b & 0x1F) << 6) | (bytes[i++] & 0x3F)
    } else if (b < 0xF0) {
      code = ((b & 0x0F) << 12) | ((bytes[i++] & 0x3F) << 6) | (bytes[i++] & 0x3F)
    } else {
      code = ((b & 0x07) << 18) | ((bytes[i++] & 0x3F) << 12) | ((bytes[i++] & 0x3F) << 6) | (bytes[i++] & 0x3F)
    }
    str += String.fromCodePoint(code)
  }
  return str
}

/** 按 bodyEncoding 把响应体字符串解码为 UTF-8 文本 */
function decodeBody(encoding, body, compression, raw) {
  // 固件对纯文本小响应默认 bodyEncoding=text（或缺省），body 即 UTF-8 原文
  const enc = encoding || 'text'
  if (enc === 'text' || enc === '') {
    return String(body || '')
  }
  if (enc === 'base64') {
    const bytes = base64ToBytes(body || '')
    if (compression && compression !== 'none') {
      // 待真机：deflate/lz4 解压未在手环侧实现（固件当前也回退为 none）
      return ''
    }
    return raw ? String(body || '') : bytesToUtf8(bytes)
  }
  if (enc === 'hex') {
    const s = String(body || '')
    const bytes = []
    for (let i = 0; i + 1 < s.length; i += 2) {
      bytes.push(parseInt(s.substr(i, 2), 16))
    }
    return raw ? String(body || '') : bytesToUtf8(bytes)
  }
  return String(body || '')
}

/* ================= 协议状态机 ================= */
let nextId = 1
const pending = {}       // id -> { resolve, reject, timer, resp }
const chunkBuffers = {}   // id -> { expected, got, chunks, encoding, resp }

let handshakeDone = false
let handshakeWaiters = []

function sendHandshake(count) {
  sendToBridge({ tag: '__hs__', count: count, caps: LOCAL_CAPS })
}

/** 确保握手完成；幂等 */
function ensureHandshake() {
  return new Promise((resolve, reject) => {
    if (handshakeDone) {
      resolve()
      return
    }
    handshakeWaiters.push({ resolve, reject })
    if (handshakeWaiters.length === 1) {
      sendHandshake(0)
      setTimeout(() => {
        const ws = handshakeWaiters
        handshakeWaiters = []
        handshakeDone = false
        ws.forEach((w) => w.reject(new Error('网桥握手超时')))
      }, config.limits.networkTimeoutMs)
    }
  })
}

/** 握手帧处理 */
function onHandshake(msg) {
  const count = msg.count || 0
  if (count > 0) {
    // 收到对端 count：按 §3.1，<2 则回 count+1；收到 1 后回 2 即视为完成
    sendHandshake(count + 1)
  }
  if (count >= 1 && !handshakeDone) {
    handshakeDone = true
    const ws = handshakeWaiters
    handshakeWaiters = []
    ws.forEach((w) => w.resolve())
  }
}

/** 单消息响应完成 */
function finishFetch(id, bodyStr) {
  const p = pending[id]
  if (!p) return
  clearTimeout(p.timer)
  delete pending[id]
  p.resolve({
    status: (p.resp && p.resp.status) || 0,
    headers: (p.resp && p.resp.headers) || {},
    body: bodyStr
  })
}

/** 错误帧完成：以 reject 结束，保留 err.bridgeError / err.isOversize 供上层映射文案 */
function finishFetchError(id, err) {
  const p = pending[id]
  if (!p) return
  clearTimeout(p.timer)
  delete pending[id]
  p.reject(err)
}

/** fetch 响应头（可能是单消息，也可能是分片头） */
function onFetchResponse(msg) {
  const id = msg.id
  const resp = msg.resp || {}
  const p = pending[id]
  if (!p) return
  p.resp = resp

  if (resp.chunked) {
    // 初始化分片缓冲；立即回 ack=0（下一个需要 seq=0）
    chunkBuffers[id] = {
      expected: resp.chunkCount || 0,
      chunks: {},
      encoding: resp.bodyEncoding || 'base64',
      compression: resp.compression || 'none',
      resp: resp
    }
    sendToBridge({ tag: 'fetch-ack', id: id, ack: 0 })
    return
  }

  // 固件错误帧：qaic.c send_error 会回 { ok:false, status:0, statusText:<msg> }
  // （含新增的整包 body 超限：BR_HTTP_MAX_BODY，约 128KB，超限回错误帧）。
  if (resp.ok === false) {
    const msg = resp.statusText || 'bridge error'
    const err = new Error(msg)
    err.bridgeError = true
    err.isOversize = isOversizeMessage(msg)
    finishFetchError(id, err)
    return
  }

  const body = decodeBody(resp.bodyEncoding, resp.body, resp.compression, resp.raw)
  finishFetch(id, body)
}

/** 识别固件是否报「整包下载 body 超限」（按关键字，待真机确认确切 statusText 文案） */
function isOversizeMessage(msg) {
  const m = String(msg || '').toLowerCase()
  const kws = (config.bridge && config.bridge.oversizeKeywords) || []
  return kws.some((k) => m.indexOf(String(k).toLowerCase()) >= 0)
}

/** fetch-chunk 分片数据帧 */
function onFetchChunk(msg) {
  const id = msg.id
  const buf = chunkBuffers[id]
  if (!buf) return
  buf.chunks[msg.seq] = msg.data

  // 计算「下一个缺失连续序号」作为 ack（乱序缓存，空洞补上后一次性前移）
  let ack = 0
  while (ack < buf.expected && buf.chunks[ack] != null) ack++
  sendToBridge({ tag: 'fetch-ack', id: id, ack: ack })

  if (ack >= buf.expected) {
    // 收齐：逐片解码 base64 -> 字节 -> 拼接 -> UTF-8（固件压缩恒为 none）
    const allBytes = []
    for (let i = 0; i < buf.expected; i++) {
      const part = base64ToBytes(buf.chunks[i] || '')
      for (let k = 0; k < part.length; k++) allBytes.push(part[k])
    }
    delete chunkBuffers[id]
    finishFetch(id, bytesToUtf8(allBytes))
  }
}

/** 协议消息总分发（真机收到 JSON 后由 _dispatchIncoming 触发） */
function handleMessage(msg) {
  if (!msg || !msg.tag) return
  if (msg.tag === '__hs__') onHandshake(msg)
  else if (msg.tag === 'fetch') onFetchResponse(msg)
  else if (msg.tag === 'fetch-chunk') onFetchChunk(msg)
  // fetch-stream*（在线音视频流）本小说用不到，忽略
}

// 注册接收分发
onBridgeMessage(handleMessage)

/* ================= 对外主接口 ================= */
/**
 * 经网桥发起一次 HTTP 请求。
 * @param {object} opts { url, method, headers, body, raw }
 * @returns {Promise<{status, headers, body}>}
 */
export function request(opts) {
  return ensureHandshake().then(() => {
    const id = String(nextId++)
    const envelope = {
      tag: 'fetch',
      id: id,
      url: opts.url,
      options: {
        method: (opts.method || 'GET').toUpperCase(),
        headers: opts.headers || {},
        body: opts.body || '',
        raw: !!opts.raw,
        followRedirects: true
      }
    }
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        delete pending[id]
        delete chunkBuffers[id]
        reject(new Error('网桥请求超时'))
      }, config.limits.networkTimeoutMs)
      pending[id] = { resolve, reject, timer, resp: null }
      sendToBridge(envelope)
    })
  })
}
