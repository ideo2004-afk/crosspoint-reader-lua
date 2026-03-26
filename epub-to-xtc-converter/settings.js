/**
 * Settings management and defaults for CLI converter
 */

const fs = require("fs");
const path = require("path");

// Device presets
const DEVICES = {
  "xteink-x4": { width: 480, height: 800, name: "Xteink X4" },
  "xteink-x3": { width: 528, height: 792, name: "Xteink X3" },
  custom: { width: 480, height: 800, name: "Custom" },
};

// Text alignment values for CREngine
const TEXT_ALIGN = {
  left: 0,
  right: 1,
  center: 2,
  justify: 3,
};

// Default settings
const DEFAULT_SETTINGS = {
  device: "xteink-x4",
  width: 480,
  height: 800,
  font: {
    path: null, // Required: path to TTF/OTF font file
    size: 34,
    weight: 400,
  },
  margins: {
    left: 16,
    top: 16,
    right: 16,
    bottom: 16,
  },
  lineHeight: 120,
  paragraphSpacing: 0.5,
  textAlign: "justify",
  hyphenation: {
    enabled: true,
    language: "en",
  },
  portraitCover: false, // Handle cover as portrait (480x800) in vertical mode
  output: {
    format: "xtc", // 'xtc' (1-bit) or 'xtch' (2-bit)
    dithering: true,
    ditherStrength: 0.7,
    negative: false,
  },
  autoTrimCover: true, // Automatically detect and trim white borders from covers
  optimizer: {
    enabled: true,
    removeCss: true,
    stripFonts: true,
    grayscale: true,
    maxImageWidth: 800, // Increased from 480 to support landscape covers in vertical mode
    injectCss: true,
    recursive: false,
    include: "*.epub",
    exclude: null,
  },
};

/**
 * Load settings from JSON file
 */
function loadSettings(configPath) {
  if (!configPath || !fs.existsSync(configPath)) {
    return { ...DEFAULT_SETTINGS };
  }

  try {
    const content = fs.readFileSync(configPath, "utf8");
    const userSettings = JSON.parse(content);
    return mergeSettings(DEFAULT_SETTINGS, userSettings);
  } catch (err) {
    throw new Error(`Failed to load config file: ${err.message}`);
  }
}

/**
 * Deep merge settings objects
 */
function mergeSettings(defaults, user) {
  const result = { ...defaults };

  for (const key of Object.keys(user)) {
    if (
      user[key] !== null &&
      typeof user[key] === "object" &&
      !Array.isArray(user[key])
    ) {
      result[key] = mergeSettings(defaults[key] || {}, user[key]);
    } else {
      result[key] = user[key];
    }
  }

  return result;
}

/**
 * Resolve settings with device preset
 */
function resolveSettings(settings) {
  const resolved = { ...settings };

  // Apply device preset dimensions if not custom
  if (settings.device !== "custom" && DEVICES[settings.device]) {
    resolved.width = DEVICES[settings.device].width;
    resolved.height = DEVICES[settings.device].height;
  }

  // Convert text align string to CREngine value
  resolved.textAlignValue = TEXT_ALIGN[settings.textAlign] || 3;

  // Resolve font path to absolute
  if (resolved.font.path) {
    resolved.font.path = path.resolve(resolved.font.path);
  }

  return resolved;
}

/**
 * Validate settings
 */
function validateSettings(settings) {
  const errors = [];

  if (!settings.font.path) {
    errors.push("Font path is required. Set font.path in your config file.");
  } else if (!fs.existsSync(settings.font.path)) {
    errors.push(`Font file not found: ${settings.font.path}`);
  }

  if (settings.width <= 0 || settings.height <= 0) {
    errors.push("Width and height must be positive integers");
  }

  if (settings.font.size < 8 || settings.font.size > 100) {
    errors.push("Font size must be between 8 and 100");
  }

  if (
    settings.output.ditherStrength < 0 ||
    settings.output.ditherStrength > 1
  ) {
    errors.push("Dither strength must be between 0 and 1");
  }

  const validFormats = ["xtc", "xtch"];
  if (!validFormats.includes(settings.output.format)) {
    errors.push(
      `Invalid output format: ${settings.output.format}. Must be 'xtc' or 'xtch'`,
    );
  }

  validateOptimizerFields(settings, errors);

  return errors;
}

/**
 * Validate optimizer-specific settings (no font.path required)
 */
function validateOptimizerSettings(settings) {
  const errors = [];
  validateOptimizerFields(settings, errors);
  return errors;
}

function validateOptimizerFields(settings, errors) {
  if (settings.optimizer) {
    if (
      settings.optimizer.maxImageWidth !== undefined &&
      (settings.optimizer.maxImageWidth < 1 ||
        settings.optimizer.maxImageWidth > 2048)
    ) {
      errors.push("optimizer.maxImageWidth must be between 1 and 2048");
    }
  }
}

/**
 * Generate default config file content
 */
function generateDefaultConfig() {
  return JSON.stringify(DEFAULT_SETTINGS, null, 2);
}

module.exports = {
  DEVICES,
  TEXT_ALIGN,
  DEFAULT_SETTINGS,
  loadSettings,
  resolveSettings,
  validateSettings,
  validateOptimizerSettings,
  generateDefaultConfig,
};
