# 摩柿小说（Morax Novels）后端接口规范笔记

> **来源说明**：本笔记基于**公开网页观察**（主站首页 + 书源站公开 HTML/前端 JS + 公开接口 GET 实测）整理。
> **未做**任何注册、爆破、鉴权绕过、用户数据导出；**未做** SSH 服务器实地测绘。凡需登录后才能看到的响应字段，一律标注「需登录后可见，未验证」。
> **调研时间**：2026-10-03（Asia/Shanghai）
> **环境限制**：仅能访问 `https://sswwgzs.cn`（工作室主站）与 `https://novel.sswwgzs.cn`（摩柿书源站）的公开页面；未登录态。服务器端真实代码 / 数据库 / 部署拓扑**未核实**，相关项标「待核实」。

---

## 0. 站点与技术栈线索（来自公开页面）

| 项 | 值 | 证据 |
|---|---|---|
| 工作室主站 | `https://sswwgzs.cn`（标题「四生万物工作室 · Quadra Genesis Studio」） | GET 首页 |
| 书源站 | `https://novel.sswwgzs.cn`（标题「摩柿书源（四生万物工作室 x 枫丹研究院）」） | GET 首页 |
| 前端形态 | 静态页 + 单文件前端逻辑 `app.js?v=8`（约 25 KB） | 首页 `<script src="app.js?v=8">` |
| 后端技术栈（推断） | 接口风格 `{"detail":"..."}`、POST 路径 GET 返回 404，**疑似 FastAPI**（Python） | 实测 `/api/login` GET→404 `{"detail":"Not Found"}`；`/api/tasks` 未登录→401 `{"detail":"请先登录"}` |
| 站点版本 | `1.2.0` | `GET /api/settings` → `site_version:"1.2.0"` |
| 限流策略 | 每 5 分钟最多下载 15 本；下载链接有效期约 24 小时（服务器缓存可能提前清理） | 主站 FAQ 原文 |
| 鉴权方式 | Token（`Authorization: Bearer <token>`），前端存于 `localStorage["booksource_token"]` | `app.js` 第 51–55 行 `authFetch`、第 35/145/189 行 |

> 说明：摩柿书源站**没有独立的「书架」页**。用户下载历史以「下载任务列表」形式存在，即下文的 `/api/tasks`，这就是其事实上的"书架"。

---

## 1. 登录接口

### 1.1 登录 `POST /api/login`
- **完整 URL**：`https://novel.sswwgzs.cn/api/login`
- **方法**：`POST`（GET 访问返回 `404 {"detail":"Not Found"}`，实测）
- **请求头**：`Content-Type: application/json`
- **请求体（JSON）**：

  | 字段 | 类型 | 必填 | 说明 |
  |---|---|---|---|
  | `username` | string | 是 | 用户名（前端做了 `trim()`） |
  | `password` | string | 是 | 明文密码（前端直接 `JSON.stringify`，未见前端加密；是否 HTTPS 加密传输需联网确认） |

- **成功响应（HTTP 200，前端 `resp.ok` 分支）**：

  ```json
  {
    "token": "<string>",
    "user": { "username": "...", "is_admin": true, "is_staff": false }
  }
  ```
  - `token`：会话令牌字符串，前端存入 `localStorage["booksource_token"]`。
  - `user`：用户对象，前端用到字段 `username`、`is_admin`（站长）、`is_staff`（高权）。完整字段「需登录后可见，未验证」。

- **失败响应（HTTP 非 200）**：

  ```json
  { "detail": "<错误信息，如 用户名或密码错误>" }
  ```
  - 前端取 `data.detail || "登录失败"` 展示。

- **会话凭证机制**：
  - **不是 Cookie**，是 **Bearer Token**。后续所有需登录请求由 `authFetch()` 统一加请求头：
    ```
    Authorization: Bearer <token>
    ```
  - 凭证有效期：**未在公开代码中看到过期时间**；主站 FAQ 提到「会话 Cookie 改加密存储」是旧版描述，当前书源站前端明确用 Bearer token（有效期**待核实**）。

### 1.2 当前用户 `GET /api/me`
- **完整 URL**：`https://novel.sswwgzs.cn/api/me`
- **方法**：`GET`，需 `Authorization: Bearer <token>`
- **未带 token 实测**：HTTP **200**，body `{"user":null}`（**注意：不是 401**，前端靠 `if(data.user)` 判空后清空本地 token）。
- **带 token 响应**：`{"user":{ "username","is_admin","is_staff", ... }}`（`user` 完整字段「需登录后可见，未验证」）。

### 1.3 登出 `POST /api/logout`
- **完整 URL**：`https://novel.sswwgzs.cn/api/logout`
- **方法**：`POST`，需 `Authorization: Bearer <token>`。
- 前端登出后本地 `localStorage.removeItem("booksource_token")`。服务端是否真正作废 token：**待核实**。

### 1.4 注册 `POST /api/register`（记录，未测试）
- **完整 URL**：`https://novel.sswwgzs.cn/api/register`
- **方法**：`POST`，`Content-Type: application/json`
- **请求体**：`{ "username": "...", "password": "..." }`（前端注册弹窗还有第二个密码框 `password2`，仅做前端"两次密码一致"校验，不发给后端）。
- **响应**：成功后与登录一致 `{token, user}`（自动登录）；失败 `{detail}`。
- ⚠️ 调研约束：**未实际注册**，以上仅为前端代码观察。

---

## 2. 书架 / 下载任务列表接口

> 摩柿没有独立 bookshelf 路径；"书架" = 当前用户的下载任务列表。

### 2.1 我的下载任务（书架）`GET /api/tasks`
- **完整 URL**：`https://novel.sswwgzs.cn/api/tasks`
- **方法**：`GET`，**需 `Authorization: Bearer <token>`**
- **未带 token 实测**：HTTP **401**，body `{"detail":"请先登录"}`。
- **带 token 响应**（前端 `renderTasks` 消费，逐字段）：

  ```json
  {
    "tasks": [
      {
        "book_name": "凡人修仙传",
        "title": "（备用名，前端兼容 t.title）",
        "state": "done",
        "status": "（备用名，前端兼容 t.status）",
        "progress": 100,
        "download_url": "https://.../xxx.txt",
        "url": "（备用下载地址名，前端兼容 t.url）",
        "source_name": "👍 笔趣阁¹（优）",
        "source": "（备用书源名）",
        "username": "black_e",
        "error": "失败时的错误描述"
      }
    ]
  }
  ```

  | 字段 | 类型 | 示例值 | 说明 |
  |---|---|---|---|
  | `book_name` / `title` | string | `"凡人修仙传"` | 书名（前端两个名都兼容） |
  | `state` / `status` | string | `"queued"` / `"running"` / `"done"` / `"failed"` / `"canceled"` | 任务状态机，见下表 |
  | `progress` | number | `0~100` | 进度百分比（done 时前端兜底 100） |
  | `download_url` / `url` | string | `"https://.../xxx.txt"` | **TXT 直链**，仅 `done` 且有值时前端渲染「下载 TXT」按钮 |
  | `source_name` / `source` | string | `"👍 笔趣阁¹（优）"` | 书源名 |
  | `username` | string | `"black_e"` | 任务归属用户 |
  | `error` | string | `"上游超时"` | 仅 `failed` 时有值 |

  - 状态机 `state` 取值（前端 `stateLabels`）：`queued`=排队中、`running`=下载中、`done`=已完成、`failed`=失败、`canceled`=已取消。
  - **分页**：前端未传任何分页参数，一次性拉全部；是否支持 `?page=`/`?limit=`：**待核实**。
  - 字段真实键名以后端为准（前端同时兼容 `book_name|title`、`state|status`、`download_url|url` 两套，说明历史上后端改过名）。

### 2.2 全站活跃任务 `GET /api/tasks/active`
- **完整 URL**：`https://novel.sswwgzs.cn/api/tasks/active`
- **方法**：`GET`，**公开免鉴权**（前端用普通 `fetch`）。
- **实测响应**：HTTP 200，`{"tasks":[]}`（当前无活跃任务）。
- 用途：首页「下载中 N / 排队 N」药丸计数（`running`/`queued` 过滤）。
- 任务对象字段同 2.1。

---

## 3. TXT 下载

### 3.1 提交下载任务 `POST /api/download`
- **完整 URL**：`https://novel.sswwgzs.cn/api/download`
- **方法**：`POST`，**需 `Authorization: Bearer <token>`**
- **请求头**：`Content-Type: application/json`
- **请求体**（前端 `download-confirm` 事件构造）：

  | 字段 | 类型 | 必填 | 说明 |
  |---|---|---|---|
  | `book_name` | string | 是 | 书名 |
  | `author` | string | 是 | 作者（可空字符串） |
  | `source_name` | string | 是 | 书源名（如下拉框选中的 `s.source`） |
  | `book_url` | string | 是 | 来自搜索结果的 `book_url`/`url`（上游书籍页 URL） |

- **成功响应**（前端判 `data.task_id || data.id`）：
  ```json
  { "task_id": "<string/number>" }
  ```
  或 `{ "id": "<...>" }`（两套键名兼容）。
- **失败响应**：`{ "error": "..." }` 或 `{ "detail": "..." }`；HTTP 401 → 前端提示"请先登录"并弹登录框。
- ⚠️ **未实际 POST 该接口**（调研约束），以上为前端代码观察。

### 3.2 搜索 `GET /api/search`（下载前置步骤，公开）
- **完整 URL**：`https://novel.sswwgzs.cn/api/search?keyword=<URL编码书名>&max_sources=100`
- **方法**：`GET`，**公开免鉴权**。
- **实测响应（搜「凡人修仙传」）**：

  ```json
  {
    "results": [
      {
        "name": "凡人修仙传",
        "author": "忘语",
        "book_url": "http://www.biqugere.net/biquge/11054/",
        "source": "👍 笔趣阁¹（优）",
        "source_url": "http://www.biqugewx.info",
        "sources": [
          { "source": "👍 笔趣阁¹（优）", "source_url": "http://www.biqugewx.info", "book_url": "http://www.biqugere.net/biquge/11054/" },
          { "source": "爱下电子（优）",   "source_url": "https://ixdzs8.com",       "book_url": "https://ixdzs8.com/read/46710/" }
        ]
      }
    ]
  }
  ```
  - 前端另读 `data.count`（命中书源总数）。
  - `sources[]` 每个元素：`source`（书源名）、`source_url`（书源首页）、`book_url`（具体书籍页，提交下载时用）。

### 3.3 TXT 文件本身的下载规律
- **URL 形态**：`/api/tasks` 返回的 `download_url` 是一个**完整直链**，前端用 `<a href="..." download>` 直接下载，**不再走 `/api/download` 包装**。
- **响应格式**：应为**纯文本 TXT**（FAQ 原文「排队提取下载 TXT」「格式：仅支持 TXT」）；**是否 zip / 是否 JSON 包装：待核实**。
- **有效期**：约 **24 小时**（主站 FAQ），服务器缓存可能提前清理。
- **是否需鉴权**：`download_url` 本身**未观察到带 token 参数**（前端裸链接 `<a download>`），**是否靠 URL 签名/一次性 token 鉴权：待核实**。
- **真实路径前缀**（如 `/files/<id>.txt`、`/download/<token>.txt`）：**待核实**——需要一个已完成任务的 `download_url` 实测，而这需登录后产生，本次未做。

---

## 4. 辅助 / 运维接口（仅记录，非 QuickApp 核心）

| 接口 | 方法 | 鉴权 | 说明 |
|---|---|---|---|
| `/api/settings` | GET | 公开 | 实测：`{"site_title","site_version":"1.2.0","site_owner","site_notice","disabled","maintenance"}` |
| `/api/admin/users` | GET | 站长 | 前端出现，未测 |
| `/api/admin/clear-tasks` | POST | 站长 | 清空下载记录 |
| `/api/admin/set-admin` `/api/admin/set-staff` `/api/admin/delete-user` | POST | 站长 | 用户管理 |

---

## 5. QuickApp 接入建议（基于以上公开观察）

1. **登录**：`POST /api/login` → 拿 `token` → 后续所有请求头加 `Authorization: Bearer <token>`。
2. **校验登录态**：`GET /api/me` 看 `data.user` 是否为 null（注意未登录也返回 200，别只看 HTTP 码）。
3. **书架**：`GET /api/tasks` → 渲染 `tasks[]`，状态用 `state`，进度 `progress`，下载按钮绑 `download_url`。
4. **下载一本新书**：先 `GET /api/search?keyword=...` 选 `book_url` → `POST /api/download` 提交任务 → 轮询 `GET /api/tasks` 直到 `state==done` → 打开 `download_url`。
5. **轮询频率**：官方前端是 **10 秒**一次（`setInterval(... 10000)`），QuickApp 可照此。

---

## 6. 待核实项清单（诚实标注）

| # | 待核实 | 现状依据 |
|---|---|---|
| 1 | 后端真实技术栈 / 部署目录 / 容器 / nginx 映射 | 仅从 `{"detail":...}` 风格推断 FastAPI；**未做服务器实地测绘** |
| 2 | `user` 对象除 `username/is_admin/is_staff` 外的完整字段 | 需登录后看 `/api/me` |
| 3 | `/api/tasks` 是否支持分页参数（`page/limit`） | 前端未传，未实测 |
| 4 | `download_url` 真实路径前缀、是否带签名/一次性 token、响应是纯 TXT 还是 zip/JSON | 需已完成任务样本 |
| 5 | token 有效期、过期/刷新机制 | 公开代码无；旧 FAQ 提到「会话 Cookie 加密存储」但当前前端是 Bearer token |
| 6 | 登录密码是否前端加密 / 是否强制 HTTPS | 前端 `JSON.stringify({username,password})` 明文，未见前端加盐 |
| 7 | 服务端 `logout` 是否真正作废 token | 前端仅本地清除 |
| 8 | 限流（5 分钟 15 本）的超限错误码 / 错误体 | 未触发 |

> 备注：以上「待核实」项均因**未登录、未产生真实下载任务、未访问服务器**而无法确证，未做任何编造。

---

## 7. 服务器实地侦察（SSH，2026-10-03 12:09 +0800）

> **范围声明**：用户本人提供 root SSH 凭据，MainAgent 授权做**只读**侦察。全程未改/未删/未重启任何服务与文件。SSH 单次会话完成，transcript 见 `ssh-recon-transcript.log`。
> **主机**：`root@cn-fj-qz-1.server.zakocloud.com:52122`（hostname `ser450677332994`），Debian 12 bookworm，kernel 6.1.0-49-amd64，uptime 46 天。

### 7.1 部署地图（核心修正：主站是 Go 后端，不是 FastAPI）

| 组件 | 位置 | 端口 | 说明 | 证据 |
|---|---|---|---|---|
| **主站 nginx 网关** | Docker 容器 `tomato-site-nginx-1`（镜像 `nginx:alpine`），宿主 `0.0.0.0:8080 → 容器:443/tcp` | 宿主 8080 | 反代入口，挂载 `/root/tomato-site/nginx.conf` 为 `/etc/nginx/conf.d/default.conf`，另挂 baidu 验证/sitemap/robots/certs | `docker ps` + `docker inspect 5d2e166394b3` |
| **主站后端（Go）** | Docker 容器 `tomato-site-app-1`（镜像 `tomato-site-app`），**仅容器内 8080/tcp，未映射到宿主** | 容器内 8080 | 源码在 `/root/tomato-site/backend/*.go`：`main.go / handlers.go / users.go / settings.go / extractor.go / worker.go / live.go / client.go / config.go / store.go / txtpost.go`。挂载 `/root/tomato-site/data/app-data→/app/data`、`/root/tomato-site/data/tomato-data→/data`、docker.sock | `docker inspect 10a94fa91085` + `find /root -name '*.go'` |
| **书源抓取引擎** | Docker 容器 `tomato-site-tomato-1`（镜像 `73805b174f36`），容器内 18423/tcp | 容器内 18423 | 容器内路径 `/tomato-novel-downloader`，挂载 `/root/tomato-site/data/tomato-data→/data`，workdir `/data` | `docker inspect f9c3cc822919` + `docker exec` 列目录 |
| **书源管理 FastAPI（独立工具，**非**主站）** | 宿主直跑：`/root/booksource-site/venv/bin/uvicorn main:app --host 0.0.0.0 --port 8000` | 宿主 8000 | 代码 `/root/booksource-site/backend/{main.py, tasks.py}`，自带前端 `/root/booksource-site/frontend/app.js`。**这是给站长管理书源用的内部工具，不是用户访问的 novel.sswwgzs.cn 主站** | `ps aux` + `grep -rn` |
| **监控脚本** | 宿主 `/opt/server_monitor.py`（python3） | 宿主 18080 | 监控脚本，与业务 API 无关 | `ss -tlnp` + `ps aux` |
| **Cloudflare 隧道** | `cloudflared` 进程，监听 `127.0.0.1:20241`、`127.0.0.1:20242` | 仅本机回环 | 公网域名很可能经 CF Tunnel → 本机 → 宿主 8080（docker nginx） | `ss -tlnp` |
| **宿主系统 nginx** | 进程在跑（master+worker），但 `/etc/nginx/conf.d/`、`/etc/nginx/sites-enabled/` 均为空目录 | — | **真正的站点头在容器里的 `/root/tomato-site/nginx.conf`，不是宿主 /etc/nginx** | `ls /etc/nginx/conf.d` + `grep -rn` |

**关键修正（推翻前端调研的假设 #1）**：前端调研时根据 `{"detail":...}` 错误风格推断主站是 FastAPI。**实地侦察证实：novel.sswwgzs.cn 主站后端是 Go 程序**（容器 `tomato-site-app-1`，源码 `/root/tomato-site/backend/*.go`），FastAPI 8000 端口是另一个站长自用的书源管理工具。公网 API 错误体里的 `{"detail":...}` 风格，可能是 Go 框架主动模仿 FastAPI 错误格式，或 nginx 把部分路径反代到了 8000——**这一项待核实**（见 7.6）。

### 7.2 nginx 路由与 TLS

- 宿主 `/etc/nginx/` 下无任何 server 块配置（`/etc/nginx/conf.d/`、`/etc/nginx/sites-enabled/` 均空）。
- 真正生效的 nginx 配置在 **容器内**，源文件是 `/root/tomato-site/nginx.conf`（1648 字节，2026-10-02 19:33 修改），通过 bind mount 挂到 `tomato-site-nginx-1:/etc/nginx/conf.d/default.conf`。
- TLS 证书目录：`/root/tomato-site/certs/` 挂到容器 `/certs`。
- **本次未 `cat /root/tomato-site/nginx.conf`**（侦察脚本的 for 循环因通配符未展开而落空）。`server_name`、`location /api/` 是 proxy_pass 到 Go 容器 8080 还是宿主 8000、`location /downloads/` 指向哪，**待核实**（见 7.6）。
- 同目录留有多个 nginx.conf 备份（`.bak-baidu`、`.bak-monitor`、`.bak-nocache-20261002_193216` 等），说明近期在调缓存与监控。

### 7.3 token 机制（部分确证 + 待核实）

- 书源管理 FastAPI（宿主:8000）：`/root/booksource-site/backend/main.py:297 @app.post("/api/login")`，且 `main.py:220` 出现 `f"{MORAX_API}/api/login"`——即该 FastAPI **会转发/调用主站（MORAX）的登录接口**，说明主站登录确实存在，且 FastAPI 把 MORAX 后端当作上游。
- **主站 Go 后端的 token 生成逻辑、有效期、刷新机制**：代码在 `/root/tomato-site/backend/users.go`、`handlers.go` 里，本次未 grep 阅读，**待核实**（见 7.6）。
- 前端调研观察到的 `Authorization: Bearer <token>` 与实地一致（FastAPI 端有 `/api/login` 路由）。

### 7.4 /api/tasks 与 download_url 真实形态

**FastAPI 书源管理侧（/root/booksource-site/backend/tasks.py，确证）**：
- 数据模型字段含 `download_url`（tasks.py:28、43、85、111）。
- 下载完成后赋值：`task.download_url = f"/downloads/{os.path.basename(txt_path)}"`（**tasks.py:214**）。
- 即 URL 形态：**相对路径 `/downloads/<书名>.txt`**，不带签名、不带过期时间戳。

**TXT 书籍实际存放位置（确证）**：
- 宿主目录：`/root/tomato-site/data/tomato-data/`（容器 `tomato-site-app-1` 挂为 `/data`，`tomato-site-tomato-1` 也挂为 `/data`）。
- 实测文件样本（M17 输出）：`十日终焉.txt`、`冒姓琅琊.txt`、`病美人疯批会演！他老婆被钓翘嘴.txt`、`我在斩神当神秘，还是白发小萝莉.txt`、`美强惨宿主他超会撩得反派受不了.txt`、`反差傲娇学姐不会主动开口说爱我.txt`。
- 另有 `/root/booksource-site/data/downloads/` 目录存在（FastAPI 书源工具自己的下载缓存）。
- nginx 如何把 `/downloads/` URL 映射到 `/data/*.txt`（是 alias 静态文件还是反代到 Go 后端读文件流）——**待核实**（取决于未读到的 nginx.conf）。

**响应是纯 TXT 还是包装**：从 FastAPI 侧看，`/downloads/` 若是 nginx 直接 alias 到磁盘目录，则响应是**原始 TXT 文件流**（`Content-Type: text/plain`）；若走 Go 后端 handler 则可能是包装。**待核实**。

### 7.5 数据库

- `/root` 下 maxdepth 3 **未发现任何 `*.db` / `*.sqlite*` 文件**（M19 输出为空）。
- 推断 Go 后端采用文件存储（JSON/BoltDB/纯文本），数据落在挂载目录 `/root/tomato-site/data/app-data/`（容器内 `/app/data`）。**未读取具体文件内容**（只读侦察纪律 + 不导出用户数据）。
- FastAPI 书源工具侧也未发现 sqlite 库文件。

### 7.6 待核实项（本次 SSH 未覆盖到的）

| # | 待核实 | 原因 |
|---|---|---|
| A | `/root/tomato-site/nginx.conf` 全文（server_name / location 分流 / TLS 证书路径 / `/api/` 与 `/downloads/` 各自 proxy_pass 到哪） | 侦察脚本 for 循环通配符未展开，未 cat 到该文件；需下一轮 SSH 补 `cat /root/tomato-site/nginx.conf` |
| B | 主站 Go 后端路由表：`/api/login` 的 token 生成算法/有效期/是否可刷新；`/api/tasks` 的 struct 字段；`download_url` 在 Go 侧的拼接逻辑是否与 FastAPI 侧一致 | 未 `grep -n` 读 `users.go / handlers.go / store.go` |
| C | 公网 novel.sswwgzs.cn 实际走哪条链路：CF Tunnel → 宿主 8080（docker nginx）→ Go 容器，还是 CF Tunnel → 宿主 8000（FastAPI） | cloudflared config 在 `/root/.cloudflared/` 与 `/root/tomato-site/config.yml`，本次未读 |
| D | `/downloads/` 响应是纯 TXT 流还是 JSON/zip 包装；是否有鉴权中间件（未登录能否直接 GET `/downloads/xxx.txt`） | 取决于 nginx.conf 与 Go handler |
| E | 用户表/任务表的物理格式（JSON 文件？BoltDB？） | 未读 `/root/tomato-site/data/app-data/` 内文件名 |
| F | 限流「5 分钟 15 本」在 Go 侧的实现与超限错误体 | 未读 worker.go / handlers.go |
| G | 宿主 uvicorn（:8000）是否对公网暴露（宿主 firewall / cloudflared 是否也转发 8000） | `ss` 显示 0.0.0.0:8000 在监听，安全组未探 |

### 7.7 与前端调研结论的逐项核对

| 前端调研结论 | 实地侦察核对结果 |
|---|---|
| 主站后端是 FastAPI | **部分推翻**。用户访问的 novel.sswwgzs.cn 主站后端是 **Go**（容器 `tomato-site-app-1`）；宿主:8000 的 FastAPI 是站长自用的书源管理工具 |
| 错误体 `{"detail":...}` 风格像 FastAPI | 与 Go 后端事实不矛盾——Go 框架可能模仿该格式，或 nginx 把部分错误透传至 FastAPI 上游；待核实 |
| 鉴权 `Authorization: Bearer <token>` | 与 FastAPI 侧 `/api/login` 路由存在一致；Go 侧 token 逻辑未读 |
| `/api/login`、`/api/me`、`/api/tasks`、`/api/search`、`/api/download`、`/api/settings` | FastAPI 侧确证有 `/api/login`、`/api/tasks`、`/api/tasks/active`；其余端点在 Go 侧，未读代码 |
| 下载按钮绑 `download_url` | FastAPI 侧确证 `download_url = "/downloads/<basename>.txt"`，相对路径，无签名 |
| 轮询 `/api/tasks` 10 秒一次 | 与代码结构一致（FastAPI 有 `/api/tasks/active` 活跃任务接口，前端 app.js:383/424 也在调它） |
| TXT 书存放位置 | **确证**：`/root/tomato-site/data/tomato-data/*.txt`（挂载到两个容器的 `/data`） |

### 7.8 侦察纪律遵守说明

- 全程**只读**：仅 `ls / find / grep / cat / docker inspect / docker exec ls / ss / ps / uname`，未执行任何 `rm / mv / systemctl restart / docker restart / pip install / > 重定向写文件`。
- SSH 仅连接 1 次（首次即成功，未触发频率防护），结束时正常 `exit`。
- 未读取任何用户数据文件内容（未 `cat` 数据库、未 `SELECT`、未 `cat` 用户上传的 txt）。
- 完整会话 transcript：`/home/user/Doubao/chats/38445255839356162/shared/ssh-recon-transcript.log`（390 行）。

---

## 8. 服务器侦察第二轮（SSH，2026-10-03 12:14 +0800）

> 单次 SSH 会话补齐第一轮缺口。transcript：`ssh-recon-transcript-2.log`。全程只读。

### 8.1 公网域名与 TLS（重要修正）

- **真实公网域名只有两个**（cloudflared `config.yml`，tunnel ID `84682cd5-b323-4950-8325-b80a8f93bc93`）：
  - `dl.1979.kdns.fr` → `https://localhost:8080`
  - `morax.sswwgzs.cn` → `https://localhost:8080`
- **`grep -rn 'sswwgzs' /root/tomato-site/nginx.conf /root/tomato-site/backend/` 结果为空**——**本机 nginx 与 Go 代码里完全没有 sswwgzs.cn 的任何引用**。
- 推断：`novel.sswwgzs.cn` 要么是用户计划中、尚未接入本隧道的新域名；要么走 CF Dashboard 侧的 zero-config ingress（不在 config.yml 里）；要么用户前端调研时实际访问的是另一套部署。**QuickApp 对接时应以 `morax.sswwgzs.cn`（或最终确证的域名）为生产基址**。
- nginx 容器单 server 块 `listen 443 ssl http2`，证书 `/certs/cert.pem`、`/certs/key.pem`，**未写 server_name**（default_server，接受任意 Host）。
- 宿主:8080 → 容器:443 是 HTTPS 端口（不是 HTTP）；本地 curl 必须用 `https://` + `-k`，本次 `curl http://127.0.0.1:8080/...` 全部返回 `400 The plain HTTP request was sent to HTTPS port`，符合预期。

### 8.2 nginx 路由（`/root/tomato-site/nginx.conf` 全文）

| location | 动作 |
|---|---|
| `= /sitemap.xml`、`= /robots.txt`、`= /baidu_verify_*.html` | 静态文件，root `/usr/share/nginx/html` |
| `/`（兜底全部，含 `/api/*`、`/dl/*`、`/assets/*`） | `proxy_pass http://app:8080`（Docker 内网 Go 容器），透传 Host/X-Real-IP/X-Forwarded-*，强制 `Cache-Control: no-cache` |
| `/monitor` | 反代 `http://192.168.16.1:18080/monitor`，**HTTP Basic Auth**（htpasswd 在 `/certs/.monitor_htpasswd`） |
| `/monitor-api` | 反代 `http://192.168.16.1:18080/api/status`，**无认证**（注释写明供 `dl.1979.kdns.fr` 拉状态） |

**关键**：**没有独立的 `/api/` 或 `/downloads/` location**。所有 API 与下载流量全部由 nginx 透明转发给 Go 容器 `app:8080`，由 Go 代码自己路由。

### 8.3 主站 Go 后端路由全表（`handlers.go` mux，标准库 `http.ServeMux` 语法）

| 方法 | 路径 | 中间件 |
|---|---|---|
| POST | `/api/register` | 无 |
| POST | `/api/login` | 无 |
| POST | `/api/logout` | 无 |
| GET | `/api/me` | 无（未登录返回 null） |
| GET | `/api/book/{id}/meta` | 无 |
| GET | `/api/book/{id}/chapters` | 无 |
| GET | `/api/book/{id}/chapter/{index}` | 无 |
| GET | `/api/site` | 无 |
| GET | `/api/search` | **无（公开）** |
| GET | `/api/status` | 无 |
| GET | `/api/tasks` | `requireLogin` |
| GET | `/api/tasks/{id}` | `requireLogin` |
| POST | `/api/tasks` | `requireLogin` → `requireEnabled`（启用账号） |
| DELETE | `/api/tasks/{id}` | `requireLogin`（取消任务） |
| GET | `/api/bookshelf` | `requireLogin` |
| POST | `/api/bookshelf` | `requireLogin` → `requireEnabled` |
| POST | `/api/bookshelf/{bookId}/redownload` | `requireLogin` → `requireEnabled` |
| DELETE | `/api/bookshelf/{bookId}` | `requireLogin` |
| GET | `/api/tasks/{id}/text` | `requireLogin`（站内阅读器取正文） |
| **GET** | **`/dl/{path...}`** | `requireEnabled`（**注意：没有 requireLogin 包装在路由层**） |
| GET | `/dl-zip/{path...}` | `requireEnabled` |
| GET | `/assets/{file...}` | 无 |
| GET | `/manifest.json`、`/admin-manifest.json` | 无 |
| GET | `/` | 无（首页） |
| * | `/admin/*`、`/agreement`、`/reader` | 静态页 |
| * | `/api/admin/*` | `requireAdmin` / `requireManage` / `requireGrant` |

### 8.4 token 机制（`users.go` 确证）

- **不是 JWT，不是签名 token，无过期时间**。
- 实现：`Login()` 校验密码（bcrypt）后调用 `newTokenLocked()` 生成随机串，写入**进程内内存 map** `us.sessions[token] = userKey`。
- `GetByToken()` 直接查表；进程重启即全部失效。
- `Logout()` 从 map 里 delete（**真正作废**）；改密码/删用户时遍历清空该用户所有 token。
- 除了返回 JSON 里的 `token` 字段，`setSessionCookie()` 还会种一个 HttpOnly Cookie（handlers.go:369）——**前端可以二选一：Bearer header 或 cookie**。
- **会话本身不过期**（无 TTL 字段）；唯一会主动清空的是改密/删号/登出。
- 与「下载链接 TTL」区分：`download_ttl_hours`（admin settings）控制的是**下载 URL 本身的有效期**，不是会话有效期。过期后 `taskView.DownloadURL` 被置空字符串（handlers.go:1149-1152），前端看到的 `download_url` 变 `""`。

### 8.5 download_url 真实形态（关键修正，推翻第一轮推断）

- **真实前缀是 `/dl/`，不是 `/downloads/`**。
- 证据：
  - `worker.go:130`：`x.DownloadURL = prefix + url.PathEscape(rel)`，prefix 在任务完成时设为 `/dl/`。
  - `handlers.go:1408`：书架恢复时 `DownloadURL: "/dl/" + url.PathEscape(rel)`。
  - `handlers.go:48`、`store.go:42`：字段统一叫 `DownloadURL`，JSON tag 为 `download_url`。
- 完整 URL 形态示例：`https://<host>/dl/<url-encoded-相对路径>`，相对路径相对于 `/data`（容器内），对应宿主 `/root/tomato-site/data/tomato-data/`。
- 第一轮看到的 `/root/booksource-site/backend/tasks.py:214` 的 `/downloads/<basename>.txt` 是**书源管理 FastAPI 工具自己的内部约定**，**不是主站用户接口**。QuickApp 对接主站必须用 `/dl/`。
- 下载处理：`handleDownload()` 调用 `s.cl.proxyDownload(w, rel, zip)`——**Go 后端并不自己读磁盘文件，而是把请求转发给书源抓取引擎容器**（`tomato-site-tomato-1`，内网 18423）流式回吐。
- 鉴权：路由层只挂了 `requireEnabled`（账号未封禁），未在路由层挂 `requireLogin`；但登录时种了 session cookie，Go 代码内部应会从 cookie 或 header 解析身份。**QuickApp 直接 `GET /dl/...` 时需带上登录后的 cookie 或 Bearer token**（待联调确认哪种方式被接受）。
- 另提供 `/dl-zip/{path...}` 打包 zip 下载。
- 站内阅读器走 `GET /api/tasks/{id}/text`（需登录），不经过 `/dl/`。

### 8.6 数据存储（确证：纯 JSON 文件，无 SQLite/无 BoltDB）

`/root/tomato-site/data/app-data/`（挂到容器 `/app/data`）：
- `users.json`（18 KB）——用户、密码 hash、session map、书架
- `tasks.json`（5 KB）——任务列表
- `config.json`（827 B）——含 `download_ttl_hours` 等设置
- 同目录有大量 `.bak-*` 备份文件，说明用户频繁手动回滚。

**未读取任何文件内容**（只读纪律，不导出用户数据）。

### 8.7 书库物理布局（`/root/tomato-site/data/tomato-data/`）

- 每本书一个**纯数字 ID 目录**（如 `7070069331475827748/`、`7143038691944959011/`），`localFileFromDownloadURL()` 按此约定反推。
- 原始抓取稿：`<书名>.txt.orig`（大量）。
- 加工产物：`extracted/` 子目录。
- 日志：`logs/` 子目录。
- 少量散装 `.txt`（`十日终焉.txt`、`冒姓琅琊.txt`）是早期遗留。

### 8.8 与前端调研结论的最终核对表

| 前端调研 | 第二轮服务器实读 |
|---|---|
| 主站后端 FastAPI | **推翻**：标准库 `net/http` + `http.ServeMux` 写的 Go，单二进制 ~3700 行 |
| 错误体 `{"detail":...}` | 待确认：Go 代码里有 `writeJSON` 统一写 JSON 错误，但未确认字段名是 `detail` 还是别的 |
| `POST /api/login` 拿 `token` | 确证：登录返回 `token` 字符串 + 种 cookie |
| `Authorization: Bearer <token>` | 确证可用；cookie 同样可用（`setSessionCookie`） |
| `GET /api/me` 未登录返回 null | 确证：路由无中间件，代码未登录返回空 |
| `GET /api/tasks` 需登录 | 确证：`requireLogin` 包装 |
| `POST /api/download` | **路径名不准**。实际是 `POST /api/tasks`（创建下载任务），不是 `/api/download` |
| `GET /api/search` | 确证：**公开**，无需登录 |
| `GET /api/settings` | **主站没有** `/api/settings`；只有 admin 侧 `GET/POST /api/admin/settings`。前端调研的「设置」页可能是 FastAPI 书源工具的，或主站未暴露此接口 |
| `download_url` 字段 | 确证：字段名就是 `download_url`，前缀 `/dl/`（非 `/downloads/`） |
| 下载是纯 TXT | 部分确证：`/dl/` 由抓取引擎代理流式返回；`/dl-zip/` 是 zip。纯 TXT 还是包装待联调 |
| 轮询 10 秒 | 与任务状态机（`StateDone` 等）一致 |
| 限流「5 分钟 15 本」 | 代码里有 `newRateLimiter(limit, window)`，具体参数在 `config.json`，未读 |

### 8.9 QuickApp 对接修正要点（基于第二轮实读）

1. 基址用 `https://morax.sswwgzs.cn`（或用户最终确认的生产域名；**不要**用 sswwgzs.cn，服务器上没配）。
2. 登录：`POST /api/login {username,password}` → 响应里取 `token`；后续请求头 `Authorization: Bearer <token>`。
3. 书架：`GET /api/tasks`（不是 `/api/bookshelf`——那是另一个「收藏」接口，字段不同）。
4. 发起下载：`POST /api/tasks`（不是 `/api/download`）。
5. 下载链接：任务完成后 `download_url` 形如 `/dl/<url-encoded-path>`，拼到基址上；**带 Bearer token 访问**。
6. 下载链接有 TTL（`download_ttl_hours`），过期后接口返回的 `download_url` 会变空串——前端要处理空串，提示重新下载。
7. 会话 token 无过期，但**服务端重启会全部失效**，App 要做好 401 自动重登。
8. 站内阅读：`GET /api/tasks/{id}/text` 拿正文，不需要自己拼 `/dl/`。


---

## 9. 域名与路由确证（SSH 第三轮，2026-10-03 15:50 +0800）

> 单次 SSH。transcript：`ssh-recon-transcript-3.log`。全程只读。

### 9.1 重大发现：双隧道、双服务，novel.sswwgzs.cn ≠ 主站

服务器上**同时跑着两个 cloudflared 实例**（systemd 各管一个），分别用不同配置文件、不同隧道 ID，分流到不同后端：

| systemd 服务 | 配置文件 | 隧道 ID | ingress hostname | 本地服务 |
|---|---|---|---|---|
| `cloudflared.service`（主站） | `/etc/cloudflared/config.yml` | `84682cd5-...` | **`morax.sswwgzs.cn`**<br>**`morax.sswwgzs.cn`** | `https://localhost:8080`（docker nginx → Go 主站） |
| `cloudflared-booksource.service`（书源工具） | `/etc/cloudflared/config-booksource.yml` | `4ffe5f18-...` | **`novel.sswwgzs.cn`**<br>`novel.sswwgzs.cn` | `http://localhost:8000`（**FastAPI 书源管理工具**） |
| （备份） `/root/tomato-site/config.yml` | 旧配置，当前未被任何进程加载 | `84682cd5-...` | dl.1979.kdns.fr、morax.sswwgzs.cn | 同主站 |

**进程证据**（`ps aux`）：
- `/usr/local/bin/cloudflared --config /etc/cloudflared/config-booksource.yml tunnel run` → 监听 127.0.0.1:20242（书源工具隧道）
- `/usr/bin/cloudflared --config /etc/cloudflared/config.yml tunnel run` → 监听 127.0.0.1:20241（主站隧道）

### 9.2 域名 → 服务映射表（QuickApp 对接用）

| 公网域名 | 实际后端 | 用途 | QuickApp 应对接？ |
|---|---|---|---|
| **`morax.sswwgzs.cn`** | Go 主站（docker nginx :8080 → Go :8080） | 摩柿小说**用户主站** | ✅ **推荐默认 apiBase** |
| `morax.sswwgzs.cn` | 同上（别名） | 同上 | ✅ 备用 |
| `novel.sswwgzs.cn` | **FastAPI 书源管理工具（:8000）** | 站长自用的书源测试/管理后台 | ❌ 不对接（是后台工具，不是用户端） |
| `novel.sswwgzs.cn` | 同上（FastAPI:8000） | 书源工具别名 | ❌ |
| `dl.1979.kdns.fr` | 主站（旧配置残留） | 监控聚合入口 | ❌ |
| `sswwgzs.cn`（裸域） | **服务器无任何配置** | 待用户确证是否在 CF 控制台单独配了 CNAME | ❓ 待用户确证 |

**这解释了为什么前两轮前端调研在 novel.sswwgzs.cn 上看到的错误体像 FastAPI**——因为 novel.sswwgzs.cn 本来就是 FastAPI 书源管理工具的入口，根本不是 Go 主站。QuickApp 对接用户端必须把 apiBase 从 `https://novel.sswwgzs.cn` 改成 **`https://morax.sswwgzs.cn`**。

### 9.3 nginx 配置全文要点（与第二轮一致，再确认）

`/root/tomato-site/nginx.conf` 只有一个 server 块 `listen 443 ssl http2`，**无 server_name 分流**（任意 Host 都接受，靠 cloudflared 层做域名区分）：
- `location = /sitemap.xml /robots.txt /baidu_verify_*.html` → 静态文件
- `location /` → `proxy_pass http://app:8080`（Go 容器），透传 Host/X-Forwarded-*
- `location /monitor` → 192.168.16.1:18080，Basic Auth
- `location /monitor-api` → 192.168.16.1:18080/api/status，无认证
- **没有 `/api/`、`/dl/`、`/downloads/` 专用 location**，全部走 `/` 兜底到 Go

### 9.4 路由存在性实测（服务器本地 curl，https://127.0.0.1:8080）

| 路径 | GET | POST | 结论 |
|---|---|---|---|
| `/api/me` | **200** | — | 公开，未登录返回 `{"user":null}` |
| `/api/search` | **200** | — | 公开，未登录返回 `{"items":[]}` |
| `/api/tasks` | **401** | **401** | 需登录；POST 即创建任务（不是 /api/download） |
| `/api/bookshelf` | **401** | — | 需登录 |
| `/api/login` | 404 | —（未测 POST） | POST-only（路由表确证 `POST /api/login`） |
| **`/api/download`** | **404** | 405 | **不存在！** 前端调研里的 `/api/download` 是 FastAPI 书源工具的接口，Go 主站没有 |
| **`/api/settings`** | **404** | 405 | **不存在！** 同上，是 FastAPI 书源工具的接口 |
| `/api/me` 未登录响应体 | — | — | `{"user":null}` |
| `/api/search?keyword=test` 未登录响应体 | — | — | `{"items":[]}` |

> 注：POST 到不存在路径返回 405 是 Go 1.22 ServeMux fallthrough 噪音（被 `/` 兜底匹配到 index handler），不代表路由存在。

### 9.5 代码级确证

- `grep -rn '/api/download' /root/tomato-site/backend/` **零命中**——Go 后端完全没有 `/api/download` 这个路径。
- 路由注册（handlers.go mux）只出现 `/api/tasks`、`/api/tasks/{id}`、`/api/tasks/{id}/text`，没有 `/api/download`。
- 创建下载任务 = `POST /api/tasks`（body 传 book_url/book_id 等）。

### 9.6 前端调研（在 novel.sswwgzs.cn 上做的）与主站实际的差异总结

| 前端调研观察（novel.sswwgzs.cn，FastAPI:8000） | 主站实际（morax.sswwgzs.cn，Go） |
|---|---|
| `POST /api/login` | 同名，存在（Go） |
| `GET /api/me` | 同名，存在（Go，未登录 `{"user":null}`） |
| `GET /api/tasks` | 同名，存在（Go，需登录） |
| `GET /api/search` | 同名，存在（Go，公开） |
| **`POST /api/download`** | **不存在**；主站用 `POST /api/tasks` |
| **`GET /api/settings`** | **不存在**；主站只有 admin 侧 `GET/POST /api/admin/settings` |
| `download_url` 形如 `/downloads/...` | 主站实际是 **`/dl/...`**（FastAPI 书源工具用 `/downloads/`） |
| 鉴权 Bearer token | 两边都支持；主站另外种 session cookie |

### 9.7 待用户确证项（服务器无法回答）

1. **裸域 `sswwgzs.cn`**：服务器上无任何 nginx/隧道配置指向它。需要用户去 Cloudflare 控制台确认是否配了 CNAME 指向本隧道；若没配，裸域当前不可用。
2. **`novel.sswwgzs.cn` 业务定位**：服务器证据表明它是**站长自用的 FastAPI 书源管理后台**，不是面向 C 端用户的主站。请用户确认：QuickApp 要对接的是 C 端用户场景（→ 用 `morax.sswwgzs.cn`），还是站长管理场景（→ 用 `novel.sswwgzs.cn`）。
3. **CF 控制台侧 ingress 规则**：隧道域名绑定在 CF Zero Trust 控制台也可能有 dashboard-managed 规则（不在 config.yml 里）。服务器本地只能看到 config.yml 里写的 4 个 hostname。
4. **DNS 解析 IP**：服务器未装 `dig`，本次未做公网 DNS 解析。用户可本地 `nslookup morax.sswwgzs.cn` 确认落在 Cloudflare anycast 段。

### 9.8 QuickApp apiBase 最终建议

- **默认**：`https://morax.sswwgzs.cn`
- **备选**：`https://morax.sswwgzs.cn`
- **不要**：`https://novel.sswwgzs.cn`（那是后台工具）
- 所有接口路径用 §8.3 的 Go 主站路由表，不要沿用前端调研在 novel.sswwgzs.cn 上看到的 `/api/download`、`/api/settings`。

