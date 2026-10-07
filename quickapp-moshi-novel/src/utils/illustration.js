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
const PROMPT_TEXT = '点击查看插图';
const MARKER_REGEX = /\[\[SINE_IMG\|([^|\]]+)(?:\|([^|\]]*))?]]/g;

function decodeComponent(value) {
    try {
        return decodeURIComponent((value || '').replace(/\+/g, '%20'));
    } catch (e) {
        return value || '';
    }
}

function appendTextBlocks(target, text) {
    if (!text) return;
    text.split('\n')
        .map(line => line.replace(/\s+$/g, ''))
        .filter(line => line && line.trim())
        .forEach(line => target.push({ type: 'text', text: line }));
}

function stripIllustrationMarkers(content) {
    if (!content) return '';
    return content
        .replace(MARKER_REGEX, '\n\n')
        .replace(/\n{3,}/g, '\n\n');
}

function parseIllustrationBlocks(content, bookDirName, showIllustration = true) {
    const blocks = [];
    if (!content) return blocks;
    if (!showIllustration) {
        appendTextBlocks(blocks, stripIllustrationMarkers(content));
        return blocks;
    }

    let lastIndex = 0;
    content.replace(MARKER_REGEX, (match, encodedPath, encodedAlt, offset) => {
        if (offset > lastIndex) {
            appendTextBlocks(blocks, content.slice(lastIndex, offset));
        }

        const relativePath = decodeComponent(encodedPath);
        const altText = encodedAlt ? decodeComponent(encodedAlt) : '';
        if (relativePath) {
            blocks.push({
                type: 'illustration',
                text: PROMPT_TEXT,
                relativePath,
                altText,
                imageUri: `internal://files/books/${bookDirName}/${relativePath}`
            });
        }

        lastIndex = offset + match.length;
        return match;
    });

    if (lastIndex < content.length) {
        appendTextBlocks(blocks, content.slice(lastIndex));
    }

    return blocks;
}

function toDisplayText(content, showIllustration = true) {
    if (!content) return '';
    if (!showIllustration) return stripIllustrationMarkers(content);
    return content
        .replace(MARKER_REGEX, `\n\n${PROMPT_TEXT}\n\n`)
        .replace(/\n{3,}/g, '\n\n');
}

function extractIllustrationUris(content, bookDirName) {
    return parseIllustrationBlocks(content, bookDirName)
        .filter(block => block.type === 'illustration')
        .map(block => block.imageUri);
}

function findFirstIllustration(content, bookDirName, showIllustration = true) {
    if (!content || !showIllustration) return null;
    const regex = new RegExp(MARKER_REGEX);
    const match = regex.exec(content);
    if (!match) return null;

    const relativePath = decodeComponent(match[1]);
    if (!relativePath) return null;

    const altText = match[2] ? decodeComponent(match[2]) : '';
    return {
        startIndex: match.index,
        endIndex: match.index + match[0].length,
        rawLength: match[0].length,
        relativePath,
        altText,
        imageUri: `internal://files/books/${bookDirName}/${relativePath}`
    };
}

export default {
    PROMPT_TEXT,
    stripIllustrationMarkers,
    parseIllustrationBlocks,
    toDisplayText,
    extractIllustrationUris,
    findFirstIllustration
};
