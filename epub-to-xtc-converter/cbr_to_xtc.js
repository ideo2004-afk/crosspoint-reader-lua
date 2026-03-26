#!/usr/bin/env node

/**
 * CBR/CBZ to XTC/XTCH CLI Converter
 * Converts CBR/CBZ comic files to Xteink e-reader format
 */

const { program } = require("commander");
const fs = require("fs");
const path = require("path");
const os = require("os");
const { execSync } = require("child_process");
const sharp = require("sharp");
const {
  loadSettings,
  resolveSettings,
} = require("./settings");
const { applyDithering, applyNegative } = require("./dither");
const { encodeXTG, encodeXTH, buildXTCContainer } = require("./encoder");

// Supported comic extensions
const COMIC_EXTENSIONS = [".cbr", ".cbz"];
// Supported image extensions
const IMAGE_EXTENSIONS = [".jpg", ".jpeg", ".png", ".webp", ".bmp"];

/**
 * Natural sort strings
 */
const collator = new Intl.Collator(undefined, {
  numeric: true,
  sensitivity: "base",
});

function naturalSort(arr) {
  return arr.sort(collator.compare);
}

/**
 * Find all images in a directory recursively
 */
function findImages(dir, fileList = []) {
  const files = fs.readdirSync(dir);
  for (const file of files) {
    const filePath = path.join(dir, file);
    const stat = fs.statSync(filePath);
    if (stat.isDirectory()) {
      findImages(filePath, fileList);
    } else {
      const ext = path.extname(file).toLowerCase();
      if (IMAGE_EXTENSIONS.includes(ext)) {
        fileList.push(filePath);
      }
    }
  }
  return fileList;
}

/**
 * Convert CBR/CBZ to XTC/XTCH
 */
async function convertComic(comicPath, outputPath, settings, useRotation, progressCallback) {
  let { width, height, output, rotation, reverseLines } = settings;
  
  // For comics, we usually want 480x800 (portrait) unless rotation is explicitly requested
  if (!useRotation) {
    rotation = 0;
    reverseLines = false;
    if (width > height) {
      [width, height] = [height, width];
    }
  }

  const isHQ = output.format === "xtch";
  const bits = isHQ ? 2 : 1;

  const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "comic-to-xtc-"));

  try {
    console.log(`Extracting ${path.basename(comicPath)}...`);
    execSync(`bsdtar -xf "${comicPath}" -C "${tmpDir}"`);

    let images = findImages(tmpDir);
    images = naturalSort(images);

    if (images.length === 0) {
      throw new Error("No images found in comic file");
    }

    console.log(`Found ${images.length} images. Processing (${width}x${height})...`);

    const pages = [];
    for (let i = 0; i < images.length; i++) {
      const imagePath = images[i];
      
      let sharpImg = sharp(imagePath)
        .resize(width, height, {
          fit: "contain",
          background: { r: 255, g: 255, b: 255, alpha: 1 }
        })
        .ensureAlpha()
        .toColorspace("srgb")
        .raw();

      const { data } = await sharpImg.toBuffer({ resolveWithObject: true });
      let imageData = new Uint8ClampedArray(data);

      if (output.dithering) {
        imageData = applyDithering(imageData, width, height, bits, output.ditherStrength);
      }

      if (output.negative) {
        applyNegative(imageData);
      }

      let finalData = imageData;
      let finalWidth = width;
      let finalHeight = height;

      if (reverseLines) {
        finalData = new Uint8ClampedArray(width * height * 4);
        for (let y = 0; y < height; y++) {
          const srcRowStart = y * width * 4;
          const dstRowStart = (height - 1 - y) * width * 4;
          finalData.set(imageData.subarray(srcRowStart, srcRowStart + width * 4), dstRowStart);
        }
      } else if (rotation === 90 || rotation === 270) {
        finalWidth = height;
        finalHeight = width;
        finalData = new Uint8ClampedArray(finalWidth * finalHeight * 4);
        for (let y = 0; y < height; y++) {
          for (let x = 0; x < width; x++) {
            let dx, dy;
            if (rotation === 90) { dx = height - 1 - y; dy = x; }
            else { dx = y; dy = width - 1 - x; }
            const srcIdx = (y * width + x) * 4;
            const dstIdx = (dy * finalWidth + dx) * 4;
            finalData[dstIdx] = imageData[srcIdx];
            finalData[dstIdx+1] = imageData[srcIdx+1];
            finalData[dstIdx+2] = imageData[srcIdx+2];
            finalData[dstIdx+3] = imageData[srcIdx+3];
          }
        }
      }

      const encoded = isHQ
        ? encodeXTH(finalData, finalWidth, finalHeight)
        : encodeXTG(finalData, finalWidth, finalHeight);
      
      pages.push({ data: encoded, width: finalWidth, height: finalHeight });

      if (progressCallback) progressCallback(i + 1, images.length);
    }

    const container = buildXTCContainer(
      pages,
      { title: path.basename(comicPath, path.extname(comicPath)), author: "Comic Book" },
      [],
      rotation === 90 || rotation === 270 ? height : width,
      rotation === 90 || rotation === 270 ? width : height,
      isHQ
    );

    fs.writeFileSync(outputPath, container);
    return { outputPath, pageCount: images.length, format: output.format };

  } finally {
    fs.rmSync(tmpDir, { recursive: true, force: true });
  }
}

program
  .name("cbr-to-xtc")
  .description("Convert CBR/CBZ comic files to XTC/XTCH format for Xteink e-readers")
  .version("1.0.0");

program
  .argument("<input>", "Input CBR/CBZ file or directory containing comic files")
  .option("-o, --output <path>", "Output file or directory")
  .option("-c, --config <path>", "Path to settings JSON file")
  .option("-f, --format <format>", "Output format: xtc (1-bit) or xtch (2-bit)")
  .option("--rotate", "Enable rotation from config file (default is OFF for comics)")
  .action(async (input, options) => {
    try {
      let settings = loadSettings(options.config);
      if (!settings.font.path) settings.font.path = "dummy.ttf";
      settings = resolveSettings(settings);

      if (options.format) settings.output.format = options.format;

      const inputPath = path.resolve(input);
      if (!fs.existsSync(inputPath)) {
        console.error(`Input not found: ${inputPath}`);
        process.exit(1);
      }

      const stat = fs.statSync(inputPath);
      const files = [];

      if (stat.isDirectory()) {
        const dirFiles = fs.readdirSync(inputPath);
        for (const f of dirFiles) {
          const ext = path.extname(f).toLowerCase();
          if (COMIC_EXTENSIONS.includes(ext)) files.push(path.join(inputPath, f));
        }
      } else {
        const ext = path.extname(inputPath).toLowerCase();
        if (COMIC_EXTENSIONS.includes(ext)) files.push(inputPath);
        else {
          console.error("Input must be a CBR/CBZ file");
          process.exit(1);
        }
      }

      if (files.length === 0) {
        console.error("No comic files found");
        process.exit(1);
      }

      let outDir = options.output ? path.resolve(options.output) : process.cwd();
      if (files.length > 1 && !fs.existsSync(outDir)) fs.mkdirSync(outDir, { recursive: true });

      for (const file of files) {
        const basename = path.basename(file, path.extname(file));
        const ext = settings.output.format === "xtch" ? ".xtch" : ".xtc";
        const outFile = stat.isDirectory() || (options.output && fs.existsSync(options.output) && fs.statSync(options.output).isDirectory())
          ? path.join(outDir, basename + ext)
          : (options.output || path.join(process.cwd(), basename + ext));

        console.log(`Converting ${path.basename(file)} -> ${path.basename(outFile)}...`);
        const result = await convertComic(file, outFile, settings, !!options.rotate, (current, total) => {
          process.stdout.write(`\rProgress: ${current}/${total} pages`);
          if (current === total) process.stdout.write("\n");
        });
        console.log(`Successfully converted ${result.pageCount} pages.`);
      }
    } catch (err) {
      console.error(`\nError: ${err.message}`);
      process.exit(1);
    }
  });

program.parse(process.argv);
