# 摩柿小说 · Vela 快应用（小米手环 8 Pro / 9 Pro）

中文 UI、竖屏小屏适配的小说快应用，运行在小米手环 Vela 系统（Vela JS）。
对接自研摩柿后端（书源主站 `https://morax.sswwgzs.cn`），实现：
**登录 → 搜书 → 下载 TXT → 导入阅读引擎 → 离线阅读（章节/书签/双模式）** 完整闭环。

> **2.0.0 重大更新：** 本版本的【本地阅读引擎、阅读器、书签、章节、阅读设置】
> 极仿并移植自开源项目「弦电子书」（AGPL-3.0，https://github.com/youshen2/com.bandbbs.ebook ）。
> 按 AGPL-3.0 要求，本快应用同样以 AGPL-3.0 开源（见 `LICENSE` / `NOTICE`）。
> 摩柿原有的网络层（网桥 FetchBridge / 登录 / 书架 / 搜索）全部保留。

---

## 一、技术路线（混合移植）

「弦电子书」与「摩柿小说」的数据模式不同：

| | 弦电子书（仿制对象） | 摩柿小说（改造对象） |
|---|---|---|
| 数据来源 | 被动接收安卓同步器经 `@system.interconnect` 推送的 TXT/封面 | 主动经 ESP32 网桥 / 本机 HTTP 访问摩柿书源 |
| 网络 | 不直接联网 | 登录 → 书架/搜索 → 下载 TXT 落盘 |
| 阅读 | 章节、书签、封面、滑动/怀旧双模式、海量设置 | （原）仅固定字符数分页 |

混合移植方案：

1. **保留摩柿网络层**：`common/bridge.js`（FetchBridge over interconnect，HTTP 代理）、
   `common/http.js`、`common/session.js`、`common/config.js`；**不采用**弦电子书的
   interconnect 文件推送（`interconnfile.js` / `interconn.js` / `handshake.js`）。
2. **移植弦电子书本地阅读引擎**：章节（chapterManager）、书库（bookStorage）、
   书签、阅读时长（readingTimeStorage）、封面/插图（illustration）。
3. **适配层**：`utils/importer.js` 把「摩柿下载 TXT」的结果转成弦电子书的磁盘布局，
   打通下载 → 阅读闭环。
4. **书架 UI 极仿**弦电子书 `pages/index`（封面/列表、分类、排序、空态、分页）。
5. **视觉复用**弦电子书 `common/style.css` 与 `common/images`，品牌名仍为「摩柿小说」。

---

## 二、工程结构

```
quickapp-moshi-novel/
├── README.md                 # 本文件
├── LICENSE                   # AGPL-3.0
├── NOTICE                    # 移植来源与逐文件清单
├── package.json              # 依赖 aiot-toolkit；build/release 带 --enable-custom-component
├── sign/                     # 签名（release 用，私钥被 .gitignore 屏蔽）
└── src/
    ├── manifest.json         # 包名/图标/版本/features/路由（已登记全部页面）
    ├── app.ux                # 应用入口（生命周期 + 会话恢复）
    ├── common/               # 摩柿网络层（保留）
    │   ├── config.js         # ★端点/字段/阈值/transport 开关
    │   ├── session.js        # 登录令牌（内存 + @system.storage）
    │   ├── http.js           # 网络层：direct / bridge 双通道分流
    │   ├── bridge.js         # ★ESP32 网桥 FetchBridge 信封
    │   ├── library.js        # 旧本地书库（保留，兼容）
    │   ├── reader.js         # 纯工具（formatSize 等）
    │   ├── style.css         # ← 移植自弦电子书
    │   └── images/           # ← 弦电子书图标（含 num_pad/）
    ├── utils/                # ← 移植的阅读引擎 + 适配层
    │   ├── bookStorage.js    # 书架 bookshelf.json
    │   ├── chapterManager.js # 章节 lindex/indexes/content
    │   ├── readingTimeStorage.js
    │   ├── illustration.js
    │   ├── storage.js / storageUtils.js / runAsyncFunc.js / XiaomiError.js / str2abWrite.js
    │   ├── importer.js       # ★下载 TXT → 阅读引擎 适配层（本项目新写）
    │   └── coverMaker.js     # ★摩柿封面本地生成（无第三方依赖，本项目新写）
    ├── components/
    │   └── number_choose/number_choose.ux   # ← 加减数字选择器
    ├── Login/login.ux        # 登录（保留）
    ├── Search/search.ux      # 搜书（保留）
    └── pages/                # ← 移植的阅读/设置页面（路由 key 即 pages/<name>）
        ├── index/            # 书架（首页）
        ├── detail/           # 主阅读器（滑动 + 怀旧双模式）
        ├── list/ readPresent/ readPercent/ textReader/
        ├── bookmarks/ editBookmark/ autoRead/ confirm/
        ├── illustrationViewer/ bookinfo/ readingTime/ detailsetting/
        ├── downloads/        # ★下载管理（本项目新写）
        └── （28 个设置页，见下）
```

---

## 三、页面清单（已移植 / 新增）

### 核心阅读（12 + 1）
| 页面 | 路由 | 说明 |
|---|---|---|
| 书架 | `pages/index` | 封面/列表两种样式、分类、排序、空态、分页（极仿弦电子书） |
| 主阅读器 | `pages/detail` | **滑动无缝 + 怀旧分页两种模式**；章节切换、点击/手势、亮度、进度条 |
| 章节列表 | `pages/list` | 全部章节、当前章高亮、跳转 |
| 章节号跳转 | `pages/readPresent` | 数字键盘输入章节号 |
| 百分比跳转 | `pages/readPercent` | 按百分比定位 |
| 书签管理 | `pages/bookmarks` | 书签列表、跳转、删除 |
| 编辑书签 | `pages/editBookmark` | 重命名（已改用原生 `<input>`，见下） |
| 定时自动翻页 | `pages/autoRead` | 开关、间隔、距离（含 number_choose） |
| 确认弹窗 | `pages/confirm` | 通用确认 |
| 插图查看 | `pages/illustrationViewer` | 全屏插图 |
| 书籍详情 | `pages/bookinfo` | 封面、占用、章节数 |
| 阅读时长 | `pages/readingTime` | 阅读统计 |
| 阅读器菜单 | `pages/detailsetting` | 阅读器内功能枢纽 |
| 通用文本查看 | `pages/textReader` | 简介等滚动文本 |
| 下载管理 | `pages/downloads` | **新增**：任务列表、下载 TXT、导入 |

### 阅读设置（28 个）
`more`（全局设置枢纽）、`readingSetting`（阅读设置枢纽）、
`fontSetting`（字号）、`marginSetting`（边距）、`screenBrightness`（亮度）、
`readingThemeSetting`（主题/字色/底色）、`progressBarSetting`、
`progressBarOpacitySetting`（进度条）、`readModeSetting`（滑动/怀旧模式）、
`chapterSwitchSetting`、`chapterSwitchStyle`、`chapterSwitchSensitivity`（章节切换）、
`autoReadDistanceSetting`、`swipe`、`swipeSensitivity`、`pageSizeSetting`（分段大小）、
`timeSetting`、`progressSavingSetting`、`extraContentSetting`（章节附加信息）、
`opacity`（文字透明度）、`marqueeSetting`（跑马灯）、`interactionSetting`（手势）、
`performanceSetting`（性能）、`shelfSetting`（书架）、`storageManagement`（存储管理）、
`about`（关于）、`help`（帮助）、`info`。

---

## 四、下载 → 阅读闭环（适配层）

弦电子书的阅读引擎期望如下磁盘布局（由其安卓同步器生成，**内容为 UTF-16LE**）。
`utils/importer.js` 在下载 TXT 后按此布局逐字段生成：

```
internal://files/books/
├── bookshelf.json                     # { version:3, books:[...] }
├── storage-api/savedFile              # 键值存储（storage.js 文件后端）
└── <dirName>/                         # dirName = 书名的 8 位哈希（djb2）
    ├── lindex.txt                     # 行0:章节总数  行1:已同步数  其余: "start,end"
    ├── indexes/<chunk>.txt            # TSV "<章号>\t<章名>\t<字数>"，100 章/块
    ├── content/<chapter>.txt          # UTF-16LE 章节正文
    ├── book_info.json                 # 名称/作者/章节数/字数/封面
    └── cover.png                      # 本地生成的摩柿封面
```

导入流程：

1. **章节识别**：正则匹配「第X章/节/回/卷/集/部/篇」、「序章/前言/楔子/番外/终章」、
   `Chapter N` 等；要求短行、且不以句读标点结尾，避免误判正文。
   识别不到时整本作为一章「正文」。
2. **写正文**：每章转 UTF-16LE（`str2abWrite`）写 `content/<idx>.txt`。
3. **写索引 / lindex / book_info**。
4. **封面**：`coverMaker.js` 用纯 JS 光栅化一张「摩柿（柿子）」主题 PNG（深色背景 +
   橙红柿子 + 绿蒂 + 金色装饰线），底色由书名哈希决定。
   摩柿书源搜索项仅含 `name/author/book_url/source/sources`，不含封面 URL（见
   `shared/backend-api-notes.md` §3.2），故封面本地生成。
5. **登记书架**：在 `bookshelf.json` 增加/更新条目（保留既有阅读进度）。

> 旧的 `common/library.js` 与 `internal://files/moshi/` 布局保留兼容，但新阅读入口
> 统一走 `bookshelf.json` + `pages/detail`。

---

## 五、构建 / 打包

> 本沙盒无真机、无 AIoT-IDE；目标是**代码结构正确、可被 `aiot build` 构建**。

1. 装依赖：项目根放 `.npmrc`（`registry="https://registry.npmmirror.com/"`），`npm i`。
2. 构建（已在 `package.json` 脚本中加 `--enable-custom-component`，供 number_choose 组件）：
   - 开发包：`npm run build`（= `aiot build --enable-custom-component`）→ `dist/*.debug.rpk`
   - 生产包：`npm run release`（= `aiot release --enable-custom-component`）→ `dist/*.release.rpk`
3. 模拟器：`npx aiot createVVD`；真机安装通道（调试器壳 / adb 推送）以 AIoT-IDE 为准。

---

## 六、网桥通道与 128KB 限制（重要）

`common/config.js` 顶部 `transport: 'direct' | 'bridge'`，默认 `direct`。

- **direct**：手环本机 `@system.fetch`；官方支持明细对手环 8 Pro/9 Pro 标注「不支持」，真机待验。
- **bridge**：ESP32 网桥代发（FetchBridge）。协议与固件 `esp32-firmware/main/qaic.c` 逐字段对齐；
  但 Vela 侧 `@system.interconnect` 收发原语（`bridge.js` 的 `sendToBridge`/`onBridgeMessage`）
  仍是**待真机确证的占位**，未假接、未编造 API。真机联调只改这两个函数体。

**整包 body 上限（约 128KB）**：固件 `BR_HTTP_MAX_BODY` 默认约 128KB，**仅作用于 bridge 路径**；
超限固件回错误帧，`http.js` 抛「文件过大，超出网桥整包下载上限（约 128KB）」。

- ⚠️ 对章节/封面的影响：单本 TXT 超过约 128KB 时，**bridge 路径无法整本下载**，
  也就无法导入。direct 路径不受此限（受本机 fetch 可用性限制）。
- **未改固件协议**；大 TXT 的 HTTP `Range` 分段下载为后续工作（待真机/固件确认 Range 支持）。
- 当前单本仍受 `config.limits.maxBookSizeBytes`（5MB，可配）约束。

---

## 七、因模式差异未移植的功能

- **interconnect 文件推送**：`interconnfile.js` / `interconn.js` / `handshake.js`
  —— 摩柿改走主动 HTTP，不采用被动推送。
- **弦电子书自研输入法** `components/InputMethod`（体积大、依赖大量素材）：
  登录/搜索沿用原生 `<input>`，`editBookmark` 也改为原生 `<input>`。
- **双端同步 / 手机端独立阅读**：`pages/push` 等无对应场景。
- 书架中原「手机同步」入口改为「下载管理」（`pages/downloads`）。

---

## 八、验证结果（无真机，静态验证）

- 所有 `.js`（`common/` + `utils/`）：`node --check` 通过。
- 所有 `.ux`：抽取 `<script>` 段做 `node --check`（用 loader 把 `@system.*` 与无后缀
  import 打桩），**全部通过**；栈式检查标签闭合**全部通过**。
- 所有相对 import、`<import>` 组件、`/common/images/*.png` 引用逐一核对**均存在**。
- `manifest.json` 每条路由的 `src/<key>/<component>.ux` 均存在；
  `designWidth=336`、包名 `cn.sswwgzs.moshi.novel` 保持不变。
- 端到端模拟（内存文件系统）跑通 `importBook`：lindex / indexes / content（UTF-16LE）/
  book_info / bookshelf / cover.png 全部正确生成。
- 封面 PNG 经 PIL 校验可正常打开（120×160 RGB）。

**未经真机验证**：真实 UI 渲染、手势手感、`@system.fetch`/`@system.interconnect` 可用性、
TXT 读写性能、真实 token 下的接口字段。

---

## 九、许可

- 本快应用以 **GNU AGPL-3.0** 开源（`LICENSE`）。
- 移植来源与逐文件清单见 `NOTICE`；移植文件头部均保留弦电子书来源说明。
- 「弦电子书」原作者 youshen2 保留所有权利。
