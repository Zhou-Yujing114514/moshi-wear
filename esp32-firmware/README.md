# ESP32 小米手环 9 Pro 互联网网桥固件

把「手机 + AstroBox」为小米手环 9 Pro 快应用提供的互联网桥角色，**原样搬到 ESP32**：
ESP32 插电自启 → 连 WiFi → BLE 直连手环 → 手环快应用的 `fetch` 请求经 ESP32 从互联网取回。
**跳过手机**。全部自研实现，协议常量与字节格式严格对照逆向笔记。

> ⚠️ **本固件未经真机验证**（见文末清单）。代码结构、协议常量、状态机均按笔记实现；
> protobuf 字段编号为「待核实」占位（见下），首次握手需抓包核对后微调 `proto_const.h`。

---

## 1. 它替你做了什么（协议分层对照）

```
手环快应用(JS @system.interconnect)
   │  JSON: {"tag":"fetch","url":"...","options":{...}}
   ▼
[Pb 通道 protobuf WearPacket]   ← L2 channel=1
   │  AES-128-CTR 加密          ← 数据面（密钥来自握手）
   ▼
[L1 帧: 0xA5A5|type|seq|len|CRC16-ARC]  ← 分片/ACK 流控
   │  切成 ≤ MTU-3 片
   ▼
BLE GATT 0x005f(Write Without Response)
   ▲
ESP32 NimBLE 中心  ← 本固件
   │  HTTP(S) over WiFi
   ▼
互联网
```

固件实现了笔记的完整两部分：
- **Part A（应用层，MIT 来源）**：`__hs__` 握手/caps 协商、FetchBridge v1~v4（单消息/分片/ACK 滑动窗口/开放流+CRC32+背压）、base64/hex/text、none/deflate/lz4 解压。
- **Part B（BLE 传输层，AGPLv3 来源）**：GATT `0xFE95`、写 `0x005f`、通知 `0x005e`、读 `0x0050`、L1/L2 帧、AES-128-CCM 认证、HKDF-SHA256 KDF、双层 HMAC、四步握手、SAR 流控。

---

## 2. 硬件要求

| 项 | 要求 |
|---|---|
| MCU | ESP32（经典，Xtensa）。其它 ESP32-S3/C3 需改 `sdkconfig.defaults` target |
| Flash | ≥ 4 MB（本工程按 4 MB DIO 40 MHz 配置） |
| 天线 | 板载或外置可用 BLE/WiFi 天线（WiFi+BLE 同时常开，天线差会不稳） |
| 供电 | USB 常电（插电自启设计，无电池） |
| 目标设备 | 已与官方 App 配对过的**小米手环 9 Pro** |

---

## 3. 构建

### 3.1 前置：安装 ESP-IDF v5.x

```bash
# 官方方式（约数 GB，耗时较长）
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32 && . ./export.sh
```

> 本次交付环境无 `idf.py`，且完整安装（clone + 工具链 + pip 依赖，数百 MB）预计远超 10 分钟，
> 按任务约定**未在沙盒内强行安装/编译**。纯逻辑模块（L1 帧编解码、CRC、base64/hex、CRC32、LZ4 块解压）
> 已在本机 gcc 下做单元向量验证（见 §8）。

### 3.2 构建命令（精确）

```bash
cd esp32-firmware
idf.py set-target esp32
idf.py build
```

产物：`build/miwear_esp_bridge.bin`。

### 3.3 烧录（按你历史方式：merge_bin 单 bin，offset 0x0，波特率 921600）

```bash
# 合并为单固件（ESP-IDF 会把 bootloader/partition/app 合成一个 bin）
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 \
  merge_bin --output build/merged.bin --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x1000 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/miwear_esp_bridge.bin

# 烧到 offset 0x0
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 921600 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x0 build/merged.bin
```

（等价地，`idf.py -p /dev/ttyUSB0 -b 921600 flash` 内部也走同样的分区偏移。）

---

## 4. 配置（必改项）

编辑 `main/config.h`（或用 `idf.py menuconfig` 自定义成 Kconfig）：

| 配置 | 说明 |
|---|---|
| `BR_WIFI_SSID` / `BR_WIFI_PASS` | 你的 WiFi |
| `BR_TARGET_ADDR` | 目标手环 BLE MAC（全 0 = 扫描时连第一个命中 `0xFE95` 的设备，仅开发期用） |
| `BR_AUTHKEY_HEX` | **唯一外部依赖**，32 字符 hex（16 字节主密钥）。**默认全 0 = 未配置，握手必失败** |

### 4.1 authkey 怎么来（⚠️ 不假设、不编造默认值）

authkey 是官方配对流程协商出来的设备主密钥，本固件**不实现首次配对换 key**。获取方式：

1. 用官方「小米运动健康」App 完成手环与手机的配对/绑定；
2. 从配对存储中提取 authkey：
   - Android：root 后从系统/小米 App 的蓝牙绑定数据库（或 Xposed/抓包）取设备密钥；
   - 或用 AstroBox（AGPLv3 仓库）完成配对后从其配置存储导出；
3. 把得到的 16 字节写成 32 hex 串填入 `BR_AUTHKEY_HEX`。

> 没有正确 authkey → 握手第 2 步 `device_sign` 校验不过，日志报 `AuthKey 错误`（见 §6）。

---

## 5. ⚠️ protobuf 字段编号（待核实，绝不编造）

协议笔记明确：`WearPacket / Account / AuthAppVerify / AuthDeviceVerify /
AuthAppConfirm / AuthDeviceConfirm / CompanionDevice` 的**完整 protobuf 字段编号**
在生成代码里，笔记未给出权威值。

本工程把**所有字段编号集中在 `main/proto_const.h`**，并给了一组最可能的占位值。
**首次握手若卡在第 2/3 步解析失败或签名不过，优先用抓包（nRF Connect / Wireshark BTLE）
核对真实字段编号，然后只改 `proto_const.h` 的宏**，协议栈逻辑无需改动。

当前占位（全部待核实）：
- `WP_FIELD_INNER=1`：WearPacket 内层消息
- `ACC_FIELD_APP_VERIFY/DEV_VERIFY/APP_CONFIRM/DEV_CONFIRM = 1/2/3/4`
- `ACC_FIELD_QAIC_PAYLOAD=5`：JSON 应用负载
- `AV/DV/AC/DC/CD_*` 各子消息字段

---

## 6. 串口排错指南（日志阶段标记）

`idf.py monitor`（或 921600 串口）按阶段看：

| 标记 | 含义 | 异常排查 |
|---|---|---|
| `WIFI` | WiFi 连接 | SSID/密码错、信号弱 |
| `BLE_CONN` | 扫描/连接/MTU/特征发现 | 没找到 `0xFE95`：手环未配对、距离远、被手机占用 |
| `L1_SEND`/`L1_RECV` | 0xA5A5 帧收发与 CRC | `crc mismatch`：链路干扰或字段方向错 |
| `L2_RECV`/`L2_SEND` | channel/opcode | 只处理 `channel=1(Pb)` |
| `FLOW_ACK` | SAR ACK/Nak/重传 | 连续重传→手环没回 ACK；`gave up`→链路断 |
| `HS_STEP` | 四步握手 | 见下 |
| `CRYPTO` | KDF/HMAC/CTR/CCM | — |
| `QAIC_HS` | 应用层 `__hs__` count/caps | 一直 count=0 不前进→手环 caps 没协商 |
| `FETCH_REQ`/`FETCH_RESP` | fetch 请求/响应 | URL 非法、HTTP 失败 |
| `STREAM` | v4 流/取消/超时 | 30s 无 ACK 被清理 |
| `HTTP_BRIDGE` | esp_http_client | 状态码、TLS、超时 |

### 握手失败典型表现（`HS_STEP`）

- `[2] device_sign mismatch -> AuthKey 错误`：**authkey 错**（最常见），或 protobuf 字段编号待核实导致 w_random/p_random 取错。
- `[2] no AuthDeviceVerify (field 待核实?)`：`proto_const.h` 的 Account/AuthDeviceVerify 字段号不对 → 抓包改。
- `[4] confirm_result=false`：手环拒绝，多半 authkey 或 CCM nonce 拼接不对。
- `[4] no confirm_result bool`：AuthDeviceConfirm 字段号待核实。

---

## 7. 许可与合规

| 层 | 来源仓库 | 许可证 |
|---|---|---|
| BLE 传输层（GATT/L1/L2/加密/握手/SAR） | `AstroBox-NG-Module-Bluetooth`、`AstroBox-NG-Module-Core` | **AGPLv3（含署名条款）** |
| 应用层（FetchBridge v1~v4 / caps / codec） | `AstroBox-NG-Plugin-MiWear-InterconnectFetch` | **MIT**（保留版权与许可声明） |

- 来源 URL：
  - https://github.com/AstralSightStudios/AstroBox-NG-Module-Bluetooth
  - https://github.com/AstralSightStudios/AstroBox-NG-Module-Core
  - https://github.com/AstralSightStudios/AstroBox-NG-Plugin-MiWear-InterconnectFetch
- **本固件采用 AGPL-3.0**（因 BLE 层为 AGPLv3，整体须以 AGPLv3 发布），并在本文署名上述两组织。
- **未逐字复制任何原仓库代码**：所有代码为本工程参照协议笔记（事实性字节格式/常量）自研重写。
- **仅实现协议桥本身**，不包含任何绕过鉴权、破解 authkey、滥用手环/服务端的功能。authkey 必须来自合法配对。

---

## 8. 已验证 vs 未真机验证

### 已在本机 gcc 下通过的单元验证（非 IDF）

- ✅ CRC32 IEEE：`crc32("123456789") = 0xCBF43926`（与笔记 §9 一致）
- ✅ base64：`base64("hello") = "aGVsbG8="`（RFC4648 带 `=`，§7）
- ✅ hex：`hex([0,0x01,0xFE,0xFF]) = "0001feff"`（小写，§7）
- ✅ L1 帧编解码往返：`8 字节头 + payload`，CRC16-ARC 校验通过、type/seq/payload 还原一致
- ✅ 纯逻辑文件语法检查：`l1_frame.c / l2_frame.c / proto_pack.c / codec.c`

### 真机验证缺口（必须上电才能确认）

1. ❌ ESP-IDF 编译链接（沙盒无 `idf.py`，未编译）。
2. ❌ NimBLE 扫描/连接 `0xFE95`、MTU 协商、特征发现句柄、notify 订阅、`0x0050` 读一次。
3. ❌ protobuf `WearPacket/Account/*` **字段编号**（待核实，§5）。
4. ❌ 四步握手真机时序：随机数、KDF、双层 HMAC、CCM CompanionDevice 能否被手环接受。
5. ❌ 数据面 CTR「IV=key」怪癖是否与该手环固件真实一致（`BR_CTR_IV_USE_KEY` 开关可切）。
6. ❌ SAR 重组边界：当前 `sar.c` 简化为「片到达即上交 L2」，真机多片 L2 重组需按 L2 长度完善。
7. ❌ FetchBridge v1~v4 与手环快应用的真实互通（caps 协商结果、ACK 增量节奏、流背压）。
8. ❌ WiFi+BLE 共存稳定性、内存占用（`sar_t` 的 256 槽待确认表约 77 KB .bss）。
9. ❌ 下行响应压缩（deflate/lz4 编码）当前回退为 none，体积优化待接 zlib/lz4 压缩器。
10. ❌ HTTPS 证书校验、跨源重定向删凭据语义是否被 esp_http_client 正确处理。

---

## 9. 目录结构

```
esp32-firmware/
├── CMakeLists.txt            # 顶层
├── sdkconfig.defaults        # ESP32/4MB DIO/NimBLE/mbedTLS 默认配置
├── partitions.csv            # 4MB 单 factory 分区
├── README.md
└── main/
    ├── CMakeLists.txt
    ├── main.c                # app_main 入口
    ├── bridge.c/.h           # 粘合：数据通路/WiFi/定时器
    ├── config.h              # WiFi/目标设备/authkey/协议开关
    ├── log_tags.h            # 阶段日志标记
    ├── ble_client.c/.h       # NimBLE 中心（0xFE95/0x005f/0x005e/0x0050）
    ├── l1_frame.c/.h         # L1 帧 + CRC16-ARC
    ├── l2_frame.c/.h         # L2 channel/opcode
    ├── sar.c/.h              # 分片流控（窗口32/重试3/超时10s/ACK/Nak）
    ├── mi_crypto.c/.h        # AES-128-CCM/CTR、HKDF-SHA256、双层HMAC
    ├── proto_pack.c/.h       # 最小 protobuf 编解码
    ├── proto_const.h         # ⚠️ protobuf 字段编号（待核实，可配置）
    ├── mi_handshake.c/.h     # 四步握手状态机
    ├── codec.c/.h            # base64/hex/UTF-8/CRC32/deflate解压/lz4块解压
    ├── qaic.c/.h             # __hs__ + FetchBridge v1~v4 状态机
    └── http_bridge.c/.h      # esp_http_client 执行 fetch
```
