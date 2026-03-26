#!/bin/bash

# Xteink X4 快速轉檔腳本
# 使用方法: ./convert_x4.sh <input_epub_path>

if [ -z "$1" ]; then
    echo "使用方法: ./convert_x4.sh <epub路徑>"
    exit 1
fi

INPUT_FILE="$1"
OUTPUT_FILE="${INPUT_FILE%.*}.xtc"
TEMP_EPUB="${INPUT_FILE%.*}_optimized.epub"

# 使用 settings.json 作為預設設定，若有第二個參數則使用該參數
CONFIG="${2:-settings.json}"

echo "使用設定檔: $CONFIG"

echo "Step 1: 正在優化 EPUB (移除字體大小等干擾)..."
node index.js optimize "$INPUT_FILE" -o "$TEMP_EPUB" -c "$CONFIG"

if [ $? -ne 0 ]; then
    echo "優化失敗，請檢查錯誤訊息。"
    exit 1
fi

echo "Step 2: 正在轉換為 XTC: $INPUT_FILE -> $OUTPUT_FILE"
node index.js convert "$TEMP_EPUB" -o "$OUTPUT_FILE" -c "$CONFIG"

if [ $? -eq 0 ]; then
    echo "轉換成功！"
    # 移除臨時優化檔
    rm "$TEMP_EPUB"

    echo "Step 3: 檢查是否需要智慧拆分 (目標 8000 頁)..."
    node split_xtc.js "$OUTPUT_FILE" 8000
    
    if [ -f "${OUTPUT_FILE}.bak" ]; then
        echo "偵測到大檔案，已完成智慧拆分。"
        # 這裡可以決定是否要移除 .bak，為了安全建議先留著，或由使用者決定
    fi
else
    echo "轉換失敗，請檢查錯誤訊息。"
    rm "$TEMP_EPUB"
fi
