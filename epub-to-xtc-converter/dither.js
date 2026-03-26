/**
 * Floyd-Steinberg dithering implementation
 * Ported from web/dither-worker.js
 */

/**
 * Quantize value to specified bit depth
 * @param {number} value - Grayscale value (0-255)
 * @param {number} bits - 1 for monochrome, 2 for 4-level grayscale
 * @returns {number} Quantized value
 */
function quantize(value, bits) {
    if (bits === 1) {
        // 1-bit: black or white
        return value < 128 ? 0 : 255;
    } else {
        // 2-bit: 4 levels for XTH format
        // Uses Xteink-specific thresholds
        if (value > 212) return 255;      // White
        if (value > 127) return 170;      // Light Gray
        if (value > 42) return 85;        // Dark Gray
        return 0;                         // Black
    }
}

/**
 * Apply Floyd-Steinberg dithering to RGBA buffer
 * @param {Uint8ClampedArray} data - RGBA pixel data
 * @param {number} width - Image width
 * @param {number} height - Image height
 * @param {number} bits - 1 for monochrome, 2 for 4-level grayscale
 * @param {number} strength - Dithering strength (0-1)
 * @returns {Uint8ClampedArray} Dithered RGBA data
 */
function applyDithering(data, width, height, bits, strength) {
    // Create grayscale buffer
    const gray = new Float32Array(width * height);

    // Convert to grayscale using ITU-R BT.601 coefficients
    for (let i = 0; i < width * height; i++) {
        const idx = i * 4;
        gray[i] = 0.299 * data[idx] + 0.587 * data[idx + 1] + 0.114 * data[idx + 2];
    }

    // Floyd-Steinberg dithering
    // Error distribution pattern:
    //     X   7/16
    // 3/16 5/16 1/16
    for (let y = 0; y < height; y++) {
        for (let x = 0; x < width; x++) {
            const idx = y * width + x;
            const oldPixel = gray[idx];
            const newPixel = quantize(oldPixel, bits);
            gray[idx] = newPixel;

            const error = (oldPixel - newPixel) * strength;

            // Distribute error to neighboring pixels
            if (x + 1 < width) {
                gray[idx + 1] += error * 7 / 16;
            }
            if (y + 1 < height) {
                if (x > 0) {
                    gray[idx + width - 1] += error * 3 / 16;
                }
                gray[idx + width] += error * 5 / 16;
                if (x + 1 < width) {
                    gray[idx + width + 1] += error * 1 / 16;
                }
            }
        }
    }

    // Write back to RGBA buffer
    for (let i = 0; i < width * height; i++) {
        const v = Math.max(0, Math.min(255, Math.round(gray[i])));
        const idx = i * 4;
        data[idx] = v;
        data[idx + 1] = v;
        data[idx + 2] = v;
        // Keep alpha unchanged (data[idx + 3])
    }

    return data;
}

/**
 * Apply negative (invert colors)
 * @param {Uint8ClampedArray} data - RGBA pixel data
 * @returns {Uint8ClampedArray} Inverted RGBA data
 */
function applyNegative(data) {
    for (let i = 0; i < data.length; i += 4) {
        data[i] = 255 - data[i];
        data[i + 1] = 255 - data[i + 1];
        data[i + 2] = 255 - data[i + 2];
        // Keep alpha unchanged
    }
    return data;
}

module.exports = {
    quantize,
    applyDithering,
    applyNegative
};
