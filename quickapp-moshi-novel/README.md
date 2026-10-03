# 摩柿小说 · Vela 快应用（小米手环 9 Pro）

面向小米手环 9 Pro（Vela 系统 / Vela JS 应用）自研的小说快应用，中文 UI、竖屏小屏适配。
对接自研摩柿后端（书源站 `https://novel.sswwgzs.cn`），实现：**登录 → 书架 → 搜书下载 → 离线阅读 → 删除** 完整闭环。

---

## 一、工程结构（遵循官方 Vela 快应用约定）

```
quickapp-moshi-novel/
├── README.md                     # 本文件
├── package.json                  # npm 配置（依赖 @aiot-toolkit）
├── sign/                         # 签名目录（生产打包用，见下文「签名」）
└── src/                          # 源码根目录（固定名，不可改）
    ├── manifest.json             # 应用配置：包名/图标/版本/features 声明/路由
    ├── app.ux                     # 应用入口：应用级生命周期 + 会话恢复
    ├── common/
    │   ├── config.js              # ★唯一接口配置入口：所有地址/字段/阈值集中于此
    │   ├── session.js             # 登录令牌：内存态 + @system.storage 持久化
    │   ├── http.js                # @system.fetch 封装：统一鉴权头、JSON/文本请求
    │   ├── library.js             # 本地书库：已下载书籍元信息 CRUD + 文件删除
    │   ├── reader.js              # 阅读器分页纯函数：切页/总页/页码钳制/大小格式化
    │   └── images/icon.png        # 占位图标（192x192，真机请替换）
    └── pages/
        ├── login/login.ux         # 登录页：用户名+密码 → POST /api/login
        ├── shelf/shelf.ux         # 书架页（首页）：下载任务列表 + 本地缓存合并
        ├── search/search.ux       # 搜书页：GET /api/search → POST /api/download
        └── reader/reader.ux       # 阅读页：本地 TXT 分页、上/下翻页、进度记忆
```

目录布局依据官方文档「项目结构 / 项目概览」：源码统一在 `src/`，每个页面一个子目录、对应一个 `.ux`；
`common/` 放跨页共享脚本；构建产物 `build/`、`dist/`（`.rpk`）由工具自动生成，无需手写。

---

## 二、五大功能与代码路径对照

| 功能 | 页面 | 关键 API | 说明 |
|---|---|---|---|
| 1. 摩柿登录 | `pages/login/login.ux` | `@system.fetch` + `@system.storage` | 用户名+密码 → `POST /api/login` → 拿 `token` → 持久化 → 跳书架 |
| 2. 书架列表 | `pages/shelf/shelf.ux` | `@system.fetch` | `GET /api/tasks`（Bearer）→ 与本地已下载合并渲染（书名/书源/状态/进度） |
| 3. 下载 TXT | `pages/shelf/shelf.ux` + `pages/search/search.ux` | `@system.fetch` + `@system.file` | 搜书→提交下载任务→任务 done 后拉直链 `download_url` 文本→`file.writeText` 落盘 `internal://files` |
| 4. 离线阅读 | `pages/reader/reader.ux` | `@system.file` | `file.readText` 读本地 TXT → 按固定字符数分页 → 上/下翻页（按钮+点按左右半屏）→ 显示 `当前页/总页数` → 进度持久化 |
| 5. 删除 | `pages/shelf/shelf.ux` | `@system.file` + `@system.storage` | `file.delete` 删本地 TXT + 删元信息 |

页面跳转用 `@system.router`（`push/replace/back`），`reader` 页通过 `router.push` 的 `params` 接收 `bookId/bookName`（页面 `protected` 下声明同名 key）。

---

## 三、构建 / 安装步骤（AIoT-IDE）

> 依据官方「使用 AIoT-IDE」文档。本沙盒**无 IDE、无真机**，以下为在目标开发机上执行的步骤。

1. **安装 AIoT-IDE**：支持 macOS 14+ / Windows 10+ / Ubuntu 20.04+。mac 下若提示"已损坏"，执行
   `sudo xattr -r -d com.apple.quarantine <应用路径>`。
2. **导入工程**：打开 AIoT-IDE →「文件」→「打开项目」→ 选择本目录 `quickapp-moshi-novel/`
   （也可「新建项目」选 watch 模板后，把 `src/` 覆盖过去）。
3. **安装依赖**：项目根若无 `.npmrc`，新建并写入 `registry="https://registry.npmmirror.com/"`，
   再在 IDE 终端执行 `npm i`（依赖 `@aiot-toolkit`）。
4. **配置模拟器**：右侧开发向导「检查模拟器环境，创建模拟器实例」→ 新建时镜像选手环/watch、
   屏幕尺寸按手环 9 Pro 实际值（见下方「分辨率假设」）。
5. **运行调试**：banner 栏选模拟器 →「运行/调试」，底部调试面板可看 Console / DOM / 断点。
6. **开发包打包**：banner「打包」→ 生成 `dist/*.debug.rpk` 与 `build/`。
7. **签名（生产包）**：banner「发布」按引导在 `sign/` 生成 `private.pem` 与 `certificate.pem`
   （需本机装 openssl；或手动
   `openssl req -newkey rsa:2048 -nodes -keyout private.pem -x509 -days 3650 -out certificate.pem`
   放入 `sign/`）。再次「发布」生成 `dist/*.release.rpk`。
8. **安装到手环**：通过 AIoT-IDE 的设备/ADB 流程把 `rpk` 推送到已配对的手环 9 Pro 并启动
   （具体真机推送通道以 IDE 当前版本为准，**本步骤未经真机验证**）。

---

## 四、接口对接（依据 `shared/backend-api-notes.md`，已集成进 `common/config.js`）

> 该笔记为 2026-10-03 公开观察实测；仍未确证项继续在代码里标注「待核实」。

**已确认并已落地（公开观察 + 两轮 SSH 只读服务器实读 §7/§8）：**
- **基址（双基址可切换）**：默认 `https://novel.sswwgzs.cn`（网页前端真实运行、最贴近用户实际访问）；
  备选 `https://morax.kdns.fr`（§8.1 云隧道 config.yml 实测的 Go 主后端隧道域名）。
  ⚠️ 服务器 nginx 与 Go 代码里 grep 不到 `sswwgzs.cn` 任何引用（§8.1），公共域名与隧道域名的最终对应关系**待用户确认**；
  真机连不上默认基址时，改 `config.js` 的 `apiBase` 为 `apiBaseAlt` 即可。
- 鉴权：`Authorization: Bearer <token>`（§8.4 确认；HttpOnly cookie 同样可用，本应用统一用 Bearer）。
- 登录：`POST /api/login`，体 `{username, password}`，响应顶层 `{ token, user }`。
- 书架 = 下载任务列表：`GET /api/tasks`（§8.3 `requireLogin`），响应 `{ tasks: [...] }`；
  字段 `book_name`(备 `title`)、`state`(`queued/running/done/failed/canceled`)、`progress`(0~100)、
  `download_url`(备 `url`)、`source_name`。
- 搜书：`GET /api/search?keyword=<URL编码>&max_sources=100`（§8.3 公开免鉴权）。
- 提交下载任务：**双端点可配置**——默认网页实测 `POST /api/download`；服务器 Go 路由实测为 `POST /api/tasks`
  （§8.3/§8.9，`requireLogin→requireEnabled`），配置项 `endpoints.downloadAlt`。404 时切换。

**服务器实读补充（§7/§8）：**
- **token 机制**：进程内存随机串、**非 JWT、无过期时间**；但**服务端重启会全部失效**（§8.4）。
  App 对 401 已做「回登录页」处理；会话有效期与「下载链接 TTL」是两回事，勿混淆。
- **下载链接**：§8.5 实测 `download_url` 为**相对路径 `/dl/<url编码路径>`**（不是 `/downloads/`）；
  `http.js` 的 `getText` 已自动按「基址 + 相对路径」拼接，带 Bearer 访问，两种形态都兼容。
  另有站内正文源 `GET /api/tasks/{id}/text`（需登录），已在 `config.js` 注释为可选阅读源。
- **限流**：官方限流「5 分钟 15 本」（§8.9），提交过快要提示稍后再试。
- **下载链接 TTL**：`download_ttl_hours` 控制，过期后 `download_url` 变**空字符串**（§8.4/§8.9）；
  书架已按「done 且非空」才显示可下载，空串时需重新触发下载。

**后端部署地图摘要（§7.1/§8.2/§8.3，服务器 SSH 只读实读）：**
- 公网入口 **Cloudflare 隧道**（`cloudflared`）→ 宿主 `:8080`（HTTPS）→ Docker 容器 `tomato-site-nginx-1`（nginx:alpine）。
- nginx 单 server 块，**无独立 `/api/`、`/downloads/` location**，全部 `proxy_pass` 到内网 Go 容器 `tomato-site-app-1:8080`
  （源码 `/root/tomato-site/backend/*.go`，标准库 `http.ServeMux`）。**主站是 Go，不是 FastAPI**（推翻早先推断）。
- 书源抓取引擎容器 `tomato-site-tomato-1`（内网 18423）：`/dl/` 由它代理流式回吐 TXT。
- `/root/booksource-site` 下 **FastAPI :8000 是站长自用的书源管理工具，不是用户访问的主站**。
- 数据：纯 JSON 文件（`users.json`/`tasks.json`/`config.json`），无 SQLite/BoltDB。

**仍待核实（改 `common/config.js` 即可，无需动业务代码）：**
- 公共域名 `novel.sswwgzs.cn` 与隧道 `morax.kdns.fr` 的最终对应关系。
- 任务对象 id 字段名（站内正文 `/api/tasks/{id}/text` 需用到）；本地暂以 `book_name` 作主键。
- `/dl/` 响应是否纯 TXT（还是 zip/包装）、限流超限错误体、`/api/download` vs `/api/tasks` 在默认域名下哪个生效。

---

## 五、假设清单

| 项 | 假设值 | 状态 |
|---|---|---|
| 手环 9 Pro 分辨率 | **336×480 竖屏**（`designWidth=336`） | **待核实**（官方文档未给出该机分辨率） |
| 单书 TXT 大小上限 | 5 MB（`limits.maxBookSizeBytes`，可调） | 可配置 |
| 阅读器每页字符数 | 300 字/页（`limits.charsPerPage`，可调） | 可配置 |
| 本地 TXT 落盘分区 | `internal://files/moshi/books/`（永久分区） | 按官方「文件组织」 |
| 下载进度呈现 | fetch 为一次性请求，无流式进度，故为「下载中…/完成」两态 | 官方 `request.download` 手环 9 Pro 不支持，见风险清单 |

---

## 六、无法真机验证的环节（重要）

本沙盒**无小米手环 9 Pro 真机、无 AIoT-IDE**，只能做**静态校验**，下列环节均未经真机运行验证：

1. **官方支持明细显示：小米手环 9 / 9 Pro 对 `@system.fetch` 与 `@system.request` 标注为「不支持」**
   （见官方 fetch / request 文档「支持明细」表）。本应用网络全部走 `@system.fetch`，
   **若该限制在目标机型上确实成立，则联网功能在真机上不可用**——需真机/IDE 实测确认。
   缓解：网络逻辑全部隔离在 `common/http.js`，一旦确认不可用可整体替换为手机侧中转（如
   `@system.interconnect` 与手机 App 通信）而不动 UI。
2. `.ux` 文件未经过 AIoT-IDE 编译器编译（沙盒无工具链）；仅对 `.js` 做了 `node --check` 语法校验。
3. 真机 UI 实际渲染、字体大小、336×480 假设是否贴合、点按/滑动翻页手感。
4. `rpk` 打包、签名、推送安装到手环的完整流程。
5. 手环实际可用存储大小、TXT 读写性能与内存占用。
6. 所有后端接口在**带真实 token、真实下载任务**下的字段与行为（笔记为未登录公开观察）。

---

## 七、本沙盒内已完成的静态校验（可复现）

- 所有 `.json`：`python3 -m json.tool` 通过（`package.json`、`src/manifest.json`）。
- 所有 `.js`：`node --check` 通过（config / session / http / library / reader 共 5 个）。

---

## 八、已知风险 / 与官方文档存疑要点

1. **手环 9 Pro 网络能力存疑**（见第六节第 1 条）——本项目最大不确定性。
2. `router.push` 的参数名：官方文档示例中同时出现 `uri` 与截图里的 `url`，本工程统一采用文档正文示例的 **`uri`**。
3. 列表渲染使用 `for="{{item in list}}"` + `tid`，事件回调直接传循环变量 `item`；
   不同 Vela 版本对「行内传参」支持可能有差异，真机若不生效需改用 `$idx` 回查。
4. `file.writeText` 写 5MB 文本在手环上可能较慢/占内存，README 已给出调低 `maxBookSizeBytes` 与
   分段（按章节/按页）落盘的可扩展方向；当前实现为整本落盘。
5. `manifest.json` 的 `minAPILevel=1`、`deviceTypeList=["watch"]` 为通用取值，真机最低 API 级别需按 SDK 对齐。
