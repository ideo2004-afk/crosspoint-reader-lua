const fs = require("fs");
const path = require("path");

// Basic XTC/XTCH Inspector
// This script parses an XTC/XTCH file and extracts the first few pages as raw PBM (Portable Bitmap) files
// which are easy to view in standard image viewers or command line tools.

function inspectXTC(filePath, startPage = 1, endPage = 3) {
  if (!fs.existsSync(filePath)) {
    console.error(`File not found: ${filePath}`);
    return;
  }

  const buffer = fs.readFileSync(filePath);
  const view = new DataView(buffer.buffer, buffer.byteOffset, buffer.length);

  // Check Magic
  const magic = buffer.toString("utf8", 0, 4);
  const isHQ = magic === "XTCH";
  if (magic !== "XTC\0" && magic !== "XTCH") {
    console.error(`Invalid magic bytes: ${magic}`);
    return;
  }

  const version = view.getUint16(4, true);
  const numPages = view.getUint16(6, true);

  console.log(`--- XTC INSPECTOR ---`);
  console.log(`File: ${path.basename(filePath)}`);
  console.log(`Format: ${isHQ ? "XTCH (2-bit)" : "XTC (1-bit)"}`);
  console.log(`Version: ${version}`);
  console.log(`Pages: ${numPages}`);

  // Flags
  const readDir = buffer[8];
  const hasMetadata = buffer[9];
  console.log(`Read Direction: ${readDir === 0 ? "LtoR" : "RtoL"}`);
  console.log(`Has Metadata: ${hasMetadata === 1}`);

  const metadataOffset = Number(view.getBigUint64(16, true));
  const indexOffset = Number(view.getBigUint64(24, true));
  const pageDataOffset = Number(view.getBigUint64(32, true));
  const thumbOffset = Number(view.getBigUint64(40, true));
  const chapterOffset = Number(view.getBigUint64(48, true));
  const thumbSize = view.getUint32(52, true);

  if (hasMetadata && metadataOffset > 0) {
    let title = "";
    for (let i = 0; i < 127; i++) {
      if (buffer[metadataOffset + i] === 0) break;
      title += String.fromCharCode(buffer[metadataOffset + i]);
    }
    let author = "";
    for (let i = 0; i < 63; i++) {
      if (buffer[metadataOffset + 128 + i] === 0) break;
      author += String.fromCharCode(buffer[metadataOffset + 128 + i]);
    }
    console.log(`Title: ${title}`);
    console.log(`Author: ${author}`);

    const hasThumbnails = buffer[10];
    if (hasThumbnails === 2) {
      console.log(`Has RAW Cover: Yes, Offset: 0x${thumbOffset.toString(16)}, Size: ${(thumbSize / 1024).toFixed(1)} KB`);
      // Try to save the raw cover
      const coverData = buffer.slice(thumbOffset, thumbOffset + thumbSize);
      const ext = coverData[0] === 0xFF && coverData[1] === 0xD8 ? 'jpg' : 'png';
      const coverPath = `/tmp/cover_extracted.${ext}`;
      fs.writeFileSync(coverPath, coverData);
      console.log(`  -> Saved as ${coverPath}`);
    } else {
      console.log(`Has RAW Cover: No`);
    }

    const hasChapters = buffer[11] === 1;
    if (hasChapters && chapterOffset > 0) {
      const chapterCount = view.getUint16(metadataOffset + 196, true);
      console.log(`Chapters: ${chapterCount}`);
      for (let i = 0; i < Math.min(chapterCount, 50); i++) {
        const chPos = chapterOffset + i * 96;
        let chTitle = "";
        for (let j = 0; j < 79; j++) {
          if (buffer[chPos + j] === 0) break;
          chTitle += String.fromCharCode(buffer[chPos + j]);
        }
        const startPage = view.getUint16(chPos + 80, true);
        console.log(`  Chapter ${i + 1}: ${chTitle} (Page ${startPage})`);
      }
      if (chapterCount > 50) console.log(`  ... and ${chapterCount - 50} more`);
    }
  }

  console.log(`\n--- EXTRACTING PAGES ---`);
  const s = Math.max(1, startPage) - 1;
  const e = Math.min(numPages, endPage);

  for (let i = s; i < e; i++) {
    const entryOffset = indexOffset + i * 16;
    const pageDataOffset = Number(view.getBigUint64(entryOffset, true));
    const pageDataSize = view.getUint32(entryOffset + 8, true);
    const width = view.getUint16(entryOffset + 12, true);
    const height = view.getUint16(entryOffset + 14, true);

    console.log(
      `Page ${i + 1}: ${width}x${height}, Size: ${(pageDataSize / 1024).toFixed(1)} KB, Offset: 0x${pageDataOffset.toString(16)}`,
    );

    // Extract the page data (which contains an XTG/XTH header)
    if (!isHQ) {
      extractXTGtoPBM(buffer, pageDataOffset, width, height, i + 1);
    } else {
      console.log(
        "XTH (2-bit) extraction to PBM not simply supported in this quick script yet.",
      );
    }
  }
}

function extractXTGtoPBM(buffer, offset, width, height, pageNum) {
  // XTG Header is 22 bytes
  const xtgMagic = buffer.toString("utf8", offset, offset + 4);
  if (xtgMagic !== "XTG\0") {
    console.warn(
      `  Warning: Expected XTG header at page ${pageNum}, got ${xtgMagic}`,
    );
    return;
  }

  const rowBytes = Math.ceil(width / 8);
  // Bitmap data starts at offset + 22
  const bitmapData = buffer.slice(offset + 22, offset + 22 + rowBytes * height);

  // Create a PBM file (P4 = raw binary PBM)
  // PBM header: P4\n width height\n
  const header = `P4\n${width} ${height}\n`;
  const headerBuffer = Buffer.from(header, "utf8");

  // In XTG 1=white, 0=black. In PBM 0=white, 1=black. We need to invert bits.
  const invertedBitmap = Buffer.alloc(bitmapData.length);
  for (let i = 0; i < bitmapData.length; i++) {
    invertedBitmap[i] = ~bitmapData[i] & 0xff;
  }

  const pbmPath = `/tmp/page_${pageNum}.pbm`;
  const outBuffer = Buffer.concat([headerBuffer, invertedBitmap]);
  fs.writeFileSync(pbmPath, outBuffer);
  console.log(`  -> Saved as ${pbmPath}`);
}

const args = process.argv.slice(2);
if (args.length > 0) {
  const start = parseInt(args[1] || "1");
  const end = parseInt(args[2] || start + 2);
  inspectXTC(args[0], start, end);
} else {
  console.log("Usage: node inspect_xtc.js <file.xtc> [startPage] [endPage]");
}
