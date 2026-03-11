# XTEINK X4 Lua Plugin Development Guide (v1.0)

This guide explains how to develop and deploy dynamic Lua plugins on XTEINK X4.

## 1. Plugin Directory Standards
All plugins must be stored in the `/plugins/` directory on the SD card, with each plugin in its own subdirectory.
- **Correct Path**: `/plugins/[PluginName]/main.lua`
- **System Recognition**: Only folders containing `main.lua` will be displayed in the X4 "Plugins" menu.

## 2. Script Lifecycle
Each `main.lua` must define the following core functions, which are called automatically by the system:

```lua
-- Initialization: Executed once when entering the plugin
function init()
    log("Plugin started")
end

-- Drawing: Called periodically by the rendering thread (currently approx. every 2 seconds or triggered by key press)
function draw()
    gui.clear()
    gui.drawText(12, 50, 50, "Hello World")
end
```

## 3. Available API List (C++ Bridge)

### 3.1 Basic System
- `log(string)`: Outputs a message to the Serial Monitor.
- `gui.width()`: Retrieves the screen width (typically 480).
- `gui.height()`: Retrieves the screen height (typically 800).

### 3.2 Graphical Drawing (GUI)
- `gui.clear()`: Clears the current FrameBuffer.
- `gui.drawRect(x, y, w, h)`: Draws an empty rectangle.
- `gui.fillRect(x, y, w, h)`: Draws a solid rectangle.
- `gui.drawPixel(x, y, [color])`: Draws a single pixel.
- `gui.drawCircle(x, y, r, [lineWidth], [color])`: Draws an empty circle.
- `gui.fillCircle(x, y, r, [color])`: Draws a solid circle.
- `gui.fillPolygon(xTable, yTable, [color])`: Fills a polygon defined by X and Y coordinate tables.
- `gui.drawText(fontId, x, y, text)`: Draws text.
  - **Common fontIds**: `12` (Standard UI font), `10` (Small font).
- `gui.drawLine(x1, y1, x2, y2, [lineWidth], [color])`: Draws a line with optional thickness.

### 3.3 Color Constants
- `COLOR_BLACK`, `COLOR_WHITE`, `COLOR_DARK_GRAY`, `COLOR_LIGHT_GRAY`, `COLOR_CLEAR`.
- *Note: In 1-bit mode, Grays are rendered using dithering.*

### 3.4 Refresh Modes
*Note: It is currently recommended to let the system handle `displayBuffer()` automatically. If manual control is needed:*
- `gui.refresh(mode)`: 
  - `REFRESH_FULL` (0): Full screen refresh (slower).
  - `REFRESH_HALF` (1): Half screen refresh.
  - `REFRESH_FAST` (2): Partial fast refresh (default).
- `gui.setOrientation(mode)`: Set orientation (`landscape_cw`, `landscape_ccw`, `portrait`, `portrait_inv`).
- `gui.drawBmp(path, x, y, maxW, maxH)`: Draw a BMP image from the SD card.
- `gui.drawButtonHints(back, confirm, prev, next)`: Draw UI button hints at the bottom.

### 3.4 Network (net)
- `net.wifiConnect()`: Starts async connection using saved credentials.
- `net.wifiStatus()`: Returns current status (`idle`, `connecting`, `connected`, `failed`).
- `net.wifiDisconnect()`: Disconnects from WiFi.
- `net.get(url [, headers_table])`: Performs an HTTP GET request.
- `net.urlencode(string)`: URL encodes a string.

### 3.5 System & Filesystem
- `sys.millis()`: Returns uptime in milliseconds.
- `sys.delay(ms)`: Pause execution.
- `sys.exit()`: Safely exit the plugin.
- `fs.listDirs(path)` / `fs.listFiles(path)`: List directory contents.
- `fs.readFile(path)` / `fs.writeFile(path, content)`: File I/O.

## 4. Development Best Practices
1. **Memory Management**: ESP32-C3 has limited RAM (approx. 140KB available). Avoid creating massive tables or loading oversized resources in Lua.
2. **Input Handling**: The system has a built-in `skipNextButtonCheck`, so you don't need to worry about long-press interference when entering a plugin.
3. **Seamless Exit**: Press the **BACK** key at any time to safely exit the plugin and reclaim Lua memory.

## 5. Example: Minimalist Plugin
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
