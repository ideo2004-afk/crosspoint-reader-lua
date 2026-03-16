-- StopWatch Plugin for XTEINK X4
-- Uses 7-segment vector drawing for large digits

local running = false
local elapsedTime = 0 -- in milliseconds
local lastFrameTime = 0
local exitConfirmation = false
local firstDraw = true

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

function drawDigit(x, y, digit, w, h)
    local segData = segments[digit]
    if not segData then return end

    local thickness = 8 -- Fixed thickness for a sleek narrow look
    local half = math.floor(h / 2)
    local segLen = half - math.floor(thickness * 1.5)

    -- Segment a (top)
    if segData[1] == 1 then gui.fillRect(x + thickness, y, w - thickness * 2, thickness, COLOR_BLACK) end
    -- Segment b (top-right)
    if segData[2] == 1 then gui.fillRect(x + w - thickness, y + thickness, thickness, segLen, COLOR_BLACK) end
    -- Segment c (bottom-right)
    if segData[3] == 1 then gui.fillRect(x + w - thickness, y + half + math.floor(thickness / 2), thickness, segLen, COLOR_BLACK) end
    -- Segment d (bottom)
    if segData[4] == 1 then gui.fillRect(x + thickness, y + h - thickness, w - thickness * 2, thickness, COLOR_BLACK) end
    -- Segment e (bottom-left)
    if segData[5] == 1 then gui.fillRect(x, y + half + math.floor(thickness / 2), thickness, segLen, COLOR_BLACK) end
    -- Segment f (top-left)
    if segData[6] == 1 then gui.fillRect(x, y + thickness, thickness, segLen, COLOR_BLACK) end
    -- Segment g (middle)
    if segData[7] == 1 then gui.fillRect(x + thickness, y + half - math.floor(thickness / 2), w - thickness * 2, thickness, COLOR_BLACK) end
end

function drawTime(totalMs)
    local totalSec = math.floor(totalMs / 1000)
    local m = math.floor(totalSec / 60)
    local s = totalSec % 60

    local sw = gui.width()
    local sh = gui.height()
    local digitH = 350
    local digitW = 50
    local spacing = 70 -- Gap between digits
    local colonWidth = 30
    
    local totalWidth = (digitW * 4) + (spacing - digitW) * 3 + colonWidth
    local startX = math.floor((sw - totalWidth) / 2)
    local centerY = math.floor((sh - digitH) / 2) - 40

    -- MM:SS
    drawDigit(startX, centerY, math.floor(m / 10), digitW, digitH)
    drawDigit(startX + spacing, centerY, m % 10, digitW, digitH)
    
    -- Colon
    gui.fillRect(startX + spacing * 2 - 5, centerY + math.floor(digitH * 0.35), 10, 10, COLOR_BLACK)
    gui.fillRect(startX + spacing * 2 - 5, centerY + math.floor(digitH * 0.65), 10, 10, COLOR_BLACK)

    drawDigit(startX + spacing * 2 + colonWidth, centerY, math.floor(s / 10), digitW, digitH)
    drawDigit(startX + spacing * 3 + colonWidth, centerY, s % 10, digitW, digitH)
end

function init()
    math.randomseed(sys.millis())
    running = false
    elapsedTime = 0
    lastFrameTime = sys.millis()
    exitConfirmation = false
    firstDraw = true
end

local lastRefreshTime = 0

function draw()
    local now = sys.millis()
    local btnPressed = false

    -- Handle Input (Must be checked every frame for responsiveness)
    if exitConfirmation then
        if input.wasPressed("confirm") then sys.exit() end
        if input.wasPressed("back") then 
            exitConfirmation = false 
            btnPressed = true
        end
    else
        if input.wasPressed("confirm") then
            running = not running
            if running then lastFrameTime = now end
            btnPressed = true
        end

        if input.wasPressed("back") then
            -- Reset
            elapsedTime = 0
            running = false
            btnPressed = true
        end

        if input.wasPressed("left") or input.wasPressed("right") then
            exitConfirmation = true
            btnPressed = true
        end
    end

    -- Update time
    if running then
        elapsedTime = elapsedTime + (now - lastFrameTime)
        lastFrameTime = now
    end

    -- Refresh Throttling (Every 1000ms OR on button press OR first draw)
    if btnPressed or (now - lastRefreshTime >= 1000) or firstDraw then
        gui.clear()

        -- Always render the timer in the background
        drawTime(elapsedTime)

        if exitConfirmation then
            local bw = gui.width()
            local bh = gui.height()
            -- Draw a floating window effect
            gui.fillRoundedRect(50, bh/2 - 80, bw - 100, 160, 10, COLOR_WHITE)
            gui.drawRoundedRect(50, bh/2 - 80, bw - 100, 160, 2, 10, COLOR_BLACK)
            gui.drawCenteredText(FONT_NOTOSANS_14, bh/2 - 30, "Exit StopWatch?", COLOR_BLACK, STYLE_BOLD)
            
            gui.drawButtonHints("<<", "o", "", "")
        else
            -- Button Hints for normal state
            gui.drawButtonHints("<<", "o", "<", ">")
        end

        local refreshMode = firstDraw and REFRESH_HALF or REFRESH_FAST
        gui.refresh(refreshMode)
        
        lastRefreshTime = now
        firstDraw = false
    end
end
