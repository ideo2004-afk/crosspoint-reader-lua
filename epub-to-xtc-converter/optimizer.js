/**
 * EPUB Optimizer for CLI
 * Optimizes EPUBs for Xteink e-paper devices (ESP32-C3, 800x480 1-bit/4-level grayscale)
 *
 * Device constraints (from papyrix-reader firmware):
 * - Viewport: 464x788px usable
 * - Max image decode: 2048x3072px
 * - JPEG: baseline only (no progressive/arithmetic)
 * - No GIF/SVG/WebP support
 * - CSS: max 1500 rules, simple selectors only (tag, .class, tag.class)
 * - No color, no transparency
 * - Max word length: 200 chars
 * - Images <20px skipped as decorative
 */

const fs = require("fs");
const path = require("path");
const JSZip = require("jszip");
const sharp = require("sharp");

const MAX_IMAGE_DECODE_WIDTH = 2048;
const MAX_IMAGE_DECODE_HEIGHT = 3072;
const MIN_IMAGE_SIZE = 20;

/**
 * Remove problematic CSS properties for e-paper rendering
 */
function cleanCss(css) {
  const problematic = [
    /float\s*:\s*[^;]+;?/gi,
    /position\s*:\s*(fixed|absolute|relative|sticky)[^;]*;?/gi,
    /display\s*:\s*(flex|grid|inline-flex|inline-grid)[^;]*;?/gi,
    /transform[^;]*;?/gi,
    /animation[^;]*;?/gi,
    /transition[^;]*;?/gi,
    /opacity\s*:\s*[^;]+;?/gi,
    /box-shadow[^;]*;?/gi,
    /text-shadow[^;]*;?/gi,
    /border-radius[^;]*;?/gi,
    /background[^;]*;?/gi,
    /color\s*:\s*[^;]+;?/gi,
    /overflow[^;]*;?/gi,
    /z-index[^;]*;?/gi,
    /visibility[^;]*;?/gi,
    /font-size\s*:\s*[^;]+;?/gi,
    /line-height\s*:\s*[^;]+;?/gi,
    /font-family\s*:\s*[^;]+;?/gi,
    /text-indent\s*:\s*[^;]+;?/gi,
    /font-weight\s*:\s*[^;]+;?/gi,
    /font-style\s*:\s*[^;]+;?/gi,
    /font-variant[^;]*;?/gi,
    /letter-spacing\s*:\s*[^;]+;?/gi,
    /word-spacing\s*:\s*[^;]+;?/gi,
    /orphans\s*:\s*[^;]+;?/gi,
    /widows\s*:\s*[^;]+;?/gi,
    /page-break-[^;]*;?/gi,
    /text-transform[^;]*;?/gi,
    /text-decoration[^;]*;?/gi,
    /column-count[^;]*;?/gi,
    /-webkit-[^;]*;?/gi,
    /:\s*(initial|inherit|revert|unset)[^;]*;?/gi,
  ];

  for (const pattern of problematic) {
    css = css.replace(pattern, "");
  }

  // Strip @media blocks with balanced brace matching
  css = stripAtBlocks(css, "@media");
  css = stripAtBlocks(css, "@font-face");
  css = stripAtBlocks(css, "@keyframes");
  css = stripAtBlocks(css, "@import");
  css = stripAtBlocks(css, "@supports");

  return css;
}

/**
 * Remove @-rule blocks using balanced brace matching
 */
function stripAtBlocks(css, atRule) {
  let result = "";
  let i = 0;
  while (i < css.length) {
    const idx = css.indexOf(atRule, i);
    if (idx === -1) {
      result += css.slice(i);
      break;
    }
    result += css.slice(i, idx);

    // @import has no braces — just a semicolon
    if (atRule === "@import") {
      const semi = css.indexOf(";", idx);
      i = semi === -1 ? css.length : semi + 1;
      continue;
    }

    const braceStart = css.indexOf("{", idx);
    if (braceStart === -1) {
      result += css.slice(idx);
      break;
    }
    let depth = 1;
    let j = braceStart + 1;
    while (j < css.length && depth > 0) {
      if (css[j] === "{") depth++;
      else if (css[j] === "}") depth--;
      j++;
    }
    i = j;
  }
  return result;
}

/**
 * Remove inline styles with problematic properties from HTML
 */
function cleanHtmlStyles(html) {
  return html.replace(/style="[^"]*"/gi, function (match) {
    let style = match;
    style = style.replace(/float\s*:\s*[^;"]+;?/gi, "");
    style = style.replace(
      /position\s*:\s*(fixed|absolute|relative|sticky)[^;"]*;?/gi,
      "",
    );
    style = style.replace(/background[^;"]*;?/gi, "");
    style = style.replace(/color\s*:\s*[^;"]+;?/gi, "");
    style = style.replace(/font-size\s*:\s*[^;"]+;?/gi, "");
    style = style.replace(/line-height\s*:\s*[^;"]+;?/gi, "");
    style = style.replace(/font-family\s*:\s*[^;"]+;?/gi, "");
    style = style.replace(/font-weight\s*:\s*[^;"]+;?/gi, "");
    style = style.replace(/text-indent\s*:\s*[^;"]+;?/gi, "");
    return style;
  });
}

/**
 * Strip base64 data URIs from any src attribute — matches firmware DataUriStripper
 * which replaces all src="data:..." with src="#" to prevent expat OOM
 */
function stripDataUris(html) {
  return html
    .replace(/(src\s*=\s*")data:[^"]+(")/gi, "$1#$2")
    .replace(/(src\s*=\s*')data:[^']+(')/gi, "$1#$2");
}

/**
 * Remove hardcoded full-width leading spaces from paragraphs.
 * This regex strictly targets the start of text nodes directly following a <p> tag
 * to avoid corrupting HTML attributes and causing WASM renderer stack overflows.
 */
function stripLeadingSpaces(html) {
  // Matches `<p...>` followed by any sequence of spaces or full-width spaces (\u3000)
  return html.replace(/(<p[^>]*>)((?:[\s\u3000]|&nbsp;|&#12288;)+)/gi, "$1");
}

/**
 * Insert soft hyphens into words longer than maxLen characters.
 * Targeting only ASCII non-whitespace characters [\x21-\x7E] to avoid
 * breaking continuous CJK (Chinese/Japanese/Korean) text which naturally has no spaces.
 */
function breakLongWords(html, maxLen) {
  if (!maxLen) maxLen = 200;
  // Only break inside text nodes (between > and <)
  return html.replace(/>([^<]+)</g, function (match, text) {
    // Only match continuous ASCII characters (URLs, base64 data)
    const re = new RegExp("[\\x21-\\x7E]{" + maxLen + ",}", "g");
    const broken = text.replace(re, function (word) {
      let result = "";
      for (let i = 0; i < word.length; i += maxLen) {
        if (i > 0) result += "\u00AD"; // soft hyphen
        result += word.slice(i, i + maxLen);
      }
      return result;
    });
    return ">" + broken + "<";
  });
}

/**
 * Strip <span> tags to prevent CREngine from forcing weird line breaks at
 * inline element boundaries during CJK vertical rendering.
 */
function stripFormattingSpans(html) {
  return html.replace(/<\/?span[^>]*>/gi, "");
}

/**
 * Remove SVG wrappers from covers that might constrain the image
 */
function neutralizeCoverSvg(html) {
  // Replace <svg...>...<image.../></svg> with just the <img> tag
  // This prevents the SVG viewBox from squashing my rotated cover image
  // Also handles xlink:href and different quote types
  return html.replace(
    /<svg[^>]*>([\s\S]*?)<(?:image|img)[^>]*(?:href|src)\s*=\s*["']([^"']*)["'][^>]*\/?>(?:[\s\S]*?<\/svg>)?/gi,
    '<img src="$2" style="width: 100%; height: 100%; object-fit: fill; position: absolute; top: 0; left: 0;" />',
  );
}

/**
 * Inject e-paper optimized CSS into HTML documents
 * Dynamically adjusts styles based on whether it's a cover page
 */
function injectEpaperCss(html, options, isCover = false) {
  // For covers, we want 0 margin to allow full-screen display
  const bodyMargin = isCover ? "0 !important" : "1em 0 !important";

  // Uniform style for e-paper
  const epaperCss =
    '<style type="text/css">' +
    "* { color: black !important; font-weight: inherit !important; }" +
    `body { font-family: serif !important; line-height: 1.4 !important; text-align: justify !important; margin: ${bodyMargin}; padding: 0 !important; }` +
    `p { margin: ${options.paragraphSpacing || 0}em 0 !important; text-indent: 2em !important; }` +
    "h1, h2, h3, h4, h5, h6 { text-indent: 0 !important; margin: 1em 0 0.5em 0 !important; font-weight: bold !important; }" +
    "img { max-width: 100%; height: auto; }" +
    "</style>";

  if (html.indexOf("</head>") !== -1) {
    return html.replace("</head>", epaperCss + "</head>");
  }
  return html;
}

/**
 * Process image: ensure baseline JPEG, resize, grayscale, flatten alpha
 */
async function processImage(
  imgBuffer,
  maxWidth,
  toGrayscale,
  rotation = 0,
  trim = false,
) {
  try {
    let pipeline = sharp(imgBuffer);

    // 1. Trim white borders if requested (useful for covers)
    if (trim) {
      pipeline = pipeline.trim();
    }

    // 2. Rotate if requested
    if (rotation) {
      pipeline = pipeline.rotate(rotation);
    }

    const metadata = await pipeline.metadata();

    // Skip tiny decorative images
    if (metadata.width < MIN_IMAGE_SIZE || metadata.height < MIN_IMAGE_SIZE) {
      return null;
    }

    // Flatten alpha to white — e-paper has no transparency
    if (metadata.channels === 4 || metadata.hasAlpha) {
      pipeline = pipeline.flatten({ background: { r: 255, g: 255, b: 255 } });
    }

    // Enforce device decode limits
    const effectiveMaxWidth = Math.min(
      maxWidth || MAX_IMAGE_DECODE_WIDTH,
      MAX_IMAGE_DECODE_WIDTH,
    );

    // For images that were rotated 90 degrees, we should allow them to be wider (landscape)
    const effectiveMaxHeight =
      rotation === 90 || rotation === 270
        ? MAX_IMAGE_DECODE_WIDTH // e.g. 2048 instead of 3072
        : MAX_IMAGE_DECODE_HEIGHT;

    if (
      metadata.width > effectiveMaxWidth ||
      metadata.height > effectiveMaxHeight
    ) {
      pipeline = pipeline.resize({
        width: effectiveMaxWidth,
        height: effectiveMaxHeight,
        fit: "inside",
        withoutEnlargement: true,
      });
    }

    if (toGrayscale) {
      pipeline = pipeline.grayscale();
    }

    // Always output baseline JPEG — device doesn't support progressive
    return await pipeline.jpeg({ quality: 85, progressive: false }).toBuffer();
  } catch (e) {
    console.warn(`[Optimizer] Image process error: ${e.message}`);
    return null;
  }
}

/**
 * Optimize an EPUB file
 * @param {string} inputPath - Path to input EPUB
 * @param {string} outputPath - Path to output EPUB
 * @param {object} options - Optimizer options
 * @returns {object} Stats about the optimization
 */
async function optimizeEpub(inputPath, outputPath, options) {
  console.log(`[Optimizer] Starting optimization for ${inputPath}`);
  console.log(`[Optimizer] Options: ${JSON.stringify(options)}`);
  const data = fs.readFileSync(inputPath);
  const originalSize = data.length;
  const epubZip = await JSZip.loadAsync(data);

  const ops = [];
  const imageRenames = {}; // old path -> new path for format conversions
  const strippedFonts = []; // paths of removed font files
  const files = Object.keys(epubZip.files);

  // Try to find cover image path from OPF
  let coverPath = null;
  for (const filePath of files) {
    if (/\.opf$/i.test(filePath)) {
      try {
        const opfStr = await epubZip.files[filePath].async("string");
        let coverId = null;

        // 1. Find cover ID from <meta name="cover" content="ID"/> or similar
        // We look for name="cover" and content="id" in any order within a meta tag
        const metaCoverMatch = opfStr.match(/<meta[^>]*>/gi);
        if (metaCoverMatch) {
          for (const metaTag of metaCoverMatch) {
            if (/name\s*=\s*["']cover["']/i.test(metaTag)) {
              const contentMatch = metaTag.match(
                /content\s*=\s*["']([^"']*)["']/i,
              );
              if (contentMatch) {
                coverId = contentMatch[1];
                console.log(`[Optimizer] Found cover ID from meta: ${coverId}`);
                break;
              }
            }
          }
        }

        // 2. If no coverId from meta, look for item with properties="cover-image" (EPUB 3)
        if (!coverId) {
          const itemCoverMatch = opfStr.match(
            /<item[^>]*properties\s*=\s*["'][^"']*cover-image[^"']*["'][^>]*>/i,
          );
          if (itemCoverMatch) {
            const idMatch = itemCoverMatch[0].match(
              /id\s*=\s*["']([^"']*)["']/i,
            );
            if (idMatch) {
              coverId = idMatch[1];
            }
          }
        }

        if (coverId) {
          // Find item tag with this ID in any order
          const allItems = opfStr.match(/<item[^>]*>/gi);
          if (allItems) {
            for (const itemTag of allItems) {
              const idMatch = itemTag.match(/id\s*=\s*["']([^"']*)["']/i);
              if (idMatch && idMatch[1] === coverId) {
                const hrefMatch = itemTag.match(/href\s*=\s*["']([^"']*)["']/i);
                if (hrefMatch) {
                  const opfDir = path.dirname(filePath);
                  coverPath = path
                    .join(opfDir, hrefMatch[1])
                    .split(path.sep)
                    .join("/");
                  if (coverPath.startsWith("./"))
                    coverPath = coverPath.substring(2);
                  break;
                }
              }
            }
          }
        }

        // 3. Fallback: find item with id="cover" or "cover-image"
        if (!coverPath) {
          const fallbackMatch = opfStr.match(
            /<item[^>]*id="cover(-image)?"[^>]*href="([^"]*)"/i,
          );
          if (fallbackMatch) {
            const opfDir = path.dirname(filePath);
            coverPath = path
              .join(opfDir, fallbackMatch[2])
              .split(path.sep)
              .join("/");
            if (coverPath.startsWith("./")) coverPath = coverPath.substring(2);
          }
        }
      } catch (e) {
        console.warn(`Warning: Failed to parse OPF for cover: ${e.message}`);
      }
      break;
    }
  }

  for (const filePath of files) {
    const zipFile = epubZip.files[filePath];
    if (zipFile.dir) continue;

    // Remove embedded fonts
    if (options.stripFonts && /\.(ttf|otf|woff|woff2)$/i.test(filePath)) {
      epubZip.remove(filePath);
      strippedFonts.push(filePath);
      ops.push({ type: "stripFont", file: filePath });
      continue;
    }

    // Remove unsupported image formats (GIF, SVG, WebP, TIFF)
    if (/\.(gif|svg|webp|tiff?)$/i.test(filePath)) {
      // Try to convert to JPEG via sharp, remove if conversion fails
      try {
        const imgData = await zipFile.async("nodebuffer");
        const processed = await processImage(
          imgData,
          options.maxImageWidth,
          options.grayscale,
        );
        if (processed) {
          const jpegPath = filePath.replace(/\.[^.]+$/, ".jpg");
          epubZip.remove(filePath);
          epubZip.file(jpegPath, processed);
          imageRenames[filePath] = jpegPath;
          ops.push({ type: "convertFormat", file: filePath, to: jpegPath });
        } else {
          epubZip.remove(filePath);
          ops.push({ type: "removeUnsupported", file: filePath });
        }
      } catch {
        epubZip.remove(filePath);
        ops.push({ type: "removeUnsupported", file: filePath });
      }
      continue;
    }

    // Process CSS
    if (options.removeCss && /\.css$/i.test(filePath)) {
      const css = await zipFile.async("string");
      const cleaned = cleanCss(css);
      epubZip.file(filePath, cleaned);
      ops.push({ type: "cleanCss", file: filePath });
    }

    // Process HTML/XHTML
    if (/\.(html|xhtml|htm)$/i.test(filePath)) {
      let html = await zipFile.async("string");
      let changed = false;

      // Force .bmp to .jpg replacement in HTML regardless of other options
      if (html.toLowerCase().includes(".bmp")) {
        html = html.replace(/\.bmp/gi, ".jpg");
        changed = true;
        ops.push({ type: "bmpToJpgRef", file: filePath });
      }

      if (options.removeCss) {
        html = cleanHtmlStyles(html);
        ops.push({ type: "cleanHtmlStyles", file: filePath });
      } // Strip data URIs to prevent OOM on device
      html = stripDataUris(html);

      // Strip hardcoded leading spaces to prevent double indents
      html = stripLeadingSpaces(html);

      // Strip <span> tags to fix CJK line-breaking bugs around inline elements
      html = stripFormattingSpans(html);

      // Break words >200 chars to prevent layout issues
      html = breakLongWords(html, 200);

      // Detect if this is the cover HTML page
      const isCoverHtml =
        coverPath &&
        html.toLowerCase().includes(path.basename(coverPath).toLowerCase());

      if (options.injectCss) {
        html = injectEpaperCss(html, options, isCoverHtml);
        ops.push({ type: "injectCss", file: filePath });
      }

      // Neutralize SVG wrappers for covers
      if (isCoverHtml) {
        html = neutralizeCoverSvg(html);
      }

      epubZip.file(filePath, html);
    }

    // Process supported images (JPEG, PNG, BMP)
    const isImage = /\.(jpg|jpeg|png|bmp)$/i.test(filePath);
    if ((options.grayscale || options.maxImageWidth) && isImage) {
      try {
        const isCover = coverPath && filePath.endsWith(coverPath);
        const imgData = await zipFile.async("nodebuffer");

        // Rotate cover 270° (CCW 90°) ONLY if it's a vertical reading book (rotation 90)
        // This compensates for the global 90° CW rotation to keep cover oriented correctly
        const rotationAngle = 0;

        const processed = await processImage(
          imgData,
          options.maxImageWidth,
          isCover ? false : options.grayscale,
          rotationAngle,
          isCover, // Always trim cover if possible
        );

        if (processed) {
          // If it was BMP or PNG, we always rename to JPG
          if (/\.(png|bmp)$/i.test(filePath)) {
            const jpegPath = filePath.replace(/\.[^.]+$/, ".jpg");
            epubZip.remove(filePath);
            epubZip.file(jpegPath, processed);
            imageRenames[filePath] = jpegPath;
            ops.push({ type: "convertImage", file: filePath, to: jpegPath });
          } else {
            // It was already JPG/JPEG, just update content
            epubZip.file(filePath, processed);
            ops.push({ type: "processImage", file: filePath });
          }
        }
      } catch (e) {
        console.warn(
          `[Optimizer] Failed to process image ${filePath}: ${e.message}`,
        );
      }
      continue;
    }
  }

  // Update HTML/XHTML references for all renamed images (post-loop so all renames are collected)
  if (Object.keys(imageRenames).length > 0) {
    for (const htmlPath of Object.keys(epubZip.files)) {
      if (!/\.(html|xhtml|htm)$/i.test(htmlPath)) continue;
      let html = await epubZip.files[htmlPath].async("string");
      let changed = false;
      for (let [oldImg, newImg] of Object.entries(imageRenames)) {
        // Use relative path from HTML location (matches EPUB reference format)
        const htmlDir = path.dirname(htmlPath);

        // Try multiple reference styles: relative path and just filename
        const oldRef = htmlDir
          ? path.relative(htmlDir, oldImg).split(path.sep).join("/")
          : oldImg;
        const newRef = htmlDir
          ? path.relative(htmlDir, newImg).split(path.sep).join("/")
          : newImg;

        const oldBase = path.basename(oldImg);
        const newBase = path.basename(newImg);

        if (html.indexOf(oldRef) !== -1) {
          html = html.split(oldRef).join(newRef);
          changed = true;
          ops.push({
            type: "updateImageRef",
            file: htmlPath,
            from: oldRef,
            to: newRef,
          });
        } else if (html.indexOf(oldBase) !== -1) {
          // Fallback: search for just the filename if full path doesn't match
          html = html.split(oldBase).join(newBase);
          changed = true;
          ops.push({
            type: "updateImageRefFilename",
            file: htmlPath,
            from: oldBase,
            to: newBase,
          });
        }
      }
      // Universal BMP to JPG string replacement as a last resort
      if (html.includes(".bmp")) {
        html = html.replace(/\.bmp/gi, ".jpg");
        changed = true;
        ops.push({ type: "bmpToJpgFallback", file: htmlPath });
      }

      if (changed) {
        epubZip.file(htmlPath, html);
      }
    }
  }

  // Update OPF manifest: remove stripped font entries, update renamed image references
  const hasOpfWork =
    strippedFonts.length > 0 || Object.keys(imageRenames).length > 0;
  if (hasOpfWork) {
    for (const opfPath of Object.keys(epubZip.files)) {
      if (!/\.opf$/i.test(opfPath)) continue;
      let opf = await epubZip.files[opfPath].async("string");

      // Remove <item> entries for stripped fonts
      for (const fontPath of strippedFonts) {
        const fontHref =
          path.relative(path.dirname(opfPath), fontPath) ||
          path.basename(fontPath);
        const escaped = fontHref.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
        opf = opf.replace(
          new RegExp('\\s*<item[^>]*href="' + escaped + '"[^>]*/>', "g"),
          "",
        );
      }

      // Update renamed image references
      for (const [oldImg, newImg] of Object.entries(imageRenames)) {
        const oldHref =
          path.relative(path.dirname(opfPath), oldImg) || path.basename(oldImg);
        const newHref =
          path.relative(path.dirname(opfPath), newImg) || path.basename(newImg);
        opf = opf.split(oldHref).join(newHref);
        // Update media-type for converted images (handle either attribute order)
        const hrefEsc = newHref.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
        opf = opf.replace(
          new RegExp(
            '(<item\\b[^>]*\\bhref="' +
              hrefEsc +
              '"[^>]*?)\\bmedia-type="[^"]*"',
          ),
          '$1media-type="image/jpeg"',
        );
        opf = opf.replace(
          new RegExp(
            '(<item\\b[^>]*?)\\bmedia-type="[^"]*"([^>]*\\bhref="' +
              hrefEsc +
              '")',
          ),
          '$1media-type="image/jpeg"$2',
        );
      }

      epubZip.file(opfPath, opf);
      ops.push({ type: "updateOpf", file: opfPath });
    }
  }

  const outputBuffer = await epubZip.generateAsync({
    type: "nodebuffer",
    compression: "DEFLATE",
    compressionOptions: { level: 9 },
  });

  // Ensure output directory exists
  const outputDir = path.dirname(outputPath);
  if (!fs.existsSync(outputDir)) {
    fs.mkdirSync(outputDir, { recursive: true });
  }

  fs.writeFileSync(outputPath, outputBuffer);

  return {
    inputPath,
    outputPath,
    originalSize,
    optimizedSize: outputBuffer.length,
    reduction: originalSize - outputBuffer.length,
    reductionPercent: ((1 - outputBuffer.length / originalSize) * 100).toFixed(
      1,
    ),
    operations: ops,
  };
}

module.exports = {
  optimizeEpub,
  cleanCss,
  cleanHtmlStyles,
  stripDataUris,
  stripLeadingSpaces,
  stripFormattingSpans,
  neutralizeCoverSvg,
  breakLongWords,
  injectEpaperCss,
  processImage,
};
