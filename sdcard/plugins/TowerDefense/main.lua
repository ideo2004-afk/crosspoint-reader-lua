-- TowerDefense Prototype for XTEINK X4
-- DESCRIPTION: Simple tower defense clone where shapes fall from the top.
-- USES Solid Filled Shapes for maximum visibility on E-ink screens.

-- ── Constants ─────────────────────────────────────────────────────────────────
local STATE_START = 0
local STATE_PLAYING = 1
local STATE_GAMEOVER = 2

local SHAPE_SIZE = 40
local SPAWN_INTERVAL = 1500 -- ms
local LIFE_INITIAL = 20
local TICK_INTERVAL = 300   -- ms (Adjusted for stable E-ink refresh)

-- Speeds in PIXELS PER TICK (every 300ms)
local SHAPE_TYPES = {
    { name = "Triangle", speed = 15, score = 10 },
    { name = "Square",   speed = 20, score = 15 },
    { name = "Circle",   speed = 25, score = 20 },
    { name = "Star",     speed = 30, score = 25 },
    { name = "Diamond",  speed = 35, score = 30 }
}

-- ── Global State ──────────────────────────────────────────────────────────────
local gameState = STATE_START
local life = LIFE_INITIAL
local score = 0
local entities = {} -- list of {x, y, type_idx, speed}
local lastTickTime = 0
local lastSpawnTime = 0
local boardW, boardH = 480, 800 
local needsDraw = true
local hasRefreshedStart = false

-- ── Drawing Helpers (Solid Shapes) ───────────────────────────────────────────

local function fillTriangle(cx, cy, size, color)
    local h = size * 0.866
    -- Draw multiple lines to simulate a fill
    for i = 0, size/2, 2 do
        local curr_s = size - (i * 2)
        local curr_h = curr_s * 0.866
        local x1, y1 = cx, cy - curr_h/2 + i*0.5
        local x2, y2 = cx - curr_s/2, cy + curr_h/2 - i*0.5
        local x3, y3 = cx + curr_s/2, cy + curr_h/2 - i*0.5
        gui.drawLine(x1, y1, x2, y2, 2, color)
        gui.drawLine(x2, y2, x3, y3, 2, color)
        gui.drawLine(x3, y3, x1, y1, 2, color)
    end
end

local function fillDiamond(cx, cy, size, color)
    for i = 0, size/2, 2 do
        local s = size - i*2
        local x1, y1 = cx, cy - s/2
        local x2, y2 = cx + s/2, cy
        local x3, y3 = cx, cy + s/2
        local x4, y4 = cx - s/2, cy
        gui.drawLine(x1, y1, x2, y2, 2, color)
        gui.drawLine(x2, y2, x3, y3, 2, color)
        gui.drawLine(x3, y3, x4, y4, 2, color)
        gui.drawLine(x4, y4, x1, y1, 2, color)
    end
end

local function fillStar(cx, cy, r, color)
    local PI = 3.14159
    -- Draw 3-4 nested stars to make it look solid
    for offset = 0, r/2, 4 do
        local curr_r = r - offset
        local points = {}
        for i = 0, 9 do
            local angle = (i * 36 - 90) * (PI / 180)
            local rad = (i % 2 == 0) and curr_r or (curr_r / 2)
            table.insert(points, {x = cx + math.cos(angle) * rad, y = cy + math.sin(angle) * rad})
        end
        for i = 1, #points do
            local next_i = (i % #points) + 1
            gui.drawLine(points[i].x, points[i].y, points[next_i].x, points[next_i].y, 2, color)
        end
    end
end

-- ── Logic ─────────────────────────────────────────────────────────────────────

local function spawnShape()
    local type_idx = math.random(1, #SHAPE_TYPES)
    local t = SHAPE_TYPES[type_idx]
    local margin = 60
    local x = math.random(margin, boardW - margin)
    local y = -20
    table.insert(entities, {
        x = x,
        y = y,
        type_idx = type_idx,
        speed = t.speed
    })
end

local function resetGame()
    boardW = gui.width() > 0 and gui.width() or 480
    boardH = gui.height() > 0 and gui.height() or 800
    
    life = LIFE_INITIAL
    score = 0
    entities = {}
    gameState = STATE_PLAYING
    
    local now = sys.millis()
    lastTickTime = now
    lastSpawnTime = now
    needsDraw = true
    spawnShape() 
end

local function update()
    if gameState ~= STATE_PLAYING then return end
    local now = sys.millis()
    
    -- Spawn logic
    if now - lastSpawnTime > SPAWN_INTERVAL then
        spawnShape()
        lastSpawnTime = now
    end

    -- Movement logic
    for i = #entities, 1, -1 do
        local e = entities[i]
        e.y = e.y + e.speed
        
        -- Boundary check
        if e.y > boardH then
            table.remove(entities, i)
            life = life - 1
            if life <= 0 then
                life = 0
                gameState = STATE_GAMEOVER
                needsDraw = true
            end
        end
    end
end

-- ── Rendering ─────────────────────────────────────────────────────────────────

local function renderStart()
    gui.clear()
    local cx, cy = boardW / 2, boardH / 2
    gui.drawText(FONT_BOOKERLY_14, cx - 80, cy - 100, "Tower Defense", COLOR_BLACK, STYLE_BOLD)
    local bw, bh = 220, 70
    gui.drawRoundedRect(cx - bw/2, cy - 20, bw, bh, 3, 15, COLOR_BLACK)
    gui.drawText(FONT_BOOKERLY_14, cx - 35, cy + 5, "START", COLOR_BLACK, STYLE_BOLD)
    gui.drawText(FONT_BOOKERLY_12, cx - 110, cy + 100, "Press CONFIRM to Play", COLOR_BLACK, STYLE_REGULAR)
    gui.refresh(REFRESH_HALF)
    hasRefreshedStart = true
end

local function renderGame()
    gui.clear()
    -- HUD
    gui.drawText(FONT_BOOKERLY_14, 20, 20, "LIFE: " .. life, COLOR_BLACK, STYLE_BOLD)
    gui.drawText(FONT_BOOKERLY_12, boardW - 150, 25, "SCORE: " .. score, COLOR_BLACK, STYLE_REGULAR)
    gui.drawLine(0, 70, boardW, 70, 2, COLOR_BLACK)
    gui.drawLine(0, boardH - 2, boardW, boardH - 2, 2, COLOR_BLACK) 
    
    -- Entities (Solid Filled)
    for _, e in ipairs(entities) do
        local t = SHAPE_TYPES[e.type_idx]
        if t.name == "Triangle" then 
            fillTriangle(e.x, e.y, SHAPE_SIZE, COLOR_BLACK)
        elseif t.name == "Square" then 
            gui.fillRect(e.x - SHAPE_SIZE/2, e.y - SHAPE_SIZE/2, SHAPE_SIZE, SHAPE_SIZE, COLOR_BLACK)
        elseif t.name == "Circle" then 
            gui.fillCircle(e.x, e.y, SHAPE_SIZE/2, COLOR_BLACK)
        elseif t.name == "Star" then 
            fillStar(e.x, e.y, SHAPE_SIZE/2, COLOR_BLACK)
        elseif t.name == "Diamond" then 
            fillDiamond(e.x, e.y, SHAPE_SIZE, COLOR_BLACK)
        end
    end
    gui.refresh(REFRESH_FAST)
end

local function renderGameOver()
    local mw, mh = 360, 240
    local mx, my = (boardW - mw) / 2, (boardH - mh) / 2
    gui.fillRoundedRect(mx, my, mw, mh, 15, COLOR_WHITE)
    gui.drawRoundedRect(mx, my, mw, mh, 3, 15, COLOR_BLACK)
    gui.drawText(FONT_BOOKERLY_14, mx + 90, my + 50, "GAME OVER", COLOR_BLACK, STYLE_BOLD)
    gui.drawText(FONT_BOOKERLY_12, mx + 110, my + 110, "Score: " .. score, COLOR_BLACK, STYLE_REGULAR)
    gui.drawText(FONT_BOOKERLY_12, mx + 40, my + 170, "Press CONFIRM to Restart", COLOR_BLACK, STYLE_REGULAR)
    gui.refresh(REFRESH_HALF)
    needsDraw = false
end

-- ── Lifecycle ─────────────────────────────────────────────────────────────────

function init()
    math.randomseed(sys.millis())
    gameState = STATE_START
    hasRefreshedStart = false
end

function draw()
    local now = sys.millis()
    
    if input.wasPressed("confirm") then
        if gameState == STATE_START or gameState == STATE_GAMEOVER then
            resetGame()
            return
        end
    end
    if input.wasReleased("back") then sys.exit() end
    
    -- State Machine
    if gameState == STATE_START then
        if not hasRefreshedStart then renderStart() end
    elseif gameState == STATE_PLAYING then
        if now - lastTickTime >= TICK_INTERVAL then
            update()
            renderGame()
            lastTickTime = now
            -- Add a small delay to let the screen update finish
            sys.delay(50)
        end
    elseif gameState == STATE_GAMEOVER then
        if needsDraw then renderGameOver() end
    end
    
    sys.delay(10)
end
