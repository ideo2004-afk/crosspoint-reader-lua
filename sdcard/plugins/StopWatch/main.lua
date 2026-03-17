-- StopWatch Plugin for XTEINK X4
-- Refined Minimalist UI: Adjusted Lap list position and count

local running = false
local elapsedTime = 0 -- total milliseconds
local lastFrameTime = 0
local exitConfirmation = false
local firstDraw = true
local laps = {} -- Store cumulative lap times to calculate split times

-- 7-segment layout definitions
local segments = {
    [0] = {1, 1, 1, 1, 1, 1, 0},
    [1] = {0, 1, 1, 0, 0, 0, 0},
    [2] = {1, 1, 0, 1, 1, 0, 1},
    [3] = {1, 1, 1, 1, 0, 0, 1},
    [4] = {0, 1, 1, 0, 0, 1, 1},
    [5] = {1, 0, 1, 1, 0, 1, 1},
    [6] = {1, 0, 1, 1, 1, 1, 1},
    [7] = {1, 1, 1, 0, 0, 0, 0},
    [8] = {1, 1, 1, 1, 1, 1, 1},
    [9] = {1, 1, 1, 1, 0, 1, 1}
}

function formatTime(ms)
    local totalSec = math.floor(ms / 1000)
    local m = math.floor(totalSec / 60)
    local s = totalSec % 60
    return string.format("%02d:%02d", m, s)
end

function drawDigit(x, y, digit, w, h)
    local segData = segments[digit]
    if not segData then return end

    local thickness = 8
    local half = math.floor(h / 2)
    local segLen = half - math.floor(thickness * 1.5)

    if segData[1] == 1 then gui.fillRect(x + thickness, y, w - thickness * 2, thickness, COLOR_BLACK) end
    if segData[2] == 1 then gui.fillRect(x + w - thickness, y + thickness, thickness, segLen, COLOR_BLACK) end
    if segData[3] == 1 then gui.fillRect(x + w - thickness, y + half + math.floor(thickness / 2), thickness, segLen, COLOR_BLACK) end
    if segData[4] == 1 then gui.fillRect(x + thickness, y + h - thickness, w - thickness * 2, thickness, COLOR_BLACK) end
    if segData[5] == 1 then gui.fillRect(x, y + half + math.floor(thickness / 2), thickness, segLen, COLOR_BLACK) end
    if segData[6] == 1 then gui.fillRect(x, y + thickness, thickness, segLen, COLOR_BLACK) end
    if segData[7] == 1 then gui.fillRect(x + thickness, y + half - math.floor(thickness / 2), w - thickness * 2, thickness, COLOR_BLACK) end
end

function drawMainDisplay(totalMs)
    local totalSec = math.floor(totalMs / 1000)
    local m = math.floor(totalSec / 60)
    local s = totalSec % 60

    local sw = gui.width()
    local sh = gui.height()
    local digitH = 300
    local digitW = 50
    local spacing = 70 
    local colonWidth = 30
    
    local totalWidth = (digitW * 4) + (spacing - digitW) * 3 + colonWidth
    local startX = math.floor((sw - totalWidth) / 2)
    local centerY = 180

    drawDigit(startX, centerY, math.floor(m / 10), digitW, digitH)
    drawDigit(startX + spacing, centerY, m % 10, digitW, digitH)
    
    gui.fillRect(startX + spacing * 2 - 5, centerY + math.floor(digitH * 0.35), 10, 10, COLOR_BLACK)
    gui.fillRect(startX + spacing * 2 - 5, centerY + math.floor(digitH * 0.65), 10, 10, COLOR_BLACK)

    drawDigit(startX + spacing * 2 + colonWidth, centerY, math.floor(s / 10), digitW, digitH)
    drawDigit(startX + spacing * 3 + colonWidth, centerY, s % 10, digitW, digitH)

    -- Draw Laps (Raised by 60px from previous 100px gap, limiting to 5 items)
    local lapY = centerY + digitH + 40
    for i = #laps, math.max(1, #laps - 4), -1 do
        local prevTime = (i > 1) and laps[i-1] or 0
        local splitTime = laps[i] - prevTime
        
        local lapText = string.format("Lap %d: %s", i, formatTime(splitTime))
        gui.drawCenteredText(FONT_BOOKERLY_12, lapY, lapText, COLOR_BLACK, STYLE_NORMAL)
        lapY = lapY + 35
    end
end

function init()
    running = false
    elapsedTime = 0
    lastFrameTime = sys.millis()
    exitConfirmation = false
    firstDraw = true
    laps = {}
end

local lastRefreshTime = 0

function draw()
    local now = sys.millis()
    local btnPressed = false

    local confirmLikePressed = input.wasPressed("confirm") or input.wasPressed("back") or input.wasPressed("up")
    local funcLikePressed = input.wasPressed("left") or input.wasPressed("right") or input.wasPressed("down")

    if exitConfirmation then
        if confirmLikePressed then sys.exit() end
        if funcLikePressed then 
            exitConfirmation = false 
            btnPressed = true
        end
    else
        if confirmLikePressed then
            running = not running
            if running then lastFrameTime = now end
            btnPressed = true
        end

        if funcLikePressed then
            if running then
                table.insert(laps, elapsedTime)
                btnPressed = true
            else
                if elapsedTime > 0 then
                    elapsedTime = 0
                    laps = {}
                    btnPressed = true
                else
                    exitConfirmation = true
                    btnPressed = true
                end
            end
        end
    end

    if running then
        elapsedTime = elapsedTime + (now - lastFrameTime)
        lastFrameTime = now
    end

    if btnPressed or (now - lastRefreshTime >= 1000) or firstDraw then
        gui.clear()
        drawMainDisplay(elapsedTime)

        if exitConfirmation then
            local bw = gui.width()
            local bh = gui.height()
            gui.fillRoundedRect(50, bh/2 - 80, bw - 100, 160, 10, COLOR_WHITE)
            gui.drawRoundedRect(50, bh/2 - 80, bw - 100, 160, 2, 10, COLOR_BLACK)
            gui.drawCenteredText(FONT_BOOKERLY_12, bh/2 - 30, "Exit StopWatch?", COLOR_BLACK, STYLE_BOLD)
        end

        local refreshMode = firstDraw and REFRESH_HALF or REFRESH_FAST
        gui.refresh(refreshMode)
        
        lastRefreshTime = now
        firstDraw = false
    end
end
