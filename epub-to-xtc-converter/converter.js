/**
 * Core EPUB to XTC/XTCH converter
 * Uses CREngine WASM for EPUB rendering
 */

const fs = require("fs");
const path = require("path");
const JSZip = require("jszip");
const { execSync } = require("child_process");
const { applyDithering, applyNegative } = require("./dither");
const { encodeXTG, encodeXTH, buildXTCContainer } = require("./encoder");

let Module = null;
let renderer = null;
let currentWidth = 0;
let currentHeight = 0;
let fontDataPointers = {}; // Cache font data pointers in WASM memory

/**
 * Destroy renderer and free WASM memory
 */
function destroyRenderer() {
  if (renderer) {
    renderer.delete(); // Emscripten destructor - frees WASM heap
    renderer = null;
    currentWidth = 0;
    currentHeight = 0;
  }
}

/**
 * Initialize CREngine WASM module
 */
async function initWasm() {
  if (Module) return;

  const wasmPath = path.join(__dirname, "..", "web", "crengine.js");

  if (!fs.existsSync(wasmPath)) {
    throw new Error(`CREngine WASM not found at: ${wasmPath}`);
  }

  // Load CREngine module
  const CREngine = require(wasmPath);
  Module = await CREngine();
}

/**
 * Create or resize renderer with specified dimensions
 */
function getRenderer(width, height) {
  if (!Module) {
    throw new Error("WASM module not initialized. Call initWasm() first.");
  }

  if (renderer) {
    if (currentWidth !== width || currentHeight !== height) {
      renderer.resize(width, height);
      currentWidth = width;
      currentHeight = height;
    }
    return renderer;
  }

  renderer = new Module.EpubRenderer(width, height);
  currentWidth = width;
  currentHeight = height;

  return renderer;
}

/**
 * Register font from file
 */
async function registerFont(fontPath) {
  if (!renderer) {
    throw new Error("Renderer not initialized");
  }

  const fontName = path.basename(fontPath);

  // Check if we already have this font in WASM memory
  if (!fontDataPointers[fontPath]) {
    const fontData = fs.readFileSync(fontPath);
    const ptr = Module.allocateMemory(fontData.length);
    Module.HEAPU8.set(new Uint8Array(fontData), ptr);
    fontDataPointers[fontPath] = { ptr, size: fontData.length };
  }

  const { ptr, size } = fontDataPointers[fontPath];
  renderer.registerFontFromMemory(ptr, size, fontName);

  return fontName;
}

/**
 * Load EPUB file into renderer
 */
async function loadEpub(epubPath) {
  if (!renderer) {
    throw new Error("Renderer not initialized");
  }

  const epubData = fs.readFileSync(epubPath);

  const ptr = Module.allocateMemory(epubData.length);
  Module.HEAPU8.set(new Uint8Array(epubData), ptr);

  try {
    renderer.loadEpubFromMemory(ptr, epubData.length);

    // Disable built-in status bar (must be after loading document)
    renderer.configureStatusBar(
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      false,
      false,
    );
  } finally {
    Module.freeMemory(ptr);
  }

  return {
    pageCount: renderer.getPageCount(),
    info: renderer.getDocumentInfo() || {},
    toc: renderer.getToc() || [],
  };
}

/**
 * Apply rendering settings
 */
function applySettings(settings) {
  if (!renderer) {
    throw new Error("Renderer not initialized");
  }

  const { margins, font, lineHeight, textAlignValue, hyphenation } = settings;

  renderer.setMargins(margins.left, margins.top, margins.right, margins.bottom);
  renderer.setFontSize(font.size);
  renderer.setFontWeight(font.weight);
  renderer.setInterlineSpace(lineHeight);
  renderer.setTextAlign(textAlignValue);

  if (hyphenation.enabled) {
    renderer.setHyphenation(2); // Dictionary-based
    if (renderer.setHyphenationLanguage) {
      renderer.setHyphenationLanguage(hyphenation.language);
    }
  } else {
    renderer.setHyphenation(0); // Disabled
  }
}

/**
 * Render a single page
 */
function renderPage(pageNum) {
  if (!renderer) {
    throw new Error("Renderer not initialized");
  }

  renderer.goToPage(pageNum);
  renderer.renderCurrentPage();

  const frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer || frameBuffer.length === 0) {
    throw new Error(`Empty frame buffer for page ${pageNum}`);
  }

  // Copy buffer (frame buffer may be reused by WASM)
  return new Uint8ClampedArray(frameBuffer);
}

/**
 * Find and extract cover image from EPUB zip
 */
async function findCoverBuffer(zip) {
  try {
    const files = Object.keys(zip.files);
    let opfPath = files.find((f) => /\.opf$/i.test(f));
    if (!opfPath) return null;

    const opfStr = await zip.files[opfPath].async("string");
    let href = null;

    // 1. Try to find item with properties="cover-image" (EPUB 3)
    const ep3Match = opfStr.match(/<item[^>]+properties=[^>]*cover-image[^>]*[^>]+href=["']([^"']+)["']/i) || 
                     opfStr.match(/<item[^>]+href=["']([^"']+)["'][^>]+properties=[^>]*cover-image/i);
    if (ep3Match) {
      href = ep3Match[1];
    }

    // 2. Try EPUB 2 meta name="cover"
    if (!href) {
      const metaMatch = opfStr.match(/<meta[^>]+name=["']cover["'][^>]+content=["']([^"']+)["']/i) ||
                        opfStr.match(/<meta[^>]+content=["']([^"']+)["'][^>]+name=["']cover["']/i);
      if (metaMatch) {
        const coverId = metaMatch[1];
        const itemRegex = new RegExp(`<item[^>]+id=["']${coverId}["'][^>]+href=["']([^"']+)["']`, "i");
        const itemMatch = opfStr.match(itemRegex);
        if (itemMatch) {
          href = itemMatch[1];
        }
      }
    }

    // 3. Fallback: item with id="cover" or "cover-image" or containing "cover" in href
    if (!href) {
      const fallbackMatch = opfStr.match(/<item[^>]+id=["']cover(?:-image)?["'][^>]+href=["']([^"']+)["']/i) ||
                            opfStr.match(/<item[^>]+href=["']([^"']*cover[^"']*\.(?:jpg|jpeg|png))["']/i);
      if (fallbackMatch) {
        href = fallbackMatch[1];
      }
    }

    if (href) {
      // Decode HTML entities in href
      href = href.replace(/&amp;/g, '&').replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&#39;/g, "'");
      
      const fullPath = path.join(path.dirname(opfPath), href).split(path.sep).join("/");
      const normalizedPath = fullPath.startsWith("./") ? fullPath.substring(2) : fullPath;
      
      // Try exact path then case-insensitive
      if (zip.files[normalizedPath]) {
        return await zip.files[normalizedPath].async("nodebuffer");
      }
      
      const lowerPath = normalizedPath.toLowerCase();
      const caseInsensitiveMatch = Object.keys(zip.files).find(f => f.toLowerCase() === lowerPath);
      if (caseInsensitiveMatch) {
        return await zip.files[caseInsensitiveMatch].async("nodebuffer");
      }
    }
  } catch (e) {
    console.warn(`[Converter] Failed to extract cover: ${e.message}`);
  }
  return null;
}

/**
 * Convert single EPUB to XTC/XTCH
 */
async function convertEpub(epubPath, outputPath, settings, progressCallback) {
  const { width, height, output } = settings;
  const isHQ = output.format === "xtch";
  const bits = isHQ ? 2 : 1;

  // Initialize and setup
  await initWasm();

  // 1. Initial setup with landscape dimensions
  getRenderer(width, height);
  await registerFont(settings.font.path);
  const initialLoad = await loadEpub(epubPath);
  applySettings(settings);
  
  const docInfo = initialLoad.info;
  const docToc = renderer.getToc() || [];

  // 2. Handle pagination
  let totalPages = renderer.getPageCount();
  if (totalPages === 0) {
    throw new Error("EPUB has no pages");
  }

  // 3. Render all pages
  const pages = [];
  for (let i = 0; i < totalPages; i++) {
    // Render page at fixed dimensions
    let imageData = renderPage(i);

    // Apply dithering if enabled
    if (output.dithering) {
      imageData = applyDithering(
        imageData,
        width,
        height,
        bits,
        output.ditherStrength,
      );
    }

    // Apply negative if enabled
    if (output.negative) {
      applyNegative(imageData);
    }

    // Apply rotation or reverse lines if specified
    let finalData = imageData;
    let finalWidth = width;
    let finalHeight = height;

    if (settings.reverseLines) {
      finalData = new Uint8ClampedArray(width * height * 4);
      for (let y = 0; y < height; y++) {
        const srcRowStart = y * width * 4;
        const dstRowStart = (height - 1 - y) * width * 4;
        finalData.set(
          imageData.subarray(srcRowStart, srcRowStart + width * 4),
          dstRowStart,
        );
      }
    } else if (settings.rotation === 90 || settings.rotation === 270) {
      finalWidth = height;
      finalHeight = width;
      finalData = new Uint8ClampedArray(finalWidth * finalHeight * 4);

      for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
          let dx, dy;
          if (settings.rotation === 90) {
            dx = height - 1 - y;
            dy = x;
          } else {
            dx = y;
            dy = width - 1 - x;
          }
          const srcIdx = (y * width + x) * 4;
          const dstIdx = (dy * finalWidth + dx) * 4;
          finalData[dstIdx] = imageData[srcIdx];
          finalData[dstIdx + 1] = imageData[srcIdx + 1];
          finalData[dstIdx + 2] = imageData[srcIdx + 2];
          finalData[dstIdx + 3] = imageData[srcIdx + 3];
        }
      }
    } else if (settings.rotation === 180) {
      finalData = new Uint8ClampedArray(width * height * 4);
      for (let j = 0; j < width * height; j++) {
        const srcIdx = j * 4;
        const dstIdx = (width * height - 1 - j) * 4;
        finalData[dstIdx] = imageData[srcIdx];
        finalData[dstIdx + 1] = imageData[srcIdx + 1];
        finalData[dstIdx + 2] = imageData[srcIdx + 2];
        finalData[dstIdx + 3] = imageData[srcIdx + 3];
      }
    }

    // Encode page with its specific dimensions
    const encoded = isHQ
      ? encodeXTH(finalData, finalWidth, finalHeight)
      : encodeXTG(finalData, finalWidth, finalHeight);
    pages.push({ data: encoded, width: finalWidth, height: finalHeight });

    // Progress callback
    if (progressCallback) {
      progressCallback(i + 1, totalPages);
    }
  }

  // Build container
  const metadata = {
    title: docInfo.title || path.basename(epubPath, ".epub"),
    author: docInfo.author || docInfo.authors || "",
  };

  const containerWidth =
    settings.rotation === 90 || settings.rotation === 270 ? height : width;
  const containerHeight =
    settings.rotation === 90 || settings.rotation === 270 ? width : height;

  // Get raw cover buffer for XTC+
  const epubZip = await JSZip.loadAsync(fs.readFileSync(epubPath));
  let rawCoverBuffer = await findCoverBuffer(epubZip);
  if (rawCoverBuffer) {
    console.log(`\n  Found high-res cover: ${rawCoverBuffer.length} bytes`);

    // No manual rotation needed for the high-res cover; the device handles orientation.
  }

  const container = buildXTCContainer(
    pages,
    metadata,
    docToc,
    containerWidth,
    containerHeight,
    isHQ,
    rawCoverBuffer
  );

  // Write output
  fs.writeFileSync(outputPath, container);

  return {
    outputPath,
    pageCount: totalPages,
    format: output.format,
  };
}

/**
 * Get output path for an EPUB file
 */
function getOutputPath(inputPath, outputDir, format) {
  const basename = path.basename(inputPath, ".epub");
  const extension = format === "xtch" ? ".xtch" : ".xtc";
  return path.join(outputDir, basename + extension);
}

/**
 * Cleanup renderer resources
 */
function cleanup() {
  destroyRenderer();
}

module.exports = {
  initWasm,
  createRenderer: getRenderer,
  registerFont,
  loadEpub,
  applySettings,
  renderPage,
  convertEpub,
  getOutputPath,
  cleanup,
};
