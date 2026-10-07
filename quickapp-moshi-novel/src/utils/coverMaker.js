/**
 * 摩柿小说 —— 本地封面生成器（无第三方依赖）
 * ----------------------------------------------------------------------------
 * 摩柿书源仅提供 TXT，搜索/任务对象不含封面 URL（见 shared/backend-api-notes.md
 * §3.2，搜索项只有 name/author/book_url/source/sources）。为让书架「封面模式」与
 * 书籍详情页仍有封面可显，本模块在导入书籍时用纯 JS 直接光栅化一张「摩柿」
 * （柿子）主题封面，并编码为 PNG。
 *
 * PNG 编码采用 zlib「stored（不压缩）」块 + 手写 CRC32/Adler32，
 * 无需任何 deflate 算法或第三方库，可在 QuickApp 运行时直接运行。
 *
 * 仅暴露：
 *   makeCover(seedName, width?, height?) -> Uint8Array  （PNGB 字节）
 * ----------------------------------------------------------------------------
 */

/* ---------------- 摩柿品牌色 ---------------- */
const PERSIMMON = [240, 108, 38]      // 柿身橙红
const PERSIMMON_DARK = [206, 74, 20]  // 柿身暗部
const CALYX = [112, 162, 70]          // 柿蒂绿
const CALYX_DARK = [86, 132, 52]
const STEM = [120, 82, 44]            // 柿柄棕
const GOLD = [242, 193, 78]           // 摩柿金

/* 由书名生成稳定的 32bit 哈希（djb2，与 interconnfile.generateDirName 同型） */
function hash32(str) {
  let hash = 0
  const s = String(str || '')
  for (let i = 0; i < s.length; i++) {
    hash = ((hash << 5) - hash) + s.charCodeAt(i)
    hash = hash & hash
  }
  return hash >>> 0
}

/* ---------------- CRC32 / Adler32 ---------------- */
const CRC_TABLE = (() => {
  const t = new Uint32Array(256)
  for (let n = 0; n < 256; n++) {
    let c = n
    for (let k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >>> 1)) : (c >>> 1)
    t[n] = c >>> 0
  }
  return t
})()

function crc32(buf) {
  let c = 0xFFFFFFFF
  for (let i = 0; i < buf.length; i++) c = CRC_TABLE[(c ^ buf[i]) & 0xFF] ^ (c >>> 8)
  return (c ^ 0xFFFFFFFF) >>> 0
}

function adler32(buf) {
  let a = 1
  let b = 0
  const MOD = 65521
  for (let i = 0; i < buf.length; i++) {
    a = (a + buf[i]) % MOD
    b = (b + a) % MOD
  }
  return ((b << 16) | a) >>> 0
}

/* 构造一个 PNG chunk（含长度/类型/数据/CRC） */
function chunk(type, data) {
  const len = data.length
  const out = new Uint8Array(len + 12)
  const dv = new DataView(out.buffer)
  dv.setUint32(0, len)
  for (let i = 0; i < 4; i++) out[4 + i] = type.charCodeAt(i)
  out.set(data, 8)
  const crcBuf = out.subarray(4, 8 + len)
  dv.setUint32(8 + len, crc32(crcBuf))
  return out
}

/* 把 RGB 原始字节用 zlib stored 块包装（不压缩） */
function zlibStored(rgb) {
  const BLOCK = 65535
  const blocks = Math.ceil(rgb.length / BLOCK) || 1
  // zlib header(2) + 每块(5 头 + 数据) + adler(4)
  let total = 2 + 4
  for (let b = 0; b < blocks; b++) {
    const off = b * BLOCK
    const n = Math.min(BLOCK, rgb.length - off)
    total += 5 + n
  }
  const out = new Uint8Array(total)
  out[0] = 0x78  // CM=deflate, CINFO=32k
  out[1] = 0x01  // FLEVEL/fcheck
  let p = 2
  for (let b = 0; b < blocks; b++) {
    const off = b * BLOCK
    const n = Math.min(BLOCK, rgb.length - off)
    out[p++] = (b === blocks - 1) ? 1 : 0  // BFINAL
    out[p++] = n & 0xFF
    out[p++] = (n >> 8) & 0xFF
    out[p++] = (~n) & 0xFF
    out[p++] = ((~n) >> 8) & 0xFF
    out.set(rgb.subarray(off, off + n), p)
    p += n
  }
  const ad = adler32(rgb)
  out[p++] = (ad >> 24) & 0xFF
  out[p++] = (ad >> 16) & 0xFF
  out[p++] = (ad >> 8) & 0xFF
  out[p++] = ad & 0xFF
  return out
}

/* 线性混合两个颜色，t∈[0,1] */
function mix(a, b, t) {
  return [
    Math.round(a[0] + (b[0] - a[0]) * t),
    Math.round(a[1] + (b[1] - a[1]) * t),
    Math.round(a[2] + (b[2] - a[2]) * t)
  ]
}

/* ---------------- 光栅化封面 ---------------- */
function rasterize(w, h, seed) {
  // 背景基调由书名哈希决定（在深灰蓝范围内取色相感）
  const topBase = [
    26 + (seed % 18),
    30 + ((seed >> 3) % 16),
    40 + ((seed >> 7) % 30)
  ]
  const bottomBase = [8, 10, 14]

  const rgb = new Uint8Array(w * h * 3)

  // 柿子几何（位于画面中下部）
  const cx = w / 2
  const cy = h * 0.56
  const r = Math.min(w, h) * 0.30

  function setPx(x, y, c) {
    const xi = x | 0
    const yi = y | 0
    if (xi < 0 || yi < 0 || xi >= w || yi >= h) return
    const o = (yi * w + xi) * 3
    rgb[o] = c[0]
    rgb[o + 1] = c[1]
    rgb[o + 2] = c[2]
  }

  for (let y = 0; y < h; y++) {
    // 背景竖向渐变
    const bg = mix(topBase, bottomBase, y / h)
    for (let x = 0; x < w; x++) {
      let c = bg
      const dx = (x - cx) / r
      const dy = (y - cy) / r
      const d = Math.sqrt(dx * dx + dy * dy)
      if (d <= 1) {
        // 柿身：径向，边缘暗、中心亮
        const shade = Math.min(1, d * 0.9)
        c = mix(PERSIMMON, PERSIMMON_DARK, shade)
        // 顶部受光略亮
        if (dy < -0.35 && d < 0.7) c = mix(c, [255, 168, 104], 0.35)
      }
      setPx(x, y, c)
    }
  }

  // 柿蒂：四片小萼（围绕柿顶的菱形）
  const calyxY = cy - r * 0.92
  for (let k = 0; k < 4; k++) {
    const ang = (k / 4) * Math.PI * 2 + Math.PI / 4
    const ex = cx + Math.cos(ang) * r * 0.34
    const ey = calyxY + Math.sin(ang) * r * 0.12
    const rw = r * 0.30
    const rh = r * 0.16
    for (let y = 0; y < h; y++) {
      for (let x = 0; x < w; x++) {
        const ddx = (x - ex) / rw
        const ddy = (y - ey) / rh
        if (ddx * ddx + ddy * ddy <= 1) setPx(x, y, mix(CALYX, CALYX_DARK, Math.min(1, Math.sqrt(ddx * ddx + ddy * ddy))))
      }
    }
  }

  // 柿柄：竖直小矩形
  const stemW = Math.max(2, w * 0.04)
  for (let y = (calyxY - r * 0.22) | 0; y < calyxY + 2; y++) {
    for (let x = (cx - stemW / 2) | 0; x < cx + stemW / 2; x++) setPx(x, y, STEM)
  }

  // 顶部 / 底部金色装饰线
  const bandY = Math.round(h * 0.12)
  for (let x = Math.round(w * 0.16); x < w * 0.84; x++) {
    setPx(x, bandY, GOLD)
    setPx(x, bandY + 1, mix(GOLD, [0, 0, 0], 0.4))
    setPx(x, h - bandY, GOLD)
  }

  return rgb
}

/* ---------------- 对外入口 ---------------- */
/**
 * 生成一张摩柿主题封面 PNG。
 * @param {string} seedName 书名（决定背景色，保证同一本书封面稳定）
 * @param {number} width 像素宽（默认 120）
 * @param {number} height 像素高（默认 160）
 * @returns {Uint8Array} PNG 字节
 */
export function makeCover(seedName, width, height) {
  const w = Math.max(16, width || 120) | 0
  const h = Math.max(16, height || 160) | 0
  const seed = hash32(seedName)
  const rgb = rasterize(w, h, seed)

  // PNG 要求每条扫描行前加 1 字节过滤器类型（0=None）
  const stride = w * 3
  const raw = new Uint8Array(h * (stride + 1))
  for (let y = 0; y < h; y++) {
    const dst = y * (stride + 1)
    raw[dst] = 0
    raw.set(rgb.subarray(y * stride, (y + 1) * stride), dst + 1)
  }

  const ihdr = new Uint8Array(13)
  const dv = new DataView(ihdr.buffer)
  dv.setUint32(0, w)
  dv.setUint32(4, h)
  ihdr[8] = 8   // bit depth
  ihdr[9] = 2   // color type: truecolor RGB
  ihdr[10] = 0
  ihdr[11] = 0
  ihdr[12] = 0

  const sig = new Uint8Array([137, 80, 78, 71, 13, 10, 26, 10])
  const cIhdr = chunk('IHDR', ihdr)
  const cIdat = chunk('IDAT', zlibStored(raw))
  const cIend = chunk('IEND', new Uint8Array(0))

  const png = new Uint8Array(sig.length + cIhdr.length + cIdat.length + cIend.length)
  let o = 0
  png.set(sig, o); o += sig.length
  png.set(cIhdr, o); o += cIhdr.length
  png.set(cIdat, o); o += cIdat.length
  png.set(cIend, o)
  return png
}

export default { makeCover }
