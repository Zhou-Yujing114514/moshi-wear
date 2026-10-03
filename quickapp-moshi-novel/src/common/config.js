/**
 * 摩柿小说 —— 全局配置 / 接口约定（唯一可改入口）
 * ----------------------------------------------------------------------------
 * 所有后端地址、字段映射、本地存储键名、大小限制都集中在此文件，
 * 做到「一处修改、全局生效」。
 *
 * 【依据】接口事实来自 shared/backend-api-notes.md（含 §9 第三轮服务器侦察 + 沙盒公网实测）：
 *   - 【§9 关键修正：两套隧道两个站点】
 *     · morax.sswwgzs.cn / morax.kdns.fr → Go 主站（隧道 → localhost:8080）= C 端用户主站，推荐 apiBase
 *       公网实测：HTTP 200、/api/me 返回 {"user":null}、/api/login 仅 POST（GET→404）、/api/search GET→200。
 *     · novel.sswwgzs.cn / dygz.kdns.fr → FastAPI 书源管理工具（隧道 → localhost:8000）= 站长自用后台，
 *       不是用户端；早先网页调研看到的 /api/download、{results,count} 等都是该后台接口，不作主站默认。
 *   - 鉴权：Authorization: Bearer <token>（已确认）；登录 POST /api/login → { token, user }。
 *   - 书架=下载任务列表 GET /api/tasks → { tasks: [...] }；另 /api/bookshelf 为收藏夹（401，字段待核实）。
 *   - 下载是异步任务流：GET /api/search → POST /api/tasks 提交 → 轮询 /api/tasks → done 后取 /dl/ 直链。
 *   - 【§9 搜索响应字段】主站实测为 { items: [...] }（非后台的 {results,count}），见 field.searchListPath。
 * 仍未确证的点继续标注「待核实」。
 * ----------------------------------------------------------------------------
 */
export default {
  // ====== 网络通道开关（关键） ======
  // 两条路当前真实可用状态（均未真机验证，勿夸大）：
  //   'direct'：手环本机 @system.fetch —— 官方支持明细标注手环 9 Pro「不支持」，真机是否可用待验。
  //   'bridge'：经 ESP32 网桥代为联网 —— 协议层已按固件对齐，但 Vela 侧 @system.interconnect
  //             收发原语（sendToBridge/onBridgeMessage）尚未确证、待真机填，故当前也不可真用。
  // 默认值选 'direct' 的理由：direct 是「零额外依赖、拿到真机即可验」的最简路径；
  //   一旦真机确认本机 fetch 不可用，或网桥硬件就绪，再切 'bridge'。业务代码无感。
  transport: 'direct',

  // ====== 网桥通道参数（仅 transport==='bridge' 时生效） ======
  bridge: {
    // 固件整包下载 body 上限 BR_HTTP_MAX_BODY（默认约 128KB），超限固件回错误帧。
    // 真机联调确认固件实际值后可改；此处用于给出清晰中文提示。
    maxBodyBytes: 128 * 1024,
    // 识别固件「超限错误帧」statusText 的关键字（待真机确认确切文案后补充）。
    oversizeKeywords: ['too large', 'body', 'max_body', 'oversize', 'limit', '上限', '过大', '超过'],
    // 给用户的超限提示文案
    oversizeTip: '文件过大，超出网桥整包下载上限（约128KB），后续支持分段(Range)下载'
  },

  // ====== 后端服务地址（双基址，可切换） ======
  // 【§9 确证】C 端用户主站 = Go 主站（隧道 → localhost:8080），公网实测 200/me 返回 user:null。
  apiBase: 'https://morax.sswwgzs.cn',
  // 备选基址：主站的隧道直连域名（同一 Go 主站，备用）。
  apiBaseAlt: 'https://morax.kdns.fr',
  // 注意：novel.sswwgzs.cn / dygz.kdns.fr 是 FastAPI 书源【站长自用后台】（:8000），
  //       不是用户端；早先看到的 /api/download、{results,count} 均为该后台接口，勿用作主站默认。
  mainSite: 'https://sswwgzs.cn',         // 裸域：服务器零配置（需 CF 控制台，待用户确证）

  // ====== 接口路径（§8.3/§9 实测，Go 主站路由） ======
  endpoints: {
    login: '/api/login',          // 登录 POST（§9 实测：GET→404，仅 POST）
    logout: '/api/logout',       // 登出 POST（§8.4：真删内存 session，token 作废）
    me: '/api/me',               // 当前用户 GET（§9 实测：未登录返回 {"user":null}）
    bookshelf: '/api/tasks',     // 书架=下载任务列表 GET（§8.3：requireLogin）
    favorites: '/api/bookshelf', // 收藏夹 GET（存在但 401，字段结构待核实；当前未用）
    search: '/api/search',       // 搜索 GET（§9 实测 200，公开；响应字段为 items，见 field.searchListPath）
    // 提交下载任务：默认 POST /api/tasks（§8.3/§9：requireLogin→requireEnabled）。
    // （旧 /api/download 是 FastAPI 后台接口，主站 404，已移除默认。）
    download: '/api/tasks',
    // 站内正文阅读源（可选）：GET /api/tasks/{id}/text（需登录），{id} 字段名待核实。
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
    booksListPath: 'tasks',        // 已确认：书架(下载任务)数组在响应 tasks 字段下
    searchListPath: 'items',       // §9 实测：搜索结果数组在响应 items 字段（非后台的 results）
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
