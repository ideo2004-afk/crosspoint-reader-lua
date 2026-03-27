#!/bin/bash

# Xteink X4 快速轉檔腳本
# 使用方法: ./convert_x4.sh <input_epub_path>

# 取得腳本所在的絕對路徑
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [ -z "$1" ]; then
    echo "使用方法: ./convert_x4.sh <epub路徑> [設定檔.json]"
    echo "例如: ./convert_x4.sh mybook.epub settings_明體直書.json"
    exit 1
fi

INPUT_FILE="$(realpath "$1")"
OUTPUT_FILE="${INPUT_FILE%.*}.xtc"
TEMP_EPUB="${INPUT_FILE%.*}_optimized.epub"

# 設定檔邏輯：
# 1. 如果有第二個參數，優先使用
# 2. 如果沒參數，先找目前的目錄下的 settings.json
# 3. 再找腳本目錄下的 settings.json
if [ -n "$2" ]; then
    CONFIG="$2"
elif [ -f "settings.json" ]; then
    CONFIG="settings.json"
else
    CONFIG="$SCRIPT_DIR/settings.json"
fi

# 檢查設定檔是否存在
if [ ! -f "$CONFIG" ]; then
    echo "錯誤: 找不到設定檔 $CONFIG"
    echo "提示: 請確保目錄中有 settings.json 或在參數中指定設定檔。"
    exit 1
fi

echo "使用設定檔: $CONFIG"

echo "Step 1: 正在優化 EPUB (移除字體大小等干擾)..."
node "$SCRIPT_DIR/index.js" optimize "$INPUT_FILE" -o "$TEMP_EPUB" -c "$CONFIG"

if [ $? -ne 0 ]; then
    echo "優化失敗，請檢查錯誤訊息。"
    exit 1
fi

echo "Step 2: 正在轉換為 XTC: $INPUT_FILE -> $OUTPUT_FILE"
node "$SCRIPT_DIR/index.js" convert "$TEMP_EPUB" -o "$OUTPUT_FILE" -c "$CONFIG"

if [ $? -eq 0 ]; then
    echo "轉換成功！"
    # 移除臨時優化檔
    rm "$TEMP_EPUB"

    echo "Step 3: 檢查是否需要智慧拆分 (目標 8000 頁)..."
    node "$SCRIPT_DIR/split_xtc.js" "$OUTPUT_FILE" 8000
    
    if [ -f "${OUTPUT_FILE}.bak" ]; then
        echo "偵測到大檔案，已完成智慧拆分。"
    fi
else
    echo "轉換失敗，請檢查錯誤訊息。"
    rm "$TEMP_EPUB"
fi
