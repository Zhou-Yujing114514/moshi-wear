# Vela Interconnect / FetchBridge 协议笔记（小米手环 9 Pro · AstroBox 网桥插件逆向）

> 面向目标：让 ESP32 固件工程师**不看原仓库**即可实现「手机/AstroBox 侧」角色——BLE 直连手环，把快应用的 fetch 请求桥接到互联网。

---

## 0. 元信息与置信度声明

| 项 | 值 | 来源 |
|---|---|---|
| 仓库 | https://github.com/AstralSightStudios/AstroBox-NG-Plugin-MiWear-InterconnectFetch | clone |
| commit | `0a7833e7bb15c3a4db233fdbc7edc6e2527b1533` | `git rev-parse HEAD` |
| 插件名/版本 | 「网桥 FetchBridge」 v1.3.2，api_level=3，wasi_version=2 | `manifest.json` |
| 许可证 | **MIT**（Copyright (c) 2026 AstralSight Studios），可自由引用/移植，须保留版权与许可声明 | `LICENSE` |
| 技术栈 | Rust edition 2024，`crate-type=cdylib`，编译目标 `wasm32-wasip2`；运行在 AstroBox NG 宿主内的 WASM 组件 | `Cargo.toml` / `.cargo/config.toml` |
| 读取时间 | 2026-10-03 (UTC+8) | — |
| 依赖关键项 | `lz4_flex 0.11`（LZ4 block 压缩）、`flate2 1 rust_backend`（raw deflate）、`waki 0.5.1`（WASI HTTP 客户端）、`serde_json`、`url`、`wit-bindgen 0.47` | `Cargo.toml` |

**置信度总览（务必先读）：**

- ✅ **高置信、可直接照做**：本文档第 3–9 节的 **JSON 级 FetchBridge 协议**（握手/caps、fetch 帧、分片、ACK 滑动窗口、v4 流、编码/压缩、CRC32）。这是 `PROTOCOL.md`（明文规范）与 `src/*.rs`（实现）**逐行互证**的结果。
- ⚠️ **重大边界（不是本仓库的内容，勿在本仓库找）**：**本仓库不包含任何 BLE GATT 布局、0xFE95 服务 UUID、AES-CCM、HMAC、MTU、连接参数。** 它是一个跑在「手机/AstroBox 宿主进程」里的 WASM 插件，只通过宿主提供的两个函数收发「已解包的 JSON 字符串」：
  - 收：`on_event(InterconnectMessage, payload)`，宿主给的是一个 **JSON 信封** `{addr, pkgName, payloadText|payload|payloadHex}`（`src/interconnect.rs:12-27` 注释）。
  - 发：`interconnect::send_qaic_message(device_addr, pkg_name, json_text)`（`wit/deps/astrobox-psys-host.wit:417`，`src/interconnect.rs:259-294`）。
  - 「QAIC」= 小米/快应用框架在 BLE 之上的互联通道；**它的 GATT UUID、加密、分包全在 AstroBox 宿主 + 手环固件里，不在这个插件仓库**。见 §10。
- ❌ **明确「未实现/存疑」**：见 §11 清单。绝不编造。

---

## 1. 分层模型（先建立正确的心智模型）

```
┌─────────────────────────────────────────────────────────────┐
│ 手环 快应用 (QuickApp, @system.interconnect)                  │
│   它把 HTTP 能力请求成一段 JSON 字符串发出去                  │
└───────────────▲──────────────────────────┬──────────────────┘
                │ QAIC interconnect 通道    │ 你要桥接的 JSON 在这里
                │ (BLE GATT, 加密/分包/重传  │
                │  = 宿主/手环固件负责)      ▼
┌─────────────────────────────────────────────────────────────┐
│ 手机 / AstroBox NG 宿主 (本插件跑在这里)                      │
│  ┌─────────────────────────────────────────────────────────┐│
│  │ 本插件 (Rust→WASM): FetchBridge 协议 v1~v4              ││
│  │   握手 caps → 收 fetch JSON → 本机 HTTP(waki)          ││
│  │   → 编码/压缩/分片 → 回 JSON                            ││
│  └─────────────────────────────────────────────────────────┘│
└──────────────────────────────────┬──────────────────────────┘
                                   │ 本机/互联网 HTTP
                                   ▼
                            互联网 (waki HTTP client)
```

**结论**：对 ESP32 而言，你需要**自己实现两层**：
1. **QAIC interconnect 的 BLE 承载层**（GATT 服务/特征/加密/分包）——本仓库**没有**，要另找小米 Vela / QAIC 开源资料或抓包（见 §11）。
2. **FetchBridge JSON 应用协议**——本仓库**完整给出**，就是下文第 3–9 节。手环快应用把 JSON 字符串从 interconnect 通道吐出来，ESP32 解 JSON、发 HTTP、再按同样格式把 JSON 吐回去即可。

---

## 2. 消息的总封装（信封）

### 2.1 宿主 → 插件（收方向，`src/interconnect.rs:12-84`）

宿主把手环消息包成一个 JSON 信封再交给插件：

```json
{ "addr": "<手环 BLE 地址>", "pkgName": "<快应用包名>",
  "payloadHex": "...", "payloadText": "<真正的 FetchBridge JSON 字符串>" }
```

- 插件从信封读 `addr`（来源手环）、`pkgName`（来源快应用包）。
- 真正的协议负载取 `payloadText`；老宿主用 `payload`（字符串或字符串化对象）；再不行就把整个 `payload` 当原文。
- `(addr, pkgName)` 二元组 = 一个会话（`src/handshake.rs:59`）。多路复用时按它区分不同快应用。

> ESP32 提示：你直连手环时，`addr` 就是你连上的那台手环；`pkgName` 由手环在 QAIC 层告诉你。你只需把「从手环收到的那串 JSON 文本」当作 §2.2 的 `data` 来解析。

### 2.2 插件 → 手环（发方向，`src/interconnect.rs:259-294`）

发送时插件把 JSON 对象序列化成紧凑字符串，调 `send_qaic_message(addr, pkg, text)`。序列化后形如：

```json
{"tag":"fetch","id":"1","resp":{...}}
```

`send_json` 的拼接规则：先放 `"tag"`，再把传入对象的所有键平铺进顶层（`wrap_with_id` 把 `id` 和 `resp` 平级放；见 `src/fetch.rs:801-808`）。

---

## 3. 握手协议（`__hs__`，`src/handshake.rs`）

### 3.1 ping-pong 时序（`PROTOCOL.md §3.1`，`src/handshake.rs:220-266`）

```
快应用(手环)                          FetchBridge(ESP32)
   |-- {tag:"__hs__", count:0, caps?} -->|
   |                                     | 记 open=true，存对端 caps
   |<-- {tag:"__hs__", count:1, caps} ---|   (若收到 count<2 则回 count+1)
   |-- {tag:"__hs__", count:2, caps} --->|
   |                                     |  count>=2：不再回包，握手完成
```

规则（`src/handshake.rs:253-265`）：收到 `count`，若 `count < 2` 就回一帧 `count+1`；`count >= 2` 视为完成，不回包。`count` 范围 `[0,2]`。
任何 `count > 0` 的包都会把会话置为 `open=true`（`src/handshake.rs:226`）。

### 3.2 `__hs__` 包字段（`PROTOCOL.md §3.2`，`src/handshake.rs:296-312`）

| JSON 键 | 类型 | 必填 | 说明 |
|---|---|---|---|
| `tag` | string | 是 | 固定 `"__hs__"`（常量 `HS_TAG`，`src/handshake.rs:13`） |
| `count` | integer | 是 | `[0,2]` 计数器 |
| `caps` | object\|null | v1 否 | 能力声明；**缺省 = v1 客户端**（不分片/不压缩/base64 基线） |

### 3.3 `caps` 对象（`PROTOCOL.md §3.3`，`src/handshake.rs:296-312` parse；`381-395` local_caps_value）

| JSON 键 | 类型 | 插件缺省值 | 说明 |
|---|---|---|---|
| `version` | int | `4`（LOCAL_PROTOCOL_VERSION） | 本端最高支持版本 |
| `chunk` | bool | `true` | 是否支持分片响应 |
| `maxChunkSize` | int | **插件对外声明 `65536`**（注意：`local_caps_value` 发的是 `MAX_CHUNK_SIZE`，不是默认 4096） | 单分片**编码前**字节数上限；插件接受范围 `[256,65536]` |
| `encodings` | string[] | `["base64","hex","text"]`（SUPPORTED_ENCODINGS） | 本端**能解码**的 wire 编码，按偏好顺序 |
| `compressions` | string[] | `["none","deflate","lz4"]` | 本端**能解压**的算法，按偏好顺序 |
| `ack` | bool | `true` | 本端是否会回 `fetch-ack`；**只有手环置 true 插件才开滑动窗口** |
| `ackWindow` | int | `4`（DEFAULT_ACK_WINDOW） | 希望在途分片数；插件与自身上限取 min 并夹到 `[1,64]`；缺省/0 = 用插件默认 |
| `stream` | bool | `true` | **v4**：支持开放流/CRC32/取消。**必须同时 chunk=true 且 ack=true** |

### 3.4 协商算法（`PROTOCOL.md §3.4`，`src/handshake.rs:324-379` `negotiate`）

```
negotiated.version   = min(peer.version, 4)
negotiated.chunked    = (本地 chunk) && peer.chunk && version >= 2
negotiated.chunkSize  = chunked ? clamp(peer.maxChunkSize==0 ? 4096 : peer.maxChunkSize, 256, 65536) : 0
negotiated.encodings    = peer.encodings  里那些本地也支持的（保留 peer 偏好顺序）
negotiated.compressions = peer.compressions 里本地也支持的
negotiated.ackWindow  = (version>=3 && chunked && 本地ack && peer.ack)
                          ? clamp(peer.ackWindow==0 ? 4 : peer.ackWindow, 1, 64) : 0
negotiated.stream     = (version>=4 && 本地stream && peer.stream && chunked && ackWindow>0)
```

逐行对应 `src/handshake.rs:325-368`。**注意**：`encodings/compressions` 的求交是「**只保留对端列表里本地也实现的项**」，并**保持对端的偏好顺序**（`filter`，`src/handshake.rs:342-351`），不是取本地顺序。

### 3.5 会话保活与过期（`src/handshake.rs:139-200`）

- 会话 key = `(addr, pkg)`，存 `open / last_seen / caps`。
- 任何 `__hs__`、`fetch` 请求、`fetch-ack` 都会刷新 `last_seen`（`record_activity`，`src/handshake.rs:175-188`）。
- **空闲 `SESSION_IDLE_TIMEOUT = 600s`（10 分钟）后丢弃会话**（`src/handshake.rs:18`）。丢弃后能力回到 v1 基线。
- 若插件收到 fetch 但还没握手，会主动发一帧 `count:0` 并**乐观**标记 open；此时因为还没拿到手环 caps，首个响应走 v1 安全路径（`src/handshake.rs:272-294` `ensure_open`）。

---

## 4. Fetch 请求：手环 → ESP32（`tag:"fetch"`，`src/fetch.rs:38-189`）

### 4.1 请求 JSON

```json
{
  "tag": "fetch",
  "id": "<可选请求 id，用于多路复用>",
  "url": "https://example.com/api",
  "options": {
    "method": "GET",
    "headers": { "Accept": "application/json" },
    "body": "<请求体字符串>",
    "raw": false,
    "stream": true,
    "fixedChunks": true,
    "followRedirects": true
  }
}
```

### 4.2 字段表（`PROTOCOL.md §4`，`src/fetch.rs:46-71`）

| JSON 键 | 类型 | 缺省 | ESP32 侧如何处理 |
|---|---|---|---|
| `id` | string | 无 | 响应/分片/ACK 原样带回，用于配对 |
| `url` | string | 必填 | `Url::parse`，非法 URL 报错 |
| `options.method` | string | `GET` | 转大写；映射 GET/POST/PUT/DELETE/HEAD/PATCH/OPTIONS/CONNECT/TRACE（`src/fetch.rs:822-835`） |
| `options.headers` | object<string> | 无 | 键值表，值非字符串则 `toString`；非法 header 名跳过 |
| `options.body` | string | 无 | 请求体；**只支持字符串**，二进制由手环自行 base64 |
| `options.raw` | bool | `false` | `true`=响应当字节返回，不做 UTF-8 解码 |
| `options.stream` | bool | 见下 | v4。`true` 强制流；`false` 强制有限 v1-v3；缺省时对 `audio/*`/`video/*` 或 `Content-Length≥64KiB` **自动开流**（`should_stream`，`src/fetch.rs:326-345`） |
| `options.fixedChunks` | bool | `false` | 仅 v4：合并短读，除尾帧外每帧解码后严格 = chunkSize |
| `options.followRedirects` | bool | `false` | 自动跟随 301/302/303/307/308，**最多 10 跳**（`MAX_REDIRECTS`） |

**重定向语义（`src/fetch.rs:293-324`）**：
- 301/302 且原方法 POST → 改 GET、丢弃 body；303（除 HEAD）→ 改 GET；307/308 → **保留方法和 body**。
- 跟随 `Location`（支持相对 `Url::join`）。
- **跨源跳转时删除** `Authorization`/`Proxy-Authorization`/`Cookie`/`Host`（防凭据泄漏）；改方法时删 `content-length/type/encoding/transfer-encoding`。
- 超过 10 跳 → 报错响应。

**关键**：v3 **未对上行请求体做任何压缩/分片**（`PROTOCOL.md §4` 注）。请求体就是明文字符串。

---

## 5. Fetch 响应：ESP32 → 手环（`tag:"fetch"` 头部 + 后续帧）

> 不管分片与否，**头部永远是 `tag:"fetch"`**，并保留 v1 六个核心字段 `{ok,status,statusText,headers,body,raw}`。新增字段全是可选（`PROTOCOL.md §5`）。

### 5.1 v1 单消息（`src/fetch.rs:566-631`）

```json
{ "tag":"fetch", "id":"<原 id>",
  "resp": {
    "ok": true, "status": 200, "statusText": "OK",
    "headers": {"content-type":"application/json"},
    "body": "<编码后的字符串>",
    "raw": false,
    "bodyEncoding": "hex",      // 可选；缺省按 raw 推断(raw=false→text, raw=true→base64)
    "compression": "deflate",   // 可选；缺省 none
    "originalBytes": 12480      // 仅 compression!=none 时出现
} }
```

**发送端编码顺序（`build_plan`，`src/fetch.rs:451-544`）**：
1. `pick_compression`（`485-499`）：无 caps 或 `body < 256 字节`（`COMPRESS_MIN_SIZE`）→ `none`；否则取 `negotiated.compressions` 第一项。
2. `compress(body, algo)`（`codec.rs:88-105`）。
3. 是否分片：`chunked && compressed_len > chunkSize`。
4. `pick_encoding`（`509-544`）：`text` 仅当 **未压缩 && 未分片 && raw==false && payload 是合法 UTF-8**；否则按 `negotiated.encodings` 顺序选 base64/hex；对端没声明 → v1 规则（UTF-8 文本用 text，否则 base64）。

**接收端解码顺序（`PROTOCOL.md §5.1`，与发送互逆）**：
```
encoded string --(bodyEncoding decode)--> bytes
bytes          --(compression decompress)--> original bytes
original bytes --(raw ? 保留 : UTF-8 解码)--> 最终 body
```

**单消息保护线**：未协商分片时，编码后 `body` 字符串长度 > `MAX_UNCHUNKED_WIRE_LEN = 16384` 字符 → **不发送大包**，改发 §5.4 错误响应（`src/fetch.rs:578-592`）。

### 5.2 v2/v3 分片（`src/fetch.rs:633-735`）

**头部帧（`tag:"fetch"`）**：
```json
{ "tag":"fetch", "id":"<原 id>",
  "resp": {
    "ok":true, "status":200, "statusText":"OK", "headers":{...},
    "body":"", "raw":true,
    "chunked": true,
    "totalBytes": 20480,   // = 所有 chunk 解码后长度之和(=压缩后字节数)
    "chunkSize": 4096,
    "chunkCount": 5,
    "bodyEncoding":"base64",
    "compression":"lz4",    // 可选
    "originalBytes": 65536, // 可选，仅 compression!=none
    "ack": true             // 可选；true=启用 ACK 流控，手环必须回 fetch-ack
} }
```

**数据帧（`tag:"fetch-chunk"`）**：
```json
{ "tag":"fetch-chunk", "id":"<原 id>", "seq":0, "total":5, "data":"<本分片编码串>" }
```

| 字段 | 含义 |
|---|---|
| `resp.chunked` | 固定 true |
| `resp.totalBytes` | 分片**解码后**总长（压缩后字节数） |
| `resp.chunkSize` | 单分片解码后字节数（尾片可更小） |
| `resp.chunkCount` | 分片总数 |
| `chunk.seq` | `0 .. chunkCount-1` |
| `chunk.total` | 冗余，应 == `chunkCount` |
| `chunk.data` | 本分片经 `bodyEncoding` 编码后的字符串 |

**重组（接收端）**：按 `id` 建缓冲 → 收齐 `chunkCount` 片 → 拼接总长 == `totalBytes` → 解压到 `originalBytes` → raw 判定。

**无流控（v2，`ackWindow==0`）**：插件在一个事件里 `for` 循环把所有 `fetch-chunk` 背靠背发完（`src/fetch.rs:723-734`）。**大响应会死锁**（详见 §5.3 动机），故手环大响应必须开 ACK。

### 5.3 v3 ACK 流控（滑动窗口，`src/transfer.rs`）

**`fetch-ack` 帧（手环 → 插件）**：`{"tag":"fetch-ack","id":"<id>","ack":3}`
- `ack` = **下一个仍缺失的连续分片序号**（= 已按序连续收到的片数）。收齐时 `ack == chunkCount`。手环**乱序缓存**，空洞补上后 `ack` 一次性跳到新前沿（`PROTOCOL.md §5.2.1`）。

**发送端窗口状态机（`src/transfer.rs`）**：
- 状态：`base`（首个未确认片=最大已收 ack）、`next`（下一个待发）、`window=ackWindow`、`retx_base`（重传过的停滞点）。
- `begin`（`165-215`）：发头部后，泵出 `seq ∈ [0, W)` 首批，**然后返回**（把控制权交还宿主排空 BLE）。
- `on_ack`（`229-277`）：
  - `ack = min(ack, chunkCount)` 防越界；
  - `ack > base` → `base=ack`，泵 `seq ∈ [next, base+W)` 且 `< chunkCount`；
  - 停滞（`ack` 不前进但有在途）且本 base 没重传过 → `next=base`，**go-back-N 重发整窗**，并记 `retx_base=base`（**每个停滞点只重传一次**，防重传风暴）；
  - `base >= chunkCount` → 删除传输。
- 超时：**30s 无 ACK 丢弃**（`TRANSFER_TIMEOUT`，`transfer.rs:45`），由宿主每 5s 的 `Timer` 事件调 `prune_idle` 清理（`lib.rs:57-60`）。

**时序（`chunkCount=5,W=4`）**：头部 → 发 seq0..3 → 手环每收一片回一个 ack=1,2,3... → 插件随窗口前移补发 → 最终 ack=5 完成。

**硬性要求**：手环必须**增量**回 ACK（每收一片回一次，或至少每 ⌈W/2⌉ 片回一次）。若「收齐才回」，`chunkCount>W` 时窗口在第 W 片后停住 → 死锁。

### 5.4 错误响应（`src/fetch.rs:789-799`）

永远用 v1 单消息形态，无任何 v2/v3 元信息：
```json
{ "tag":"fetch","id":"<id>",
  "resp":{"ok":false,"status":0,"statusText":"<错误描述>","headers":{},"body":"","raw":false} }
```

---

## 6. v4 开放长度流（`tag:"fetch-stream*"`，`src/stream.rs`）

用途：在线音视频、无法整体缓冲的大文件。与 v3 有限分片并存：`resp.chunked=true` 仍表示 v2/v3；**`resp.stream=true` 才是 v4**。流式请求必须带**非空且并发唯一**的 `id`（否则回 v1 错误，`src/fetch.rs:159-170`）。

### 6.1 流头部（`src/fetch.rs:355-428` `send_streaming`）

```json
{ "tag":"fetch","id":"media-1",
  "resp": {
    "ok":true,"status":200,"statusText":"OK","headers":{"content-type":"audio/mpeg"},
    "body":"","raw":true,
    "stream": true,
    "chunkSize": 4096,
    "fixedChunks": true,         // 仅请求要了才出现
    "bodyEncoding": "base64",   // stream 只可能 base64 或 hex（stream_encoding，fetch.rs:347-353）
    "compression": "none",       // v4 固定 none（避免跨帧压缩状态）
    "ack": true,
    "checksum": "crc32",
    "contentLength": 12345678   // 仅源站给了有效 Content-Length 才出现
} }
```

### 6.2 数据/结束帧（`src/stream.rs:178-206` `flush`）

数据帧：
```json
{ "tag":"fetch-stream","id":"media-1",
  "seq":0,"offset":0,"data":"<base64|hex>","crc32":"9a71bb4c" }
```
- `seq` 从 0 严格递增；手环可乱序缓存，只按连续消费。
- `offset` = 本帧在整个响应体的**绝对字节偏移**，直接给 `writeArrayBuffer`。
- fixedChunks 时非尾帧 `offset == seq*chunkSize`；动态模式必须用 `offset`。
- `crc32` = **编码前本帧原始字节**的 IEEE CRC-32，**小写 8 位 hex**（`{:08x}`）。校验失败不得推进 ACK。

结束帧（HTTP EOF，同一序列）：
```json
{ "tag":"fetch-stream","id":"media-1",
  "seq":417,"offset":1703936,"data":"","crc32":"00000000","final":true,"totalBytes":1703936 }
```
- `data:""`,`crc32` 全 0；`final:true`；`totalBytes` = 所有非结束帧解码后累计字节。结束帧也占一个 seq、也受 ACK/重传保护。

### 6.3 ACK / 背压 / 重传（`src/stream.rs:280-335`）

`{"tag":"fetch-stream-ack","id":"media-1","ack":12}`
- `ack` = 「下一个缺失连续帧序号」。发送端只在 ACK 前移后释放缓冲、从 HTTP 源补读恰好填满窗口的数据 → 慢播放端对下载端**背压**。
- 重复 ACK → 对当前未确认窗 go-back-N 重传一次；前沿推进后才允许下一次。
- **v4 不允许无 ACK 流式发送**。

### 6.4 取消/错误/清理

- 取消（手环→插件）：`{"tag":"fetch-stream-cancel","id":"media-1","reason":"player closed"}` → 立即删状态、drop HTTP 源。
- 错误（插件→手环）：`{"tag":"fetch-stream-error","id":"media-1","message":"read streaming body failed: ..."}`。
- 连续 **30s** 无 ACK 清理；宿主每 5s Timer 主动清理；迟到的 ACK/cancel 静默忽略。
- 并发流上限 **8 个**（`MAX_CONCURRENT_STREAMS`，`stream.rs:28`）。
- 内存上界：每流至多缓存 `ackWindow*chunkSize + chunkSize` 字节。

---

## 7. 编码（`bodyEncoding`，`src/codec.rs:12-42,111-170`）

| 值 | 字节→字符串 | 膨胀 | 说明 |
|---|---|---|---|
| `text` | 直接 UTF-8 文本 | 1.0× | 仅未压缩/未分片/合法 UTF-8 |
| `base64` | **RFC 4648，带 `=` 填充**，字母表 `ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/` | ~1.33× | **v1/v2 基线，任何端必须支持** |
| `hex` | **小写** `0-9a-f`，两字符一字节 | 2.0× | 解码极简，适合 MCU |

测试向量（`codec.rs:182-192`）：hex(`[0,1,0xFE,0xFF]`)=`"0001feff"`；base64(`"hello"`)=`"aGVsbG8="`。

---

## 8. 压缩（`compression`，`src/codec.rs:44-105`）

| 值 | 实现 | 字节格式 |
|---|---|---|
| `none` | 直接拷贝 | 原始字节 |
| `deflate` | `flate2` DeflateEncoder，**raw deflate（RFC 1951，无 zlib 头）** | 裸 deflate 块 |
| `lz4` | `lz4_flex::compress(data)` | **LZ4 block 格式（裸 block，非 frame）** |

- 触发：`body ≥ 256 字节`（`COMPRESS_MIN_SIZE`）才压，短包不压（`codec.rs:86`，`fetch.rs:489`）。
- 解压时手环需提供目标长度（LZ4 block 需要 `originalBytes`，`PROTOCOL.md §9.1` 客户端 `lz4Decompress(bytes, originalBytes)`）。
- **v4 流固定 `compression:none`**（跨帧不维护压缩状态）。

---

## 9. CRC32（仅 v4 流，`src/codec.rs:121-131`）

- 标准 IEEE CRC-32：初值 `0xFFFFFFFF`，多项式（反射）`0xEDB88320`，最终异或 `0xFFFFFFFF`。
- 输出小写 8 位 hex。
- 测试向量（`codec.rs:177-180`）：`crc32("123456789") = 0xCBF43926`；`crc32("") = 0`。

---

## 10. 所有魔数 / 常量 / 固定串汇总表

| 常量 | 值 | 出处 |
|---|---|---|
| 握手 tag | `"__hs__"` | `handshake.rs:13` |
| 请求/响应 tag | `"fetch"` | `fetch.rs:16` |
| 分片 tag | `"fetch-chunk"` | `fetch.rs:20` |
| 分片 ACK tag | `"fetch-ack"` | `fetch.rs:25` |
| v4 数据帧 tag | `"fetch-stream"` | `stream.rs:21` |
| v4 ACK tag | `"fetch-stream-ack"` | `stream.rs:22` |
| v4 取消 tag | `"fetch-stream-cancel"` | `stream.rs:23` |
| v4 错误 tag | `"fetch-stream-error"` | `stream.rs:24` |
| 协议版本 | 4 | `handshake.rs:25` |
| 默认 chunkSize | 4096 B | `handshake.rs:32` |
| chunkSize 范围 | `[256, 65536]` | `handshake.rs:36,39` |
| 默认 ackWindow | 4 | `handshake.rs:48` |
| ackWindow 范围 | `[1, 64]` | `handshake.rs:52,53` |
| 压缩最小字节 | 256 B | `codec.rs:86` |
| 单消息上限 | 16384 字符 | `fetch.rs:30` |
| 自动开流阈值 | Content-Length ≥ 65536 B（64KiB） | `fetch.rs:33` |
| 重定向上限 | 10 跳 | `fetch.rs:36` |
| 会话空闲超时 | 600 s | `handshake.rs:18` |
| 分片/流 ACK 超时 | 30 s | `transfer.rs:45` / `stream.rs:26` |
| 清理 Timer 周期 | 5 s（宿主侧；插件只在 Timer 事件里 prune） | `lib.rs:57-60` |
| 并发 v4 流上限 | 8 | `stream.rs:28` |
| base64 字母表 | `A..Z a..z 0..9 + /`，`=` 填充 | `codec.rs:134` |
| hex 字母表 | `0123456789abcdef`（小写） | `codec.rs:163` |
| CRC32 多项式 | 反射 `0xEDB88320`，init/xorout `0xFFFFFFFF` | `codec.rs:122-130` |
| 历史包名占位 | `"com.fetch"` | `state.rs:11` |
| 持久化配置文件 | `./miwear-interconn-fetch.config.json` | `persist.rs:11` |

---

## 11. 「未实现 / 存疑」清单（务必知晓，勿在本仓库找答案）

1. ❌ **BLE GATT 布局（服务/特征 UUID、0xFE95、读/写/通知/指示方向、MTU、分包大小、连接参数）——本仓库完全没有。** 插件只调 `send_qaic_message(addr,pkg,text)`，承载在 AstroBox 宿主。`wit` 里另有一个 `transport` 接口（`protocol = XIAOMI-VELA-V5-PROTOBUF`，`send(device_addr, list<u8>)`），但**本插件未使用它**（`wit/deps/astrobox-psys-host.wit:14-24`）。→ ESP32 的 GATT 层需另查小米 Vela / QAIC 互联规范或抓包。
2. ❌ **认证握手（密钥/口令派生、AES-CCM 的 key/IV/nonce、HMAC 双层校验）——本仓库完全没有。** 这里的「握手」只是 `__hs__` 的 `count` ping-pong + caps 声明，**无任何加密/签名**。链路安全由宿主 QAIC 层负责，插件不可见。
3. ❌ **`payloadHex` 信封字段**：代码注释提到但 `parse_message` 实际只读 `payloadText`/`payload`，**`payloadHex` 无解析分支**（`interconnect.rs:37-40`）。ESP32 若走裸 BLE，十六进制 payload 如何编码要自行确认。
4. ⚠️ **QAIC 单帧最大字节**：代码只说「受底层 QAIC 通道限制」（`PROTOCOL.md §2`），**未给具体数值**；`MAX_UNCHUNKED_WIRE_LEN=16384` 是应用层保护线，不等于 BLE MTU。
5. ⚠️ **手环侧快应用实现**：本仓库是「网桥/手机侧」。手环上 `@system.interconnect` 的具体收发 API 形态只在 `PROTOCOL.md §9` 的参考 JS 里出现，非本仓库代码。
6. ⚠️ **waki 0.5.1 的 HTTP chunk 读取是阻塞式**（`PROTOCOL.md §10.16`），插件作者注明「可挂起而不阻塞」的直播源需宿主另提供 future 化 stream-read import——当前实现未达完全非阻塞。ESP32 自行实现时需注意。
7. ⚠️ **`addr`/`pkgName` 的具体格式**（MAC 字符串？包名如 `com.x.y`？）代码未约束，直接透传。ESP32 按 QAIC 层约定即可。

---

## 12. ESP32 落地最小实现清单（照做即可）

1. 实现 QAIC interconnect BLE 承载层（**本仓库未提供，另查/抓包**）。
2. 收到手环 JSON 文本 → 按顶层 `tag` 分发：
   - `__hs__`：按 §3.1 回 `count+1`（若 `<2`），解析 `caps` 并按 §3.4 协商。
   - `fetch`：按 §4 发本机 HTTP（注意重定向/跨源删凭据），按 §5/§6 组响应。
   - `fetch-ack`：喂给 §5.3 滑动窗口状态机。
   - `fetch-stream-*`：喂给 §6 流状态机，维护 `seq/offset/crc32` 与窗口背压。
3. 编码/压缩：base64(RFC4648,带`=`)、hex(小写)、LZ4 block(需 originalBytes)、raw deflate。
4. 常量全部照 §9 表。
5. 会话保活 600s；ACK 超时 30s；清理 Timer。

---

# 【续篇】BLE 传输层（0xFE95 / AES 握手 / 分包流控）

> 来源仓库（均为 AstralSightStudios 组织下开源，已 clone 到本机逐行核对）：
> - `AstroBox-NG-Module-Bluetooth`（AGPLv3 + 额外署名条款）——GATT 连接/发现/读写。
> - `AstroBox-NG-Module-Core`（AGPLv3）——`device/xiaomi/` 完整小米协议栈（L1/L2/握手/SAR/加密）。
> - 上层应用协议见原插件 `MiWear-InterconnectFetch`（MIT）。
> ⚠️ 许可证差异：**BLE 这一层是 AGPLv3（且要求署名）**，与 FetchBridge 的 MIT 不同。移植到 ESP32 固件时须遵守 AGPLv3 + 署名要求。

## B1. GATT 布局（服务/特征/方向）

| 项 | UUID / 值 | 方向 | 用途 | 来源 |
|---|---|---|---|---|
| 服务 | **0xFE95**（`XIAOMI_BLE_V2_SERVICE_UUID="fe95"`；iOS 全名 `0000fe95-0000-1000-8000-00805f9b34fb`） | — | 小米私有服务 | `Module-Core/src/constants.rs:1`；`Bluetooth/stdimp.rs:307,433` |
| TX 特征（手机→手环写） | **0x005f**（`XIAOMI_BLE_V2_TX_UUID="005f"`；全名 `0000005f-…-00805f9b34fb`） | **Write Without Response** | 手机发往下行 | `constants.rs:2`；`stdimp.rs:31,447,639` |
| RX 特征（手环→手机通知） | **0x005e**（`XIAOMI_BLE_V2_RX_UUID="005e"`；全名 `0000005e-…-00805f9b34fb`） | **Notify** | 手环上行；手机订阅 | `constants.rs:3`；`stdimp.rs:32,445,694` |
| Service 特征（控制点） | **0x0050**（`"0050"`） | Read | 订阅 notify 后会对它 read 一次（握手/信息） | `stdimp.rs:30,449,727-734` |

- 发现逻辑：遍历服务，UUID 含子串 `"fe95"` 即小米；特征 UUID 含 `"005e"`→recv、`"005f"`→sent、`"0050"`→service（`stdimp.rs:214-233,431-458`）。
- 连接顺序：`connect_device` → **`pair()`（绑定/配对，即 BLE 层加密）** → `discover_services` → 发现特征（`stdimp.rs:567-582`）。
- **MTU**：本层**不硬编码 MTU**，运行时通过特征的 `max_write_len_async()` 读「每次无响应写的最大字节」（`stdimp.rs:648-670`）。ESP32 上对应你协商到的 ATT MTU−3（有效负载字节）。
- 写操作：手机→手环用 **`write_without_response`**（写命令，不带响应，`stdimp.rs:639-642`）。

## B2. 字节级帧格式（L1 + L2 两层封装）

### B2.1 L1 链路层帧（`packet/v2/layer1.rs`，小端）

| 偏移 | 长度 | 字段 | 说明 |
|---|---|---|---|
| 0 | 2 | magic | **`0xA5A5`**（小端字节流为 `A5 A5`） |
| 2 | 1 | `type|frx` | 低 4 位=类型；bit4(0x10)=frx |
| 3 | 1 | seq | u8 序号（回绕） |
| 4 | 2 | length | u16 LE = payload 字节数 |
| 6 | 2 | crc | u16 LE = **CRC16-ARC(payload)** |
| 8 | N | payload | L2 帧 |

- 类型 `L1DataType`：0=Nak，1=Ack，2=Cmd，3=Data（`layer1.rs:5-10`）。
- **CRC16-ARC**：多项式 `0xA001`（=反射 0x8005），初值 `0x0000`，对 payload 计算（`layer1.rs:86-99`）。
- L1 头固定 **8 字节**。最小合法包=8 字节。

### B2.2 L1 Cmd（链路建立命令，`layer1cmd.rs`）

payload 首字节=命令码，其后为 **TLV**：`key(1B) | len(2B LE) | value`。
- 命令码：1=startReq，2=startRsp，3=stopReq，4=stopRsp。
- 配置 key：1=version(3B)，2=MPS(2B LE)，3=TX_WIN(2B LE)，4=SEND_TIMEOUT(2B LE)，5=DEVICE_TYPE(1B)，6=DEVICE_NAME(bytes)，7=OS_VERSION(3B)。
- **startReq 协商值（`sar/mod.rs:116-123`）**：version=`1.0.0`，**MPS=64512**（重组缓冲上限），**TX_WIN=32**，SEND_TIMEOUT=**10000ms**。
  - MPS=64512 是逻辑最大 payload，**不是 BLE MTU**；落到 `005f` 的实际写字节受 `max_write_len`（MTU−3）限制，由下层自动切片。

### B2.3 L2 通道层帧（`packet/v2/layer2.rs`）

| 偏移 | 长度 | 字段 | 说明 |
|---|---|---|---|
| 0 | 1 | channel | 通道号 |
| 1 | 1 | opcode | 操作码 |
| 2 | N | payload | （可能是密文） |

- **channel**：1=Pb(protobuf，**interconnect/QAIC JSON 就走这里**)，2=Mass，3=MassVoice，4=FileSensor，5=FileFitness，6=Ota，7=Network，8=Lyra，9=Research，10=MultiModal。
- **opcode**：1=Write(明文)，2=WriteEnc(**加密**)，3=Read。
- `WriteEnc` 的 payload = `L2Cipher.encrypt(protobuf(WearPacket))`。

> 即：FetchBridge 的 JSON 文本 → 被包成 protobuf `WearPacket` → L2(channel=Pb,opcode=WriteEnc) → 加密 → L1(Data) → 切成 MTU 片 → `005f` 无响应写。手环侧反向解包后把 JSON 交给快应用。

## B3. 加密与认证（关键！与直觉不同的两层）

### B3.1 握手前：AES-128-CCM 仅用于认证阶段（`crypto/aesccm.rs`）

- 参数：**key=16B，nonce=12B，tag=4B**（`Ccm<Aes128,U4,U12>`），输出=密文‖4B tag，AAD 可空。

### B3.2 握手时序（Pb 通道，密钥建立前为明文，`components/auth.rs`）

```
手机                                      手环
 |-- [1] Account.AuthAppVerify{ app_random(16B) } -->|   手机随机数 p_random
 |                                                 |
 |<-- [2] Account.AuthDeviceVerify{               |   手环随机数 w_random(16B)
 |        device_random(16B), device_sign(32B) }    |
 |                                                 |
 |   手机做 KDF + 校验，然后回 [3]：
 |-- [3] Account.AuthAppConfirm{                  |
 |        app_sign(32B),                           |
 |        encrypt_companion_device(CCM密文‖tag) }   |
 |                                                 |
 |<-- [4] Account.AuthDeviceConfirm{ confirm_result }|  true=认证成功
```

### B3.3 KDF 密钥派生（`kdf_miwear`，`auth.rs:354-383`）

输入：`secret_key = authkey`（32 字符 hex 串解码成 **16 字节主密钥**）、`phone_nonce=p_random(16B)`、`watch_nonce=w_random(16B)`。

1. `init_key = phone_nonce ‖ watch_nonce`（32B）。
2. `hmac_key = HMAC-SHA256(key=init_key, msg=secret_key)` → 32B（PRK）。
3. HKDF 扩展，info tag = ASCII `"miwear-auth"`，counter=1..3：
   `T_i = HMAC-SHA256(PRK, T_{i-1} ‖ "miwear-auth" ‖ counter)`，拼接取前 64B → `block64`。
4. 切分：
   - `dec_key   = block64[0..16]`（**手环→手机**数据密钥）
   - `enc_key   = block64[16..32]`（**手机→手环**数据密钥）
   - `dec_nonce = block64[32..36]`（4B）
   - `enc_nonce = block64[36..40]`（4B）

### B3.4 双层 HMAC 校验（`auth.rs:258-273`）

- **手环签名校验**：`expect = HMAC-SHA256(key=dec_key, msg = w_random ‖ p_random)`（32B），必须等于手环发来的 `device_sign`；不等 → 报「AuthKey 错误」。
- **手机签名**：`app_sign = HMAC-SHA256(key=enc_key, msg = p_random ‖ w_random)`（32B），回给手环。
- 即：**方向相反、密钥相反、拼接顺序相反**——这就是「双层 HMAC」。

### B3.5 CCM 加密 CompanionDevice（`auth.rs:290-301`）

- 把 `CompanionDevice{device_type, device_name="AstroBox", app_capability=0xFFFFFFFF,…}` 编成 protobuf。
- **CCM nonce = enc_nonce(4B) ‖ counterHi(4B LE=0) ‖ counterLo(4B LE=0)** = 12B；key=enc_key(16B)；AAD 空。
- 输出 = 密文‖4B tag，填入 `encrypt_companion_device`。

### B3.6 数据面加密（⚠️ 注意：不是 CCM，是 CTR）

`packet/cipher.rs:178-194` 的 `V2L2Cipher`：
- **手机→手环**：`AES-128-CTR(key=enc_key, IV=enc_key, data)`。
- **手环→手机**：`AES-128-CTR(key=dec_key, IV=dec_key, data)`。
- **实现怪癖**：CTR 的 IV 直接复用 16B 密钥本身（key 与 IV 同值）；派生出来的 `enc_nonce/dec_nonce` **存了但数据面未使用**。ESP32 实现时照此复现即可（与手环互操作），但这是非标准用法，建议留配置开关。
- **数据面完整性靠 L1 的 CRC16-ARC**，CTR 无认证 tag。

## B4. 分片与流控（SAR，`sar/mod.rs`）

- seq 为 u8 回绕计数器；每包分配一个 seq（`enqueue`）。
- 发送窗口 `LOCAL_TX_WIN=32`；`MAX_PACKET_RETRIES=3`；`SEND_TIMEOUT=10s`。
- **ACK/Nak**：L1 type=1(Ack)/0(Nak)，seq 字段=被确认序号，payload 空；收到数据回 ACK，错包回 Nak。
- 累计 ACK 定时器 + 重传；MPS=64512 为重组上限。
- 落到 BLE：`005f` 用无响应写，每包不超过 `max_write_len`（=MTU−3）。

## B5. 与 FetchBridge（本笔记 §1–§12）的衔接

```
快应用 JSON("fetch"…)
  → protobuf WearPacket (Pb 通道)
  → L2 WriteEnc: AES-128-CTR 加密
  → L1 Data: +magic 0xA5A5/seq/len/CRC16-ARC
  → SAR 切成 ≤max_write_len 片
  → 005f write-without-response  → 手环
手环 → 反向：005e notify → L1 校验 → L2 CTR 解密 → WearPacket → 快应用
```

## B6. 已知事实 vs 待逆向缺口（ESP32 落地对照）

**已确认（可直接实现）**：
- GATT：fe95 服务；005f(写,无响应)/005e(notify)/0050(read)。
- L1：magic 0xA5A5 + type|frx + seq + len(LE) + CRC16-ARC(payload)。
- L2：channel(1=Pb) + opcode(1=明文/2=加密) + payload。
- 握手四步 + KDF(HKDF-SHA256, info="miwear-auth") + 双层 HMAC + CCM(nonce=enc_nonce‖0‖0)。
- 数据面：AES-128-CTR（key=IV=enc/dec_key）。
- 流控：seq 回绕、窗口 32、重试 3、超时 10s、ACK/Nak。

**待逆向/存疑**：
- ⚠️ **`authkey`（16B 主密钥）从哪来**：代码里它是配对后存储的设备密钥（`AuthComponent.authkey`），获取流程在小米官方配对/绑定流程里，本仓库未实现完整首次配对换 key 的过程——ESP32 需先用小米运动官方 App 与手环配对，再从存储/抓包取出 authkey。
- ⚠️ protobuf `WearPacket` / `Account` / `AuthAppVerify` / `AuthDeviceVerify` / `AppConfirm` / `CompanionDevice` 的**完整字段编号**在 `pb::xiaomi::protocol`（protobuf 生成代码，本仓库未展开）；ESP32 需用相同 .proto 生成编解码。
- ⚠️ CTR 的 IV 复用密钥这一怪癖是否为该手环固件真实行为，建议抓包验证；`enc_nonce/dec_nonce` 未用是否为有意为之存疑。
- ⚠️ MPS=64512、窗口=32 是手机侧**发起**的协商值；手环侧实际接受值要以 startRsp 为准。
- ⚠️ Android/iOS 上 BLE 层加密（pair/bond）是系统行为；ESP32 作为中心需自行处理配对绑定与密钥持久化。

---

# 【附录 C】Protobuf 信封字段编号（已找到，无需抓包）

> 来源：`AstroBox-NG-Module-Pb`（crate `pb` v5.0.0，`protos/xiaomi/*.proto`，**proto2 语法**，package `protocol`）。本附录所有字段编号均来自 `.proto` 源文件，可直接照做。

## C1. 顶层信封 WearPacket（`wear.proto:28-82`）

```proto
message WearPacket {
  required Type   type = 1;   // enum，见下
  required uint32  id   = 2;   // 子消息 ID（AccountID/InterconnectionID…）
  oneof payload {
    Account account = 3;  …  Interconnection interconnection = 25;  ErrorCode error_code = 100;
  }
}
```

| 字段 | 编号 | 类型 |
|---|---|---|
| `type` | **1** | required Type enum |
| `id` | **2** | required uint32 |
| `account` | 3 | Account |
| `interconnection` | **25** | Interconnection |
| `error_code` | 100 | ErrorCode |

Type enum（`wear.proto:30-53`）：ACCOUNT=1、SYSTEM=2、WATCH_FACE=4…、**INTERCONNECTION=23**。
> 即：QAIC/interconnect 业务 = `type=23`，载荷走 oneof 字段 **25**；`id` 用 `InterconnectionID`。

## C2. 认证消息 Account（`wear_account.proto:17-84`）

`Account.oneof payload` 中认证相关（`wear_account.proto:75-78`）：

| oneof 字段 | 编号 | 子消息 |
|---|---|---|
| `auth_app_verify` | **30** | Auth.AppVerify |
| `auth_device_verify` | **31** | Auth.DeviceVerify |
| `auth_app_confirm` | **32** | Auth.AppConfirm |
| `auth_device_confirm` | **33** | Auth.DeviceConfirm |

AccountID enum（`wear_account.proto:31-32`，用作 `WearPacket.id`）：**AUTH_VERIFY=26，AUTH_CONFIRM=27**。

### Auth 子消息字段（`wear_account.proto:217-241`）

| 消息 | 字段 | 编号 | 类型 |
|---|---|---|---|
| AppVerify | app_random | 1 | bytes（手机随机数 p_random） |
| AppVerify | app_device_id | 2 | string（可选） |
| AppVerify | check_dynamic_code | 3 | bool（可选） |
| DeviceVerify | device_random | 1 | bytes（手环随机数 w_random，16B） |
| DeviceVerify | device_sign | 2 | bytes（HMAC-SHA256，32B） |
| AppConfirm | app_sign | 1 | bytes（HMAC-SHA256，32B） |
| AppConfirm | encrypt_companion_device | 2 | bytes（CCM 密文‖tag） |
| DeviceConfirm | confirm_result | 1 | bool |
| DeviceConfirm | device_capability | 2 | uint32（可选） |
| DeviceConfirm | device_capability_2 | 3 | uint32（可选） |

### CompanionDevice（`wear_account.proto:101-117`）——被 CCM 加密的对象

| 字段 | 编号 | 类型 |
|---|---|---|
| device_type | 1 | enum ANDROID=0 / IOS=1 / **VELA=2** / OTHER=15 |
| system_version | 2 | float（可选） |
| device_name | 3 | string（required） |
| app_capability | 4 | uint32（可选） |
| region | 5 | string（可选） |
| server_prefix | 6 | string（可选） |

## C3. Interconnection（QAIC 业务，`wear_interconnection.proto:18-107`）

```proto
message Interconnection {
  enum InterconnectionID { … SEND_MIS_PACKET = 33; … }
  oneof payload { … Mis.Payload mis_payload = 22; … }
}
```

- **通用透传载体 = `Mis.Payload`，oneof 字段 22**（`wear_interconnection.proto:92`）。
- InterconnectionID 用 `SEND_MIS_PACKET=33`（`:54`）。

### Mis.Payload（`wear_interconnection.proto:367-370`）

| 字段 | 编号 | 类型 |
|---|---|---|
| spec | 1 | string |
| packet | 2 | bytes（**这里塞 QAIC 原始字节**，即应用层 FetchBridge JSON 之上的 QAIC 信封） |

## C4. 关于「addr / pkgName / payloadText」的澄清（重要）

- `addr`、`pkgName`、`payloadText` **不是 protobuf 字段**——它们是 AstroBox **宿主进程 → WASM 插件**之间的 **JSON 信封**（见本笔记 §2.1，`MiWear-InterconnectFetch/src/interconnect.rs:12-27`）。
- 真实链路里，FetchBridge 的 JSON 文本先被 QAIC 框架包成 `Mis.Payload.packet(bytes)`，再经上面 §C1 信封 → L2(channel=Pb) → L1 → BLE。
- 即对 ESP32：你要解码的最里层是 **`Mis.Payload{spec=1, packet=2}`** 的 `packet` 字节；其内部（QAIC 自己对 addr/pkgName 的封装格式）在本仓库内**未展开**——属 QAIC 框架私有，若需要可抓包或逆向官方 App。

## C5. 给 ESP32 的最小 protobuf 实现清单（proto2）

只需编解码这几个消息即可跑通认证+FetchBridge：
1. `WearPacket{type=1,id=2, oneof: account=3 / interconnection=25 / error_code=100}`。
2. 认证：`Account` oneof 30/31/32/33（`Auth.AppVerify/DeviceVerify/AppConfirm/DeviceConfirm`，字段见 C2 表）。
3. 业务：`Interconnection` oneof 22 = `Mis.Payload{spec=1(string), packet=2(bytes)}`。
4. 其余消息（watchface/fitness/ota…）FetchBridge 用不到，可忽略。
