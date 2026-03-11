# XTEINK X4 Lua 插件開發指南 (v1.0)

本指南說明如何在 XTEINK X4 上開發與部署動態 Lua 插件。

## 1. 插件存放規範
所有插件必須存放在 SD 卡的 `/plugins/` 目錄下，每個插件擁有獨立的子目錄。
- **正確路徑**: `/plugins/[PluginName]/main.lua`
- **系統識別**: 只有包含 `main.lua` 的資料夾才會顯示在 X4 的 "Plugins" 選單中。

## 2. 腳本生命週期 (Lifecycle)
每個 `main.lua` 必須定義以下核心函式，由系統自動呼叫：

```lua
-- 初始化：在進入插件時執行一次
function init()
    log("Plugin started")
end

-- 渲染：由渲染執行緒定期呼叫 (目前約 2 秒一次或按鍵觸發)
function draw()
    gui.clear()
    gui.drawText(12, 50, 50, "Hello World")
end
```

## 3. 可用 API 列表 (C++ Bridge)

### 3.1 基礎系統
- `log(string)`: 將訊息輸出到 Serial Monitor。
- `gui.width()`: 獲取螢幕寬度 (通常為 480)。
- `gui.height()`: 獲取螢幕高度 (通常為 800)。

### 3.2 圖形繪製 (GUI)
- `gui.clear()`: 清除目前的 FrameBuffer。
- `gui.drawRect(x, y, w, h)`: 繪製空心矩形。
- `gui.fillRect(x, y, w, h)`: 繪製實心矩形。
- `gui.drawText(fontId, x, y, text)`: 繪製文字。
  - **常用 fontId**: `12` (標準 UI 字體), `10` (小字體)。

### 3.3 重新整理模式 (Refresh)
*注意：目前建議由系統自動處理 `displayBuffer()`，若需手動控制可使用：*
- `gui.refresh(mode)`: 
  - `REFRESH_FULL` (0): 全場清理 (較慢)。
  - `REFRESH_HALF` (1): 半場清理。
  - `REFRESH_FAST` (2): 局部快刷 (預設)。
- `gui.setOrientation(mode)`: 設定螢幕旋轉 (`landscape_cw`, `landscape_ccw`, `portrait`, `portrait_inv`)。
- `gui.drawBmp(path, x, y, maxW, maxH)`: 從 SD 卡繪製 BMP 圖片。
- `gui.drawButtonHints(back, confirm, prev, next)`: 在底部繪製按鈕提示。

### 3.4 網路功能 (net)
- `net.wifiConnect()`: 使用儲存的認證啟動非同步 WiFi 連接。
- `net.wifiStatus()`: 獲取目前狀態 (`idle`, `connecting`, `connected`, `failed`)。
- `net.wifiDisconnect()`: 斷開 WiFi 連接。
- `net.get(url [, headers_table])`: 執行 HTTP GET 請求。
- `net.urlencode(string)`: URL 編碼字串。

### 3.5 系統與檔案系統
- `sys.millis()`: 獲取系統執行時間 (毫秒)。
- `sys.delay(ms)`: 暫停執行。
- `sys.exit()`: 安全退出插件。
- `fs.listDirs(path)` / `fs.listFiles(path)`: 列出目錄內容。
- `fs.readFile(path)` / `fs.writeFile(path, content)`: 檔案讀寫。

## 4. 開發最佳實踐
1. **記憶體管理**: ESP32-C3 的 RAM 有限 (約 140KB 可用)，請避免在 Lua 中建立巨大的 Table 或載入過大的資源。
2. **防誤觸**: 系統已內建 `skipNextButtonCheck`，進入插件時無需擔心長按按鍵帶來的干擾。
3. **無縫退出**: 隨時按下 **BACK** 鍵即可安全退出插件並回收 Lua 記憶體。

## 5. 範例：極簡插件
```lua
-- /plugins/Clock/main.lua
function init()
    log("Clock App Ready")
end

function draw()
    gui.clear()
    gui.drawRect(10, 10, 460, 780)
    gui.drawText(12, 100, 400, "X4 DYNAMIC PLATFORM")
end
```
