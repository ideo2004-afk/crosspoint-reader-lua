-- XTEINK X4 Snake Plugin
-- DESCRIPTION: Classic Snake game optimized for E-ink.

-- ── Constants ─────────────────────────────────────────────────────────────────
local GRID_SIZE = 30
local MARGIN_TOP = 100
local MARGIN_SIDE = 20
local MARGIN_BOTTOM = 50 
local SPECIAL_FOOD_DURATION = 5000 
local SPECIAL_FOOD_CHANCE = 0.2 

-- ── Game State ────────────────────────────────────────────────────────────────
local snake = {} -- list of {x, y}
local foods = {} -- list of {x, y}
local specialFood = {x = -1, y = -1, spawnTime = 0, active = false}
local dir = {x = 1, y = 0}
local inputQueue = {} 
local score = 0
local isGameOver = false
local gameOverMsg = ""
local gameOverMenuIdx = 0
local hasRefreshedGameOver = false
local needsDraw = true
local lastMoveTime = 0
local moveInterval = 700 

local gridW, gridH = 0, 0

-- ── Logic ─────────────────────────────────────────────────────────────────────

local function isOccupied(x, y)
    for _, segment in ipairs(snake) do
        if segment.x == x and segment.y == y then return true end
    end
    for _, f in ipairs(foods) do
        if f.x == x and f.y == y then return true end
    end
    if specialFood.active and specialFood.x == x and specialFood.y == y then
        return true
    end
    return false
end

local function spawnRegularFood()
    local x, y
    repeat
        x = math.random(0, gridW - 1)
        y = math.random(0, gridH - 1)
    until not isOccupied(x, y)
    table.insert(foods, {x = x, y = y})
end

local function spawnSpecialFood()
    local x, y
    repeat
        x = math.random(0, gridW - 1)
        y = math.random(0, gridH - 1)
    until not isOccupied(x, y)
    specialFood.x, specialFood.y = x, y
    specialFood.spawnTime = sys.millis()
    specialFood.active = true
end

local function resetGame()
    gridW = math.floor((gui.width() - 2 * MARGIN_SIDE) / GRID_SIZE)
    gridH = math.floor((gui.height() - MARGIN_TOP - MARGIN_BOTTOM) / GRID_SIZE)
    
    snake = {
        {x = math.floor(gridW / 2), y = math.floor(gridH / 2)},
        {x = math.floor(gridW / 2) - 1, y = math.floor(gridH / 2)},
        {x = math.floor(gridW / 2) - 2, y = math.floor(gridH / 2)}
    }
    dir = {x = 1, y = 0}
    inputQueue = {}
    score = 0
    isGameOver = false
    gameOverMsg = ""
    gameOverMenuIdx = 0
    hasRefreshedGameOver = false
    specialFood.active = false
    
    -- Spawn 1-3 foods initially
    foods = {}
    local numFoods = math.random(1, 3)
    for i = 1, numFoods do
        spawnRegularFood()
    end
    
    lastMoveTime = sys.millis()
    needsDraw = true
end

local function update()
    if isGameOver then return end

    local now = sys.millis()
    
    -- Check special food expiration
    if specialFood.active and now - specialFood.spawnTime > SPECIAL_FOOD_DURATION then
        specialFood.active = false
        needsDraw = true
    end

    if now - lastMoveTime < moveInterval then return end
    lastMoveTime = now

    -- Process input queue
    if #inputQueue > 0 then
        local nextDir = table.remove(inputQueue, 1)
        if not (nextDir.x == -dir.x and nextDir.y == -dir.y) then
            dir = nextDir
        end
    end

    local head = snake[1]
    local newHead = {x = head.x + dir.x, y = head.y + dir.y}

    -- Wall collision
    if newHead.x < 0 or newHead.x >= gridW or newHead.y < 0 or newHead.y >= gridH then
        isGameOver = true
        gameOverMsg = "哎呀！撞到牆壁了"
        needsDraw = true
        return
    end

    -- Self collision
    for i, segment in ipairs(snake) do
        if newHead.x == segment.x and newHead.y == segment.y then
            isGameOver = true
            gameOverMsg = "不要吃自己呀！"
            needsDraw = true
            return
        end
    end

    table.insert(snake, 1, newHead)

    -- Check food collisions
    local ate = false
    
    -- Regular foods
    for i, f in ipairs(foods) do
        if newHead.x == f.x and newHead.y == f.y then
            score = score + 10
            table.remove(foods, i)
            ate = true
            
            -- Replenish foods (maintain 1-3)
            if #foods == 0 or math.random() < 0.5 then
                local toSpawn = math.random(1, 2)
                for j = 1, toSpawn do
                    if #foods < 3 then spawnRegularFood() end
                end
            end

            -- Special food chance
            if not specialFood.active and math.random() < SPECIAL_FOOD_CHANCE then
                spawnSpecialFood()
            end
            break
        end
    end

    -- Special food
    if not ate and specialFood.active and newHead.x == specialFood.x and newHead.y == specialFood.y then
        score = score + 100
        specialFood.active = false
        ate = true
    end

    if ate then
        if moveInterval > 150 then
            moveInterval = moveInterval - 10
        end
    else
        table.remove(snake)
    end

    needsDraw = true
end

-- ── Rendering ─────────────────────────────────────────────────────────────────

local function render()
    gui.clear()
    
    -- Header
    gui.drawText(FONT_BOOKERLY_14, 20, 20, "Snake", COLOR_BLACK, STYLE_BOLD)
    gui.drawText(FONT_BOOKERLY_12, gui.width() - 150, 20, "Score: " .. score, COLOR_BLACK, STYLE_REGULAR)
    gui.drawLine(0, 70, gui.width(), 70, 2, COLOR_BLACK)

    -- Grid area parameters
    local bx = MARGIN_SIDE
    local by = MARGIN_TOP

    -- Draw Dot Grid (Notebook style)
    for gy = 0, gridH do
        for gx = 0, gridW do
            gui.fillRect(bx + gx * GRID_SIZE, by + gy * GRID_SIZE, 2, 2, COLOR_BLACK)
        end
    end

    -- Grid boundary
    gui.drawRect(bx - 2, by - 2, gridW * GRID_SIZE + 4, gridH * GRID_SIZE + 4, 3, COLOR_BLACK)

    -- Draw Regular Foods
    for _, f in ipairs(foods) do
        gui.fillRoundedRect(bx + f.x * GRID_SIZE + 5, by + f.y * GRID_SIZE + 5, GRID_SIZE - 10, GRID_SIZE - 10, 4, COLOR_BLACK)
    end

    -- Draw Special Food
    if specialFood.active then
        local cx = bx + specialFood.x * GRID_SIZE + math.floor(GRID_SIZE / 2)
        local cy = by + specialFood.y * GRID_SIZE + math.floor(GRID_SIZE / 2)
        local r = math.floor(GRID_SIZE / 2) - 3
        gui.fillCircle(cx, cy, r, COLOR_BLACK)
        gui.drawCircle(cx, cy, r - 4, 2, COLOR_WHITE)
    end

    -- Draw Snake
    for i, segment in ipairs(snake) do
        local sx = bx + segment.x * GRID_SIZE
        local sy = by + segment.y * GRID_SIZE
        if i == 1 then
            -- Larger, more distinct head
            gui.fillRoundedRect(sx - 1, sy - 1, GRID_SIZE + 2, GRID_SIZE + 2, 8, COLOR_BLACK)
            -- Small eyes for the head
            local eyeSize = 3
            gui.fillCircle(sx + 8, sy + 10, eyeSize, COLOR_WHITE)
            gui.fillCircle(sx + 22, sy + 10, eyeSize, COLOR_WHITE)
        else
            gui.drawRoundedRect(sx + 3, sy + 3, GRID_SIZE - 6, GRID_SIZE - 6, 2, 4, COLOR_BLACK)
        end
    end

    if isGameOver then
        local mw, mh = 360, 320
        local mx, my = (gui.width() - mw) / 2, (gui.height() - mh) / 2
        gui.fillRoundedRect(mx, my, mw, mh, 15, COLOR_WHITE)
        gui.drawRoundedRect(mx, my, mw, mh, 3, 15, COLOR_BLACK)

        -- Helper for centered text
        local function drawTextCentered(font, y, text, color, style)
            local tw = gui.getTextWidth and gui.getTextWidth(font, text) or (#text * 10)
            gui.drawText(font, math.floor((gui.width() - tw) / 2), y, text, color, style)
        end

        drawTextCentered(FONT_BOOKERLY_14, my + 35, "GAME OVER", COLOR_BLACK, STYLE_BOLD)
        drawTextCentered(FONT_BOOKERLY_12, my + 75, gameOverMsg, COLOR_BLACK, STYLE_BOLD)
        drawTextCentered(FONT_BOOKERLY_12, my + 115, "Final Score: " .. score, COLOR_BLACK, STYLE_REGULAR)
        
        -- Buttons: Restart | Exit
        local opts = { "Restart Game", "Exit to Menu" }
        for i, opt in ipairs(opts) do
            local ry = my + 170 + (i - 1) * 65
            local tw = gui.getTextWidth(FONT_BOOKERLY_12, opt, STYLE_REGULAR)
            local tx = mx + math.floor((mw - tw) / 2)
            if gameOverMenuIdx == i - 1 then
                gui.fillRoundedRect(mx + 30, ry - 5, mw - 60, 50, 10, COLOR_BLACK)
                gui.drawText(FONT_BOOKERLY_12, tx, ry + 10, opt, COLOR_WHITE, STYLE_BOLD)
            else
                gui.drawRoundedRect(mx + 30, ry - 5, mw - 60, 50, 2, 10, COLOR_BLACK)
                gui.drawText(FONT_BOOKERLY_12, tx, ry + 10, opt, COLOR_BLACK, STYLE_REGULAR)
            end
        end
    end

    gui.drawButtonHints("<<", "o", "<", ">")
    if isGameOver then
        gui.refresh(REFRESH_HALF)
        hasRefreshedGameOver = true
    else
        gui.refresh(REFRESH_FAST)
    end
end

-- ── Lifecycle ─────────────────────────────────────────────────────────────────

function init()
    math.randomseed(sys.millis())
    resetGame()
end

function draw()
    -- Input Handling
    if not isGameOver then
        local d = nil
        if input.wasPressed("left") then d = {x = -1, y = 0}
        elseif input.wasPressed("right") then d = {x = 1, y = 0}
        elseif input.wasPressed("page_back") then d = {x = 0, y = -1} 
        elseif input.wasPressed("page_forward") then d = {x = 0, y = 1} 
        end

        if d and #inputQueue < 2 then
            table.insert(inputQueue, d)
            needsDraw = true 
        end
    else
        if input.wasPressed("page_back") or input.wasPressed("left") then
            gameOverMenuIdx = (gameOverMenuIdx > 0) and gameOverMenuIdx - 1 or 1
            needsDraw = true
        elseif input.wasPressed("page_forward") or input.wasPressed("right") then
            gameOverMenuIdx = (gameOverMenuIdx + 1) % 2
            needsDraw = true
        elseif input.wasPressed("confirm") then
            if gameOverMenuIdx == 0 then
                resetGame()
            else
                sys.exit()
            end
        end
    end

    if input.wasReleased("back") then
        sys.exit()
    end

    update()

    if needsDraw or (isGameOver and not hasRefreshedGameOver) then
        render()
        needsDraw = false
        -- If game just ended, add a small delay to prevent rapid loops
        if isGameOver then sys.delay(100) end
    end
end
