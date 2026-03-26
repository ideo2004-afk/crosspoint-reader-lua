/**
 * XTG/XTH/XTC format encoders
 * Ported from web/app.js
 */

/**
 * Encode image data to XTG format (1-bit monochrome)
 * @param {Uint8ClampedArray} data - RGBA pixel data
 * @param {number} width - Image width
 * @param {number} height - Image height
 * @returns {Uint8Array} XTG encoded data
 */
function encodeXTG(data, width, height) {
    // XTG: 1-bit monochrome, row-major, MSB = leftmost pixel

    // Header: 22 bytes
    const header = new Uint8Array(22);
    const view = new DataView(header.buffer);

    // Magic "XTG\0"
    header[0] = 0x58; // X
    header[1] = 0x54; // T
    header[2] = 0x47; // G
    header[3] = 0x00;

    // Dimensions (per XTG spec - no version field!)
    view.setUint16(4, width, true);    // offset 0x04
    view.setUint16(6, height, true);   // offset 0x06
    header[8] = 0;                      // colorMode = 0 (monochrome)
    header[9] = 0;                      // compression = 0 (uncompressed)

    // Bitmap: 8 pixels per byte, MSB = leftmost
    const rowBytes = Math.ceil(width / 8);
    const dataSize = rowBytes * height;
    view.setUint32(10, dataSize, true); // offset 0x0A (dataSize)
    // md5 at 0x0E left as zeros (optional)
    const bitmap = new Uint8Array(rowBytes * height);

    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const srcIdx = (y * width + x) * 4;
            const gray = data[srcIdx]; // Already grayscale after dithering

            if (gray >= 128) {
                // White pixel - set bit (per XTG spec: 0=black, 1=white)
                const byteIdx = y * rowBytes + Math.floor(x / 8);
                const bitIdx = 7 - (x % 8); // MSB first
                bitmap[byteIdx] |= (1 << bitIdx);
            }
        }
    }

    // Combine header + bitmap
    const result = new Uint8Array(header.length + bitmap.length);
    result.set(header, 0);
    result.set(bitmap, header.length);

    return result;
}

/**
 * Encode image data to XTH format (2-bit grayscale)
 * @param {Uint8ClampedArray} data - RGBA pixel data
 * @param {number} width - Image width
 * @param {number} height - Image height
 * @returns {Uint8Array} XTH encoded data
 */
function encodeXTH(data, width, height) {
    // XTH: 2-bit grayscale, vertical scan (columns right-to-left)

    // Header: 22 bytes
    const header = new Uint8Array(22);
    const view = new DataView(header.buffer);

    // Magic "XTH\0"
    header[0] = 0x58; // X
    header[1] = 0x54; // T
    header[2] = 0x48; // H
    header[3] = 0x00;

    // Dimensions (per XTH spec - no version field!)
    view.setUint16(4, width, true);    // offset 0x04
    view.setUint16(6, height, true);   // offset 0x06
    header[8] = 0;                      // colorMode = 0
    header[9] = 0;                      // compression = 0

    // Two bit planes, vertical scan, columns right-to-left
    const colBytes = Math.ceil(height / 8);
    const dataSize = colBytes * width * 2; // Two bit planes
    view.setUint32(10, dataSize, true); // offset 0x0A (dataSize)
    // md5 at 0x0E left as zeros (optional)
    const plane0 = new Uint8Array(colBytes * width); // bit 0
    const plane1 = new Uint8Array(colBytes * width); // bit 1

    for (let x = width - 1; x >= 0; x--) {
        const colIdx = width - 1 - x;

        for (let y = 0; y < height; y++) {
            const srcIdx = (y * width + x) * 4;
            const gray = data[srcIdx];

            // Quantize to 2-bit (XTH LUT)
            let level;
            if (gray > 212) level = 0b00;      // White
            else if (gray > 127) level = 0b10; // Light Gray
            else if (gray > 42) level = 0b01;  // Dark Gray
            else level = 0b11;                 // Black

            const byteIdx = colIdx * colBytes + Math.floor(y / 8);
            const bitIdx = 7 - (y % 8);

            if (level & 0b01) plane0[byteIdx] |= (1 << bitIdx);
            if (level & 0b10) plane1[byteIdx] |= (1 << bitIdx);
        }
    }

    // Combine header + plane0 + plane1
    const result = new Uint8Array(header.length + plane0.length + plane1.length);
    result.set(header, 0);
    result.set(plane0, header.length);
    result.set(plane1, header.length + plane0.length);

    return result;
}

/**
 * Build XTC/XTCH container from encoded pages
 * @param {Uint8Array[]} pages - Array of encoded XTG/XTH pages
 * @param {Object} metadata - Document metadata
 * @param {string} metadata.title - Book title
 * @param {string} metadata.author - Book author
 * @param {Object[]} toc - Table of contents
 * @param {string} toc[].title - Chapter title
 * @param {number} toc[].page - Chapter start page (0-indexed)
 * @param {number} width - Page width
 * @param {number} height - Page height
 * @param {boolean} isHQ - true for XTCH (2-bit), false for XTC (1-bit)
 * @param {Uint8Array} [coverBuffer] - RAW JPG/PNG cover image (XTC+ support)
 * @returns {Uint8Array} XTC/XTCH container data
 */
function buildXTCContainer(pages, metadata, toc, defaultWidth, defaultHeight, isHQ, coverBuffer = null) {
    const magic = isHQ ? 'XTCH' : 'XTC\0';

    const title = metadata.title || 'Unknown';
    const author = metadata.author || '';

    // Filter valid TOC entries to avoid holes in the chapter section
    const validToc = toc.filter(ch => ch && (ch.title || ch.name || ch.page || ch.startPage));
    
    const headerSize = 56;
    const metadataSize = 256;
    const chapterEntrySize = 96;
    const chaptersSize = validToc.length * chapterEntrySize;
    const indexEntrySize = 16;
    const indexSize = pages.length * indexEntrySize;

    const metadataOffset = headerSize;
    const chapterOffset = metadataOffset + metadataSize;
    const indexOffset = chapterOffset + chaptersSize;
    const pageDataOffset = indexOffset + indexSize;

    // Build page index
    const pageOffsets = [];
    let currentOffset = pageDataOffset;
    for (let i = 0; i < pages.length; i++) {
        const page = pages[i];
        const pageData = page.data || page;
        pageOffsets.push({ 
            offset: currentOffset, 
            size: pageData.length,
            width: page.width || defaultWidth,
            height: page.height || defaultHeight
        });
        currentOffset += pageData.length;
    }

    const xtcDataSize = currentOffset;
    const thumbOffset = coverBuffer ? xtcDataSize : 0;
    const totalSize = xtcDataSize + (coverBuffer ? coverBuffer.length : 0);

    const buffer = new ArrayBuffer(totalSize);
    const view = new DataView(buffer);
    const bytes = new Uint8Array(buffer);

    // Write header (56 bytes)
    for (let i = 0; i < 4; i++) {
        bytes[i] = magic.charCodeAt(i);
    }
    view.setUint16(4, 1, true); // Version
    view.setUint16(6, pages.length, true); // Page count
    // Individual flag bytes per XTC spec
    bytes[8] = 0;   // readDirection (0 = L→R)
    bytes[9] = 1;   // hasMetadata
    bytes[10] = coverBuffer ? 2 : 0;  // hasThumbnails (2 = XTC+ High-res RAW cover)
    bytes[11] = validToc.length > 0 ? 1 : 0;  // hasChapters
    view.setUint32(12, 1, true); // Current page (1-indexed)

    // Use BigInt for 64-bit values
    view.setBigUint64(16, BigInt(metadataOffset), true);
    view.setBigUint64(24, BigInt(indexOffset), true);
    view.setBigUint64(32, BigInt(pageDataOffset), true);
    view.setBigUint64(40, BigInt(thumbOffset), true); // thumbOffset / RAW cover offset
    view.setBigUint64(48, BigInt(0), true); // Clear 8 bytes first
    view.setUint32(48, chapterOffset, true); // chapterOffset (4 bytes)
    // Padding field (offset 52) used for cover size in XTC+
    if (coverBuffer) {
        view.setUint32(52, coverBuffer.length, true);
    }

    // Write metadata (256 bytes)
    const encoder = new TextEncoder();
    const titleBytes = encoder.encode(title.substring(0, 126));
    const authorBytes = encoder.encode(author.substring(0, 62));

    bytes.set(titleBytes, metadataOffset);
    bytes[metadataOffset + 127] = 0; // Null terminator
    bytes.set(authorBytes, metadataOffset + 128);
    bytes[metadataOffset + 191] = 0; // Null terminator
    view.setUint32(metadataOffset + 192, Math.floor(Date.now() / 1000), true); // Timestamp
    view.setUint16(metadataOffset + 196, validToc.length, true); // Chapter count

    // Write chapters
    let chapterPos = chapterOffset;
    for (let i = 0; i < validToc.length; i++) {
        const ch = validToc[i];
        const chTitle = ch.title || ch.name || `Chapter ${i + 1}`;
        const chPage = ch.page || ch.startPage || 0;
        const chNameBytes = encoder.encode(chTitle.substring(0, 78));
        bytes.set(chNameBytes, chapterPos);
        bytes[chapterPos + 79] = 0;
        view.setUint16(chapterPos + 80, chPage + 1, true); // Start page (1-indexed)
        view.setUint16(chapterPos + 82, chPage + 1, true); // End page (placeholder)
        chapterPos += chapterEntrySize;
    }

    // Write index
    let indexPos = indexOffset;
    for (let i = 0; i < pages.length; i++) {
        view.setBigUint64(indexPos, BigInt(pageOffsets[i].offset), true);
        view.setUint32(indexPos + 8, pageOffsets[i].size, true);
        view.setUint16(indexPos + 12, pageOffsets[i].width, true);
        view.setUint16(indexPos + 14, pageOffsets[i].height, true);
        indexPos += indexEntrySize;
    }

    // Write page data
    let dataPos = pageDataOffset;
    for (let i = 0; i < pages.length; i++) {
        const pageData = pages[i].data || pages[i];
        bytes.set(pageData, dataPos);
        dataPos += pageData.length;
    }

    // If XTC+, append raw cover image at the end
    if (coverBuffer) {
        bytes.set(coverBuffer, xtcDataSize);
    }

    return bytes;
}

module.exports = {
    encodeXTG,
    encodeXTH,
    buildXTCContainer
};
