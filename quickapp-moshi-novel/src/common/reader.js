/**
 * 摩柿小说 —— 离线阅读器分页工具（纯函数，便于校验与复用）
 * 按固定字符数切分 TXT，支持上/下翻页、页码/总页数计算。
 */

/**
 * 把全文按 charsPerPage 切分为页数组
 * @param {string} text 全文
 * @param {number} charsPerPage 每页字符数
 * @returns {string[]} 页内容数组（至少返回一页）
 */
export function splitPages(text, charsPerPage) {
  const size = Math.max(1, charsPerPage || 300)
  const src = text || ''
  if (src.length === 0) return ['']
  const pages = []
  for (let i = 0; i < src.length; i += size) {
    pages.push(src.slice(i, i + size))
  }
  return pages.length ? pages : ['']
}

/** 计算总页数（至少 1 页） */
export function calcTotalPages(textLength, charsPerPage) {
  const size = Math.max(1, charsPerPage || 300)
  return Math.max(1, Math.ceil(textLength / size))
}

/** 限制页码在 [0, totalPages-1] 范围内 */
export function clampPage(page, totalPages) {
  const total = Math.max(1, totalPages)
  const p = page || 0
  if (p < 0) return 0
  if (p > total - 1) return total - 1
  return p
}

/** 字节数格式化：B / KB / MB */
export function formatSize(bytes) {
  const n = Number(bytes) || 0
  if (n < 1024) return n + 'B'
  if (n < 1024 * 1024) return (n / 1024).toFixed(1) + 'KB'
  return (n / 1024 / 1024).toFixed(2) + 'MB'
}
