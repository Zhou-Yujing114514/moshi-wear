# 摩柿小说 · 小米手环 9 Pro 生态工程

面向小米手环 9 Pro（Vela 系统）的双模块从零自研工程：

1. **摩柿小说 QuickApp**（`quickapp-moshi-novel/`）—— 运行在手环上的 Vela JS 快应用：摩柿账号登录 → 书架/下载任务列表 → 搜索选书 → 下载 TXT 到本地 → 离线分页阅读 → 删除本地小说。
2. **ESP32 网桥固件**（`esp32-firmware/`）—— 跳过手机：ESP32 插电自启 → 连 WiFi → BLE 直连手环（0xFE95）→ 桥接手环与互联网。角色等价于「手机 + AstroBox」。

两份共享笔记（`shared/`）为两模块的共同依据：后端真实接口规范、Vela 互联协议全链路逆向。

---

## 目录结构

```
/home/user/Doubao/chats/38445255839356162/
├── README.md                     ← 本文件（总览）
├── quickapp-moshi-novel/         ← 模块 A：摩柿小说手环快应用（Vela JS）
│   ├── README.md                 ←   构建/安装/接口假设/风险清单
│   ├── package.json
│   └── src/  (manifest.json, app.ux, common/, pages/{login,shelf,search,reader})
├── esp32-firmware/               ← 模块 B：ESP32 网桥固件（ESP-IDF, C）
│   ├── README.md                 ←   构建/烧录/排错/许可/待核实清单
│   ├── sdkconfig.defaults, partitions.csv, CMakeLists.txt
│   └── main/  (l1/l2_frame, mi_crypto, mi_handshake, sar, qaic, codec,
│               ble_client, proto_pack, http_bridge, bridge, main, config)
└── shared/                       ← 共享研究产出
    ├── vela-interconnect-protocol-notes.md   ← 互联协议全链路笔记（应用层+BLE 传输层+protobuf 附录，约 600 行）
    ├── backend-api-notes.md                  ← 摩柿后端接口规范（网页实测 §0–§6 + 服务器侦察 §7/§8）
    ├── ssh-recon-transcript*.log             ← SSH 只读侦察会话日志（两轮）
    └── recon*.py                             ← 侦察脚本（可复跑）
```

---

## 关键结论（工程依据）

### 协议侧（`shared/vela-interconnect-protocol-notes.md`）
对 AstroBox 生态 4 个仓库逐行逆向，全链路字节级还原（除一处标注缺口）：
- **应用层**（来源 `AstroBox-NG-Plugin-MiWear-InterconnectFetch`，MIT）：JSON-over-QAIC；`__hs__` 握手（count 0→1→2 + caps 协商）；FetchBridge v1–v4（单消息 / 分片 / ACK 滑动窗口 go-back-N / 开放流 offset+CRC32+背压）；base64/hex/text 编码；none/deflate/lz4 解压。
- **BLE 传输层**（来源 `AstroBox-NG-Module-Bluetooth` / `-Core`，AGPLv3）：服务 0xFE95，写 0x005f（WWR）/ 通知 0x005e / 读 0x0050；L1 帧 `0xA5A5|type|seq|len|CRC16-ARC`；L2 `channel(1=Pb)|opcode(1明文/2加密/3Read)`；认证期 AES-128-CCM、数据面 AES-128-CTR；KDF=HKDF-SHA256（info="miwear-auth"）；双层 HMAC-SHA256；四步握手；SAR 流控（窗口 32 / 重试 3 / 超时 10s）。
- **protobuf 信封**（来源 `AstroBox-NG-Module-Pb`，proto2）：WearPacket（type=1/id=2，oneof：account=3 / interconnection=25）；Account 认证字段 30–33；Interconnection 用 `Mis.Payload` oneof 字段 22。
- **唯一外部依赖**：`authkey` 主密钥需由官方 App（小米运动健康）或 AstroBox 完成首次配对后提取；固件做成可配置项，无默认值。
- **唯一未闭环缺口**：`Mis.Payload.packet` 内层对 addr/pkgName 的 QAIC 私有封装格式（需抓包/逆向官方 App），已在固件中做成可配置占位。

### 后端侧（`shared/backend-api-notes.md`）
- 公开实测（novel.sswwgzs.cn 前端 JS + 接口探测）：登录 `POST /api/login`（Bearer token）、`GET /api/me`、`GET /api/tasks`（书架=下载任务列表）、`GET /api/search`、提交下载 `POST /api/download`、`GET /api/settings`。
- 服务器两轮只读侦察（用户自有服务器，单会话+重试纪律，全程只读）：主站后端是 **Go**（容器 tomato-site-app-1）+ nginx 网关容器 + CF 隧道（域名 **morax.kdns.fr / dl.1979.kdns.fr**）；服务器 nginx/代码中**无 sswwgzs.cn 配置**（公共域名与隧道域名的对应关系待用户确认）；提交任务实为 `POST /api/tasks`；下载直链形态 `/dl/{path}`（另有 `GET /api/tasks/{id}/text` 站内正文）；token 为内存随机串会话（无 JWT、无过期、重启失效，Bearer 或 HttpOnly cookie）；数据为纯 JSON 文件；限流约 5 分钟 15 本、下载链接 TTL 约 24h。
- QuickApp `config.js` 已按「默认网页实测 + 服务器实测备选」双配置实现，真机联调一处切换。

### 设备侧风险（README 已标注）
- 官方「支持明细」标注小米手环 9/9 Pro **不支持 `@system.fetch` / `@system.request`**；QuickApp 网络层已隔离在 `http.js`，真机若不可用可整体替换为手机中转或经 ESP32 网桥通道。

---

## 构建与烧录（详见各模块 README）

- **QuickApp**：用小米 AIoT-IDE 导入 `quickapp-moshi-novel/` → 构建生成 `.rpk` → 推送安装到手环 9 Pro。沙盒内已通过全部 JSON/JS 静态校验。
- **ESP32 固件**：`idf.py set-target esp32 && idf.py build`；烧录 `esptool.py merge_bin` 合并单 bin、offset 0x0、波特率 921600。沙盒无 ESP-IDF 未编译；纯逻辑模块已用 gcc 单元验证（CRC32/base64/hex/L1 帧往返）。

## 真机验证清单（汇总）

| 环节 | 状态 |
|---|---|
| QuickApp JSON/JS 静态校验 | ✅ 通过 |
| QuickApp 真机运行/联网/存储/翻页/`.rpk` 打包签名 | ⚠️ 未验证（无真机与 AIoT-IDE） |
| 手环 9 Pro 对 `@system.fetch` 支持 | ⚠️ 官方标注不支持，待真机确认 |
| 后端真实 token/接口联调 | ⚠️ 域名对应关系待用户确认后联调 |
| ESP32 纯逻辑模块单元验证 | ✅ 通过 |
| ESP32 IDF 编译/烧录/真机握手 | ⚠️ 未验证 |
| protobuf 字段编号 / CTR IV 怪癖 / QAIC 内层封装 | ⚠️ 待抓包核对（可配置占位） |
| authkey 提取流程 | ⚠️ 需官方 App 配对后提取 |

## GitHub 组织建议（供 MainAgent 推送时参考）

建议单个仓库 `moshi-wear`（或两个独立仓库 `moshi-novel-quickapp` + `miwear-esp32-bridge`）：

- **推荐**：单仓库 monorepo，顶层即本目录结构（`quickapp-moshi-novel/`、`esp32-firmware/`、`shared/`、`README.md`），便于协议/后端笔记随工程同步维护；两个模块本质独立，拆两仓库也合理，共享笔记各放一份即可。
- 许可：QuickApp 建议 MIT；ESP32 固件已采用 **AGPL-3.0**（因协议 BLE 层参考源为 AGPLv3），需保留署名声明。
- 敏感信息：勿把 SSH 凭据、recon 脚本中的密码、authkey 样本推入仓库；`shared/ssh-recon-transcript*.log` 与 `recon*.py` 属内部产物，建议 `.gitignore` 或仅本地保留。
