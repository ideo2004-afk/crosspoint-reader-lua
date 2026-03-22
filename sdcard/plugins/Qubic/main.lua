-- Qubic — 3D Tic-Tac-Toe
-- DESCRIPTION: 3D Tic-Tac-Toe Game with AI.

local NONE  = 0
local HUMAN = 1
local AIPL  = 2

-- ── board ─────────────────────────────────────────────────────────────────────

local board    = {}
local winLines = {}   -- 76 lines, each {a,b,c,d} 1-based

local function cellIdx(x, y, z)   -- x,y,z: 0-based → 1-based table index
    return z * 16 + y * 4 + x + 1
end

local function resetBoard()
    for i = 1, 64 do board[i] = NONE end
end

local function initLines()
    winLines = {}
    local function add(a, b, c, d)  -- 0-based raw indices
        winLines[#winLines + 1] = { a+1, b+1, c+1, d+1 }
    end

    -- 1. Horizontal rows in each layer
    for z = 0, 3 do for y = 0, 3 do
        local b = z*16 + y*4; add(b, b+1, b+2, b+3)
    end end

    -- 2. Vertical columns in each layer
    for z = 0, 3 do for x = 0, 3 do
        local b = z*16 + x; add(b, b+4, b+8, b+12)
    end end

    -- 3. Pillar lines (z direction)
    for y = 0, 3 do for x = 0, 3 do
        local b = y*4 + x; add(b, b+16, b+32, b+48)
    end end

    -- 4. Flat layer diagonals (2 per layer)
    for z = 0, 3 do
        local b = z*16
        add(b, b+5, b+10, b+15)       -- (0,0)→(3,3)
        add(b+3, b+6, b+9, b+12)      -- (3,0)→(0,3)
    end

    -- 5. X-Z plane diagonals (fixed y)
    for y = 0, 3 do
        local b = y*4
        add(b,   16+b+1, 32+b+2, 48+b+3)
        add(b+3, 16+b+2, 32+b+1, 48+b)
    end

    -- 6. Y-Z plane diagonals (fixed x)
    for x = 0, 3 do
        add(x,    16+4+x,  32+8+x,  48+12+x)
        add(12+x, 16+8+x,  32+4+x,  48+x)
    end

    -- 7. Space diagonals (4 main diagonals of the cube)
    add(0,  21, 42, 63)
    add(3,  22, 41, 60)
    add(12, 25, 38, 51)
    add(15, 26, 37, 48)
end

-- ── game logic ────────────────────────────────────────────────────────────────

local function makeMove(i1, player)   -- i1: 1-based
    if board[i1] ~= NONE then return false end
    board[i1] = player
    return true
end

local function checkWinner()
    for _, line in ipairs(winLines) do
        local p = board[line[1]]
        if p ~= NONE and p == board[line[2]] and p == board[line[3]] and p == board[line[4]] then
            return p
        end
    end
    return NONE
end

local function isFull()
    for i = 1, 64 do if board[i] == NONE then return false end end
    return true
end

-- ── AI ────────────────────────────────────────────────────────────────────────

local function getBestMove(diff)
    local numLines = (diff == 1) and 40 or (diff == 2) and 55 or 76

    -- Partial Fisher-Yates shuffle of line order
    local order = {}
    for i = 1, 76 do order[i] = i end
    if numLines < 76 then
        for i = 1, numLines do
            local j = math.random(i, 76)
            order[i], order[j] = order[j], order[i]
        end
    end

    local bestScore = -1000000
    local best = {}

    for i1 = 1, 64 do
        if board[i1] == NONE then
            local score = 0

            for li = 1, numLines do
                local line = winLines[order[li]]
                local onLine = false
                for j = 1, 4 do if line[j] == i1 then onLine = true; break end end
                if onLine then
                    local pc, oc = 0, 0
                    for j = 1, 4 do
                        local p = board[line[j]]
                        if     p == AIPL  then pc = pc + 1
                        elseif p == HUMAN then oc = oc + 1 end
                    end
                    if oc == 0 then
                        if     pc == 3 then score = score + 100000
                        elseif pc == 2 then score = score + 1000
                        elseif pc == 1 then score = score + 100
                        else             score = score + 10 end
                    elseif pc == 0 then
                        if     oc == 3 then score = score + 50000
                        elseif oc == 2 then score = score + 500
                        else             score = score + 50 end
                    end
                end
            end

            -- Center bonus
            local i0 = i1 - 1
            local bx = i0 % 4
            local by = math.floor(i0 / 4) % 4
            local bz = math.floor(i0 / 16)
            if (bx == 1 or bx == 2) and (by == 1 or by == 2) and (bz == 1 or bz == 2) then
                score = score + 5
            end

            if score > bestScore then
                bestScore = score; best = { i1 }
            elseif score == bestScore then
                best[#best + 1] = i1
            end
        end
    end

    if #best == 0 then return -1 end
    return best[math.random(#best)]
end

-- ── game state ────────────────────────────────────────────────────────────────

local curX, curY, curZ = 0, 0, 0
local aiDiff       = 1
local gameStatus   = "playing"   -- "playing" | "won" | "lost" | "draw"
local lastAiMove   = -1
local isAiThinking = false
local aiThinkStart = 0
local postMenuIdx  = 0
local inEscMenu    = false
local escIdx       = 0
local showDiffSelection = true
local needsDraw        = true

local function restartGame()
    resetBoard()
    curX, curY, curZ = 0, 0, 0
    gameStatus   = "playing"
    lastAiMove   = -1
    isAiThinking = false
    postMenuIdx  = 0
    needsDraw    = true
end

-- ── isometric helpers ─────────────────────────────────────────────────────────

local function isoPoint(x, y, z)
    local cx = math.floor(gui.width() / 2)
    local cy = 160 + z * 150
    return cx + math.floor((x - y) * 36),
           cy + math.floor((x + y) * 16)
end

-- ── rendering ─────────────────────────────────────────────────────────────────

local renderEscMenu  -- forward declaration

local function renderBoard()
    gui.clear()

    -- Header
    gui.drawText(FONT_UI_12, 20, 20, "3D Tic-Tac-Toe")
    gui.drawLine(0, 60, gui.width(), 60, 3)
    local diffLabel = (aiDiff == 1) and "L1 (Easy)" or (aiDiff == 2) and "L2 (Medium)" or "L3 (Hard)"
    gui.drawText(FONT_UI_12, 20, 80, diffLabel)

    -- Grid and pieces for all 4 layers
    for z = 0, 3 do
        -- Grid lines
        for i = 0, 4 do
            local x1,y1 = isoPoint(0, i, z); local x2,y2 = isoPoint(4, i, z)
            gui.drawLine(x1, y1, x2, y2)
            local x3,y3 = isoPoint(i, 0, z); local x4,y4 = isoPoint(i, 4, z)
            gui.drawLine(x3, y3, x4, y4)
        end

        -- Pieces
        for y = 0, 3 do
            for x = 0, 3 do
                local p = board[cellIdx(x, y, z)]
                if p == HUMAN then
                    local px, py = isoPoint(x + 0.5, y + 0.5, z)
                    gui.drawLine(px,    py-8,  px,    py+8,  2)
                    gui.drawLine(px-14, py,    px+14, py,    2)
                elseif p == AIPL then
                    local px, py = isoPoint(x + 0.5, y + 0.5, z)
                    local th = (lastAiMove == cellIdx(x, y, z)) and 4 or 2
                    gui.drawLine(px-6,  py-8,  px+6,  py-8,  th)
                    gui.drawLine(px+6,  py-8,  px+12, py-2,  th)
                    gui.drawLine(px+12, py-2,  px+12, py+2,  th)
                    gui.drawLine(px+12, py+2,  px+6,  py+8,  th)
                    gui.drawLine(px+6,  py+8,  px-6,  py+8,  th)
                    gui.drawLine(px-6,  py+8,  px-12, py+2,  th)
                    gui.drawLine(px-12, py+2,  px-12, py-2,  th)
                    gui.drawLine(px-12, py-2,  px-6,  py-8,  th)
                end
            end
        end
    end

    -- Cursor frame
    if gameStatus == "playing" and not inEscMenu and not isAiThinking then
        local c1x,c1y = isoPoint(curX,       curY,       curZ)
        local c2x,c2y = isoPoint(curX + 1.0, curY,       curZ)
        local c3x,c3y = isoPoint(curX + 1.0, curY + 1.0, curZ)
        local c4x,c4y = isoPoint(curX,       curY + 1.0, curZ)
        gui.drawLine(c1x, c1y, c2x, c2y, 3)
        gui.drawLine(c2x, c2y, c3x, c3y, 3)
        gui.drawLine(c3x, c3y, c4x, c4y, 3)
        gui.drawLine(c4x, c4y, c1x, c1y, 3)
    end

    -- AI thinking indicator
    if isAiThinking then
        gui.drawText(FONT_SMALL, 340, 700, "AI Thinking...")
    end

    -- Post-game result overlay
    if gameStatus ~= "playing" and not isAiThinking then
        local sw = gui.width()
        local sh = gui.height()
        local bw, bh = 400, 150
        local bx = math.floor((sw - bw) / 2)
        local by = math.floor((sh - bh) / 2)

        gui.fillRoundedRect(bx, by, bw, bh, 15, false)  -- white background
        gui.drawRoundedRect(bx, by, bw, bh, 3, 15)      -- black border

        local resultStr = (gameStatus == "won") and "VICTORY!" or
                          (gameStatus == "lost") and "DEFEAT." or "DRAW."
        gui.drawCenteredText(FONT_UI_12, by + 30, resultStr)

        -- Two buttons: New | Exit
        local labels = { "New", "Exit" }
        for i = 0, 1 do
            local btnW, btnH = 140, 60
            local btnX = bx + math.floor(bw / 4) + i * math.floor(bw / 2) - math.floor(btnW / 2)
            local btnY = by + 75
            local label = labels[i + 1]
            local tx = btnX + math.floor((btnW - gui.getTextWidth(FONT_UI_12, label)) / 2)
            local ty = btnY + 20
            if postMenuIdx == i then
                gui.fillRoundedRect(btnX, btnY, btnW, btnH, 10)      -- black fill
                gui.drawText(FONT_UI_12, tx, ty, label, false)       -- white text
            else
                gui.drawRoundedRect(btnX, btnY, btnW, btnH, 2, 10)
                gui.drawText(FONT_UI_12, tx, ty, label, true)
            end
        end
    end
    
    -- Startup difficulty selection
    if showDiffSelection then
        local mw, mh = 320, 260
        local mx, my = math.floor((gui.width()-mw)/2), math.floor((gui.height()-mh)/2)
        gui.fillRoundedRect(mx, my, mw, mh, 10, false)
        gui.drawRoundedRect(mx, my, mw, mh, 2, 10)
        gui.drawCenteredText(FONT_UI_12, my + 25, "Select Difficulty", true)
        
        local opts = {"Easy (L1)", "Medium (L2)", "Hard (L3)"}
        for i, opt in ipairs(opts) do
            local ry = my + 80 + (i-1)*50
            local tx = mx + math.floor((mw - gui.getTextWidth(FONT_UI_12, opt))/2)
            if aiDiff == i then
                gui.fillRoundedRect(mx+20, ry-5, mw-40, 40, 8)
                gui.drawText(FONT_UI_12, tx, ry+2, opt, false)
            else
                gui.drawText(FONT_UI_12, tx, ry+2, opt, true)
            end
        end
        gui.drawButtonHints("<<", "o", "<", ">")
    end

    -- Esc menu overlay (drawn on top of everything)
    if inEscMenu then
        renderEscMenu()
    elseif not showDiffSelection then
        gui.drawButtonHints("<<", "o", "<", ">")
    end

    gui.refresh(REFRESH_FAST)
end

renderEscMenu = function()
    local sw = gui.width()
    local sh = gui.height()
    local mw, mh = 320, 330
    local mx = math.floor((sw - mw) / 2)
    local my = math.floor((sh - mh) / 2)

    gui.fillRoundedRect(mx, my, mw, mh, 10, false)
    gui.drawRoundedRect(mx, my, mw, mh, 2, 10)
    gui.drawText(FONT_UI_12, mx + 20, my + 20, "Game Menu")

    local options = { "Resume", "L1 (Easy)", "L2 (Medium)", "L3 (Hard)", "Exit Game" }
    for i, opt in ipairs(options) do
        local ry = my + 65 + (i - 1) * 50
        if escIdx == i - 1 then
            gui.fillRoundedRect(mx + 10, ry - 5, mw - 20, 40, 8)
            gui.drawText(FONT_UI_12, mx + 20, ry + 2, opt, false)  -- white text
        else
            gui.drawText(FONT_UI_12, mx + 20, ry + 2, opt, true)
        end
    end

    gui.drawButtonHints("«", "o", "<", ">")
end

-- ── input ─────────────────────────────────────────────────────────────────────

local function handleEscInput()
    if input.wasReleased("up") or input.wasReleased("left") then
        escIdx = (escIdx > 0) and escIdx - 1 or 4
        needsDraw = true
    elseif input.wasReleased("down") or input.wasReleased("right") then
        escIdx = (escIdx < 4) and escIdx + 1 or 0
        needsDraw = true
    elseif input.wasReleased("confirm") then
        if escIdx == 0 then
            inEscMenu = false; needsDraw = true
        elseif escIdx >= 1 and escIdx <= 3 then
            aiDiff = escIdx
            inEscMenu = false
            restartGame()
        elseif escIdx == 4 then
            sys.exit()
        end
    elseif input.wasReleased("back") then
        inEscMenu = false; needsDraw = true
    end
end

local function handleDiffInput()
    if input.wasReleased("up") or input.wasReleased("left") then
        aiDiff = (aiDiff > 1) and aiDiff - 1 or 3; needsDraw = true
    elseif input.wasReleased("down") or input.wasReleased("right") then
        aiDiff = (aiDiff < 3) and aiDiff + 1 or 1; needsDraw = true
    elseif input.wasReleased("confirm") then
        showDiffSelection = false
        restartGame()
    end
end

local function doAiMove()
    local bestMove = getBestMove(aiDiff)
    if bestMove ~= -1 then
        makeMove(bestMove, AIPL)
        lastAiMove = bestMove
    end
    isAiThinking = false

    local winner = checkWinner()
    if winner == AIPL then
        gameStatus = "lost"; postMenuIdx = 0
    elseif isFull() then
        gameStatus = "draw"; postMenuIdx = 0
    end
    needsDraw = true
end

local function handleGameInput()
    -- Side buttons: cycle layers
    if input.wasPressed("page_back") then
        curZ = (curZ > 0) and curZ - 1 or 3; needsDraw = true
    elseif input.wasPressed("page_forward") then
        curZ = (curZ < 3) and curZ + 1 or 0; needsDraw = true
    end

    -- Left / Right: move cursor or select post-game option
    if gameStatus == "playing" then
        if input.wasPressed("left") then
            local i = curY * 4 + curX
            i = (i > 0) and i - 1 or 15
            curX = i % 4; curY = math.floor(i / 4); needsDraw = true
        elseif input.wasPressed("right") then
            local i = curY * 4 + curX
            i = (i < 15) and i + 1 or 0
            curX = i % 4; curY = math.floor(i / 4); needsDraw = true
        end
    else
        if input.wasReleased("left") or input.wasReleased("right") then
            postMenuIdx = (postMenuIdx == 0) and 1 or 0; needsDraw = true
        end
    end

    -- Back: open esc menu
    if input.wasReleased("back") then
        inEscMenu = true; escIdx = 0; needsDraw = true

    -- Confirm: place piece or select post-game action
    elseif input.wasReleased("confirm") then
        if gameStatus ~= "playing" then
            if postMenuIdx == 0 then
                restartGame()
            else
                sys.exit()
            end
        else
            local i1 = cellIdx(curX, curY, curZ)
            if makeMove(i1, HUMAN) then
                local winner = checkWinner()
                if winner == HUMAN then
                    gameStatus = "won"; postMenuIdx = 0
                elseif isFull() then
                    gameStatus = "draw"; postMenuIdx = 0
                else
                    isAiThinking = true
                    aiThinkStart = sys.millis()
                end
                needsDraw = true
            end
        end
    end
end

-- ── main ──────────────────────────────────────────────────────────────────────

function init()
    initLines()
    restartGame()
    log("Qubic init: " .. #winLines .. " winning lines")
end

function draw()
    -- AI thinking: wait 1200ms then compute
    if isAiThinking then
        if sys.millis() - aiThinkStart > 1200 then
            doAiMove()
        end
        if not needsDraw then return end
        needsDraw = false
        renderBoard()
        return
    end

    -- Input handling
    if inEscMenu then
        handleEscInput()
    elseif showDiffSelection then
        handleDiffInput()
    else
        handleGameInput()
    end

    -- Render only when needed
    if not needsDraw then return end
    needsDraw = false
    renderBoard()
end
