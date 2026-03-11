-- XTEINK X4 Sudoku Plugin
-- DESCRIPTION: Sudoku on X4.

-- ── Game State ─────────────────────────────────────────────────────────────────

local grid = {}         -- Current board [1..81]
local initialGrid = {}  -- Initial puzzle [1..81] (0 = empty)
local solutionGrid = {} -- Full solution for "Help Me!" check
local cursorX, cursorY = 4, 4
local showBackMenu = false
local showDiffMenu = true -- Start with difficulty selection
local isGameOver = false
local isPickerFocused = false
local backMenuIdx = 0
local gameOverMenuIdx = 0
local diffMenuIdx = 0
local pickerNumbers = {}
local pickerIdx = 1
local history = {}      -- For Undo: list of {idx, prevVal}
local needsDraw = true
local currentHoles = 45 -- Default

local diffLevels = {
    { name = "Knight", desc = "Easy", holes = 38 },
    { name = "Queen", desc = "Medium", holes = 44 },
    { name = "King", desc = "Hard", holes = 50 }
}

-- Trash Talk State
local talkIdx = 1
local talkMessages = {
    "My grandma could solve this\nwith her eyes closed.", "The screen refresh is\nfaster than your brain.", "Are you serious?\nI thought we were playing Sudoku.",
    "Can you hear the X4 processor\nlaughing at you?", "Hint:\nThe answer is not 42.", "Need 'Help Me!'?\nI've been waiting forever.",
    "That move...\nLee Sedol would be speechless.", "Are you calculating\nor just daydreaming?", "Putting a 1 there?\nBold move, human.",
    "I'm E-paper, not a tissue.\nDon't make me cry.", "Your logic is... unconventional,\nto say the least.", "My battery will die\nbefore you finish this board.",
    "Sudoku isn't a lottery.\nStop guessing.", "If you're stuck, I can suggest\na... wrong answer.", "My kernel just winced\nat that last move.",
    "Can you do anything\nbesides filling colors?", "This cell deserves\na better digit than that.", "Thinking... Actually, I give up.\n(Just kidding!)",
    "E-ink is expensive.\nDon't waste it on mistakes.", "Do you need to reboot\nyour brain?", "The 9x9 world seems\nto be beyond you.",
    "Does it really take 5 seconds\nto think of that?", "I calculated 420k paths;\nyou picked the worst one.", "Keep going, you're one\nmistake away from a trophy.",
    "Maybe you should go back\nto playing Snake.", "A 9 in this cell?\nIs that your final will?", "I'm your opponent,\nnot your eye doctor.",
    "A snail could finish this\npuzzle faster than you.", "I'm considering a factory reset.\nThis is painful.", "If you get this right,\nI'll call you... 'Master'.",
    "It's not magic. The numbers\nwon't fix themselves.", "Your logic is as stiff\nas my plastic casing.", "I'm B&W, but your future\nis looking even darker.",
    "Staring won't help.\nI'm not going to give you a hint.", "This difficulty was meant\nfor humans. Are you one?", "You fill cells at the speed\nof a sunset.",
    "Can I quit now?\nI’m getting bored.", "That's not a solution,\nit's performance art.", "What's your CPU's clock speed?\nIt feels slow.",
    "Tired?\nThere's always the EXIT button.", "This cell is crying because\nit's been misfilled.", "You're not just filling cells,\nyou're failing them.",
    "I almost reached out\nto correct you there.", "My CPU almost melted\nwatching that move.", "You should probably take a nap\nafter this game.",
    "The puzzle is easy.\nYour thinking is hard.", "Are your fingers shaking?\nIs it fear or doubt?", "You fill fast,\nbut you fail even faster.",
    "Is this difficulty level\ntoo high for you?", "If Sudoku were a war,\nyou'd be a prisoner.", "Don't blame the hardware.\nBlame your logic core.",
    "A 5 here? I almost\nsprayed ink laughing.", "Do you need me to turn up\nthe brightness for you?", "In my memory, that move\nis ranked dead last.",
    "If you win this,\nI'll eat my own source code.", "Don't worry, you haven't\npressed EXIT... yet.", "How is this different\nfrom clicking randomly?",
    "Maybe you should stick\nto reading books.", "My clock is getting tired\nof waiting for you.", "Congratulations! You just\nwasted 3 seconds of life."
}

-- ── Color/Style constants ──────────────────────────────────────────────────────

-- ── Sudoku Logic ──────────────────────────────────────────────────────────────

local function getIdx(x, y) return (y - 1) * 9 + x end

local function isValid(val, x, y)
    if val == 0 then return true end
    for i = 1, 9 do
        if i ~= x and grid[getIdx(i, y)] == val then return false end
    end
    for i = 1, 9 do
        if i ~= y and grid[getIdx(x, i)] == val then return false end
    end
    local bx = math.floor((x - 1) / 3) * 3
    local by = math.floor((y - 1) / 3) * 3
    for i = 1, 3 do
        for j = 1, 3 do
            local cx = bx + i
            local cy = by + j
            if (cx ~= x or cy ~= y) and grid[getIdx(cx, cy)] == val then
                return false
            end
        end
    end
    return true
end

local function checkWin()
    for i = 1, 81 do
        local val = grid[i]
        if val == 0 then return false end
        local x = (i - 1) % 9 + 1
        local y = math.floor((i - 1) / 9) + 1
        if not isValid(val, x, y) then return false end
    end
    return true
end

local function updatePicker(stayOnIdx)
    pickerNumbers = {0}
    for v = 1, 9 do
        if isValid(v, cursorX, cursorY) then
            table.insert(pickerNumbers, v)
        end
    end
    if not stayOnIdx then
        pickerIdx = 1
    elseif pickerIdx > #pickerNumbers then
        pickerIdx = #pickerNumbers
    end
end

local function generatePuzzle()
    local base = {
        5,3,4,6,7,8,9,1,2,6,7,2,1,9,5,3,4,8,1,9,8,3,4,2,5,6,7,
        8,5,9,7,6,1,4,2,3,4,2,6,8,5,3,7,9,1,7,1,3,9,2,4,8,5,6,
        9,6,1,5,3,7,2,8,4,2,8,7,4,1,9,6,3,5,3,4,5,2,8,6,1,7,9
    }
    local map = {1,2,3,4,5,6,7,8,9}
    for i = 9, 2, -1 do
        local j = math.random(i)
        map[i], map[j] = map[j], map[i]
    end
    local temp = {}
    for i = 1, 81 do temp[i] = map[base[i]] end
    for b = 0, 2 do
        local r = {1,2,3}
        for i = 3, 2, -1 do local j = math.random(i); r[i], r[j] = r[j], r[i] end
        local offset = b * 27
        local source = {}
        for i = 1, 81 do source[i] = temp[i] end
        for i = 1, 3 do
            for x = 1, 9 do temp[offset + (i-1)*9 + x] = source[offset + (r[i]-1)*9 + x] end
        end
    end
    for i = 1, 81 do solutionGrid[i] = temp[i] end
    local indices = {}
    for i = 1, 81 do indices[i] = i end
    for i = 81, 2, -1 do local j = math.random(i); indices[i], indices[j] = indices[j], indices[i] end
    
    for i = 1, currentHoles do temp[indices[i]] = 0 end
    
    for i = 1, 81 do 
        grid[i] = temp[i]
        initialGrid[i] = temp[i] 
    end
    history = {}
    cursorX, cursorY = 4, 4
    isGameOver = false
    isPickerFocused = false
    talkIdx = math.random(#talkMessages)
    updatePicker()
    needsDraw = true
end

-- ── Rendering ─────────────────────────────────────────────────────────────────

local function renderGrid()
    local sw = gui.width()
    local cellSize = math.floor((sw - 60) / 9)
    local startX, startY = 30, 150
    for i = 0, 9 do
        local thick = (i % 3 == 0) and 4 or 1
        gui.drawLine(startX, startY + i * cellSize, startX + 9 * cellSize, startY + i * cellSize, thick, COLOR_BLACK)
        gui.drawLine(startX + i * cellSize, startY, startX + i * cellSize, startY + 9 * cellSize, thick, COLOR_BLACK)
    end
    for y = 1, 9 do
        for x = 1, 9 do
            local val = grid[getIdx(x, y)]
            if val ~= 0 then
                local isFixed = (initialGrid[getIdx(x, y)] ~= 0)
                local style = isFixed and STYLE_BOLD or STYLE_REGULAR
                local sText = tostring(val)
                local tw = gui.getTextWidth(FONT_BOOKERLY_12, sText, style)
                gui.drawText(FONT_BOOKERLY_12, startX + (x - 1) * cellSize + math.floor((cellSize - tw) / 2),
                             startY + (y - 1) * cellSize + math.floor(cellSize / 2) - 15, sText, COLOR_BLACK, style)
            end
        end
    end
    if not (showBackMenu or isGameOver or showDiffMenu) then
        local cx = startX + (cursorX - 1) * cellSize
        local cy = startY + (cursorY - 1) * cellSize
        if isPickerFocused then
            gui.drawRoundedRect(cx + 4, cy + 4, cellSize - 6, cellSize - 6, 1, 5, COLOR_BLACK)
        else
            gui.drawRoundedRect(cx + 3, cy + 3, cellSize - 4, cellSize - 4, 3, 5, COLOR_BLACK)
        end
    end
end

local function renderNumberPicker()
    local sw, sh = gui.width(), gui.height()
    local py, cellW, pickerH = sh - 230, 38, 60
    if initialGrid[getIdx(cursorX, cursorY)] ~= 0 then return end
    local totalW = #pickerNumbers * cellW
    local startX = math.floor((sw - totalW) / 2)
    for i, v in ipairs(pickerNumbers) do
        local bx = startX + (i - 1) * cellW
        local label = (v == 0) and "X" or tostring(v)
        local tw = gui.getTextWidth(FONT_BOOKERLY_12, label, STYLE_REGULAR)
        local tx = bx + math.floor((cellW - tw) / 2)
        if isPickerFocused and pickerIdx == i then
            gui.fillRoundedRect(bx + 2, py, cellW - 4, pickerH, 8, COLOR_BLACK)
            gui.drawText(FONT_BOOKERLY_12, tx, py + 18, label, COLOR_WHITE, STYLE_BOLD)
        else
            gui.drawText(FONT_BOOKERLY_12, tx, py + 18, label, COLOR_BLACK, (pickerIdx == i) and STYLE_BOLD or STYLE_REGULAR)
        end
    end
end

local function renderTrashTalk()
    if isGameOver or showBackMenu or showDiffMenu then return end
    local sw, sh = gui.width(), gui.height()
    local py = sh - 230
    local msg = talkMessages[talkIdx]
    
    local lines = {}
    for line in (msg .. "\n"):gmatch("(.-)\n") do
        table.insert(lines, line)
    end
    
    for i, line in ipairs(lines) do
        local tw = gui.getTextWidth(FONT_BOOKERLY_12, line, STYLE_BOLD)
        gui.drawText(FONT_BOOKERLY_12, math.floor((sw - tw) / 2), py + 60 + (i-1)*25, line, COLOR_BLACK, STYLE_BOLD)
    end
end

local function renderDiffMenu()
    local sw, sh = gui.width(), gui.height()
    local mw, mh = 400, 480
    local mx, my = (sw - mw) / 2, (sh - mh) / 2
    gui.fillRoundedRect(mx, my, mw, mh, 15, COLOR_WHITE)
    gui.drawRoundedRect(mx, my, mw, mh, 3, 15, COLOR_BLACK)
    gui.drawCenteredText(FONT_BOOKERLY_14, my + 40, "Choose Difficulty", COLOR_BLACK, STYLE_BOLD)
    
    local opts = diffLevels
    
    for i, opt in ipairs(opts) do
        local ry = my + 110 + (i - 1) * 110
        if diffMenuIdx == i - 1 then
            gui.fillRoundedRect(mx + 30, ry, mw - 60, 90, 10, COLOR_BLACK)
            gui.drawCenteredText(FONT_BOOKERLY_14, ry + 25, opt.name, COLOR_WHITE, STYLE_BOLD)
            gui.drawCenteredText(FONT_BOOKERLY_12, ry + 55, opt.desc, COLOR_WHITE, STYLE_REGULAR)
        else
            gui.drawRoundedRect(mx + 30, ry, mw - 60, 90, 2, 10, COLOR_BLACK)
            gui.drawCenteredText(FONT_BOOKERLY_14, ry + 25, opt.name, COLOR_BLACK, STYLE_BOLD)
            gui.drawCenteredText(FONT_BOOKERLY_12, ry + 55, opt.desc, COLOR_BLACK, STYLE_REGULAR)
        end
    end
end

local function renderBackMenu()
    local sw, sh = gui.width(), gui.height()
    local mw, mh = 320, 420
    local mx, my = (sw - mw) / 2, (sh - mh) / 2
    gui.fillRoundedRect(mx, my, mw, mh, 15, COLOR_WHITE)
    gui.drawRoundedRect(mx, my, mw, mh, 3, 15, COLOR_BLACK)
    gui.drawCenteredText(FONT_BOOKERLY_12, my + 30, "Sudoku Menu", COLOR_BLACK, STYLE_BOLD)
    local opts = { "Resume", "Undo", "Help Me!", "Clear", "New", "EXIT" }
    for i, opt in ipairs(opts) do
        local ry = my + 75 + (i - 1) * 55
        local tx = mx + math.floor((mw - gui.getTextWidth(FONT_BOOKERLY_12, opt, STYLE_REGULAR)) / 2)
        if backMenuIdx == i - 1 then
            gui.fillRoundedRect(mx + 20, ry - 5, mw - 40, 45, 8, COLOR_BLACK)
            gui.drawText(FONT_BOOKERLY_12, tx, ry + 8, opt, COLOR_WHITE, STYLE_BOLD)
        else
            gui.drawText(FONT_BOOKERLY_12, tx, ry + 8, opt, COLOR_BLACK, STYLE_REGULAR)
        end
    end
end

local function renderGameOverMenu()
    local sw, sh = gui.width(), gui.height()
    local mw, mh = 320, 280
    local mx, my = (sw - mw) / 2, (sh - mh) / 2
    gui.fillRoundedRect(mx, my, mw, mh, 15, COLOR_WHITE)
    gui.drawRoundedRect(mx, my, mw, mh, 3, 15, COLOR_BLACK)
    gui.drawCenteredText(FONT_BOOKERLY_12, my + 30, "Congratulations!", COLOR_BLACK, STYLE_BOLD)
    gui.drawCenteredText(FONT_BOOKERLY_12, my + 65, "Puzzle Cleared", COLOR_BLACK, STYLE_REGULAR)
    local opts = { "New Game", "EXIT" }
    for i, opt in ipairs(opts) do
        local ry = my + 130 + (i - 1) * 60
        local tx = mx + math.floor((mw - gui.getTextWidth(FONT_BOOKERLY_12, opt, STYLE_REGULAR)) / 2)
        if gameOverMenuIdx == i - 1 then
            gui.fillRoundedRect(mx + 20, ry - 5, mw - 40, 50, 8, COLOR_BLACK)
            gui.drawText(FONT_BOOKERLY_12, tx, ry + 10, opt, COLOR_WHITE, STYLE_BOLD)
        else
            gui.drawText(FONT_BOOKERLY_12, tx, ry + 10, opt, COLOR_BLACK, STYLE_REGULAR)
        end
    end
end

-- ── Input Handlers ─────────────────────────────────────────────────────────────

local function handleDiffMenuInput()
    if input.wasPressed("page_back") then diffMenuIdx = (diffMenuIdx > 0) and diffMenuIdx - 1 or 2; needsDraw = true
    elseif input.wasPressed("page_forward") then diffMenuIdx = (diffMenuIdx + 1) % 3; needsDraw = true
    elseif input.wasPressed("confirm") then
        currentHoles = diffLevels[diffMenuIdx + 1].holes
        showDiffMenu = false
        generatePuzzle()
        needsDraw = true
    elseif input.wasPressed("back") then sys.delay(150); sys.exit() end
end

local function handleGameInput()
    local oldX, oldY = cursorX, cursorY
    if input.wasPressed("page_back") then cursorY = (cursorY > 1) and (cursorY - 1) or 9
    elseif input.wasPressed("page_forward") then cursorY = (cursorY % 9) + 1
    elseif input.wasPressed("left") then cursorX = (cursorX > 1) and (cursorX - 1) or 9
    elseif input.wasPressed("right") then cursorX = (cursorX % 9) + 1
    elseif input.wasPressed("confirm") then
        if initialGrid[getIdx(cursorX, cursorY)] == 0 then isPickerFocused = true; needsDraw = true end
    elseif input.wasPressed("back") then
        showBackMenu = true; backMenuIdx = 0; needsDraw = true
    end
    if cursorX ~= oldX or cursorY ~= oldY then updatePicker(); needsDraw = true end
end

local function handlePickerInput()
    if input.wasPressed("left") then pickerIdx = (pickerIdx > 1) and pickerIdx - 1 or #pickerNumbers; needsDraw = true
    elseif input.wasPressed("right") then pickerIdx = (pickerIdx % #pickerNumbers) + 1; needsDraw = true
    elseif input.wasPressed("confirm") then
        local val = pickerNumbers[pickerIdx]
        local idx = getIdx(cursorX, cursorY)
        table.insert(history, {idx = idx, prev = grid[idx]})
        grid[idx] = val; isPickerFocused = false
        
        -- Update trash talk on every input
        talkIdx = math.random(#talkMessages)

        if checkWin() then isGameOver = true end
        needsDraw = true
    elseif input.wasPressed("back") then isPickerFocused = false; needsDraw = true end
end

local function handleBackMenuInput()
    if input.wasPressed("page_back") then backMenuIdx = (backMenuIdx > 0) and backMenuIdx - 1 or 5; needsDraw = true
    elseif input.wasPressed("page_forward") then backMenuIdx = (backMenuIdx + 1) % 6; needsDraw = true
    elseif input.wasPressed("confirm") then
        if backMenuIdx == 0 then showBackMenu = false
        elseif backMenuIdx == 1 then
            if #history > 0 then local last = table.remove(history); grid[last.idx] = last.prev; updatePicker(true) end
            showBackMenu = false
        elseif backMenuIdx == 2 then -- Help Me!
            for i = 1, 81 do if initialGrid[i] == 0 and grid[i] ~= 0 and grid[i] ~= solutionGrid[i] then grid[i] = 0 end end
            showBackMenu = false; updatePicker()
        elseif backMenuIdx == 3 then
            for i = 1, 81 do grid[i] = initialGrid[i] end history = {} showBackMenu = false updatePicker()
        elseif backMenuIdx == 4 then showDiffMenu = true; showBackMenu = false
        elseif backMenuIdx == 5 then sys.delay(150); sys.exit() end
        needsDraw = true
    elseif input.wasPressed("back") then showBackMenu = false; needsDraw = true end
end

-- ── Lifecycle ──────────────────────────────────────────────────────────────────

function init()
    math.randomseed(sys.millis())
    showDiffMenu = true
end

function draw()
    if showDiffMenu then handleDiffMenuInput()
    elseif isGameOver then 
        if input.wasPressed("page_back") or input.wasPressed("page_forward") then gameOverMenuIdx = 1 - gameOverMenuIdx; needsDraw = true
        elseif input.wasPressed("confirm") then
            if gameOverMenuIdx == 0 then showDiffMenu = true else sys.delay(150); sys.exit() end
            needsDraw = true
        end
    elseif showBackMenu then handleBackMenuInput()
    elseif isPickerFocused then handlePickerInput()
    else handleGameInput() end

    if not needsDraw then return end
    needsDraw = false
    gui.clear()
    gui.drawText(FONT_BOOKERLY_14, 20, 20, "Sudoku", COLOR_BLACK, STYLE_BOLD)
    gui.drawLine(0, 60, gui.width(), 60, 3, COLOR_BLACK)
    renderGrid()
    renderNumberPicker()
    renderTrashTalk()
    if showDiffMenu then renderDiffMenu()
    elseif showBackMenu then renderBackMenu()
    elseif isGameOver then renderGameOverMenu() end
    gui.drawButtonHints("<<", "o", "<", ">")
    gui.refresh(REFRESH_FAST)
end
