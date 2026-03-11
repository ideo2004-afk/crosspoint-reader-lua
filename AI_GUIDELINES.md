# XTEINK X4 Flow - AI Development Guidelines

This document contains critical constraints and instructions for AI agents (like Antigravity, Cursor, or Windsurf) assisting with the development of the XTEINK X4 Flow project.

## 🔴 CRITICAL CONSTRAINTS (NEVER VIOLATE)

### 1. DO NOT MODIFY THE PARTITION TABLE
- **Files**: `partitions.csv`, `platformio.ini` (specifically `board_build.partitions`).
- **Reason**: Modifying the partition layout (start addresses or sizes) will cause existing devices to **Hard Brick** or enter a bootloop upon update.
- **Rule**: AI agents must **never** suggest or implement changes to these files unless explicitly and repeatedly instructed by the user for a major architectural refactor.

### 2. FLASH SETTINGS STABILITY
- **Files**: `platformio.ini` (specifically `board_build.flash_mode`, `board_build.flash_size`).
- **Reason**: Current settings (`dio`, `16MB`) are chosen for maximum compatibility with X4 hardware. Changing these may result in unburnable or unstable firmware.

## 🟡 PROJECT HABITS & PREFERENCES

### 1. Language & Fonts
- Maintain support for **Traditional Chinese (Taiwan standard)**.
- Primary fonts: `Taipei Sans TC` (台北黑體), `MingLan` (明蘭體).
- Ensure Lua plugins handle UTF-8 correctly for Chinese display.

### 2. Branding
- Use `XTEINK X4 Flow` as the primary project name.
- Avoid legacy "Crosspoint" branding in user-facing UI, except where acknowledging original authors.

### 3. Release Workflow
- For standard updates, only the `firmware.bin` (generated in `.pio/build/gh_release/`) is required for user distribution.
- Do not include `bootloader.bin` or `partitions.bin` in standard update instructions unless specifically asked.

### 4. Code Style
- Follow the existing C++ style (Arduino/C++20).
- For Lua plugins, prioritize memory efficiency due to the ESP32-C3's limited RAM (~140KB available).

---
*Note to AI: If you are unsure about an operation, ALWAYS check this file first.*
