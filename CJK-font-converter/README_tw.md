# CJK Font Converter for XTEINK X4

這是專為 XTEINK X4 (CrossPoint Reader) 開發的字型轉換工具，可將 TrueType (TTF) 或 OpenType (OTF) 字型轉換為系統支援的 `.bin` 點陣格式，以支援外掛字體。

## 準備工作

1. **安裝 Python 環境**：確保您的電腦已安裝 Python 3。
2. **安裝 Pillow 庫**：
   ```bash
   pip install Pillow
   ```
3. **放置字型**：將您想轉換的 `.ttf` 或 `.otf` 字型放入 `fonts/` 資料夾中。

## 製作字型 .bin 檔

使用 `convert_font.py` 腳本進行轉換。

### 基本指令
```bash
python convert_font.py --font fonts/您的字型檔.ttf --size 32
```

### 常用參數說明
- `--font`: 來源字型路徑（必填）。
- `--size`: 字體大小（必填，單位為 pt）。
- `--measure`: 僅測量建議的網格大小（Cell Size），不進行實際轉換。
- `--width` / `--height`: 手動指定網格寬高（若不指定，工具會根據字型內容自動偵測）。

### 範例：預覽建議網格大小
```bash
python convert_font.py --font fonts/TaipeiSansTC-Regular.ttf --size 32 --measure
```

### 範例：完整轉換
```bash
python convert_font.py --font fonts/TaipeiSansTC-Regular.ttf --size 32
```
轉換完成後，轉出的檔案會出現在 `output/` 資料夾中。

## 放置到 SD 卡

為了讓系統支援外掛字體，請遵循以下步驟：

1. **檢查檔名**：轉出的檔案名稱應符合格式 `字型名_大小_寬x高.bin`（例如：`TaipeiSansTC_32_32x33.bin`）。
2. **放置路徑**：將產生的 `.bin` 檔案複製到 SD 卡的 **/fonts/** 資料夾中。
   - 路徑：`SD卡/fonts/`
3. **使用字體**：在 Lua 插件編寫時，可以直接引用該字體名稱與大小。

## 格式細節 (技術參考)
- **範圍**：支援 Unicode U+0000 至 U+FFFF (BMP)。
- **格式**：1-bit per pixel (MSB-first), row-major order。
- **定位**：字體的基準線 (Baseline) 預設位於 Bitmap 的下方第 4 像素行。
- **檔案大小計算**：每個字元占用字節 = `ceil(寬/8) * 高`；總檔案大小 = `65536 * 每個字元字節`。
