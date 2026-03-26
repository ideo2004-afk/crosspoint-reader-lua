#!/usr/bin/env node

/**
 * EPUB to XTC/XTCH CLI Converter
 * Converts EPUB files to Xteink e-reader format
 */

const { program } = require("commander");
const fs = require("fs");
const path = require("path");
const { minimatch } = require("minimatch");
const {
  loadSettings,
  resolveSettings,
  validateSettings,
  validateOptimizerSettings,
  generateDefaultConfig,
} = require("./settings");
const { convertEpub, getOutputPath, cleanup } = require("./converter");
const { optimizeEpub } = require("./optimizer");

program
  .name("epub-to-xtc")
  .description("Convert EPUB files to XTC/XTCH format for Xteink e-readers")
  .version("1.0.0");

program
  .command("convert <input>")
  .description("Convert EPUB file(s) to XTC/XTCH format")
  .option("-o, --output <path>", "Output file or directory")
  .option("-c, --config <path>", "Path to settings JSON file")
  .option("-f, --format <format>", "Output format: xtc (1-bit) or xtch (2-bit)")
  .action(async (input, options) => {
    try {
      // Load and resolve settings
      let settings = loadSettings(options.config);
      settings = resolveSettings(settings);

      // Override format if specified
      if (options.format) {
        settings.output.format = options.format;
      }

      // Validate settings
      const errors = validateSettings(settings);
      if (errors.length > 0) {
        console.error("Configuration errors:");
        errors.forEach((e) => console.error(`  - ${e}`));
        process.exit(1);
      }

      // Resolve input path
      const inputPath = path.resolve(input);

      if (!fs.existsSync(inputPath)) {
        console.error(`Input not found: ${inputPath}`);
        process.exit(1);
      }

      const stat = fs.statSync(inputPath);

      if (stat.isDirectory()) {
        // Convert all EPUBs in directory - use process isolation for stability
        await convertDirectory(inputPath, options.output, settings, options);
      } else if (stat.isFile() && inputPath.endsWith(".epub")) {
        // Convert single file
        await convertSingleFile(inputPath, options.output, settings);
      } else {
        console.error(
          "Input must be an EPUB file or directory containing EPUB files",
        );
        process.exit(1);
      }
    } catch (err) {
      console.error(`Error: \n${err.stack}`);
      process.exit(1);
    } finally {
      cleanup();
    }
  });

program
  .command("init")
  .description("Generate default settings.json file")
  .option("-o, --output <path>", "Output path", "settings.json")
  .action((options) => {
    const outputPath = path.resolve(options.output);

    if (fs.existsSync(outputPath)) {
      console.error(`File already exists: ${outputPath}`);
      process.exit(1);
    }

    fs.writeFileSync(outputPath, generateDefaultConfig());
    console.log(`Created default settings file: ${outputPath}`);
    console.log(
      "\nImportant: Edit the file to set font.path to your TTF/OTF font file.",
    );
  });

program
  .command("optimize <input>")
  .description("Optimize EPUB file(s) for e-paper devices")
  .option("-o, --output <path>", "Output file or directory")
  .option("-c, --config <path>", "Path to settings JSON file")
  .action(async (input, options) => {
    try {
      const settings = loadSettings(options.config);
      const opts = settings.optimizer || {};

      // Pass rotation setting to optimizer if present
      if (settings.rotation) {
        opts.rotation = settings.rotation;
        if (settings.rotation === 90) {
          opts.maxImageWidth = Math.max(
            opts.maxImageWidth || 0,
            settings.width || 800,
          );
        }
      }

      const errors = validateOptimizerSettings(settings);
      if (errors.length > 0) {
        console.error("Configuration errors:");
        errors.forEach((e) => console.error(`  - ${e}`));
        process.exit(1);
      }

      const inputPath = path.resolve(input);

      if (!fs.existsSync(inputPath)) {
        console.error(`Input not found: ${inputPath}`);
        process.exit(1);
      }

      const stat = fs.statSync(inputPath);

      if (stat.isDirectory()) {
        await optimizeDirectory(inputPath, options.output, opts);
      } else if (stat.isFile() && inputPath.endsWith(".epub")) {
        await optimizeSingleFile(inputPath, options.output, opts);
      } else {
        console.error(
          "Input must be an EPUB file or directory containing EPUB files",
        );
        process.exit(1);
      }
    } catch (err) {
      console.error(`Error: ${err.message}`);
      process.exit(1);
    }
  });

function formatSize(bytes) {
  return (bytes / 1024).toFixed(1) + " KB";
}

async function optimizeSingleFile(inputPath, outputPath, opts) {
  if (!outputPath) {
    const dir = path.dirname(inputPath);
    const ext = path.extname(inputPath);
    const base = path.basename(inputPath, ext);
    outputPath = path.join(dir, `${base}_optimized${ext}`);
  } else if (
    fs.existsSync(outputPath) &&
    fs.statSync(outputPath).isDirectory()
  ) {
    outputPath = path.join(outputPath, path.basename(inputPath));
  } else {
    outputPath = path.resolve(outputPath);
  }

  const result = await optimizeEpub(inputPath, outputPath, opts);

  console.log(
    `Optimized: ${path.basename(inputPath)} -> ${path.basename(result.outputPath)} (Size: ${formatSize(result.originalSize)} -> ${formatSize(result.optimizedSize)} (${result.reductionPercent}% reduction))`,
  );
}

/**
 * Collect EPUB files from a directory, optionally recursive
 */
function collectEpubFiles(dir, opts, basedir) {
  basedir = basedir || dir;
  let results = [];
  const entries = fs.readdirSync(dir, { withFileTypes: true });
  const include = opts.include || "*.epub";
  const exclude = opts.exclude || null;

  for (const entry of entries) {
    const fullPath = path.join(dir, entry.name);
    const relPath = path.relative(basedir, fullPath);

    if (entry.isDirectory() && opts.recursive) {
      results = results.concat(collectEpubFiles(fullPath, opts, basedir));
    } else if (entry.isFile()) {
      if (!minimatch(entry.name, include)) continue;
      if (exclude && minimatch(entry.name, exclude)) continue;
      results.push({ absolute: fullPath, relative: relPath });
    }
  }

  return results;
}

async function optimizeDirectory(inputDir, outputDir, opts) {
  const files = collectEpubFiles(inputDir, opts, inputDir);

  if (files.length === 0) {
    console.error("No EPUB files found in directory");
    process.exit(1);
  }

  const inPlace = !outputDir;
  if (!outputDir) {
    outputDir = inputDir;
  } else {
    outputDir = path.resolve(outputDir);
    if (!fs.existsSync(outputDir)) {
      fs.mkdirSync(outputDir, { recursive: true });
    }
  }

  console.log(`Optimizing ${files.length} EPUB file(s)...\n`);

  let successCount = 0;
  let failCount = 0;

  for (let i = 0; i < files.length; i++) {
    const file = files[i];
    // Preserve relative directory structure in output
    let outputPath;
    if (inPlace) {
      // Add _optimized suffix to avoid overwriting originals
      const ext = path.extname(file.relative);
      const base = file.relative.slice(0, -ext.length);
      outputPath = path.join(outputDir, `${base}_optimized${ext}`);
    } else {
      outputPath = path.join(outputDir, file.relative);
    }

    console.log(`[${i + 1}/${files.length}] ${file.relative}`);

    try {
      const result = await optimizeEpub(file.absolute, outputPath, opts);

      console.log(`  Output: ${path.basename(result.outputPath)}`);
      console.log(
        `  Size: ${formatSize(result.originalSize)} -> ${formatSize(result.optimizedSize)} (${result.reductionPercent}% reduction)\n`,
      );
      successCount++;
    } catch (err) {
      console.log(`  Error: ${err.message}\n`);
      failCount++;
    }
  }

  console.log(
    `\nOptimization complete: ${successCount} succeeded, ${failCount} failed`,
  );
}

async function convertSingleFile(inputPath, outputPath, settings) {
  let finalInput = inputPath;
  let tempPath = null;

  // 1. Determine output path
  if (!outputPath) {
    const dir = path.dirname(inputPath);
    outputPath = getOutputPath(inputPath, dir, settings.output.format);
  } else if (
    fs.existsSync(outputPath) &&
    fs.statSync(outputPath).isDirectory()
  ) {
    outputPath = getOutputPath(inputPath, outputPath, settings.output.format);
  } else {
    outputPath = path.resolve(outputPath);
  }

  const filename = path.basename(inputPath);
  console.log(`Converting: ${filename}`);

  try {
    // 2. Optimization step
    if (settings.optimizer && settings.optimizer.enabled) {
      process.stdout.write(`  Optimizing... `);
      tempPath = path.join(
        path.dirname(inputPath),
        "_optimized_" + path.basename(inputPath),
      );
      const stats = await optimizeEpub(inputPath, tempPath, settings.optimizer);
      finalInput = tempPath;
      process.stdout.write(`${stats.reductionPercent}% reduced\n`);
    }

    // 3. Conversion step
    const result = await convertEpub(
      finalInput,
      outputPath,
      settings,
      (current, total) => {
        const percent = Math.round((current / total) * 100);
        process.stdout.write(
          `\r  Progress: ${current}/${total} pages (${percent}%)`,
        );
      },
    );

    console.log(`\n  Output: ${result.outputPath}`);
    console.log(`  Pages: ${result.pageCount}`);
    console.log(`  Format: ${result.format.toUpperCase()}`);
  } finally {
    // 4. Cleanup temporary optimized file
    if (tempPath && fs.existsSync(tempPath)) {
      try {
        fs.unlinkSync(tempPath);
      } catch (e) {
        /* ignore */
      }
    }
    // 5. Cleanup renderer
    cleanup();
  }
}

async function convertDirectory(inputDir, outputDir, settings, options) {
  const files = fs
    .readdirSync(inputDir)
    .filter((f) => f.endsWith(".epub"))
    .map((f) => path.join(inputDir, f));

  if (files.length === 0) {
    console.error("No EPUB files found in directory");
    process.exit(1);
  }

  if (!outputDir) outputDir = inputDir;
  else {
    outputDir = path.resolve(outputDir);
    if (!fs.existsSync(outputDir)) fs.mkdirSync(outputDir, { recursive: true });
  }

  console.log(`Converting ${files.length} EPUB file(s)...\n`);

  const { fork } = require("child_process");
  let successCount = 0; let failCount = 0;

  for (let i = 0; i < files.length; i++) {
    const inputPath = files[i];
    const outputPath = getOutputPath(inputPath, outputDir, settings.output.format);

    console.log(`[${i + 1}/${files.length}] ${path.basename(inputPath)}`);

    // Force each book to run in its own process for 100% stability
    const success = await new Promise((resolve) => {
      const args = ["convert", inputPath, "-c", options.config || "settings.json", "-o", outputPath];
      const child = fork(__filename, args, { stdio: "inherit" });
      child.on("exit", (code) => resolve(code === 0));
    });

    if (success) successCount++; else failCount++;
    console.log(""); // Spacing
  }
  console.log(`\nConversion complete: ${successCount} succeeded, ${failCount} failed`);
}

program.parse();
