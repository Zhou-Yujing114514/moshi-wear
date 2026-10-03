# 摩柿小说 · Vela 快应用（小米手环 9 Pro）

面向小米手环 9 Pro（Vela 系统 / Vela JS 应用）自研的小说快应用，中文 UI、竖屏小屏适配。
对接自研摩柿后端（书源站 `https://novel.sswwgzs.cn`），实现：**登录 → 书架 → 搜书下载 → 离线阅读 → 删除** 完整闭环。

---

## 一、工程结构（遵循官方 Vela 快应用约定）

```
quickapp-moshi-novel/
├── README.md                     # 本文件
├── package.json                  # npm 配置（依赖 aiot-toolkit；scripts: build/release）
├── sign/                         # 签名目录：private.pem / certificate.pem（release 用）
├── dist/                         # 构建产物：.rpk（已构建好 debug / release 两个签名包）
└── src/                          # 源码根目录（固定名，不可改）
    ├── manifest.json             # 应用配置：包名/图标/版本/features 声明/路由
    ├── app.ux                     # 应用入口：应用级生命周期 + 会话恢复
    ├── common/
    │   ├── config.js              # ★唯一接口配置入口：所有地址/字段/阈值/transport 开关
    │   ├── session.js             # 登录令牌：内存态 + @system.storage 持久化
    │   ├── http.js                # 网络层：direct(@system.fetch) / bridge 双通道分流
    │   ├── bridge.js             # ★ESP32 网桥适配层：FetchBridge 信封（绕开机身 fetch 限制）
    │   ├── library.js             # 本地书库：已下载书籍元信息 CRUD + 文件删除
    │   ├── reader.js              # 阅读器分页纯函数：切页/总页/页码钳制/大小格式化
    │   └── images/icon.png        # 占位图标（192x192，真机请替换）
    ├── Login/login.ux             # 登录页：用户名+密码 → POST /api/login
    ├── Shelf/shelf.ux             # 书架页（首页）：下载任务列表 + 本地缓存合并
    ├── Search/search.ux           # 搜书页：GET /api/search → POST /api/download
    └── Reader/reader.ux           # 阅读页：本地 TXT 分页、上/下翻页、进度记忆
```

> 布局实测修正：本工程用 `aiot-toolkit@2.0.5` 真实打包验证——Vela 工具链按
> **`src/<页面Key>/<component>.ux`** 定位页面（页面 Key 与 `manifest.json` 的 `router.pages` 键同名，
> 如 `Login`），**不是** `src/pages/` 层级。页面间 `common/` 相对导入为 `../common/...`。

目录布局依据官方文档「项目结构 / 项目概览」：源码统一在 `src/`，每个页面一个子目录、对应一个 `.ux`；
`common/` 放跨页共享脚本；构建产物 `build/`、`dist/`（`.rpk`）由工具自动生成，无需手写。

---

## 二、五大功能与代码路径对照

| 功能 | 页面 | 关键 API | 说明 |
|---|---|---|---|
| 1. 摩柿登录 | `Login/login.ux` | `@system.fetch` + `@system.storage` | 用户名+密码 → `POST /api/login` → 拿 `token` → 持久化 → 跳书架 |
| 2. 书架列表 | `Shelf/shelf.ux` | `@system.fetch` | `GET /api/tasks`（Bearer）→ 与本地已下载合并渲染（书名/书源/状态/进度） |
| 3. 下载 TXT | `Shelf/shelf.ux` + `Search/search.ux` | `@system.fetch` + `@system.file` | 搜书→提交下载任务→任务 done 后拉直链 `download_url` 文本→`file.writeText` 落盘 `internal://files` |
| 4. 离线阅读 | `Reader/reader.ux` | `@system.file` | `file.readText` 读本地 TXT → 按固定字符数分页 → 上/下翻页（按钮+点按左右半屏）→ 显示 `当前页/总页数` → 进度持久化 |
| 5. 删除 | `Shelf/shelf.ux` | `@system.file` + `@system.storage` | `file.delete` 删本地 TXT + 删元信息 |

页面跳转用 `@system.router`（`push/replace/back`），`reader` 页通过 `router.push` 的 `params` 接收 `bookId/bookName`（页面 `protected` 下声明同名 key）。

---

## 三、构建 / 打包 / 安装（已用 aiot-toolkit 真实构建验证）

> 本沙盒已用 `aiot-toolkit@2.0.5`（npm npmmirror 源）**真实构建出 .rpk 并签名**。
> **证书已于 2026-10-03 重建**（旧私钥曾误推公开仓库，旧证书/旧包全部作废重签）：
> - 新 release 证书主题 `CN=mosshi-quickapp-v2`，SHA256 指纹
>   `20:FA:C7:48:D9:B3:67:AB:C3:6A:96:1E:20:A1:B7:8E:ED:A3:34:5B:40:A5:F2:2A:F7:A3:97:6C:AD:FE:EE:E9`
> - 当前 `dist/` 产物（新证书重签后）：
>   - `cn.sswwgzs.moshi.novel.release.1.0.0.rpk` — **23519 B**，SHA256 `f37e04e44155f11fc8602f3c8b1ad710b4d3103d439790aa85f5f641c7b8be74`
>   - `cn.sswwgzs.moshi.novel.debug.1.0.0.rpk` — **30923 B**，SHA256 `8f6202210a60e3ec710446770d4886c4b16282916aec4871f8cc4e9083779183`
>   - 重签后两包 `META-INF/CERT` 的 SHA256 均与旧包不同（旧 release CERT `991f3fc5…` → 新 `3bf2785d…`）。

1. **装依赖**：项目根新建 `.npmrc` 写入 `registry="https://registry.npmmirror.com/"`，执行
   `npm i`（依赖 `aiot-toolkit`）。
2. **命令行构建**（等价于 IDE 的「打包/发布」）：
   - 开发包：`npx aiot build` → 生成 `dist/*.debug.rpk` + `build/`
   - 生产包：`npx aiot release` → 用 `sign/` 证书生成 `dist/*.release.rpk`
   - 重签名已有 build：`npx aiot resign`
3. **签名与 CI 策略**：`sign/private.pem`（权限 600，已被 `.gitignore` 屏蔽）+ `sign/certificate.pem`。
   - **CI 只产 debug 包**（`aiot build`，用工具链自带调试证书，无需私钥）；
   - **正式 release 为本地受控操作**（`aiot release`，需本地 `sign/` 私钥，私钥绝不入 CI/仓库）。
   - release 模式强制校验 `sign/` 下证书，缺失会报「problem with the certification path」。
4. **模拟器调试**：`npx aiot createVVD` 创建 Vela 虚拟设备；banner 选设备后运行/调试。
5. **安装到手环 9 Pro**（**以下真机通道待核实**，沙盒无手环）：
   - 手环开启「开发者模式 / 调试」，用 ADB 连接（`adb connect <手环IP>:5555`）。
   - `npx aiot installDbgAndMkp` 安装调试器壳（`org.hapjs.debugger` / `org.hapjs.mockup`）。
   - 通过 `npx aiot getConnectedDevices` / `getPlatforms` 确认设备与平台，再把 `rpk` 推送安装。
   - 具体手环型号的开发者模式入口与 rpk 推送命令以 AIoT-IDE 当前版本/官方文档为准。

---

## 四、接口对接（依据 `shared/backend-api-notes.md`，已集成进 `common/config.js`）

> 该笔记为 2026-10-03 公开观察实测；仍未确证项继续在代码里标注「待核实」。

**已确认并已落地（公开观察 + 三轮 SSH 服务器实读 §7/§8/§9 + 沙盒公网实测）：**
- **【§9 关键：两套隧道两个站点】**
  - **C 端用户主站（Go，:8080）**：`morax.sswwgzs.cn` / `morax.kdns.fr` —— **`apiBase` 默认**。
    沙盒公网实测：HTTP 200、`/api/me` 返回 `{"user":null}`、`/api/login` 仅 POST（GET→404）、`/api/search` GET→200。
  - **站长自用后台（FastAPI，:8000）**：`novel.sswwgzs.cn` / `dygz.kdns.fr` —— **不是用户端**；
    早先网页调研看到的 `/api/download`、`{results,count}` 等都是该后台接口，**已移出主站默认**。
- 鉴权：`Authorization: Bearer <token>`（§8.4；HttpOnly cookie 同样可用，本应用统一用 Bearer）。
- 登录：`POST /api/login`，体 `{username, password}`，响应顶层 `{ token, user }`。
- 书架 = 下载任务列表：`GET /api/tasks`（§8.3 `requireLogin`），响应 `{ tasks: [...] }`；
  字段 `book_name`(备 `title`)、`state`、`progress`(0~100)、`download_url`(备 `url`)、`source_name`。
  另有 `GET /api/bookshelf`（收藏夹，存在但 401，字段待核实，当前未用）。
- 搜书：`GET /api/search?keyword=<URL编码>&max_sources=100`（§9 实测 200，公开）；
  **响应字段为 `{ items: [...] }`**（非后台 `{results,count}`），已用 `config.field.searchListPath` 配置化并适配 `search.ux`。
- 提交下载任务：**默认 `POST /api/tasks`**（§8.3/§9，`requireLogin→requireEnabled`）；
  旧 `POST /api/download` 是 FastAPI 后台接口（主站 404），已移除默认。
- `/api/settings`：主站 404（仅 admin 有 `/api/admin/settings`），已从主站默认移除。

**服务器实读补充（§7/§8）：**
- **token 机制**：进程内存随机串、**非 JWT、无过期时间**；但**服务端重启会全部失效**（§8.4）。
  App 对 401 已做「回登录页」处理；会话有效期与「下载链接 TTL」是两回事，勿混淆。
- **下载链接**：§8.5 实测 `download_url` 为**相对路径 `/dl/<url编码路径>`**；
  `http.js` 的 `getText` 已自动按「基址 + 相对路径」拼接，带 Bearer 访问。
  另有站内正文源 `GET /api/tasks/{id}/text`（需登录），已在 `config.js` 注释为可选阅读源。
- **限流**：「5 分钟 15 本」（§8.9），提交过快要提示稍后再试。
- **下载链接 TTL**：`download_ttl_hours` 控制，过期后 `download_url` 变**空字符串**（§8.4/§8.9）；
  书架已按「done 且非空」才显示可下载，空串时需重新触发下载。

**后端部署地图摘要（§7.1/§8.2/§8.3，服务器 SSH 只读实读）：**
- 公网入口 **Cloudflare 隧道**（`cloudflared`）→ 宿主 `:8080`（HTTPS）→ Docker 容器 `tomato-site-nginx-1`（nginx:alpine）。
- nginx 单 server 块，**无独立 `/api/`、`/downloads/` location**，全部 `proxy_pass` 到内网 Go 容器 `tomato-site-app-1:8080`
  （源码 `/root/tomato-site/backend/*.go`，标准库 `http.ServeMux`）。**主站是 Go，不是 FastAPI**。
- 书源抓取引擎容器 `tomato-site-tomato-1`（内网 18423）：`/dl/` 由它代理流式回吐 TXT。
- `/root/booksource-site` 下 **FastAPI :8000 是站长自用的书源管理工具，不是用户访问的主站**。
- 数据：纯 JSON 文件（`users.json`/`tasks.json`/`config.json`），无 SQLite/BoltDB。

**仍待用户确证（改 `common/config.js` 即可，无需动业务代码）：**
- 裸域 `sswwgzs.cn`（服务器零配置，需 Cloudflare 控制台接入）。
- 业务定位最终确认（C 端主站 Go vs 站长后台 FastAPI）。
- Cloudflare Dashboard ingress 与 DNS 记录配置。
- 任务对象 id 字段名（站内正文 `/api/tasks/{id}/text` 需用到）；本地暂以 `book_name` 作主键。
- `/dl/` 响应是否纯 TXT（还是 zip/包装）、限流超限错误体。

---

## 五、网桥通道（ESP32，绕开机身 fetch 限制）

**开关与两条路真实可用状态（均未真机验证，勿夸大）**：
`src/common/config.js` 顶部 `transport: 'direct' | 'bridge'`，默认 `direct`。
- `direct`：手环本机 `@system.fetch` —— 官方支持明细标注手环 9 Pro「不支持」，**真机是否可用待验**。
- `bridge`：ESP32 网桥代发 —— 协议层已按固件对齐，但 Vela 侧 `@system.interconnect` 收发原语
  （`sendToBridge/onBridgeMessage`）尚未确证、待真机填，**当前也不可真用**。
- **默认选 `direct` 的理由**：它零额外依赖、拿到真机即可第一时间验证；待真机确认本机 fetch 不可用
  （或网桥硬件就绪、原语填好）再切 `bridge`。切换只改这一行，业务代码无感。

**架构**：
```
QuickApp(.ux) ──@system.互联通道──> ESP32 网桥(qaic.c) ──HTTP──> novel.sswwgzs.cn
   http.js(按 transport 分流)        FetchBridge 协议
   bridge.js(信封收发/解码)          代发请求并回包
```

**与固件 `esp32-firmware/main/qaic.c` 的契约（逐字段已对齐）**：
- 握手：`{"tag":"__hs__","count":0,"caps":{...}}` → 收 `count:1` → 回 `count:2` → 完成。
- 请求：`{"tag":"fetch","id":"<n>","url":"<完整URL>","options":{method,headers,body,raw,followRedirects}}`。
- 单消息响应：`{"tag":"fetch","id","resp":{ok,status,statusText,headers,body,raw,bodyEncoding}}`。
- 错误帧：`ok:false` + `statusText`；`bridge.js` 已识别并 reject，`http.js` 映射为中文提示。
- 分片响应：`resp.chunked=true` → 收若干 `{"tag":"fetch-chunk","id","seq","total","data"}`，
  每收一片回 `{"tag":"fetch-ack","id","ack":<下一个缺失连续序号>}`，收齐后逐片 base64 解码再按 UTF-8 拼回。
- 本端 caps 只声明 `text/base64`、压缩只认 `none`（deflate/lz4 未在手环侧实现，待真机）。

**网桥整包下载上限（固件新增）**：固件整包下载 body 上限 `BR_HTTP_MAX_BODY`（**默认约 128KB**），
超限固件回错误帧。`bridge.js` 按 `config.bridge.oversizeKeywords` 识别后，`http.js` 抛出
**「文件过大，超出网桥整包下载上限（约128KB），后续支持分段(Range)下载」**。
- ⚠️ 该 128KB 上限**只作用于 `bridge` 路径**；`direct` 路径不受此限（direct 受本机 `@system.fetch` 可用性限制）。
- **待办**：大 TXT 超过网桥整包上限时，后续实现 HTTP `Range` 分段下载（待真机/固件确认 Range 支持后启用）；
  当前单本仍受 `config.limits.maxBookSizeBytes`（5MB，可配）约束。

**待真机确证（bridge.js 已用「双原语收敛」隔离）**：Vela 手环侧 `@system.interconnect` 的确切
模块名与 send/onMessage 调用形式无法从公开文档确证。`bridge.js` 顶部只暴露两个函数
`sendToBridge(msgObj)` 与 `onBridgeMessage(cb)`，真机联调时**只改这两个函数体**对接真实互联 API，
其余协议/解码逻辑零改动。已在代码注释标注候选接入点，未编造任何 API 名。

**真机联调步骤**：① 固件 `qaic_init(send,ctx)` 接好串口/BLE 发送；② 把 `config.transport` 改为 `bridge`；
③ 在 `sendToBridge/onBridgeMessage` 填真实互联调用；④ 装包运行，观察握手日志与首条 `/api/login` 回包。

---

## 六、假设清单

| 项 | 假设值 | 状态 |
|---|---|---|
| 手环 9 Pro 分辨率 | **336×480 竖屏**（`designWidth=336`） | **待核实**（官方文档未给出该机分辨率） |
| 单书 TXT 大小上限 | 5 MB（`limits.maxBookSizeBytes`，可调） | 可配置 |
| 阅读器每页字符数 | 300 字/页（`limits.charsPerPage`，可调） | 可配置 |
| 本地 TXT 落盘分区 | `internal://files/moshi/books/`（永久分区） | 按官方「文件组织」 |
| 下载进度呈现 | fetch 为一次性请求，无流式进度，故为「下载中…/完成」两态 | 官方 `request.download` 手环 9 Pro 不支持，见风险清单 |

---

## 七、无法真机验证的环节（重要）

本沙盒**无小米手环 9 Pro 真机**，已用 `aiot-toolkit` 真实完成 webpack 编译与签名打包，但下列环节仍未经真机运行验证：

1. **官方支持明细显示：小米手环 9 / 9 Pro 对 `@system.fetch` 与 `@system.request` 标注为「不支持」**。
   本机直连（`direct`）真机可能不可用——**已提供 `bridge` 网桥通道作为绕开方案**（见第五节），二者可一键切换。
2. **网桥互联原语待真机确证**：`@system.interconnect` 的真实模块名/调用形式未从文档确证，
   `bridge.js` 已收敛为 `sendToBridge/onBridgeMessage` 两个待填函数。
3. 真机 UI 实际渲染、336×480 假设是否贴合、点按翻页手感。
4. 手环实际可用存储大小、TXT 读写性能与内存占用。
5. 所有后端接口在**带真实 token、真实下载任务**下的字段与行为（笔记为公开观察/服务器只读实读）。
6. rpk 在真实手环上的安装、启动与联网（`direct`/`bridge` 两种）。

---

## 八、本沙盒内已完成的校验与构建（可复现）

- 所有 `.json`：`python3 -m json.tool` 通过（`package.json`、`src/manifest.json`）。
- 所有 `.js`：`node --check` 通过（config / session / http / bridge / library / reader 共 6 个）。
- `.ux` 已通过 `aiot-toolkit@2.0.5` 真实 webpack 编译（`webpack complete` → `build success`）。
- 已产出签名 `.rpk`（`dist/`）：debug ~30KB、release ~22KB，均含 `META-INF/CERT`，`unzip -l` 验证为合法 zip。

---

## 九、已知风险 / 与官方文档存疑要点

1. **手环 9 Pro 本机网络能力存疑**（见第七节第 1 条）——已用 bridge 通道对冲，仍需真机确认。
2. `router.push` 的参数名：官方文档示例中同时出现 `uri` 与截图里的 `url`，本工程统一采用文档正文示例的 **`uri`**。
3. 列表渲染使用 `for="{{item in list}}"` + `tid`，事件回调直接传循环变量 `item`；
   不同 Vela 版本对「行内传参」支持可能有差异，真机若不生效需改用 `$idx` 回查。
4. `file.writeText` 写 5MB 文本在手环上可能较慢/占内存，README 已给出调低 `maxBookSizeBytes` 与
   分段（按章节/按页）落盘的可扩展方向；当前实现为整本落盘。
5. `manifest.json` 的 `minAPILevel=1`、`deviceTypeList=["watch"]` 为通用取值，真机最低 API 级别需按 SDK 对齐。
6. 网桥侧 deflate/lz4 压缩与在线流（stream）未在手环端实现（固件当前也回退为 none）；TXT 文本走 text/base64 足够。
