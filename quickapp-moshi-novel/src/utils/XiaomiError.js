/**
 * 摩柿小说（cn.sswwgzs.moshi.novel）
 * ----------------------------------------------------------------------------
 * 本文件移植自开源项目「弦电子书」（com.bandbbs.ebook.plus）。
 * 上游仓库：https://github.com/youshen2/com.bandbbs.ebook ，原作者保留所有权利。
 * 「弦电子书」依据 GNU AGPL-3.0 协议开源；按协议要求，本文件及整个摩柿小说
 * 快应用同样以 AGPL-3.0 开源（见仓库根目录 LICENSE 与 NOTICE）。
 * 移植说明：保留原逻辑，仅在必要处适配摩柿工程；品牌名仍为「摩柿小说」。
 * ----------------------------------------------------------------------------
 */
export default class XiaomiError extends Error {
    constructor(data, code) {
        super(`${code}:${data}`)
        this.data = data
        this.code = code
    }
}