# Crosspoint Reader - Flow

**Crosspoint Reader - Flow** is a high-performance, plugin-driven firmware for the **Xteink X4** e-paper display reader. This project is a heavily modified fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), specifically optimized for the X4 hardware with expanded capabilities in reading, gaming, and extensibility.

Built using **PlatformIO** and targeting the **ESP32-C3** microcontroller.

---

## 🚀 Key Innovations & Features

### 1. iPod-Inspired "Flow Theme"

Experience a premium, classic interface inspired by the iPod. The **Flow Theme** features smooth animations and a refined layout designed for the X4's e-ink screen.

### 2. Full CJK Support & Proper Rendering

Unlock high-quality reading for Traditional Chinese and other CJK languages. The system correctly displays **Chinese filenames** and ensures **Chinese EPUB rendering** when external fonts are provided.

### 3. Advanced Recent Page

Stay organized with a beautiful "Recent" view. Browse up to **18 of your most recently read books** (optimized for memory) with full cover art support for quick access.

### 4. Heavyweight XTC Support

Read massive volumes without compromise. Our optimized **XTC binary format** supports files over **200MB** and **2000+ pages**, ensuring stability on constrained hardware.

### 5. Contextual Menu & Dark Mode

Access tools without leaving the page. Both XTC and EPUB formats support a **floating inner-page menu** and a dedicated **Dark Mode** for comfortable night reading.

### 6. Dynamic Lua Plugin System

XTEINK X4 is a platform, not just a reader. The integrated **Lua scripting engine** allows for dynamic plugins that can extend core logic and create entirely new interfaces.

### 7. MiniGo (Lua Plugin)

A full-featured **9x9 Go game** powered by a professional **MCTS (Monte Carlo Tree Search)** engine. Challenge the AI directly on your reader.

### 8. Qubic (Lua Plugin)

Enjoy the classic **3D Tic-Tac-Toe** logic game, reimagined for the e-ink experience.

### 9. Flashcard (Lua Plugin)

Turn your reading materials into learning opportunities with an integrated **SRS (Spaced Repetition System)** Flashcard application.

### 10. System Intelligence

- **Reading Time Tracking**: The system automatically logs and calculates your reading duration for every book.
- **Smart Maintenance**: Automatic handling of reading records, metadata, and cache files to keep the system lean and responsive.

### 11. Core Performance

- **Memory Breakthrough**: 50% reduction in page table memory usage.
- **Instant Start**: Optimized refresh logic for near-instant book opening.
- **Snappy Response**: Reduced input cooldown (200ms) for a more responsive feel.

---

## 🖼️ Visual Showcase

### System Interface

|             Flow Theme (7 Books)              |          Recent Browser (18 Books)           |
| :-------------------------------------------: | :------------------------------------------: |
| ![Flow Theme](./screenshots/01-flowtheme.png) | ![Recent Books](./screenshots/02-recent.png) |

### Reading Experience

|            High-Capacity XTC             |               Floating Menu                |                  Dark Mode                  |
| :--------------------------------------: | :----------------------------------------: | :-----------------------------------------: |
| ![XTC Reading](./screenshots/03-xtc.png) | ![XTC Menu](./screenshots/04-xtc_menu.png) | ![Dark Mode](./screenshots/05-darkmode.png) |

### Gaming & Apps (Lua Plugins)

|            MiniGo (MCTS AI)            |        Qubic (3D Tic-Tac-Toe)        |               Flashcard (SRS)                |
| :------------------------------------: | :----------------------------------: | :------------------------------------------: |
| ![MiniGo](./screenshots/06-minigo.png) | ![Qubic](./screenshots/06-qubic.png) | ![Flashcard](./screenshots/06-flashcard.png) |

---

## 📂 SD Card Setup

To use the full features of the XTEINK X4 Flow, you must manually set up specific directories and files on your SD card. These files are not included in the firmware due to size constraints.

### 1. CJK Font Support

The system supports external font files for high-quality CJK (Chinese, Japanese, Korean) rendering:

- **Requirement**: You must download the font `.bin` files (e.g., **Taipei Sans TC / 台北黑體**) and place them into the `/fonts/` directory on your SD card.
- **Reference**: Samples can be found in the `sdcard/fonts/` directory of this repository. Copy your preferred size (e.g., `TaipeiSansTC_30_30x31.bin`) to the SD card.

### 2. Lua Plugins

The integrated gaming and learning tools are powered by external scripts:

- **Requirement**: Download the plugin folders from this repository and manually place them into the `/plugins/` directory on your SD card.
- **Path**: Copy all subfolders from `sdcard/plugins/` to your SD card's root `/plugins/` folder.

### 3. Book Organization

For best compatibility and performance:

- **Recommended Path**: Place your eBook files (EPUB, XTC, TXT, MD) in a `/books/` subdirectory on the SD card.
- **Categorization**: You are encouraged to create subdirectories within `/books/` to organize your library (e.g., `/books/Fiction/`, `/books/Non-Fiction/`). The system will navigate these folders automatically.

---

## 🛠 Workflow: EPUB to XTC

To get the most out of the XTEINK X4, we recommend converting your EPUBs to XTC.
See the [EPUB to XTC Conversion Guide](../README.md#epub-轉檔-xtc-指南) in the root directory for the SOP.

---

## 🛠️ Companion Tools

This project includes specialized tools to optimize content for the XTEINK X4:

### 1. CJK Font Converter
Located in `tools/crosspoint-reader-lua/CJK-font-converter`.
- **Function**: Converts TTF/OTF fonts into high-performance 1-bit or 2-bit `.bin` formats.
- **Support**: Specifically optimized for vertical reading and different anti-aliasing levels.

### 2. EPUB to XTC Converter
Located in `tools/crosspoint-reader-lua/epub-to-xtc-converter`.
- **Function**: A full-featured CLI tool for converting standard EPUB files into the device-friendly XTC format.
- **Features**: Supports image dithering, layout optimization, and multi-language settings (Vertical, Horizontal, etc.).

---

## 💾 Installation & Development

### Web Flash

Visit [xteink.dve.al](https://xteink.dve.al/) for one-click setup and updates.

### Manual Build

```sh
git clone --recursive https://github.com/lee/xteink-x4-reader
pio run --target upload
```

---

## ⚖️ Disclaimer & Acknowledgments

This project is **not affiliated with Xteink** or the original CrossPoint authors. It is a community-driven enhancement.

Huge thanks to:

- The original **CrossPoint Reader** team.
- **atomic14** for the [diy-esp32-epub-reader](https://github.com/atomic14/diy-esp32-epub-reader).

---

## 🕒 Update History

- **2026-03-27 (v2.5.0-Lee)**:
  - 更新系統版本號為 `v2.5.0-Lee`。
  - **EpubReader 浮動選單優化**：
    - 移除「螢幕截圖」功能。
    - 新增「閱讀字體」切換（Bookerly / Noto Sans）。
    - 新增「外部字體」選擇（支援跨目錄字體選取）。
    - 更改字體後自動觸發重新排版（Indexing）與緩存更新。
    - 優化選單佈局，調整高度以適應新項目。
  - **UI/UX 增強**：
    - 全球性地將所有清單頁面（如書庫、閱讀統計）的右側滾動條替換為更優雅的 **點狀分頁指示器**（8x8 px 方塊）。
    - 統一 XTC 與 EPUB 閱讀器的書籤樣式，將 EPUB 原本的黑塊升級為精緻的 **黑色緞帶圖示**。
    - 為 XTC 與 EPUB 章節選擇頁面新增標準按鈕導航提示，提升一致性。
  - **轉檔工具優化**：
    - 重構 `convert_x4.sh`，支援從任何工作目錄啟動，並具備智慧型路徑解析。
  - **專案瘦身**：
    - 移除冗餘的大型字體檔案與編譯輸出，大幅優化 GitHub 儲存庫體積。
- **2026-03-13**:
  - `Recents` page optimization: Reduced maximum books from 36 to 18 to save memory.
  - `Flow Theme` optimization: Reduced carousel book count from 10 to 7 to improve stability.
- **2026-03-12**:
  - Added Flashcard (SRS) data support.
  - Implemented `HomeActivity` loading cancellation for smoother navigation.
  - Enhanced resource cleanup on activity transition to prevent memory errors.
- **2026-03-11**:
  - Reduced power button wake duration from 600ms to 300ms for faster responsiveness.
  - Fixed `XTC` reading time display in `Flow Theme`.
  - Resolved `XTH` memory allocation issues causing auto-restarts.
- **2026-03-10**:
  - Fixed `XTC` memory error after WiFi sync by implementing 1-bit page streaming.
  - Optimized NTP time synchronization (force sync on connect, immediate refresh).
  - Implemented automatic book path repair using filename and file size.
- **2026-03-09**:
  - Added inline "Go-to" page scrubber for XTC, EPUB, and TXT readers.
  - Moved "Zzz" sleep indicator to top center to prevent overlap with the date.

---

_XTEINK X4: Unlock the true potential of your E-reader._
