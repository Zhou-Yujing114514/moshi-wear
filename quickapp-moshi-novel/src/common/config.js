/**
 * 摩柿小说 —— 全局配置 / 接口约定（唯一可改入口）
 * ----------------------------------------------------------------------------
 * 所有后端地址、字段映射、本地存储键名、大小限制都集中在此文件，
 * 做到「一处修改、全局生效」。
 *
 * 【依据】接口事实来自 shared/backend-api-notes.md（2026-10-03 公开观察实测）：
 *   - 全部 API 位于书源站 https://novel.sswwgzs.cn（主站 sswwgzs.cn 仅为工作室门户）
 *   - 鉴权：Authorization: Bearer <token>（已确认）
 *   - 登录 POST /api/login → { token, user }（token 为顶层字段，已确认）
 *   - 无独立「书架」：书架 = 当前用户下载任务列表 GET /api/tasks → { tasks: [...] }
 *   - 下载是异步任务流：GET /api/search → POST /api/download → 轮询 /api/tasks
 *     直到 state==done → 取 download_url 直链下载 TXT
 * 仍未确证的点继续标注「待核实」。
 * ----------------------------------------------------------------------------
 */
export default {
  // ====== 网络通道开关（关键） ======
  // 'direct'：手环本机 @system.fetch（官方支持明细里手环 9 Pro 标「不支持」，可能真机不可用）
  // 'bridge'：经 ESP32 网桥（bridge.js，FetchBridge 协议）代为联网，绕开本机 fetch 限制。
  // 真机联调时改这一行即可，业务代码无感。默认 direct。
  transport: 'direct',

  // ====== 后端服务地址（双基址，可切换） ======
  // 默认基址：公开网页前端真实运行、最贴近用户实际访问的 API（公开实测）。
  apiBase: 'https://novel.sswwgzs.cn',
  // 备选基址：服务器 SSH 实测的 Go 主后端隧道域名（cloudflared 指向宿主 8080→nginx→Go 容器）。
  // 证据：§8.1 云隧道 config.yml 仅配了 dl.1979.kdns.fr / morax.kdns.fr；
  //       且服务器 nginx 与 Go 代码里 grep 不到 sswwgzs.cn 任何引用（§8.1）。
  // 【待定】公共域名 novel.sswwgzs.cn 与隧道域名 morax.kdns.fr 的最终对应关系需用户确认；
  //       若真机连不上默认基址，把 apiBase 临时切换为 apiBaseAlt 即可（业务代码无需改）。
  apiBaseAlt: 'https://morax.kdns.fr',
  mainSite: 'https://sswwgzs.cn',         // 主站门户（仅 FAQ/说明，应用不直接调用）

  // ====== 接口路径（已确认/待核实见注释） ======
  endpoints: {
    login: '/api/login',          // 登录 POST（§8.3 Go 路由表已确认：无中间件）
    logout: '/api/logout',       // 登出 POST（§8.4 已确认：Logout 真删内存 session，token 真正作废）
    me: '/api/me',               // 当前用户 GET（已确认：未登录也返回，需判 data.user 是否为 null）
    bookshelf: '/api/tasks',     // 书架=下载任务列表 GET（§8.3 已确认：requireLogin）
    search: '/api/search',       // 搜索 GET（§8.3 已确认：公开免鉴权）
    // 提交下载任务：网页前端 JS 实测为 POST /api/download；服务器 Go 路由表实测为 POST /api/tasks
    // （requireLogin→requireEnabled，§8.3/§8.9 第 4 条）。默认用网页实测版；若该 404，改为下面 downloadAlt。
    download: '/api/download',
    downloadAlt: '/api/tasks',
    // 站内正文阅读源（可选）：GET /api/tasks/{id}/text（需登录），
    // 不经过 /dl/ 直链；{id} 为任务 id（任务对象 id 字段名待核实）。
    taskText: '/api/tasks/{id}/text'
  },

  // ====== 鉴权约定（§8.4 服务器实读确认） ======
  auth: {
    header: 'Authorization',      // 已确认：Bearer header 可用
    scheme: 'Bearer',             // 已确认；同时 HttpOnly cookie 也可用，本应用统一用 Bearer
    loginTokenField: 'token',     // 已确认：登录响应顶层 token 字段
    loginTokenPath: '',           // 空串=顶层取；已确认无需点路径
    // §8.4：token 为进程内存随机串，非 JWT、无过期时间；但服务端重启会全部失效，
    //       故 App 收到 401 应引导重新登录（书架页已对 401 做了回登录页处理）。
    noExpiryButRestartInvalidates: true
  },

  // ====== 响应 / 业务字段映射 ======
  field: {
    booksListPath: 'tasks',        // 已确认：书架数组在响应 tasks 字段下
    // 任务对象没有稳定数字 id（已确认字段中无 id），本地以 book_name 作为书籍主键（待核实稳定性）
    bookId: 'book_name',           // 本地主键字段（已确认书名字段为 book_name/title）
    bookName: 'book_name',         // 已确认：书名（前端兼容 title）
    bookNameFallback: 'title',     // 已确认：备用书名键
    sourceName: 'source_name',    // 已确认：书源名（任务列表无 author，用它做副信息）
    state: 'state',                // 已确认：任务状态 queued/running/done/failed/canceled
    stateFallback: 'status',      // 已确认：备用状态键
    progress: 'progress',         // 已确认：0~100 百分比
    downloadUrl: 'download_url',   // TXT 直链（仅 done 且有值时有效）
    downloadUrlFallback: 'url',   // 备用下载键
    // §8.5：download_url 实测为相对路径 /dl/<url编码路径>（非 /downloads/），
    //       http.js 会自动按 apiBase + 相对路径拼接；带 Bearer 访问。
    // §8.4/§8.9：直链有 TTL（download_ttl_hours），过期后该字段变空串，前端需处理空串并提示重新下载。
    downloadUrlPrefix: '/dl/'
  },

  // ====== 本地存储（@system.storage）键名 ======
  storageKeys: {
    token: 'moshi_token',             // 登录令牌
    username: 'moshi_username',       // 用户名
    shelfMeta: 'moshi_shelf_meta'     // 已下载到本地的书籍元信息（JSON 字符串）
  },

  // ====== 本地文件分区（@system.file，internal://files 为永久分区） ======
  files: {
    booksDir: 'internal://files/moshi/books/'   // TXT 落盘目录（URI 规范：不含 ..，允许 / . _ -）
  },

  // ====== 运行限制（手环存储/内存有限，可配置） ======
  limits: {
    maxBookSizeBytes: 5 * 1024 * 1024,   // 单本 TXT 大小上限 5MB（待核实手环实际可用空间，可下调）
    charsPerPage: 300,                   // 阅读器每页字符数（手环小屏，可在阅读页调整）
    taskPollMs: 10000,                   // 下载任务轮询间隔：官方前端为 10s（已确认），手环省电可手动刷新
    downloadUrlTtlHours: 24              // download_url 直链有效期约 24h（已确认，过期需重新触发）
  },

  // ====== UI 假设 ======
  screen: {
    // 手环 9 Pro 分辨率官方文档未明确给出，按 336×480 竖屏假设（待核实）
    designWidth: 336,
    designHeight: 480
  }
}
