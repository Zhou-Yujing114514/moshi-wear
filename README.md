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
│   ├── README.md                 ←   构建/安装/网桥通道/风险清单
│   ├── package.json
│   ├── dist/                     ←   ✅ 已真实打包（aiot-toolkit@2.0.5 构建，npmmirror 源）
│   │   ├── cn.sswwgzs.moshi.novel.release.1.0.0.rpk  （23,491 B，openssl 自签证书签名）
│   │   └── cn.sswwgzs.moshi.novel.debug.1.0.0.rpk    （30,890 B，工具链调试证书签名）
│   └── src/  (manifest.json, app.ux, common/{config,http,bridge,session,library,reader}.js,
│              Login/ Shelf/ Search/ Reader/ —— Vela 工具链实际布局)
├── esp32-firmware/               ← 模块 B：ESP32 网桥固件（ESP-IDF v5.3, C，✅ 已真实编译）
│   ├── README.md                 ←   构建/烧录/排错/许可/待核实清单
│   ├── sdkconfig.defaults, partitions.csv, CMakeLists.txt
│   ├── scripts/build.sh          ←   一键：装 IDF → set-target → build → merge_bin
│   ├── dist/esp32-miwear-bridge-v1.0.0.bin   ←   ✅ 合并单 bin（1,194,816 B，offset 0x0 直烧）
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

### 设备侧风险与对策（README 已标注）
- 官方「支持明细」标注小米手环 9/9 Pro **不支持 `@system.fetch` / `@system.request`**；QuickApp 已新增 **bridge 传输通道**（`src/common/bridge.js`，与固件 qaic.c 的 FetchBridge 信封逐字段对齐：`__hs__` 握手、`tag:"fetch"` 请求、`fetch-chunk`+`fetch-ack` 分片流控、text/base64 解码）绕开该限制；`config.js` 的 `transport: 'direct' | 'bridge'` 开关切换，direct（@system.fetch）保留为备选。Vela 侧 `@system.interconnect` 收发原语收敛在 bridge.js 顶部待真机确证填写。

---

## 构建与烧录（已真实执行，详见各模块 README）

- **QuickApp（✅ 已打包）**：沙盒内用真实工具链 `aiot-toolkit@2.0.5`（npm npmmirror 源）执行 `aiot build` / `aiot release`，产出两个**已签名** `.rpk`（release=openssl 自签证书、debug=工具链调试证书），zip 结构验证通过（manifest.json / app.js / 四页面编译产物 / META-INF/CERT）。命令行：`npx aiot build` / `npx aiot release`。
- **ESP32 固件（✅ 已编译）**：沙盒内真实安装 ESP-IDF v5.3（xtensa-esp-elf gcc 13.2.0）并 `idf.py build`，最终 **0 error、0 主组件 warning**；`esptool.py merge_bin` 产出合并单 bin（DIO、40MHz、4MB，offset 0x0 直烧）。一键复现：`bash esp32-firmware/scripts/build.sh`。烧录命令：
  ```bash
  python3 -m esptool --chip esp32 -p /dev/ttyUSB0 -b 921600 \
    --before default_reset --after hard_reset \
    write_flash 0x0 esp32-firmware/dist/esp32-miwear-bridge-v1.0.0.bin
  ```

## 真机验证清单（汇总）

| 环节 | 状态 |
|---|---|
| QuickApp 打包 `.rpk`（debug+release，已签名） | ✅ 沙盒真实构建通过 |
| QuickApp JSON/JS 静态校验 + `.ux` webpack 编译 | ✅ 通过（JS 6/6，JSON 2/2） |
| QuickApp 真机安装/启动/联网/存储/翻页 | ⚠️ 需用户侧（AIoT-IDE 或 adb 推送） |
| 手环 9 Pro `@system.fetch`（direct 模式真机实测） | ⚠️ 官方标注不支持；bridge 通道已实现待联调 |
| `@system.interconnect` 真实 API 名/调用形式（bridge 收发原语） | ⚠️ 待真机确证后填入 bridge.js 顶部 |
| ESP32 固件真实编译（IDF v5.3） | ✅ 0 error / 0 主组件 warning |
| ESP32 合并 bin 产出与校验（magic/SHA256/尺寸） | ✅ 通过（1,194,816 B，SHA256 92084e07…，≪4 MB） |
| ESP32 真机烧录/握手/WiFi+BLE 共存 | ⚠️ 需用户侧（串口看 BLE_CONN/HS_STEP/FETCH_REQ 日志） |
| protobuf 字段编号 / CTR IV 怪癖 / QAIC 内层封装 | ⚠️ 待真机抓包核对（固件可配置占位） |
| authkey 提取流程 | ⚠️ 需官方 App 配对后提取，写入 main/config.h |

## 第三方静态审查修复记录（已重编译重产出）

| 问题 | 修复 |
|---|---|
| 🔴 栈溢出：bridge.c 三处 `[BR_MPS]`(64KB) 栈数组嵌套峰值约 192KB | 全部堆化（按实际长度 malloc/free，错误路径全释放）；qaic.c 4KB 流缓冲同步堆化；主栈 8192→16384 作次要余量 |
| 🔴 HTTP body 无上限：acc_push 无限 realloc | `BR_HTTP_MAX_BODY` 64KB→128KB（与可用堆匹配）硬上限，超限回可识别错误帧 `response body too large for buffer`，不 OOM |
| 🔴 chunked 响应 body 为空 | ON_DATA 无条件累积（IDF 已剥分块帧）；content-length 与累积量不一致仅记日志 |
| 🟠 协商 deflate 但未真压缩 | 本地 caps `compressions` 仅 `["none"]`，标签与数据一致 |
| 🟠 on_fetch_ack 注释声称 go-back-N | 注释与实现对齐：「丢片仅靠 30s 超时兜底」 |
| 🟡 CI：esptool 下划线参数 / aiot-toolkit 全局安装失败 | 改连字符 `--flash-mode/--flash-size/--flash-freq`；改项目内 `npm install` + `npx aiot` |
| 🟠 bridge.js 空壳（interconnect 未确证） | 保持收敛待真机；transport 默认 'direct' 且 README 如实标注双路径限制；网桥 128KB 上限错误映射为清晰用户提示 |

修复后：ESP32 重编译 **0 error / 0 主组件 warning**，新 bin SHA256 `92084e070dd983b1757826f6bacd314169be6b218b65b43c7c0e929e6a572af1`；QuickApp 重新打包并重跑校验（JSON 2/2、JS 6/6 全过）。

## GitHub 组织建议（供 MainAgent 推送时参考）

建议单个仓库 `moshi-wear`（或两个独立仓库 `moshi-novel-quickapp` + `miwear-esp32-bridge`）：

- **推荐**：单仓库 monorepo，顶层即本目录结构（`quickapp-moshi-novel/`、`esp32-firmware/`、`shared/`、`README.md`），便于协议/后端笔记随工程同步维护；两个模块本质独立，拆两仓库也合理，共享笔记各放一份即可。
- 许可：QuickApp 建议 MIT；ESP32 固件已采用 **AGPL-3.0**（因协议 BLE 层参考源为 AGPLv3），需保留署名声明。
- 敏感信息：勿把 SSH 凭据、recon 脚本中的密码、authkey 样本推入仓库；`shared/ssh-recon-transcript*.log` 与 `recon*.py` 属内部产物，建议 `.gitignore` 或仅本地保留。
