/**
 * 摩柿小说 —— 本地书库（已下载到手环的书籍元信息管理）
 * 元信息以 JSON 字符串存于 @system.storage；TXT 正文落盘在 internal://files。
 */
import storage from '@system.storage'
import file from '@system.file'
import config from './config.js'

/** 计算某本书本地 TXT 的 URI */
export function bookFileUri(bookId) {
  return config.files.booksDir + bookId + '.txt'
}

/** 读取本地已下载书籍列表，Promise<Array> */
export function loadLocalBooks() {
  return new Promise((resolve) => {
    storage.get({
      key: config.storageKeys.shelfMeta,
      success: (raw) => {
        try {
          const list = JSON.parse(raw || '[]')
          resolve(Array.isArray(list) ? list : [])
        } catch (e) {
          resolve([])
        }
      },
      fail: () => resolve([])
    })
  })
}

/** 整体写回本地书库 */
function saveLocalBooks(list) {
  return new Promise((resolve, reject) => {
    storage.set({
      key: config.storageKeys.shelfMeta,
      value: JSON.stringify(list),
      success: () => resolve(list),
      fail: (err, code) => reject(new Error('保存书库失败 code=' + code))
    })
  })
}

/**
 * 新增或更新一本已下载书籍的元信息
 * @param {object} meta { bookId, bookName, author, size, progressPage, updatedAt }
 */
export async function upsertBook(meta) {
  const list = await loadLocalBooks()
  const idx = list.findIndex((b) => b.bookId === meta.bookId)
  const record = {
    bookId: meta.bookId,
    bookName: meta.bookName || '',
    author: meta.author || '',
    size: meta.size || 0,
    progressPage: meta.progressPage || 0,
    updatedAt: meta.updatedAt || Date.now()
  }
  if (idx >= 0) {
    // 保留既有阅读进度，除非显式传入
    record.progressPage = meta.progressPage != null ? meta.progressPage : list[idx].progressPage
    list[idx] = record
  } else {
    list.push(record)
  }
  await saveLocalBooks(list)
  return record
}

/** 更新某本书的阅读进度（页码） */
export async function updateProgress(bookId, page) {
  const list = await loadLocalBooks()
  const item = list.find((b) => b.bookId === bookId)
  if (item) {
    item.progressPage = page
    await saveLocalBooks(list)
  }
}

/** 查询单本 */
export async function findBook(bookId) {
  const list = await loadLocalBooks()
  return list.find((b) => b.bookId === bookId) || null
}

/** 删除一本：先删本地 TXT 文件，再删元信息 */
export async function removeBook(bookId) {
  const uri = bookFileUri(bookId)
  // 文件删除失败（可能本就不存在）不阻断元信息清理
  await new Promise((resolve) => {
    file.delete({
      uri: uri,
      success: () => resolve(),
      fail: () => resolve()
    })
  })
  const list = await loadLocalBooks()
  const next = list.filter((b) => b.bookId !== bookId)
  await saveLocalBooks(next)
  return next
}
