const fs = require('fs');
const path = require('path');

/**
 * Smart XTC/XTCH File Splitter with Cover Preservation
 * Splits large XTC files at chapter boundaries and prepends covers to all parts.
 */

function smartSplitXTC(filePath, targetMaxPages = 1500) {
    let sourcePath = filePath;
    if (filePath.endsWith('.xtc') || filePath.endsWith('.xtch')) {
        if (fs.existsSync(filePath + '.bak')) {
            sourcePath = filePath + '.bak';
        }
    }
    
    if (!sourcePath.endsWith('.bak') && !fs.existsSync(sourcePath)) return;

    const buffer = fs.readFileSync(sourcePath);
    const view = new DataView(buffer.buffer, buffer.byteOffset, buffer.length);
    const magic = buffer.toString('utf8', 0, 4);
    const isHQ = magic === 'XTCH';
    const numPages = view.getUint16(6, true);

    if (numPages <= targetMaxPages) return;

    console.log(`Smart Splitting (with Covers): ${path.basename(sourcePath)} (${numPages} pages)`);

    const hasMetadata = buffer[9] === 1;
    const hasChapters = buffer[11] === 1;
    const metadataOffset = Number(view.getBigUint64(16, true));
    const indexOffset = Number(view.getBigUint64(24, true));
    const chapterOffset = Number(view.getBigUint64(48, true));

    // 1. Extract Metadata
    let title = "";
    let author = "";
    if (hasMetadata) {
        for (let i = 0; i < 127; i++) {
            if (buffer[metadataOffset + i] === 0) break;
            title += String.fromCharCode(buffer[metadataOffset + i]);
        }
        for (let i = 0; i < 63; i++) {
            if (buffer[metadataOffset + 128 + i] === 0) break;
            author += String.fromCharCode(buffer[metadataOffset + 128 + i]);
        }
    }

    // 2. Extract Chapters
    let chapters = [];
    if (hasChapters) {
        const chapterCount = view.getUint16(metadataOffset + 196, true);
        for (let i = 0; i < chapterCount; i++) {
            const chPos = chapterOffset + i * 96;
            let chTitle = "";
            for (let j = 0; j < 79; j++) {
                if (buffer[chPos + j] === 0) break;
                chTitle += String.fromCharCode(buffer[chPos + j]);
            }
            const startPage = view.getUint16(chPos + 80, true) - 1;
            chapters.push({ title: chTitle, page: startPage });
        }
    }

    // 3. Extract All Pages Data
    const pages = [];
    for (let i = 0; i < numPages; i++) {
        const entryOffset = indexOffset + i * 16;
        const pageDataOffset = Number(view.getBigUint64(entryOffset, true));
        const pageDataSize = view.getUint32(entryOffset + 8, true);
        const width = view.getUint16(entryOffset + 12, true);
        const height = view.getUint16(entryOffset + 14, true);
        pages.push({ data: buffer.slice(pageDataOffset, pageDataOffset + pageDataSize), width, height });
    }

    // Capture the first two pages as cover pages
    const coverPages = pages.slice(0, 2);

    // 4. Calculate Smart Split Points
    const numParts = Math.ceil(numPages / targetMaxPages);
    const idealPartSize = numPages / numParts;
    const splitPoints = [0];

    if (chapters.length > 0) {
        let lastSplitPoint = 0;
        for (let p = 1; p < numParts; p++) {
            const idealSplitPage = p * idealPartSize;
            
            // Search for the closest chapter within a reasonable window (e.g., 20% of targetMaxPages)
            // if no chapter is found in the window, fallback to the closest chapter overall, 
            // but ensure it's further than the last split point.
            let bestChapter = null;
            let minDiff = Infinity;

            for (let i = 0; i < chapters.length; i++) {
                const chPage = chapters[i].page;
                if (chPage <= lastSplitPoint + 200) continue; // Avoid tiny parts or splitting too close to previous

                const diff = Math.abs(chPage - idealSplitPage);
                if (diff < minDiff) {
                    minDiff = diff;
                    bestChapter = chapters[i];
                }
            }
            
            // If the best chapter we found is still too far from the ideal point (e.g., >200 pages)
            // Or if we didn't find any chapter at all, use the ideal page instead.
            if (!bestChapter || Math.abs(bestChapter.page - idealSplitPage) > 200) {
                splitPoints.push(Math.floor(idealSplitPage));
                lastSplitPoint = Math.floor(idealSplitPage);
            } else {
                splitPoints.push(bestChapter.page);
                lastSplitPoint = bestChapter.page;
            }
        }
    } else {
        for (let p = 1; p < numParts; p++) {
            splitPoints.push(Math.floor(p * idealPartSize));
        }
    }
    splitPoints.sort((a, b) => a - b);
    splitPoints.push(numPages);

    // 5. Clean up previous parts
    const baseDir = path.dirname(sourcePath);
    const baseName = path.basename(sourcePath, '.xtc.bak').replace('.xtch.bak', '');
    const ext = isHQ ? '.xtch' : '.xtc';
    
    fs.readdirSync(baseDir).forEach(file => {
        if (file.startsWith(baseName + ' - Part ') && (file.endsWith('.xtc') || file.endsWith('.xtch'))) {
            fs.unlinkSync(path.join(baseDir, file));
        }
    });

    // 6. Write new parts
    for (let i = 0; i < splitPoints.length - 1; i++) {
        const start = splitPoints[i];
        const end = splitPoints[i+1];
        let partPages;
        let partChapters;
        
        if (i === 0) {
            // Part 1: Keep as is
            partPages = pages.slice(start, end);
            partChapters = chapters
                .filter(ch => ch.page >= start && ch.page < end)
                .map(ch => ({ ...ch, page: ch.page - start }));
        } else {
            // Part 2, 3...: Prepend cover pages
            const contentPages = pages.slice(start, end);
            partPages = [...coverPages, ...contentPages];
            
            // Adjust existing chapter pages by +2 (for the cover)
            partChapters = chapters
                .filter(ch => ch.page >= start && ch.page < end)
                .map(ch => ({ ...ch, page: ch.page - start + 2 }));
            
            // Add a "Cover" entry at the start of TOC
            partChapters.unshift({ title: "Cover", page: 0 });
        }

        const partNum = i + 1;
        const partTitle = `${title} (P${partNum}/${numParts})`;
        const partFilename = path.join(baseDir, `${baseName} - Part ${partNum}${ext}`);

        console.log(`  -> Part ${partNum}: ${partPages.length} pages (${partChapters.length} chapters)`);
        
        const container = buildContainer(partPages, { title: partTitle, author }, partChapters, isHQ);
        fs.writeFileSync(partFilename, container);
    }
}

function buildContainer(pages, metadata, toc, isHQ) {
    const magic = isHQ ? 'XTCH' : 'XTC\0';
    const title = metadata.title || 'Unknown';
    const author = metadata.author || '';

    const headerSize = 56;
    const metadataSize = 256;
    const chapterEntrySize = 96;
    const chaptersSize = toc.length * chapterEntrySize;
    const indexEntrySize = 16;
    const indexSize = pages.length * indexEntrySize;

    const metadataOffset = headerSize;
    const chapterOffset = metadataOffset + metadataSize;
    const indexOffset = chapterOffset + chaptersSize;
    const pageDataOffset = indexOffset + indexSize;

    let currentOffset = pageDataOffset;
    const pageOffsets = [];
    for (let i = 0; i < pages.length; i++) {
        const p = pages[i];
        pageOffsets.push({ 
            offset: currentOffset, 
            size: p.data.length,
            width: p.width,
            height: p.height
        });
        currentOffset += p.data.length;
    }

    const totalSize = currentOffset;
    const buffer = Buffer.alloc(totalSize);
    const view = new DataView(buffer.buffer, buffer.byteOffset, buffer.length);

    buffer.write(magic, 0, 4, 'utf8');
    view.setUint16(4, 1, true);
    view.setUint16(6, pages.length, true);
    buffer[8] = 0; 
    buffer[9] = 1; 
    buffer[10] = 0;
    buffer[11] = toc.length > 0 ? 1 : 0;
    view.setUint32(12, 1, true);

    view.setBigUint64(16, BigInt(metadataOffset), true);
    view.setBigUint64(24, BigInt(indexOffset), true);
    view.setBigUint64(32, BigInt(pageDataOffset), true);
    view.setBigUint64(48, BigInt(chapterOffset), true);

    const titleBuf = Buffer.alloc(128);
    titleBuf.write(title.substring(0, 127), 0, 'utf8');
    buffer.set(titleBuf, metadataOffset);
    const authorBuf = Buffer.alloc(64);
    authorBuf.write(author.substring(0, 63), 0, 'utf8');
    buffer.set(authorBuf, metadataOffset + 128);
    view.setUint32(metadataOffset + 192, Math.floor(Date.now() / 1000), true);
    view.setUint16(metadataOffset + 196, toc.length, true);

    for (let i = 0; i < toc.length; i++) {
        const ch = toc[i];
        const chPos = chapterOffset + i * 96;
        const chTitleBuf = Buffer.alloc(80);
        chTitleBuf.write(ch.title.substring(0, 79), 0, 'utf8');
        buffer.set(chTitleBuf, chPos);
        view.setUint16(chPos + 80, ch.page + 1, true);
        view.setUint16(chPos + 82, ch.page + 1, true);
    }

    for (let i = 0; i < pages.length; i++) {
        const idxPos = indexOffset + i * 16;
        view.setBigUint64(idxPos, BigInt(pageOffsets[i].offset), true);
        view.setUint32(idxPos + 8, pageOffsets[i].size, true);
        view.setUint16(idxPos + 12, pageOffsets[i].width, true);
        view.setUint16(idxPos + 14, pageOffsets[i].height, true);
        buffer.set(pages[i].data, pageOffsets[i].offset);
    }

    return buffer;
}

const args = process.argv.slice(2);
const inputPath = args[0];
const targetMax = parseInt(args[1] || "8000");

function processPath(p) {
    if (!fs.existsSync(p)) return;
    const stat = fs.statSync(p);
    if (stat.isDirectory()) {
        fs.readdirSync(p).forEach(f => processPath(path.join(p, f)));
    } else if (p.endsWith('.bak')) {
        smartSplitXTC(p, targetMax);
    } else if ((p.endsWith('.xtc') || p.endsWith('.xtch')) && !p.includes(' - Part ')) {
        // If it's a regular XTC and doesn't have a .bak yet, or we're calling it specifically
        smartSplitXTC(p, targetMax);
    }
}

processPath(inputPath);
