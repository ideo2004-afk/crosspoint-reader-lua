-- X4 Tower Defense Plugin
-- Character-based tower defense optimized for E-ink
-- Grid: 24 columns x 36 rows

-- ── Constants ─────────────────────────────────────────────────────────────────
local GRID_COLS = 24
local GRID_ROWS = 36
local MARGIN_TOP = 100
local MARGIN_SIDE = 0
local CELL_W = 20
local CELL_H = math.floor((800 - MARGIN_TOP - 40) / GRID_ROWS) -- ~19px

-- ── Game State ────────────────────────────────────────────────────────────────
local grid = {} -- 0: empty, 1: tower
local cursor = {x = 12, y = 18}
local gold = 100
local lives = 20
local score = 0
local isRunning = false -- false: Build Mode, true: Run Mode
local firstDraw = true
local lastRefreshTime = 0

-- ── Pathfinding (BFS) ────────────────────────────────────────────────────────
local path = {} -- List of {x, y}

local function calculatePath()
    local startNode = {x = 12, y = 1}
    local endNode = {x = 12, y = GRID_ROWS}
    
    local queue = {startNode}
    local visited = {}
    local parent = {}
    
    visited[startNode.y * 100 + startNode.x] = true
    
    local found = false
    local head = 1
    while head <= #queue do
        local curr = queue[head]
        head = head + 1
        
        if curr.x == endNode.x and curr.y == endNode.y then
            found = true
            break
        end
        
        -- Neighbors: Up, Down, Left, Right
        local dirs = {{0,1}, {0,-1}, {1,0}, {-1,0}}
        for _, d in ipairs(dirs) do
            local nx, ny = curr.x + d[1], curr.y + d[2]
            if nx >= 1 and nx <= GRID_COLS and ny >= 1 and ny <= GRID_ROWS then
                local key = ny * 100 + nx
                if not visited[key] and grid[ny][nx] ~= 1 then
                    visited[key] = true
                    parent[key] = curr
                    table.insert(queue, {x = nx, y = ny})
                end
            end
        end
    end
    
    if found then
        local newPath = {}
        local curr = endNode
        while curr do
            table.insert(newPath, 1, curr)
            curr = parent[curr.y * 100 + curr.x]
        end
        path = newPath
        return true
    end
    return false
end

-- ── Game Logic ────────────────────────────────────────────────────────────────
local creeps = {}
local towers = {} -- {x, y, range, damage, cooldown, lastFire}
local spawnTimer = 0
local wave = 1

local function spawnEnemy()
    table.insert(creeps, {
        hp = 10 + wave * 5,
        maxHp = 10 + wave * 5,
        pathIdx = 1,
        x = path[1].x,
        y = path[1].y,
        lastMove = sys.millis()
    })
end

local function updateGame()
    local now = sys.millis()
    local changed = false
    
    -- 1. Spawning
    if isRunning and now - spawnTimer > 2000 then
        spawnEnemy()
        spawnTimer = now
        changed = true
    end
    
    -- 2. Enemy Movement
    for i = #creeps, 1, -1 do
        local c = creeps[i]
        if now - c.lastMove > 500 then
            c.pathIdx = c.pathIdx + 1
            if c.pathIdx <= #path then
                c.x = path[c.pathIdx].x
                c.y = path[c.pathIdx].y
                c.lastMove = now
                changed = true
            else
                -- Reached exit
                lives = lives - 1
                table.remove(creeps, i)
                changed = true
            end
        end
    end
    
    -- 3. Tower Combat
    for _, t in ipairs(towers) do
        if now - t.lastFire > t.cooldown then
            -- Find target
            for i = #creeps, 1, -1 do
                local c = creeps[i]
                local dist = math.abs(c.x - t.x) + math.abs(c.y - t.y)
                if dist <= t.range then
                    c.hp = c.hp - t.damage
                    t.lastFire = now
                    changed = true
                    if c.hp <= 0 then
                        gold = gold + 2
                        score = score + 10
                        table.remove(creeps, i)
                    end
                    break -- Single target
                end
            end
        end
    end
    
    if lives <= 0 then
        isRunning = false
        changed = true
    end
    return changed
end

-- ── Initialization ────────────────────────────────────────────────────────────
function init()
    math.randomseed(sys.millis())
    grid = {}
    for y = 1, GRID_ROWS do
        grid[y] = {}
        for x = 1, GRID_COLS do
            grid[y][x] = 0
        end
    end

    for x = 11, 14 do
        grid[1][x] = 2
        grid[GRID_ROWS][x] = 2
    end

    isRunning = false
    firstDraw = true
    lastRefreshTime = 0
    creeps = {}
    towers = {}
    gold = 100
    lives = 20
    score = 0
    calculatePath()
    spawnTimer = 0
end

-- ── Rendering ─────────────────────────────────────────────────────────────────
local function render()
    gui.clear()

    gui.drawText(FONT_BOOKERLY_14, 20, 20, "TD: XTEINK Edition", COLOR_BLACK, STYLE_BOLD)
    local stats = string.format("Gold: %d  Lives: %d  Score: %d", gold, lives, score)
    gui.drawText(FONT_BOOKERLY_12, 20, 55, stats, COLOR_BLACK, STYLE_REGULAR)
    
    local modeText = isRunning and "[ RUNNING ]" or "[ BUILD MODE ]"
    gui.drawText(FONT_BOOKERLY_12, gui.width() - 150, 20, modeText, COLOR_BLACK, STYLE_BOLD)
    gui.drawLine(0, 85, gui.width(), 85, 2, COLOR_BLACK)

    local startY = MARGIN_TOP
    for y = 1, GRID_ROWS do
        for x = 1, GRID_COLS do
            local px = (x - 1) * CELL_W
            local py = startY + (y - 1) * CELL_H
            local cellType = grid[y][x]
            
            if cellType == 1 then
                gui.fillRect(px + 1, py + 1, CELL_W - 2, CELL_H - 2, COLOR_BLACK)
            elseif cellType == 2 then
                gui.drawRect(px + 1, py + 1, CELL_W - 2, CELL_H - 2, 2, COLOR_BLACK)
                local char = (y == 1) and "v" or "^"
                gui.drawText(FONT_NOTOSANS_12, px + 5, py + 1, char, COLOR_BLACK, STYLE_BOLD)
            else
                gui.fillRect(px + CELL_W/2 - 1, py + CELL_H/2 - 1, 2, 2, COLOR_BLACK)
            end
            
            if not isRunning and cursor.x == x and cursor.y == y then
                gui.drawRect(px, py, CELL_W, CELL_H, 2, COLOR_BLACK)
            end
        end
    end
    
    -- Draw Creeps
    for _, c in ipairs(creeps) do
        local px = (c.x - 1) * CELL_W
        local py = startY + (c.y - 1) * CELL_H
        gui.drawText(FONT_NOTOSANS_12, px + 4, py, "&", COLOR_BLACK, STYLE_BOLD)
    end

    gui.drawButtonHints("<<", "o", "<", ">")

    local refreshMode = firstDraw and REFRESH_HALF or REFRESH_FAST
    gui.refresh(refreshMode)
    firstDraw = false
    lastRefreshTime = sys.millis()
end

-- ── Main Loop ─────────────────────────────────────────────────────────────────
function draw()
    local now = sys.millis()
    local needsRedraw = false

    -- Mode Switching (Back key)
    if input.wasPressed("back") then
        isRunning = not isRunning
        needsRedraw = true
        if isRunning then spawnTimer = now - 2000 end -- Spawn one immediately
    end

    if not isRunning then
        -- Vertical Movement (including side buttons)
        if input.wasPressed("up") or input.wasPressed("page_back") then 
            cursor.y = math.max(1, cursor.y - 1)
            needsRedraw = true 
        end
        if input.wasPressed("down") or input.wasPressed("page_forward") then 
            cursor.y = math.min(GRID_ROWS, cursor.y + 1)
            needsRedraw = true 
        end
        
        -- Horizontal Movement
        if input.wasPressed("left") then cursor.x = math.max(1, cursor.x - 1); needsRedraw = true end
        if input.wasPressed("right") then cursor.x = math.min(GRID_COLS, cursor.x + 1); needsRedraw = true end
        
        -- Place/Remove Tower
        if input.wasPressed("confirm") then
            local current = grid[cursor.y][cursor.x]
            if current == 0 and gold >= 10 then
                grid[cursor.y][cursor.x] = 1
                if calculatePath() then
                    table.insert(towers, {x = cursor.x, y = cursor.y, range = 3, damage = 5, cooldown = 1000, lastFire = 0})
                    gold = gold - 10
                else
                    grid[cursor.y][cursor.x] = 0 -- Revert if blocks path
                end
            elseif current == 1 then
                grid[cursor.y][cursor.x] = 0
                for i, t in ipairs(towers) do
                    if t.x == cursor.x and t.y == cursor.y then table.remove(towers, i); break end
                end
                gold = gold + 5
                calculatePath()
            end
            needsRedraw = true
        end
    end

    if isRunning then
        if updateGame() then
            needsRedraw = true
        end
    end

    -- Dynamic refresh rate: faster when running, slower when building
    local refreshInterval = isRunning and 500 or 1000
    if needsRedraw or (now - lastRefreshTime >= refreshInterval) or firstDraw then
        render()
    end
end
