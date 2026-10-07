/**
 * 摩柿小说 —— 下载 TXT → 弦电子书阅读引擎 适配层（importBook）
 * ----------------------------------------------------------------------------
 * 摩柿快应用的数据流是「主动经 ESP32 网桥 / 直连 HTTP 访问摩柿书源：登录 → 书架 /
 * 搜索 → 下载整本 TXT 落盘 → 离线阅读」。而移植自「弦电子书」的阅读引擎
 * （chapterManager / bookStorage / detail 阅读器）期望一套由安卓同步器生成的
 * 目录结构（内容为 UTF-16LE）。本模块就是二者之间的桥：
 *
 *   下载到的 UTF-8 TXT 文本
 *        │  ① 正则识别章节标题
 *        │  ② 逐章写 content/<idx>.txt（UTF-16LE）
 *        │  ③ 写 indexes/<chunk>.txt（章节索引）
 *        │  ④ 写 lindex.txt（章节总数 / 已同步数 / 分块范围）
 *        │  ⑤ 写 book_info.json
 *        │  ⑥ 生成并写入摩柿封面 cover.png
 *        ▼
 *   internal://files/books/<dirName>/  +  bookshelf.json
 *
 * 目录/文件格式与「弦电子书」utils/interconnfile.js 逐字段对齐，因此移植来的
 * 阅读器、章节列表、书签、阅读时长等页面无需改动即可工作。
 *
 * 对外接口：
 *   importBook({ name, author, text, localCategory }) -> { dirName, chapterCount, wordCount }
 *   removeImportedBook(dirName) -> void
 *   dirNameFor(name) -> string
 * ----------------------------------------------------------------------------
 */
import file from '@system.file'
import runAsyncFunc from './runAsyncFunc.js'
import str2abWrite from './str2abWrite.js'
import bookStorage from './bookStorage.js'
import { makeCover } from './coverMaker.js'

const BASE_URI = 'internal://files/books/'
const CHAPTERS_PER_FILE = 100
const COVER_FILE_NAME = 'cover.png'

/* 与 interconnfile.generateDirName 完全一致的稳定哈希（8 位十六进制） */
export function dirNameFor(filename) {
  let hash = 0
  const s = String(filename || '')
  if (!s.length) return '00000000'
  for (let i = 0; i < s.length; i++) {
    hash = ((hash << 5) - hash) + s.charCodeAt(i)
    hash = hash & hash
  }
  return (hash >>> 0).toString(16).padStart(8, '0')
}

/* ---------------- 章节识别 ---------------- */
// 形如：第X章 / 第X节 / 第X回 / 第X卷 / 第X集 / 第X部 / 第X篇
const CN_NUM = '0-9０-９零一二三四五六七八九十百千万两〇○'
const RE_DICN = new RegExp('^[\\s\\u3000]*第[' + CN_NUM + ']+[章节回卷集部篇]')
const RE_DICN_PLAIN = /^[\s\u3000]*第\s*\d+\s*[章节回卷集部篇季]/
const RE_SPECIAL = /^[\s\u3000]*(序章|序言|序|引子|楔子|前言|引言|尾声|番外|番外篇|终章)\s*[:：、.．]?.*$/
const RE_ENGLISH = /^[\s\u3000]*chapter\s+\d+.*$/i
const RE_VOLUME = /^[\s\u3000]*卷[一二三四五六七八九十零\d]+.*$/

/** 判断一行是否为章节标题（要求短行，避免误判正文） */
function isChapterHeading(line) {
  const t = line.trim()
  if (!t || t.length > 42) return false
  let matched = false
  if (RE_DICN.test(t) || RE_DICN_PLAIN.test(t)) {
    matched = true
  } else if (RE_SPECIAL.test(t)) {
    matched = true
  } else if (RE_ENGLISH.test(t)) {
    matched = true
  } else if (RE_VOLUME.test(t) && t.length <= 20) {
    matched = true
  }
  if (!matched) return false
  // 标题不应以句读标点结尾（正文句子的特征），所有类型统一拦截
  return !/[。！？；，、…,.!?;]$/.test(t)
}

/** 统计字数（非空白字符数） */
function wordCountOf(text) {
  const m = String(text || '').match(/\S/g)
  return m ? m.length : 0
}

/**
 * 把 TXT 文本解析为章节数组
 * @returns {Array<{name:string, body:string, wordCount:number}>}
 */
export function parseChapters(text) {
  const src = String(text || '')
  const lines = src.split('\n')
  const chapters = []
  let current = null
  let foundHeading = false

  function pushCurrent() {
    if (!current) return
    current.body = current.bodyLines.join('\n')
    current.wordCount = wordCountOf(current.body)
    chapters.push(current)
    current = null
  }

  for (let i = 0; i < lines.length; i++) {
    const line = lines[i]
    if (isChapterHeading(line)) {
      foundHeading = true
      pushCurrent()
      current = { name: line.trim(), bodyLines: [line] }
    } else if (current) {
      current.bodyLines.push(line)
    } else {
      // 首个章节标题之前的内容（可能是简介）
      current = { name: '前言', bodyLines: [line] }
    }
  }
  pushCurrent()

  // 未识别到任何章节标题：整本作为一章「正文」
  if (!foundHeading || chapters.length === 0) {
    return [{ name: '正文', body: src, wordCount: wordCountOf(src) }]
  }

  // 若「前言」过短（纯空白/极少字），并入下一章
  if (chapters.length > 1 && chapters[0].name === '前言' && wordCountOf(chapters[0].body) < 4) {
    chapters[1].body = chapters[0].body + '\n' + chapters[1].body
    chapters[1].wordCount = wordCountOf(chapters[1].body)
    chapters.shift()
  }
  return chapters
}

/* ---------------- 文件写入小工具 ---------------- */
function ensureDir(uri) {
  return runAsyncFunc(file.access, { uri: uri })
    .catch(() => runAsyncFunc(file.mkdir, { uri: uri, recursive: true }))
}

function writeText(uri, text) {
  return runAsyncFunc(file.writeText, { uri: uri, text: text })
}

function writeBuffer(uri, buffer) {
  return runAsyncFunc(file.writeArrayBuffer, { uri: uri, buffer: buffer, append: false })
}

/* ---------------- 主入口 ---------------- */
/**
 * 把下载到的 TXT 文本导入阅读引擎。
 * @param {object} opts
 * @param {string} opts.name 书名（必填，作为唯一标识来源）
 * @param {string} [opts.author] 作者
 * @param {string} opts.text 下载到的 TXT 全文（UTF-8 已解码）
 * @param {string} [opts.localCategory] 本地分类
 * @returns {Promise<{dirName:string, chapterCount:number, wordCount:number}>}
 */
export async function importBook(opts) {
  const options = opts || {}
  const name = options.name
  if (!name) throw new Error('importBook 缺少书名 name')
  const text = options.text || ''

  const dirName = dirNameFor(name)
  const base = BASE_URI + dirName + '/'

  // 重新导入：先清掉旧目录，避免残留章节文件
  await runAsyncFunc(file.rmdir, { uri: base, recursive: true }).catch(() => {})

  await ensureDir(BASE_URI)
  await ensureDir(base)
  await ensureDir(base + 'indexes')
  await ensureDir(base + 'content')

  const chapters = parseChapters(text)
  const total = chapters.length

  // ① 章节正文（UTF-16LE）
  for (let i = 0; i < total; i++) {
    const contentUri = base + 'content/' + i + '.txt'
    const buf = str2abWrite(chapters[i].body)
    if (buf.byteLength > 0) {
      await writeBuffer(contentUri, buf)
    } else {
      await writeText(contentUri, ' ')
    }
  }

  // ② 章节索引（每 100 章一个 chunk，chunk 序号从 1 开始）
  const numChunks = Math.ceil(total / CHAPTERS_PER_FILE) || 1
  for (let c = 1; c <= numChunks; c++) {
    const start = (c - 1) * CHAPTERS_PER_FILE
    const end = Math.min(c * CHAPTERS_PER_FILE, total)
    let tsv = ''
    for (let i = start; i < end; i++) {
      tsv += i + '\t' + chapters[i].name + '\t' + chapters[i].wordCount + '\n'
    }
    await writeText(base + 'indexes/' + c + '.txt', tsv)
  }

  // ③ lindex.txt：总数 / 已同步数 / 每块 start,end
  let lindex = total + '\n' + total + '\n'
  for (let k = 0; k < numChunks; k++) {
    const s = k * CHAPTERS_PER_FILE
    const e = Math.min(s + CHAPTERS_PER_FILE - 1, total - 1)
    lindex += s + ',' + e + '\n'
  }
  await writeText(base + 'lindex.txt', lindex)

  // ⑥ 封面（本地生成）
  let hasCover = false
  try {
    const cover = makeCover(name)
    await writeBuffer(base + COVER_FILE_NAME, cover)
    hasCover = true
  } catch (e) {
    // 封面失败不阻断导入
    hasCover = false
  }

  // 总字数
  const wordCount = chapters.reduce((a, ch) => a + ch.wordCount, 0)

  // ⑤ book_info.json
  const bookInfo = {
    name: name,
    chapterCount: total,
    wordCount: wordCount,
    hasCover: hasCover,
    coverFileName: hasCover ? COVER_FILE_NAME : null,
    author: options.author || '',
    summary: '',
    bookStatus: '',
    category: '',
    localCategory: options.localCategory || null
  }
  await writeText(base + 'book_info.json', JSON.stringify(bookInfo))

  // ⑦ bookshelf.json 条目（保留既有进度）
  const books = await bookStorage.getBooks()
  const idx = books.findIndex((b) => b.dirName === dirName)
  const oldProgress = idx >= 0 ? books[idx].progress : {}
  const oldCategory = idx >= 0 ? books[idx].localCategory : null
  const entry = {
    name: name,
    dirName: dirName,
    chapterCount: total,
    wordCount: wordCount,
    hasCover: hasCover,
    coverFileName: hasCover ? COVER_FILE_NAME : null,
    progress: oldProgress || {},
    localCategory: options.localCategory || oldCategory || null
  }
  if (idx >= 0) books[idx] = entry
  else books.push(entry)
  await bookStorage.updateBooks(books)

  return { dirName: dirName, chapterCount: total, wordCount: wordCount }
}

/** 删除一本已导入的书（目录 + bookshelf 条目） */
export async function removeImportedBook(dirName) {
  if (!dirName) return
  await runAsyncFunc(file.rmdir, { uri: BASE_URI + dirName, recursive: true }).catch(() => {})
  await bookStorage.removeBook(dirName)
}

export default {
  importBook: importBook,
  removeImportedBook: removeImportedBook,
  dirNameFor: dirNameFor,
  parseChapters: parseChapters
}
