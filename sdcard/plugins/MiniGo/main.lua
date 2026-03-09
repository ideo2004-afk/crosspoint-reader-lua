-- ABBA Go (MiniGo) - Full Lua Port
-- DESCRIPTION: Full Go game with MCTS AI.
-- VERSION: 2026-03-09 "Zhuge Liang" Evolved

local EMPTY = 0
local BLACK = 1
local WHITE = 2

-- Score Config
local KOMI = 7.5

-- AI Performance Config
local AI_MAX_SIMULATIONS = 400
local AI_ROLLOUT_DEPTH   = 4
local AI_EXPLORATION_C   = 1.414

-- Tactical Heuristics
local H_RESCUE_1LIB      = 0.250
local H_RESCUE_2LIB      = 0.100
local H_CAPTURE_BONUS    = 0.300
local H_BLOCK_EXTENSION  = 0.120
local H_CUT_BIAS         = 0.130
local H_BLOCK_BASE       = 0.050
local H_EXPANSION_BIAS   = 0.050
local H_CENTRAL_BIAS     = 0.050
local H_CONNECT_BASE     = 0.020

-- ── Mumbles ───────────────────────────────────────────────────────────────────

local aiMumbles = {
    "Let me think... This move is interesting.",
    "Calculating 420,000 possibilities...",
    "Is this a trap?",
    "I've seen this joseki in 19x19 books.",
    "My Monte Carlo algorithm is burning!"
}

local playerMumbles = {
    "Your turn, human.",
    "Take your time. I've already won.",
    "Are you intimidated by my 200 sims?",
    "Go ahead, make my day.",
    "I calculated 200 ways you lose from here."
}

-- ── Game State ────────────────────────────────────────────────────────────────

local boardSize = 7
local board = {}
local lastBoard = {}
local consecutivePasses = 0
local status = "playing" -- "playing", "won", "lost", "draw"
local playerColor = WHITE
local aiColor = BLACK
local cursorX, cursorY = 3, 3
local lastMoveX, lastMoveY = -1, -1
local lastMovePass = false
local aiSimulationsDone = 0
local isAiThinking = false
local aiThinkStartTime = 0
local resultsCached = false
local blackScoreCache = 0
local whiteScoreCache = 0

-- UI State
local showSizeSelection = true
local sizeIdx = 0
local showHandicapSelection = false
local handicapIdx = 0
local inEscMenu = false
local escIdx = 0
local postMenuIdx = 0
local aiMumbleIdx = -1
local playerMumbleIdx = -1
local needsDraw = true

-- ── Engine Helpers ────────────────────────────────────────────────────────────

function getAt(x, y, b)
    if x < 0 or x >= boardSize or y < 0 or y >= boardSize then return nil end
    return b[y * boardSize + x + 1]
end

function setAt(x, y, v, b)
    b[y * boardSize + x + 1] = v
end

function copyBoard(src)
    local dest = {}
    for i = 1, boardSize * boardSize do 
        dest[i] = src[i] 
    end
    return dest
end

function copyBoardTo(src, dest)
    for i = 1, boardSize * boardSize do 
        dest[i] = src[i] 
    end
end

-- ── Go Logic ──────────────────────────────────────────────────────────────────

local g_visited = {}
local g_stack_x = {}
local g_stack_y = {}
local g_stones_x = {}
local g_stones_y = {}

function getGroup(x, y, b)
    local color = b[y * boardSize + x + 1]
    if color == EMPTY then return { stones = {}, liberties = 0 } end
    
    local liberties = 0
    local stonesCount = 0
    
    -- Reset globals for this call (up to max board size 81)
    for i = 1, boardSize * boardSize do g_visited[i] = false end
    
    local startIdx = y * boardSize + x + 1
    g_visited[startIdx] = true
    
    local stackTop = 1
    g_stack_x[1] = x
    g_stack_y[1] = y
    
    local dx = {0, 0, 1, -1}
    local dy = {1, -1, 0, 0}
    
    while stackTop > 0 do
        local currX = g_stack_x[stackTop]
        local currY = g_stack_y[stackTop]
        stackTop = stackTop - 1
        
        stonesCount = stonesCount + 1
        g_stones_x[stonesCount] = currX
        g_stones_y[stonesCount] = currY
        
        for i = 1, 4 do
            local nx, ny = currX + dx[i], currY + dy[i]
            if nx >= 0 and nx < boardSize and ny >= 0 and ny < boardSize then
                local idx = ny * boardSize + nx + 1
                if not g_visited[idx] then
                    local c = b[idx]
                    if c == EMPTY then
                        g_visited[idx] = true
                        liberties = liberties + 1
                    elseif c == color then
                        g_visited[idx] = true
                        stackTop = stackTop + 1
                        g_stack_x[stackTop] = nx
                        g_stack_y[stackTop] = ny
                    end
                end
            end
        end
    end
    
    -- copy the results into a new table to return since g_stones is reused
    local result = { stones = {}, liberties = liberties }
    for i = 1, stonesCount do
        result.stones[i] = {g_stones_x[i], g_stones_y[i]}
    end
    return result
end

function captureStonesFast(x, y, color, b)
    local opponent = (color == BLACK) and WHITE or BLACK
    local totalCaptured = 0
    local dx = {0, 0, 1, -1}
    local dy = {1, -1, 0, 0}
    
    for i = 1, 4 do
        local nx, ny = x + dx[i], y + dy[i]
        local idx = ny * boardSize + nx + 1
        if nx >= 0 and nx < boardSize and ny >= 0 and ny < boardSize and b[idx] == opponent then
            local g = getGroup(nx, ny, b)
            if g.liberties == 0 then
                for _, s in ipairs(g.stones) do
                    b[s[2] * boardSize + s[1] + 1] = EMPTY
                end
                totalCaptured = totalCaptured + #g.stones
            end
        end
    end
    return totalCaptured
end

function captureStones(x, y, color, b)
    return captureStonesFast(x, y, color, b)
end

function wouldBeSuicide(x, y, color, b)
    -- Instead of full copy, make temp modification
    local startIdx = y * boardSize + x + 1
    local orig = b[startIdx]
    b[startIdx] = color
    
    -- First check if it captures anything
    local opponent = (color == BLACK) and WHITE or BLACK
    local dx = {0, 0, 1, -1}
    local dy = {1, -1, 0, 0}
    local captured = false
    
    for i = 1, 4 do
        local nx, ny = x + dx[i], y + dy[i]
        if nx >= 0 and nx < boardSize and ny >= 0 and ny < boardSize then
            if b[ny * boardSize + nx + 1] == opponent then
                local g = getGroup(nx, ny, b)
                if g.liberties == 0 then
                    captured = true
                    break
                end
            end
        end
    end
    
    if captured then
        b[startIdx] = orig
        return false
    end
    
    -- Then check if group has liberties
    local g = getGroup(x, y, b)
    b[startIdx] = orig
    
    return g.liberties == 0
end

function isValidMove(x, y, color)
    if getAt(x, y, board) ~= EMPTY then return false end
    
    -- Special rule: first move can't be center (optional, match C++)
    local movedCount = 0
    for i=1, #board do if board[i] ~= EMPTY then movedCount = movedCount + 1 end end
    if movedCount == 0 and x == math.floor(boardSize/2) and y == math.floor(boardSize/2) then return false end
    
    if wouldBeSuicide(x, y, color, board) then return false end
    
    -- Ko check
    local testB = copyBoard(board)
    setAt(x, y, color, testB)
    captureStones(x, y, color, testB)
    
    local isKo = true
    for i = 1, #testB do
        if testB[i] ~= lastBoard[i] then
            isKo = false
            break
        end
    end
    return not isKo
end

function calculateScore(color)
    local score = 0
    local visited = {}
    for i = 0, boardSize*boardSize - 1 do
        local x = i % boardSize
        local y = math.floor(i / boardSize)
        local c = board[i+1]
        
        if c == color then
            score = score + 1
        elseif c == EMPTY and not visited[i+1] then
            local region = {}
            local stack = { {x, y} }
            visited[i+1] = true
            local touchesBlack = false
            local touchesWhite = false
            local regionCount = 0
            
            while #stack > 0 do
                local curr = table.remove(stack)
                regionCount = regionCount + 1
                local cx, cy = curr[1], curr[2]
                local dx = {0, 0, 1, -1}
                local dy = {1, -1, 0, 0}
                for j = 1, 4 do
                    local nx, ny = cx + dx[j], cy + dy[j]
                    if nx >= 0 and nx < boardSize and ny >= 0 and ny < boardSize then
                        local nidx = ny * boardSize + nx + 1
                        local nc = board[nidx]
                        if nc == BLACK then touchesBlack = true
                        elseif nc == WHITE then touchesWhite = true
                        elseif nc == EMPTY and not visited[nidx] then
                            visited[nidx] = true
                            table.insert(stack, {nx, ny})
                        end
                    end
                end
            end
            
            if touchesBlack and not touchesWhite and color == BLACK then
                score = score + regionCount
            elseif touchesWhite and not touchesBlack and color == WHITE then
                score = score + regionCount
            end
        end
    end
    if color == WHITE then score = score + KOMI end
    return score
end

-- ── AI Engine (MCTS) ──────────────────────────────────────────────────────────

function calculateInfluence(b)
    local influence = {}
    for i = 1, boardSize * boardSize do influence[i] = 0 end
    
    local groupLibertiesCache = {}
    for i = 1, boardSize * boardSize do groupLibertiesCache[i] = -1 end

    for i = 1, boardSize * boardSize do
        if b[i] ~= EMPTY and groupLibertiesCache[i] == -1 then
            local x, y = (i - 1) % boardSize, math.floor((i - 1) / boardSize)
            local g = getGroup(x, y, b)
            for _, s in ipairs(g.stones) do
                groupLibertiesCache[s[2] * boardSize + s[1] + 1] = g.liberties
            end
        end
    end

    local dx = {0, 0, 1, -1}
    local dy = {1, -1, 0, 0}
    local dx2 = {0, 0, 1, -1, 1, 1, -1, -1}
    local dy2 = {1, -1, 0, 0, 1, -1, 1, -1}

    for i = 1, boardSize * boardSize do
        if b[i] ~= EMPTY then
            local weight = 2.5
            local libs = groupLibertiesCache[i]
            if libs == 2 then weight = 0.8
            elseif libs == 1 then weight = -1.5 end
            
            local val = (b[i] == BLACK) and weight or -weight
            influence[i] = influence[i] + val
            
            local x, y = (i - 1) % boardSize, math.floor((i - 1) / boardSize)
            for k = 1, 4 do
                local nx, ny = x + dx[k], y + dy[k]
                if nx >= 0 and nx < boardSize and ny >= 0 and ny < boardSize then
                    local nidx = ny * boardSize + nx + 1
                    influence[nidx] = influence[nidx] + (val > 0 and 2 or -2)
                    for k2 = 1, 8 do
                        local nnx, nny = nx + dx2[k2], ny + dy2[k2]
                        if nnx >= 0 and nnx < boardSize and nny >= 0 and nny < boardSize and (nnx ~= x or nny ~= y) then
                            local nnidx = nny * boardSize + nnx + 1
                            influence[nnidx] = influence[nnidx] + (val > 0 and 1 or -1)
                        end
                    end
                end
            end
        end
    end

    local bScore, wScore = 0, KOMI
    for i = 1, boardSize * boardSize do
        if influence[i] > 1 then bScore = bScore + 1
        elseif influence[i] < -1 then wScore = wScore + 1 end
    end
    return bScore - wScore
end

function createNode(move, parent, playerToMove)
    return {
        move = move, -- {x, y, pass}
        visits = 0,
        wins = 0,
        children = {},
        parent = parent,
        playerToMove = playerToMove
    }
end

function selectBestChild(node, exploration)
    local bestChild = nil
    local bestScore = -1e18
    local logVisits = math.log(node.visits > 0 and node.visits or 1)
    
    for _, child in ipairs(node.children) do
        local score
        if exploration == 0 then
            score = child.visits
        elseif child.visits == 0 then
            score = 10000.0
        else
            score = (child.wins / child.visits) + exploration * math.sqrt(logVisits / child.visits)
        end

        -- Zhuge Liang Heuristics for Root
        if not node.parent then
            if not child.move.pass then
                if not child.hScoreCalculated then
                    child.hScoreCalculated = true
                    local connectivityBias = 0
                    local expansionBias = 0
                    local captureBonus = 0
                    local blockingBias = 0
                    local cutBias = 0
                    
                    local x, y = child.move.x, child.move.y
                    local opponentNeighbors = 0
                    local dx = {0, 0, 1, -1}
                    local dy = {1, -1, 0, 0}
                    
                    for k = 1, 4 do
                        local nx, ny = x + dx[k], y + dy[k]
                        if nx >= 0 and nx < boardSize and ny >= 0 and ny < boardSize then
                            local neighbor = getAt(nx, ny, board)
                            if neighbor == node.playerToMove then
                                local libs = getGroup(nx, ny, board).liberties
                                if libs == 1 then connectivityBias = connectivityBias + H_RESCUE_1LIB
                                elseif libs == 2 then connectivityBias = connectivityBias + H_RESCUE_2LIB
                                else connectivityBias = connectivityBias + H_CONNECT_BASE end
                            elseif neighbor == EMPTY then
                                expansionBias = expansionBias + H_EXPANSION_BIAS
                            else
                                opponentNeighbors = opponentNeighbors + 1
                                blockingBias = blockingBias + H_BLOCK_BASE
                                
                                local testB = copyBoard(board)
                                setAt(x, y, node.playerToMove, testB)
                                if captureStones(x, y, node.playerToMove, testB) > 0 then
                                    captureBonus = captureBonus + H_CAPTURE_BONUS
                                end
                                
                                for d = 1, 4 do
                                    local nnx, nny = nx + dx[d], ny + dy[d]
                                    if nnx >= 0 and nnx < boardSize and nny >= 0 and nny < boardSize and (nnx ~= x or nny ~= y) then
                                        if getAt(nnx, nny, board) == neighbor then
                                            blockingBias = blockingBias + H_BLOCK_EXTENSION
                                            break
                                        end
                                    end
                                end
                            end
                        end
                    end
                    
                    if opponentNeighbors >= 2 then cutBias = cutBias + H_CUT_BIAS end
                    local distCenter = math.sqrt((x - boardSize/2)^2 + (y - boardSize/2)^2)
                    local centralBias = (boardSize - distCenter) * H_CENTRAL_BIAS
                    child.hScore = connectivityBias + expansionBias + captureBonus + blockingBias + cutBias + centralBias
                end
                
                if exploration == 0 then score = score + child.hScore * 1000.0 else score = score + child.hScore end
            end
        end

        if score > bestScore then
            bestScore = score
            bestChild = child
        end
    end
    return bestChild
end

function rollout(simBoard, toMove)
    local currColor = toMove
    local lastMoverColor = nil
    
    for step = 1, AI_ROLLOUT_DEPTH do
        local validMoves = {}
        local vCount = 0
        for r_y = 0, boardSize - 1 do
            for r_x = 0, boardSize - 1 do
                if getAt(r_x, r_y, simBoard) == EMPTY and not wouldBeSuicide(r_x, r_y, currColor, simBoard) then
                    vCount = vCount + 1
                    validMoves[vCount] = {x = r_x, y = r_y}
                end
            end
        end
        
        if vCount > 0 then
            local rm = validMoves[math.random(vCount)]
            setAt(rm.x, rm.y, currColor, simBoard)
            captureStonesFast(rm.x, rm.y, currColor, simBoard)
            currColor = (currColor == BLACK) and WHITE or BLACK
            if step == AI_ROLLOUT_DEPTH then lastMoverColor = currColor break end
        else
            break
        end
    end
    
    local diff = calculateInfluence(simBoard)
    return (aiColor == BLACK) and (diff > 0 and 1 or 0) or (diff < 0 and 1 or 0)
end

local mctsRoot = nil

function startAiThinking()
    isAiThinking = true
    aiThinkStartTime = sys.millis()
    aiMumbleIdx = math.random(#aiMumbles)
    aiSimulationsDone = 0
    
    local opponent = (aiColor == BLACK) and WHITE or BLACK
    mctsRoot = createNode({pass = true}, nil, aiColor)
    
    -- Get unattempted moves
    local possibleMoves = {}
    local count = 0
    local color = mctsRoot.playerToMove
    for y = 0, boardSize - 1 do
        for x = 0, boardSize - 1 do
            if isValidMove(x, y, color) then
                count = count + 1
                possibleMoves[count] = {x = x, y = y, pass = false}
            end
        end
    end
    count = count + 1
    possibleMoves[count] = {x = -1, y = -1, pass = true}

    for _, move in ipairs(possibleMoves) do
        local child = createNode(move, mctsRoot, opponent)
        -- Center bias for initial visits
        if not move.pass then
            local x, y = move.x, move.y
            local mid = math.floor(boardSize/2)
            if math.abs(x - mid) <= 1 and math.abs(y - mid) <= 1 then
                child.visits = 5
                child.wins = 2.5
                mctsRoot.visits = mctsRoot.visits + 5
            end
        end
        table.insert(mctsRoot.children, child)
    end
end

function runAiStep()
    if aiSimulationsDone < AI_MAX_SIMULATIONS then
        local node = mctsRoot
        local simBoard = copyBoard(board)
        local simPasses = consecutivePasses
        
        -- Selection
        while #node.children > 0 do
            node = selectBestChild(node, AI_EXPLORATION_C)
            if node.move.pass then
                simPasses = simPasses + 1
            else
                setAt(node.move.x, node.move.y, node.playerToMove == BLACK and WHITE or BLACK, simBoard)
                captureStones(node.move.x, node.move.y, node.playerToMove == BLACK and WHITE or BLACK, simBoard)
                simPasses = 0
            end
            if simPasses >= 2 then break end
        end
        
        -- Expansion (if not a terminal node and not fully expanded)
        if #node.children == 0 and simPasses < 2 then
            local color = node.playerToMove
            local opponent = (color == BLACK) and WHITE or BLACK
            local unattempted = {}
            local unattemptedCount = 0

            -- Find unattempted moves
            for y = 0, boardSize - 1 do
                for x = 0, boardSize - 1 do
                    if getAt(x, y, simBoard) == EMPTY and not wouldBeSuicide(x, y, color, simBoard) then
                        local isAttempted = false
                        for _, child in ipairs(node.children) do
                            if not child.move.pass and child.move.x == x and child.move.y == y then
                                isAttempted = true
                                break
                            end
                        end
                        if not isAttempted then
                            unattemptedCount = unattemptedCount + 1
                            unattempted[unattemptedCount] = {x = x, y = y, pass = false}
                        end
                    end
                end
            end
            
            local isPassAttempted = false
            for _, child in ipairs(node.children) do
                if child.move.pass then
                    isPassAttempted = true
                    break
                end
            end
            if not isPassAttempted then
                unattemptedCount = unattemptedCount + 1
                unattempted[unattemptedCount] = {x = -1, y = -1, pass = true}
            end

            if unattemptedCount > 0 then
                local r = math.random(unattemptedCount)
                local move = unattempted[r]
                local child = createNode(move, node, opponent)
                table.insert(node.children, child)
                node = child
                
                -- Make the move on simulation board
                if not move.pass then
                    setAt(move.x, move.y, color, simBoard)
                    captureStonesFast(move.x, move.y, color, simBoard)
                end
            end
        end
        
        -- Rollout
        local result = rollout(simBoard, node.playerToMove)
        
        -- Backpropagate
        local backNode = node
        while backNode do
            backNode.visits = backNode.visits + 1
            backNode.wins = backNode.wins + result
            backNode = backNode.parent
        end
        
        aiSimulationsDone = aiSimulationsDone + 1
        return true
    end
    return false
end

function finishAiThinking()
    local best = selectBestChild(mctsRoot, 0)
    local move = best and best.move or {pass = true}
    
    if best and best.visits > 0 then
        local winRate = best.wins / best.visits
        if winRate < 0.10 then move = {pass = true} end
    end
    
    -- Apply Move
    lastBoard = copyBoard(board)
    if move.pass then
        consecutivePasses = consecutivePasses + 1
        lastMovePass = true
    else
        setAt(move.x, move.y, aiColor, board)
        captureStones(move.x, move.y, aiColor, board)
        lastMoveX, lastMoveY = move.x, move.y
        lastMovePass = false
        consecutivePasses = 0
    end
    
    isAiThinking = false
    playerMumbleIdx = math.random(#playerMumbles)
    
    if consecutivePasses >= 2 then
        status = calculateWinner()
    end
    needsDraw = true
end

function calculateWinner()
    local bScore = calculateScore(BLACK)
    local wScore = calculateScore(WHITE)
    blackScoreCache = bScore
    whiteScoreCache = wScore
    resultsCached = true
    
    if bScore > wScore then
        return (playerColor == BLACK) and "won" or "lost"
    elseif wScore > bScore then
        return (playerColor == WHITE) and "won" or "lost"
    else
        return "draw"
    end
end

-- ── Rendering ─────────────────────────────────────────────────────────────────

local function drawTextWrapped(font, cy, text, isPlayer)
    local sw = gui.width()
    local textW = gui.getTextWidth(font, text)
    if textW > sw - 40 then
        -- Simple split
        local split = math.floor(#text / 2)
        local s1 = string.sub(text, 1, split)
        local s2 = string.sub(text, split + 1)
        gui.drawCenteredText(font, cy - 15, s1, isPlayer)
        gui.drawCenteredText(font, cy + 15, s2, isPlayer)
    else
        gui.drawCenteredText(font, cy, text, isPlayer)
    end
end

function renderBoard()
    gui.clear()

    -- Header
    gui.drawText(FONT_UI_12, 20, 20, "ABBA Go")
    gui.drawLine(0, 60, gui.width(), 60, 3)
    
    local info = boardSize .. "x" .. boardSize
    gui.drawText(FONT_UI_12, 20, 80, info)

    -- Board Geometry
    local sw = gui.width()
    local margin = 60
    local boardDispSize = sw - margin * 2
    local cellSize = math.floor(boardDispSize / (boardSize - 1))
    local startX = margin
    local startY = 200

    -- Grid
    for i = 0, boardSize - 1 do
        local thick = (i == 0 or i == boardSize - 1) and 4 or 2
        -- Horizontal
        local hy = startY + i * cellSize
        gui.drawLine(startX, hy, startX + boardDispSize, hy, thick)
        -- Vertical
        local vx = startX + i * cellSize
        gui.drawLine(vx, startY, vx, startY + boardDispSize, thick)
    end

    -- Star Points
    local function drawStar(sx, sy)
        local px = startX + sx * cellSize
        local py = startY + sy * cellSize
        gui.fillRoundedRect(px - 5, py - 5, 10, 10, 5)
    end

    if boardSize == 7 then drawStar(3, 3)
    elseif boardSize == 9 then
        drawStar(2, 2); drawStar(6, 2)
        drawStar(4, 4)
        drawStar(2, 6); drawStar(6, 6)
    end

    -- Stones
    for y = 0, boardSize - 1 do
        for x = 0, boardSize - 1 do
            local c = getAt(x, y, board)
            if c ~= EMPTY then
                local px = startX + x * cellSize
                local py = startY + y * cellSize
                local r = math.floor(cellSize / 2) - 2
                
                if c == BLACK then
                    gui.fillRoundedRect(px - r, py - r, r * 2, r * 2, r)
                else
                    gui.fillRoundedRect(px - r, py - r, r * 2, r * 2, r, false) -- white
                    gui.drawRoundedRect(px - r, py - r, r * 2, r * 2, 2, r)      -- border
                end

                -- Last Move Marker
                if not lastMovePass and lastMoveX == x and lastMoveY == y then
                    local markerColor = (c == BLACK) -- if black stone, use white marker (wait, C++: boolean markerColor = (c == WHITE);)
                    -- Actually if white stone (c=2), markerColor is true (draw on white surface?)
                    -- Let's match C++: renderer.drawRoundedRect(px - 4, py - 4, 8, 8, 2, 4, markerColor);
                    -- In our gui.drawRoundedRect, last param is white=true
                    gui.drawRoundedRect(px - 4, py - 4, 8, 8, 2, 4, (c == WHITE))
                end
            end
        end
    end

    -- Cursor
    if status == "playing" and not inEscMenu and not showHandicapSelection and not showSizeSelection then
        local cx = startX + cursorX * cellSize
        local cy = startY + cursorY * cellSize
        local r = math.floor(cellSize / 4)
        gui.drawRoundedRect(cx - r, cy - r, r * 2, r * 2, 2, r)
    end

    -- Status Texts
    if isAiThinking then
        gui.drawText(FONT_SMALL, 340, 80, "AI Thinking...")
        if aiMumbleIdx > 0 then
            drawTextWrapped(FONT_UI_12, 640, aiMumbles[aiMumbleIdx], false)
        end
    elseif status == "playing" and not showHandicapSelection and not showSizeSelection and not inEscMenu then
        if lastMovePass then
            gui.drawText(FONT_UI_12, 340, 50, "AI PASS!")
        end
        gui.drawText(FONT_SMALL, 340, 80, "Your Turn (White)")
        if playerMumbleIdx > 0 then
            drawTextWrapped(FONT_UI_12, 640, playerMumbles[playerMumbleIdx], true)
        end
    end

    -- Menus
    if showSizeSelection then renderSizeSelection()
    elseif showHandicapSelection then renderHandicapSelection()
    elseif inEscMenu then renderEscMenu()
    elseif status ~= "playing" then renderResult() end

    -- Button Hints
    if showSizeSelection or showHandicapSelection then
        gui.drawButtonHints("<<", "o", "<", ">")
    elseif inEscMenu then
        gui.drawButtonHints("<<", "o", "<", ">")
    else
        gui.drawButtonHints("<<", "o", "<", ">")
    end

    gui.refresh(REFRESH_FAST)
end

function renderSizeSelection()
    local sw, sh = gui.width(), gui.height()
    local mw, mh = 400, 250
    local mx, my = (sw - mw) / 2, (sh - mh) / 2
    gui.fillRoundedRect(mx, my, mw, mh, 15, false)
    gui.drawRoundedRect(mx, my, mw, mh, 3, 15)
    
    local tx_title = mx + math.floor((mw - gui.getTextWidth(FONT_UI_12, "Select Board Size")) / 2)
    gui.drawText(FONT_UI_12, tx_title, my + 40, "Select Board Size", true)
    
    local labels = {"7 x 7", "9 x 9"}
    for i = 0, 1 do
        local bx = mx + 40 + (i * 180)
        local by = my + 110
        local btnW, btnH = 140, 70
        local label = labels[i+1]
        local tx = bx + math.floor((btnW - gui.getTextWidth(FONT_UI_12, label)) / 2)
        local ty = by + 25
        if sizeIdx == i then
            gui.fillRoundedRect(bx, by, btnW, btnH, 15)
            gui.drawText(FONT_UI_12, tx, ty, label, false)
        else
            gui.drawRoundedRect(bx, by, btnW, btnH, 2, 15)
            gui.drawText(FONT_UI_12, tx, ty, label, true)
        end
    end
end

function renderHandicapSelection()
    local sw, sh = gui.width(), gui.height()
    local mw, mh = 400, 250
    local mx, my = (sw - mw) / 2, (sh - mh) / 2
    gui.fillRoundedRect(mx, my, mw, mh, 15, false)
    gui.drawRoundedRect(mx, my, mw, mh, 3, 15)
    
    local tx_title = mx + math.floor((mw - gui.getTextWidth(FONT_UI_12, "AI Handicap")) / 2)
    gui.drawText(FONT_UI_12, tx_title, my + 30, "AI Handicap", true)
    
    local labels = {"1", "2", "3", "Chaos"}
    for i = 0, 3 do
        local bx = mx + 16 + (i * 96)
        local by = my + 100
        local btnW, btnH = 80, 80
        local label = labels[i+1]
        local tx = bx + math.floor((btnW - gui.getTextWidth(FONT_UI_12, label)) / 2)
        local ty = by + 30
        if handicapIdx == i then
            gui.fillRoundedRect(bx, by, btnW, btnH, 10)
            gui.drawText(FONT_UI_12, tx, ty, label, false)
        else
            gui.drawRoundedRect(bx, by, btnW, btnH, 2, 10)
            gui.drawText(FONT_UI_12, tx, ty, label, true)
        end
    end
end

function renderEscMenu()
    local sw, sh = gui.width(), gui.height()
    local mw, mh = 340, 400
    local mx, my = (sw - mw) / 2, (sh - mh) / 2
    gui.fillRoundedRect(mx, my, mw, mh, 10, false)
    gui.drawRoundedRect(mx, my, mw, mh, 2, 10)
    
    local title = "Game Menu"
    local tx_title = mx + math.floor((mw - gui.getTextWidth(FONT_UI_12, title)) / 2)
    gui.drawText(FONT_UI_12, tx_title, my + 20, title, true)
    
    local opts = {"Resume", "New 7x7", "New 9x9", "Pass Turn", "Exit Game"}
    for i, opt in ipairs(opts) do
        local ry = my + 65 + (i-1) * 60
        local tx = mx + 10 + math.floor((320 - gui.getTextWidth(FONT_UI_12, opt)) / 2)
        if escIdx == i - 1 then
            gui.fillRoundedRect(mx + 10, ry - 5, 320, 50, 8)
            gui.drawText(FONT_UI_12, tx, ry + 12, opt, false)
        else
            gui.drawText(FONT_UI_12, tx, ry + 12, opt, true)
        end
    end
end

function renderResult()
    local sw, sh = gui.width(), gui.height()
    local mw, mh = 400, 200
    local mx, my = (sw - mw) / 2, (sh - mh) / 2
    gui.fillRoundedRect(mx, my, mw, mh, 15, false)
    gui.drawRoundedRect(mx, my, mw, mh, 3, 15)
    
    local resStr = (status == "won") and "VICTORY!" or (status == "draw") and "DRAW." or "DEFEAT."
    local tx_res = mx + math.floor((mw - gui.getTextWidth(FONT_UI_12, resStr)) / 2)
    gui.drawText(FONT_UI_12, tx_res, my + 15, resStr, true)
    
    local scoreStr = string.format("Black:%.1f White:%.1f", blackScoreCache, whiteScoreCache)
    local tx_score = mx + math.floor((mw - gui.getTextWidth(FONT_SMALL, scoreStr)) / 2)
    gui.drawText(FONT_SMALL, tx_score, my + 65, scoreStr, true)
    
    local labels = {"New", "Exit"}
    for i = 0, 1 do
        local bx = mx + 50 + (i * 180)
        local by = my + 115
        local btnW, btnH = 120, 50
        local label = labels[i+1]
        local tx = bx + math.floor((btnW - gui.getTextWidth(FONT_UI_12, label)) / 2)
        local ty = by + 12
        if postMenuIdx == i then
            gui.fillRoundedRect(bx, by, btnW, btnH, 10)
            gui.drawText(FONT_UI_12, tx, ty, label, false)
        else
            gui.drawRoundedRect(bx, by, btnW, btnH, 2, 10)
            gui.drawText(FONT_UI_12, tx, ty, label, true)
        end
    end
end

-- ── Input & Main ──────────────────────────────────────────────────────────────

function handleSizeInput()
    if input.wasReleased("left") or input.wasReleased("right") then
        sizeIdx = (sizeIdx == 0) and 1 or 0
        needsDraw = true
    elseif input.wasReleased("confirm") then
        local size = (sizeIdx == 0) and 7 or 9
        resetGame(size)
        showSizeSelection = false
        showHandicapSelection = true
        needsDraw = true
    elseif input.wasReleased("back") then
        sys.exit()
    end
end

function handleHandicapInput()
    if input.wasReleased("left") or input.wasReleased("right") then
        handicapIdx = (handicapIdx + (input.wasReleased("left") and 3 or 1)) % 4
        needsDraw = true
    elseif input.wasReleased("confirm") then
        showHandicapSelection = false
        if handicapIdx == 3 then
            -- Chaos Mode
            local avail = {}
            for i = 1, boardSize * boardSize do table.insert(avail, i) end
            local function placeRandom(count, color)
                for k = 1, count do
                    if #avail == 0 then break end
                    local r = math.random(#avail)
                    local pos = table.remove(avail, r)
                    local x, y = (pos-1) % boardSize, math.floor((pos-1) / boardSize)
                    setAt(x, y, color, board)
                    lastMoveX, lastMoveY = x, y
                end
            end
            local bCount = (boardSize == 9) and 6 or 4
            placeRandom(bCount, BLACK)
            placeRandom(2, WHITE)
            isAiThinking = false
        else
            -- Classic Handicap
            local count = handicapIdx + 1
            local p1 = 2
            local p2 = (boardSize == 9) and 6 or 4
            if count >= 1 then setAt(p2, p2, BLACK, board) end
            if count >= 2 then setAt(p1, p1, BLACK, board) end
            if count >= 3 then setAt(p2, p1, BLACK, board) end
            lastMoveX, lastMoveY = p2, p2 -- simple marker
            isAiThinking = false
        end
        playerMumbleIdx = math.random(#playerMumbles)
        needsDraw = true
    elseif input.wasReleased("back") then
        showHandicapSelection = false
        showSizeSelection = true
        needsDraw = true
    end
end

function handleEscInput()
    if input.wasReleased("up") or input.wasReleased("left") then
        escIdx = (escIdx > 0) and escIdx - 1 or 4
        needsDraw = true
    elseif input.wasReleased("down") or input.wasReleased("right") then
        escIdx = (escIdx < 4) and escIdx + 1 or 0
        needsDraw = true
    elseif input.wasReleased("confirm") then
        if escIdx == 0 then inEscMenu = false
        elseif escIdx == 1 then showSizeSelection = false; showHandicapSelection = true; resetGame(7); inEscMenu = false
        elseif escIdx == 2 then showSizeSelection = false; showHandicapSelection = true; resetGame(9); inEscMenu = false
        elseif escIdx == 3 then
            -- Pass Turn
            lastMovePass = true
            consecutivePasses = consecutivePasses + 1
            inEscMenu = false
            if consecutivePasses >= 2 then status = calculateWinner()
            else startAiThinking() end
        elseif escIdx == 4 then sys.exit() end
        needsDraw = true
    elseif input.wasReleased("back") then
        inEscMenu = false; needsDraw = true
    end
end

function handleResultInput()
    if input.wasReleased("left") or input.wasReleased("right") then
        postMenuIdx = (postMenuIdx == 0) and 1 or 0
        needsDraw = true
    elseif input.wasReleased("confirm") then
        if postMenuIdx == 0 then showSizeSelection = false; showHandicapSelection = true; resetGame(boardSize)
        else sys.exit() end
        needsDraw = true
    end
end

function handleGameInput()
    -- Board Navigation
    if input.wasReleased("left") then
        cursorX = (cursorX > 0) and cursorX - 1 or boardSize - 1; needsDraw = true
    elseif input.wasReleased("right") then
        cursorX = (cursorX < boardSize - 1) and cursorX + 1 or 0; needsDraw = true
    elseif input.wasReleased("page_back") then
        cursorY = (cursorY > 0) and cursorY - 1 or boardSize - 1; needsDraw = true
    elseif input.wasReleased("page_forward") then
        cursorY = (cursorY < boardSize - 1) and cursorY + 1 or 0; needsDraw = true
    end

    if input.wasReleased("confirm") then
        if isValidMove(cursorX, cursorY, playerColor) then
            lastBoard = copyBoard(board)
            setAt(cursorX, cursorY, playerColor, board)
            captureStones(cursorX, cursorY, playerColor, board)
            lastMoveX, lastMoveY = cursorX, cursorY
            lastMovePass = false
            consecutivePasses = 0
            
            if consecutivePasses >= 2 then -- Not possible here but for consistency
                status = calculateWinner()
            else
                startAiThinking()
            end
            needsDraw = true
        end
    elseif input.wasReleased("back") then
        inEscMenu = true; escIdx = 0; needsDraw = true
    end
end

-- ── Main Loop ─────────────────────────────────────────────────────────────────

function draw()
    if isAiThinking then
        if sys.millis() - aiThinkStartTime > 1200 then
            if runAiStep() then
                -- Still thinking
            else
                finishAiThinking()
            end
        end
        if not needsDraw then return end
        needsDraw = false
        renderBoard()
        return
    end

    if showSizeSelection then handleSizeInput()
    elseif showHandicapSelection then handleHandicapInput()
    elseif inEscMenu then handleEscInput()
    elseif status ~= "playing" then handleResultInput()
    else handleGameInput() end

    if not needsDraw then return end
    needsDraw = false
    renderBoard()
end

function resetGame(size)
    boardSize = size
    board = {}
    for i = 1, size * size do board[i] = EMPTY end
    lastBoard = {}
    for i = 1, size * size do lastBoard[i] = EMPTY end
    consecutivePasses = 0
    status = "playing"
    cursorX, cursorY = math.floor(boardSize/2), math.floor(boardSize/2)
    lastMoveX, lastMoveY = -1, -1
    lastMovePass = false
    aiSimulationsDone = 0
    isAiThinking = false
    resultsCached = false
    needsDraw = true
    playerMumbleIdx = math.random(#playerMumbles)
end

function init()
    showSizeSelection = true
    sizeIdx = 0
    resetGame(7)
    log("MiniGo Lua Initialized")
end
