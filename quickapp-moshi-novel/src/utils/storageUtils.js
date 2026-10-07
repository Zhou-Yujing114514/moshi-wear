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
export function getReservedStorage(product) {
    if (!product) return 0;
    if (product === "REDMI Watch 6") return 120 * 1024 * 1024;
    if (product === "REDMI Watch 5") return 120 * 1024 * 1024;
    if (product === "Xiaomi Smart Band 9") return 64 * 1024 * 1024;
    if (product === "Xiaomi Smart Band 9 Pro") return 64 * 1024 * 1024;
    if (product === "Xiaomi Smart Band 8 Pro") return 84 * 1024 * 1024;
    if (product === "o65m") return 1024 * 1024 * 1024;
    if (product && product.includes("Xiaomi Smart Band 10")) return 90 * 1024 * 1024;
    return 0;
}

export function calculateStorageInfo(totalStorage, availableStorage, product) {
    const reservedStorage = getReservedStorage(product);
    const usedStorage = totalStorage - availableStorage;
    const actualAvailable = totalStorage - reservedStorage - usedStorage;
    return {
        totalStorage,
        availableStorage,
        reservedStorage,
        usedStorage,
        actualAvailable
    };
}

export function isStorageLow(actualAvailable) {
    return actualAvailable < 2 * 1024 * 1024;
}

