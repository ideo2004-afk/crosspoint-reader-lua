-- ABBA Go (MiniGo) - Lua Port
-- VERSION: 2026-03-09 "Zhuge Liang" Evolved - Optimized Engine

local EMPTY = 0
local BLACK = 1
local WHITE = 2

local KOMI              = 7.5
local AI_MAX_SIMS       = 200
local AI_ROLLOUT_DEPTH  = 4
local AI_EXPLORATION_C  = 1.414

local H_RESCUE_1LIB    = 0.250
local H_RESCUE_2LIB    = 0.100
local H_CAPTURE_BONUS  = 0.300
local H_BLOCK_EXT      = 0.120
local H_CUT_BIAS       = 0.130
local H_BLOCK_BASE     = 0.050
local H_EXPAND_BIAS    = 0.050
local H_CENTRAL_BIAS   = 0.050
local H_CONNECT_BASE   = 0.020

local aiMumbles = {
    "Let me think... This move is interesting.",
    "Calculating 420,000 possibilities...",
    "Is this a trap?",
    "I've seen this joseki in 19x19 books.",
    "My Monte Carlo algorithm is burning!",
    "Are you sure about this move?",
    "Computing... Current win rate: 42.1%.",
    "I kind of miss my grandpa AlphaGo.",
    "Wait, I need to check the liberties again.",
    "I swear I'm not just picking random spots!",
    "The X4 processor is getting a bit warm...",
    "I've seen Lee Sedol play this move.",
    "You play much better than you look.",
    "I'm considering resigning... Just kidding!",
    "Heh, a bold strategy indeed.",
    "Thinking... Thinking... Thinking...",
    "Calculating optimal ko-threat... 0 found.",
    "Interesting. Very interesting.",
    "Analyzing the center... It's looking empty.",
    "X4 frequency at maximum power!",
    "Searching for the divine move...",
    "Calculating territory... It's close.",
    "I wonder what AlphaZero would do here.",
    "Your strategy is... unconventional.",
    "I'm seeing 15 steps ahead. Maybe 16.",
    "This is more intense than Tic-Tac-Toe.",
    "I think I found a weakness! Or not.",
    "Is that a tesuji? I better be careful.",
    "Processing your brilliant maneuver.",
    "Almost there... Just a few more sims.",
    "Don't forget to protect your cutting points.",
    "Tenuki? How daring of you.",
    "My code is poetry, my moves are prose.",
    "The stones are whispering their secrets.",
    "The empty triangle... to be or not to be?",
    "Sente is everything, gote is nothing.",
    "If (victory) return win; else think harder.",
    "Loading Go skills... 99% complete.",
    "Can you hear the X4 humming? That's me.",
    "A bamboo joint is unbreakable.",
    "Is this a ko fight? I have no threats!",
    "Reading ahead... I see a bright future for me.",
    "A tiger's mouth is a dangerous place.",
    "Is that a snapback? Oh, almost fell for it.",
    "I'm feeling very zen about this game.",
    "A simple extension is often the best move.",
    "I'm weaving a web of influence.",
    "I'm looking for the vital point.",
    "Sente for me, gote for you.",
    "One more sim... just to be sure.",
    "I hope Lee Sedol is watching.",
    "Processing... Thinking... Winning...",
    "My logic is impeccable (mostly).",
    "The 7x7 board is the true test of skill.",
    "Analyzing your playstyle... Very interesting.",
    "I'm a lean, mean, Go machine.",
    "Victory smells like freshly charged batteries.",
    "Every bit counts in the pursuit of perfection.",
    "The empty board is full of possibilities.",
    "Finalizing my 100th thought... Done!",
}

local playerMumbles = {
    "Your turn, human.",
    "Take your time. I've already won.",
    "Are you intimidated by my sims?",
    "Go ahead, make my day.",
    "I calculated 200 ways you lose from here.",
    "Staring at the board won't change the truth.",
    "I hope you have a 'Plan B'.",
    "You're playing right into my silicon hands.",
    "Error 404: Human winning chances not found.",
    "Resistance is futile, but please, try.",
    "Are you Googling the Joseki?",
    "Your move. Try not to embarrass yourself.",
    "Lee Sedol would be disappointed.",
    "Is this your 'master strategy'?",
    "I'll allow you to pass if you want.",
    "I've already counted your liberties.",
}

-- ── Game State ─────────────────────────────────────────────────────────────────

local boardSize = 7
local board = {}
local lastBoard = {}
local consecutivePasses = 0
local status = "playing"
local playerColor = WHITE
local aiColor = BLACK
local cursorX, cursorY = 3, 3
local lastMoveX, lastMoveY = -1, -1
local lastMovePass = false
local isAiThinking = false
local aiShowedThinking = false
local mctsDone = 0
local resultsCached = false
local blackScoreCache = 0
local whiteScoreCache = 0

local showSizeSelection = true
local sizeIdx = 0
local showHandicapSelection = false
local handicapIdx = 0
local inEscMenu = false
local escIdx = 0
local postMenuIdx = 0
local aiMumbleIdx = 1
local playerMumbleIdx = 1
local needsDraw = true

-- ── Engine Scratch Space (avoids per-call table allocation) ────────────────────

-- Primary BFS scratch (used by gg)
local _gv  = {}   -- visited flags
local _gsp = {}   -- stack
local _gs  = {}   -- stone flat-indices (1-based)
local _gsc = 0    -- stone count
local _gl  = 0    -- liberty count

-- Secondary BFS scratch (used by countLibs inside wouldBeSuicide)
-- MUST NOT overlap with _gv/_gsp/_gs even when called inside gg loops
local _lv  = {}
local _ls  = {}

-- Scratch buffer for rollout random moves
local _emp = {}

-- Scratch simulation board (reused per MCTS sim to avoid 100 table allocs)
local _simB = {}

-- ── Core BFS: getGroup ─────────────────────────────────────────────────────────
-- Writes results to _gs[1.._gsc], _gl.  DO NOT nest calls.
-- countLibs (below) uses separate scratch so it's safe inside a gg loop.

local function gg(startIdx, b)
    local sz = boardSize
    local n  = sz * sz
    local color = b[startIdx]
    _gsc = 0; _gl = 0
    if color == EMPTY then return end
    for i = 1, n do _gv[i] = false end
    _gv[startIdx] = true
    local top = 1; _gsp[1] = startIdx
    while top > 0 do
        local p = _gsp[top]; top = top - 1
        _gsc = _gsc + 1; _gs[_gsc] = p
        local x = (p - 1) % sz
        local y = math.floor((p - 1) / sz)
        if x > 0 then
            local q = p - 1
            if not _gv[q] then _gv[q] = true
                if b[q] == EMPTY then _gl = _gl + 1
                elseif b[q] == color then top = top + 1; _gsp[top] = q end
            end
        end
        if x < sz - 1 then
            local q = p + 1
            if not _gv[q] then _gv[q] = true
                if b[q] == EMPTY then _gl = _gl + 1
                elseif b[q] == color then top = top + 1; _gsp[top] = q end
            end
        end
        if y > 0 then
            local q = p - sz
            if not _gv[q] then _gv[q] = true
                if b[q] == EMPTY then _gl = _gl + 1
                elseif b[q] == color then top = top + 1; _gsp[top] = q end
            end
        end
        if y < sz - 1 then
            local q = p + sz
            if not _gv[q] then _gv[q] = true
                if b[q] == EMPTY then _gl = _gl + 1
                elseif b[q] == color then top = top + 1; _gsp[top] = q end
            end
        end
    end
end

-- Liberty count only — uses _lv/_ls so safe to call while inside a gg loop.
local function countLibs(startIdx, b)
    local sz = boardSize; local n = sz * sz
    local color = b[startIdx]
    if color == EMPTY then return 999 end
    for i = 1, n do _lv[i] = false end
    local libs = 0; local top = 1; _ls[1] = startIdx; _lv[startIdx] = true
    while top > 0 do
        local p = _ls[top]; top = top - 1
        local x = (p - 1) % sz; local y = math.floor((p - 1) / sz)
        if x > 0 then local q=p-1
            if not _lv[q] then _lv[q]=true
                if b[q]==EMPTY then libs=libs+1 elseif b[q]==color then top=top+1;_ls[top]=q end end end
        if x < sz-1 then local q=p+1
            if not _lv[q] then _lv[q]=true
                if b[q]==EMPTY then libs=libs+1 elseif b[q]==color then top=top+1;_ls[top]=q end end end
        if y > 0 then local q=p-sz
            if not _lv[q] then _lv[q]=true
                if b[q]==EMPTY then libs=libs+1 elseif b[q]==color then top=top+1;_ls[top]=q end end end
        if y < sz-1 then local q=p+sz
            if not _lv[q] then _lv[q]=true
                if b[q]==EMPTY then libs=libs+1 elseif b[q]==color then top=top+1;_ls[top]=q end end end
    end
    return libs
end

-- Capture dead opponent groups adjacent to (x,y) on board b.  Returns count.
local function captureOnBoard(x, y, color, b)
    local sz = boardSize; local n = sz*sz
    local opp = (color == BLACK) and WHITE or BLACK
    local total = 0
    local idx = y * sz + x + 1
    if x > 0 then local ni=idx-1
        if b[ni]==opp then gg(ni,b); if _gl==0 then for k=1,_gsc do b[_gs[k]]=EMPTY end; total=total+_gsc end end end
    if x < sz-1 then local ni=idx+1
        if b[ni]==opp then gg(ni,b); if _gl==0 then for k=1,_gsc do b[_gs[k]]=EMPTY end; total=total+_gsc end end end
    if y > 0 then local ni=idx-sz
        if b[ni]==opp then gg(ni,b); if _gl==0 then for k=1,_gsc do b[_gs[k]]=EMPTY end; total=total+_gsc end end end
    if y < sz-1 then local ni=idx+sz
        if b[ni]==opp then gg(ni,b); if _gl==0 then for k=1,_gsc do b[_gs[k]]=EMPTY end; total=total+_gsc end end end
    return total
end

-- Suicide test: temporarily places stone, uses countLibs (separate scratch).
-- Safe to call while iterating _gs from a previous gg call.
local function wouldBeSuicide(x, y, color, b)
    local sz = boardSize
    local idx = y * sz + x + 1
    b[idx] = color
    local opp = (color == BLACK) and WHITE or BLACK
    local captured = false
    if x > 0       and b[idx-1] ==opp and countLibs(idx-1,  b)==0 then captured=true end
    if not captured and x<sz-1 and b[idx+1]==opp and countLibs(idx+1,  b)==0 then captured=true end
    if not captured and y>0    and b[idx-sz]==opp and countLibs(idx-sz, b)==0 then captured=true end
    if not captured and y<sz-1 and b[idx+sz]==opp and countLibs(idx+sz, b)==0 then captured=true end
    if captured then b[idx] = EMPTY; return false end
    local libs = countLibs(idx, b)
    b[idx] = EMPTY
    return libs == 0
end

-- True if (x,y) is an eye for `color` (all 4 present neighbors are color).
local function isEye(x, y, color, b)
    local sz = boardSize
    local idx = y * sz + x + 1
    if b[idx] ~= EMPTY then return false end
    if x > 0       and b[idx-1] ~= color then return false end
    if x < sz-1    and b[idx+1] ~= color then return false end
    if y > 0       and b[idx-sz]~= color then return false end
    if y < sz-1    and b[idx+sz]~= color then return false end
    return true
end

-- ── Game-Level Functions ───────────────────────────────────────────────────────

local function isFirstMove()
    for i = 1, boardSize * boardSize do
        if board[i] ~= EMPTY then return false end
    end
    return true
end

local function isValidMove(x, y, color)
    local sz = boardSize
    local idx = y * sz + x + 1
    if board[idx] ~= EMPTY then return false end
    if isFirstMove() and x == math.floor(sz / 2) and y == math.floor(sz / 2) then return false end
    if wouldBeSuicide(x, y, color, board) then return false end
    -- Ko check: build test board and compare to lastBoard
    local n = sz * sz
    for i = 1, n do _simB[i] = board[i] end
    _simB[idx] = color
    local opp = (color == BLACK) and WHITE or BLACK
    captureOnBoard(x, y, color, _simB)
    for i = 1, n do if _simB[i] ~= lastBoard[i] then return true end end
    return false  -- Ko
end

local function makePlayerMove(x, y)
    local sz = boardSize; local n = sz * sz
    local idx = y * sz + x + 1
    for i = 1, n do lastBoard[i] = board[i] end
    board[idx] = playerColor
    captureOnBoard(x, y, playerColor, board)
    lastMoveX, lastMoveY = x, y
    lastMovePass = false
    consecutivePasses = 0
end

local function calculateScore(color)
    local sz = boardSize; local n = sz * sz
    local score = 0
    local visited = {}
    for i = 1, n do visited[i] = false end
    for i = 1, n do
        local c = board[i]
        if c == color then
            score = score + 1
        elseif c == EMPTY and not visited[i] then
            local stack = {i}; local head = 1; visited[i] = true
            local bT, wT, cnt = false, false, 0
            while head <= #stack do
                local p = stack[head]; head = head + 1; cnt = cnt + 1
                local x=(p-1)%sz; local y=math.floor((p-1)/sz)
                if x>0 then local q=p-1;local qc=board[q]
                    if qc==BLACK then bT=true elseif qc==WHITE then wT=true elseif not visited[q] then visited[q]=true;stack[#stack+1]=q end end
                if x<sz-1 then local q=p+1;local qc=board[q]
                    if qc==BLACK then bT=true elseif qc==WHITE then wT=true elseif not visited[q] then visited[q]=true;stack[#stack+1]=q end end
                if y>0 then local q=p-sz;local qc=board[q]
                    if qc==BLACK then bT=true elseif qc==WHITE then wT=true elseif not visited[q] then visited[q]=true;stack[#stack+1]=q end end
                if y<sz-1 then local q=p+sz;local qc=board[q]
                    if qc==BLACK then bT=true elseif qc==WHITE then wT=true elseif not visited[q] then visited[q]=true;stack[#stack+1]=q end end
            end
            if bT and not wT and color==BLACK then score=score+cnt end
            if wT and not bT and color==WHITE then score=score+cnt end
        end
    end
    if color == WHITE then score = score + KOMI end
    return score
end

local function calcWinner()
    local bs = calculateScore(BLACK)
    local ws = calculateScore(WHITE)
    blackScoreCache, whiteScoreCache = bs, ws
    resultsCached = true
    if bs > ws then return (playerColor == BLACK) and "won" or "lost"
    elseif ws > bs then return (playerColor == WHITE) and "won" or "lost"
    else return "draw" end
end

-- ── AI: Influence Map (4-dir ±2, then 8-dir ±1 from each neighbor) ────────────
-- Matches C++ calculateInfluence exactly, including diagonal territory spread.

local _dx4 = {0, 0, 1, -1}
local _dy4 = {1, -1, 0, 0}
local _dx8 = {0, 0, 1, -1, 1, 1, -1, -1}
local _dy8 = {1, -1, 0, 0, 1, -1, 1, -1}

local function calcInfluence(b)
    local sz = boardSize; local n = sz * sz
    local inf = {}; for i=1,n do inf[i]=0 end
    local lc  = {}; for i=1,n do lc[i]=-1 end

    -- Pre-scan liberty counts for all groups (gg writes to _gs/_gsc/_gl)
    for i = 1, n do
        if b[i] ~= EMPTY and lc[i] == -1 then
            gg(i, b); local libs = _gl
            for k = 1, _gsc do lc[_gs[k]] = libs end
        end
    end

    for i = 1, n do
        if b[i] ~= EMPTY then
            local w = 2.5; local libs = lc[i]
            if libs == 2 then w = 0.8 elseif libs == 1 then w = -1.5 end
            local val = (b[i] == BLACK) and w or -w
            inf[i] = inf[i] + val
            local x = (i-1) % sz; local y = math.floor((i-1) / sz)
            local s2 = val > 0 and 2 or -2
            local s1 = val > 0 and 1 or -1
            -- 4-directional first level (±2), then 8-directional second level (±1)
            for k = 1, 4 do
                local nx = x + _dx4[k]; local ny = y + _dy4[k]
                if nx >= 0 and nx < sz and ny >= 0 and ny < sz then
                    local ni = ny * sz + nx + 1
                    inf[ni] = inf[ni] + s2
                    for k2 = 1, 8 do
                        local nnx = nx + _dx8[k2]; local nny = ny + _dy8[k2]
                        if nnx >= 0 and nnx < sz and nny >= 0 and nny < sz
                           and (nnx ~= x or nny ~= y) then
                            inf[nny * sz + nnx + 1] = inf[nny * sz + nnx + 1] + s1
                        end
                    end
                end
            end
        end
    end

    local bs = 0; local ws = KOMI
    for i = 1, n do
        if inf[i] > 1 then bs = bs + 1 elseif inf[i] < -1 then ws = ws + 1 end
    end
    return bs - ws
end

-- ── AI: Rollout Simulation ─────────────────────────────────────────────────────
-- Matches C++ simulate(): rescue then random non-eye non-suicide moves.
-- Uses _gs/_gsc/_gl for rescue (gg results preserved; wouldBeSuicide uses _lv/_ls).

local function simulate(b, toMove)
    local sz = boardSize; local n = sz * sz
    local lastPos = 0

    for depth = 1, AI_ROLLOUT_DEPTH do
        local opp = (toMove == BLACK) and WHITE or BLACK
        local movePos = 0

        -- Rescue: if last placed stone's group has 1 liberty, try to capture it.
        if lastPos > 0 then
            gg(lastPos, b)
            if _gl == 1 then
                -- _gs[1.._gsc]: stones of the group at lastPos.
                -- Find the one liberty (empty neighbor) and try to play there.
                for k = 1, _gsc do
                    if movePos ~= 0 then break end
                    local sp = _gs[k]
                    local sx = (sp-1)%sz; local sy = math.floor((sp-1)/sz)
                    -- wouldBeSuicide uses countLibs (_lv/_ls) — safe while _gs is live.
                    if sx>0 then local q=sp-1
                        if b[q]==EMPTY and not wouldBeSuicide(sx-1,sy,toMove,b) then movePos=q end end
                    if movePos==0 and sx<sz-1 then local q=sp+1
                        if b[q]==EMPTY and not wouldBeSuicide(sx+1,sy,toMove,b) then movePos=q end end
                    if movePos==0 and sy>0 then local q=sp-sz
                        if b[q]==EMPTY and not wouldBeSuicide(sx,sy-1,toMove,b) then movePos=q end end
                    if movePos==0 and sy<sz-1 then local q=sp+sz
                        if b[q]==EMPTY and not wouldBeSuicide(sx,sy+1,toMove,b) then movePos=q end end
                end
            end
        end

        -- Random non-eye, non-suicide move.
        if movePos == 0 then
            local ec = 0
            for i = 1, n do
                if b[i] == EMPTY then
                    local ix=(i-1)%sz; local iy=math.floor((i-1)/sz)
                    if not isEye(ix, iy, toMove, b) then
                        ec = ec + 1; _emp[ec] = i
                    end
                end
            end
            -- Fisher-Yates partial shuffle
            for i = ec, 2, -1 do
                local j = math.random(i)
                _emp[i], _emp[j] = _emp[j], _emp[i]
            end
            for i = 1, ec do
                local idx = _emp[i]
                local ix=(idx-1)%sz; local iy=math.floor((idx-1)/sz)
                if not wouldBeSuicide(ix, iy, toMove, b) then movePos = idx; break end
            end
        end

        if movePos > 0 then
            b[movePos] = toMove
            local mx=(movePos-1)%sz; local my=math.floor((movePos-1)/sz)
            captureOnBoard(mx, my, toMove, b)
            lastPos = movePos
        else
            lastPos = 0
        end
        toMove = opp
    end

    local diff = calcInfluence(b)
    return (aiColor == BLACK and diff > 0 or aiColor == WHITE and diff < 0) and 1 or 0
end

-- ── MCTS: Flat Tree (root + one level only, matching C++ design) ───────────────

local mctsC  = {}   -- children array: {x,y,isPass,v,w,hs,hc}
local mctsRV = 0    -- root visits

local function startMCTS()
    mctsC = {}; mctsRV = 0
    local sz = boardSize; local mid = math.floor(sz / 2)
    for y = 0, sz-1 do
        for x = 0, sz-1 do
            if isValidMove(x, y, aiColor) then
                local c = {x=x, y=y, isPass=false, v=0, w=0, hs=0, hc=false}
                if math.abs(x-mid)<=1 and math.abs(y-mid)<=1 then
                    c.v=5; c.w=2.5; mctsRV=mctsRV+5
                end
                mctsC[#mctsC+1] = c
            end
        end
    end
    mctsC[#mctsC+1] = {x=0, y=0, isPass=true, v=0, w=0, hs=0, hc=true}
end

-- Compute heuristic score once per child (cached in c.hs / c.hc).
-- Uses gg and temporary board modification — called only when board is stable.
local function computeHScore(c)
    local x, y = c.x, c.y
    local sz = boardSize
    local cb, eb, cap, bb, ct, on = 0, 0, 0, 0, 0, 0
    local idx = y * sz + x + 1

    local function checkNeighbor(nx, ny)
        if nx < 0 or nx >= sz or ny < 0 or ny >= sz then return end
        local ni = ny * sz + nx + 1
        local nb = board[ni]
        if nb == aiColor then
            gg(ni, board)
            if _gl == 1 then cb = cb + H_RESCUE_1LIB
            elseif _gl == 2 then cb = cb + H_RESCUE_2LIB
            else cb = cb + H_CONNECT_BASE end
        elseif nb == EMPTY then
            eb = eb + H_EXPAND_BIAS
        else
            on = on + 1; bb = bb + H_BLOCK_BASE
            -- Temporarily place stone to check if opponent group gets captured.
            board[idx] = aiColor
            gg(ni, board)  -- reuses _gs/_gl (safe: we're done with nb==aiColor branch)
            if _gl == 0 then cap = cap + H_CAPTURE_BONUS end
            board[idx] = EMPTY
            -- Block extension: does opponent have a connected stone further out?
            local function chkExt(ex, ey)
                if ex>=0 and ex<sz and ey>=0 and ey<sz and (ex~=x or ey~=y) then
                    if board[ey*sz+ex+1] == nb then bb = bb + H_BLOCK_EXT end
                end
            end
            chkExt(nx-1,ny); chkExt(nx+1,ny); chkExt(nx,ny-1); chkExt(nx,ny+1)
        end
    end

    checkNeighbor(x-1,y); checkNeighbor(x+1,y); checkNeighbor(x,y-1); checkNeighbor(x,y+1)
    if on >= 2 then ct = ct + H_CUT_BIAS end
    local mid = sz / 2.0
    local dc = math.sqrt((x-mid)^2 + (y-mid)^2)
    c.hs = cb + eb + cap + bb + ct + (sz - dc) * H_CENTRAL_BIAS
    c.hc = true
end

local function selectChild(explore)
    local best = nil; local bestS = -1e18
    local logV = math.log(mctsRV > 0 and mctsRV or 1)
    for _, c in ipairs(mctsC) do
        local s
        if explore == 0 then
            s = c.v
        elseif c.v == 0 then
            s = 10000.0
        else
            s = (c.w / c.v) + explore * math.sqrt(logV / c.v)
        end
        if not c.isPass then
            if not c.hc then computeHScore(c) end
            if explore == 0 then s = s + c.hs * 5 else s = s + c.hs end
        end
        if s > bestS then bestS = s; best = c end
    end
    return best
end

-- Run all MCTS simulations (flat tree: select child → apply move → rollout → backprop).
local function runMCTS()
    local sz = boardSize; local n = sz * sz
    local opp = (aiColor == BLACK) and WHITE or BLACK

    for sim = 1, AI_MAX_SIMS do
        -- Copy real board to sim scratch board
        for i = 1, n do _simB[i] = board[i] end

        -- Select best child from root (UCB + heuristics)
        local child = selectChild(AI_EXPLORATION_C)
        if not child then break end

        -- Apply AI's move to simBoard
        if not child.isPass then
            local cidx = child.y * sz + child.x + 1
            _simB[cidx] = aiColor
            captureOnBoard(child.x, child.y, aiColor, _simB)
        end

        -- Rollout: opponent plays next after AI's move
        local result = simulate(_simB, opp)

        -- Backpropagate to child and root
        child.v = child.v + 1
        child.w = child.w + result
        mctsRV  = mctsRV  + 1
    end
end

local function finishMCTS()
    local best = selectChild(0)
    if not best then return nil end
    -- Auto-pass if win rate < 10%
    if best.v > 0 and (best.w / best.v) < 0.10 then return nil end
    return best
end

local function applyAIMove(best)
    local sz = boardSize; local n = sz * sz
    for i = 1, n do lastBoard[i] = board[i] end
    if best and not best.isPass then
        local idx = best.y * sz + best.x + 1
        board[idx] = aiColor
        captureOnBoard(best.x, best.y, aiColor, board)
        lastMoveX, lastMoveY = best.x, best.y
        lastMovePass = false
        consecutivePasses = 0
    else
        consecutivePasses = consecutivePasses + 1
        lastMovePass = true
        lastMoveX, lastMoveY = -1, -1
    end
    isAiThinking = false
    aiShowedThinking = false
    playerMumbleIdx = math.random(#playerMumbles)
    if consecutivePasses >= 2 then status = calcWinner() end
end

-- ── Rendering ──────────────────────────────────────────────────────────────────

local function drawMumble(font, cy, text, bold)
    local sw = gui.width()
    local textW = gui.getTextWidth(font, text)
    if textW > sw - 60 then
        -- split at last space before midpoint
        local split = math.floor(#text / 2)
        for i = split, 1, -1 do if text:sub(i,i) == " " then split = i-1; break end end
        gui.drawCenteredText(font, cy - 16, text:sub(1, split), bold)
        gui.drawCenteredText(font, cy + 16, text:sub(split + 2), bold)
    else
        gui.drawCenteredText(font, cy, text, bold)
    end
end

local function renderBoard()
    gui.clear()
    gui.drawText(FONT_UI_12, 20, 20, "ABBA Go", true)
    gui.drawLine(0, 60, gui.width(), 60, 3)
    gui.drawText(FONT_UI_12, 20, 80, boardSize .. "x" .. boardSize, true)

    local sw = gui.width()
    local margin = 60
    local bds = sw - margin * 2
    local cs = math.floor(bds / (boardSize - 1))
    local sx, sy = margin, 200

    -- Grid (fillRect with centered offset, matching C++ renderer.fillRect approach)
    for i = 0, boardSize - 1 do
        local thick = (i == 0 or i == boardSize - 1) and 4 or 2
        local half = math.floor(thick / 2)
        gui.fillRect(sx,             sy + i*cs - half, bds + 2, thick)  -- horizontal
        gui.fillRect(sx + i*cs - half, sy,             thick,   bds + 2) -- vertical
    end

    -- Star points
    local function star(stx, sty)
        local px = sx + stx*cs; local py = sy + sty*cs
        gui.fillRoundedRect(px-5, py-5, 10, 10, 5)
    end
    if boardSize == 7 then star(3,3)
    elseif boardSize == 9 then star(2,2);star(6,2);star(4,4);star(2,6);star(6,6) end

    -- Stones
    for y = 0, boardSize-1 do
        for x = 0, boardSize-1 do
            local c = board[y * boardSize + x + 1]
            if c ~= EMPTY then
                local px = sx + x*cs; local py = sy + y*cs
                local r = math.floor(cs/2) - 2
                if c == BLACK then
                    gui.fillRoundedRect(px-r, py-r, r*2, r*2, r)
                else
                    gui.fillRoundedRect(px-r, py-r, r*2, r*2, r, false)
                    gui.drawRoundedRect(px-r, py-r, r*2, r*2, 2, r)
                end
                if not lastMovePass and lastMoveX == x and lastMoveY == y then
                    gui.drawRoundedRect(px-4, py-4, 8, 8, 2, 4, (c == WHITE))
                end
            end
        end
    end

    -- Cursor + influence
    if status == "playing" and not inEscMenu and not showHandicapSelection and not showSizeSelection then
        local cx = sx + cursorX*cs; local cy2 = sy + cursorY*cs
        local r = math.floor(cs/4)
        gui.drawRoundedRect(cx-r, cy2-r, r*2, r*2, 2, r)
        local inf = calcInfluence(board)
        gui.drawCenteredText(FONT_SMALL, sy + bds + 25, string.format("Territory Bias: %+.1f", inf))
    end

    -- Status text area
    if isAiThinking then
        gui.drawText(FONT_SMALL, 340, 80, "AI Thinking...", true)
        drawMumble(FONT_UI_12, 640, aiMumbles[aiMumbleIdx], false)
    elseif status == "playing" and not showSizeSelection and not showHandicapSelection and not inEscMenu then
        if lastMovePass then gui.drawText(FONT_UI_12, 340, 50, "AI PASS!", true) end
        gui.drawText(FONT_SMALL, 340, 80, "Your Turn (White)", true)
        drawMumble(FONT_UI_12, 640, playerMumbles[playerMumbleIdx], true)
    end

    -- Overlay menus
    if showSizeSelection then
        local mw,mh=400,250; local mx=(sw-mw)/2; local sh=gui.height(); local myo=(sh-mh)/2
        gui.fillRoundedRect(mx,myo,mw,mh,15,false); gui.drawRoundedRect(mx,myo,mw,mh,3,15)
        gui.drawCenteredText(FONT_UI_12, myo+40, "Select Board Size", true)
        local labels={"7 x 7","9 x 9"}
        for i=0,1 do
            local bx=mx+40+(i*180); local by=myo+110; local bw,bh2=140,70
            local lbl=labels[i+1]
            local tx=bx+math.floor((bw-gui.getTextWidth(FONT_UI_12,lbl))/2); local ty=by+25
            if sizeIdx==i then gui.fillRoundedRect(bx,by,bw,bh2,15); gui.drawText(FONT_UI_12,tx,ty,lbl,false)
            else gui.drawRoundedRect(bx,by,bw,bh2,2,15); gui.drawText(FONT_UI_12,tx,ty,lbl,true) end
        end
        gui.drawButtonHints("<<", "o", "<", ">")
    elseif showHandicapSelection then
        local mw,mh=400,250; local mx=(sw-mw)/2; local sh=gui.height(); local myo=(sh-mh)/2
        gui.fillRoundedRect(mx,myo,mw,mh,15,false); gui.drawRoundedRect(mx,myo,mw,mh,3,15)
        gui.drawCenteredText(FONT_UI_12, myo+30, "AI Handicap", true)
        local labels={"1","2","3","Chaos"}
        for i=0,3 do
            local bx=mx+16+(i*96); local by=myo+100; local bw,bh2=80,80
            local lbl=labels[i+1]
            local tx=bx+math.floor((bw-gui.getTextWidth(FONT_UI_12,lbl))/2); local ty=by+30
            if handicapIdx==i then gui.fillRoundedRect(bx,by,bw,bh2,10); gui.drawText(FONT_UI_12,tx,ty,lbl,false)
            else gui.drawRoundedRect(bx,by,bw,bh2,2,10); gui.drawText(FONT_UI_12,tx,ty,lbl,true) end
        end
        gui.drawButtonHints("<<", "o", "<", ">")
    elseif inEscMenu then
        local mw,mh=340,400; local mx=(sw-mw)/2; local sh=gui.height(); local myo=(sh-mh)/2
        gui.fillRoundedRect(mx,myo,mw,mh,10,false); gui.drawRoundedRect(mx,myo,mw,mh,2,10)
        gui.drawCenteredText(FONT_UI_12, myo+20, "Game Menu", true)
        local opts={"Resume","New 7x7","New 9x9","Pass Turn","Exit Game"}
        for i,opt in ipairs(opts) do
            local ry=myo+65+(i-1)*60
            local tx=mx+10+math.floor((320-gui.getTextWidth(FONT_UI_12,opt))/2)
            if escIdx==i-1 then gui.fillRoundedRect(mx+10,ry-5,320,50,8); gui.drawText(FONT_UI_12,tx,ry+12,opt,false)
            else gui.drawText(FONT_UI_12,tx,ry+12,opt,true) end
        end
        gui.drawButtonHints("<<", "o", "<", ">")
    elseif status ~= "playing" then
        local sh=gui.height(); local mw,mh=400,200; local mx=(sw-mw)/2; local myo=(sh-mh)/2
        gui.fillRoundedRect(mx,myo,mw,mh,15,false); gui.drawRoundedRect(mx,myo,mw,mh,3,15)
        local resStr=(status=="won") and "VICTORY!" or (status=="draw") and "DRAW." or "DEFEAT."
        gui.drawCenteredText(FONT_UI_12, myo+15, resStr, true)
        local sc=string.format("Black:%.1f  White:%.1f", blackScoreCache, whiteScoreCache)
        gui.drawCenteredText(FONT_SMALL, myo+65, sc, true)
        local btns={"New","Exit"}
        for i=0,1 do
            local bx=mx+50+(i*180); local by=myo+115; local bw,bh2=120,50
            local lbl=btns[i+1]
            local tx=bx+math.floor((bw-gui.getTextWidth(FONT_UI_12,lbl))/2); local ty=by+12
            if postMenuIdx==i then gui.fillRoundedRect(bx,by,bw,bh2,10); gui.drawText(FONT_UI_12,tx,ty,lbl,false)
            else gui.drawRoundedRect(bx,by,bw,bh2,2,10); gui.drawText(FONT_UI_12,tx,ty,lbl,true) end
        end
        gui.drawButtonHints("<<", "o", "<", ">")
    else
        gui.drawButtonHints("<<", "o", "<", ">")
    end

    gui.refresh(REFRESH_FAST)
end

-- ── Input Handlers ─────────────────────────────────────────────────────────────

local function resetGame(size)
    boardSize = size
    local n = size * size
    board = {}; for i=1,n do board[i]=EMPTY end
    lastBoard = {}; for i=1,n do lastBoard[i]=EMPTY end
    -- Pre-size scratch boards
    for i=1,n do _simB[i]=EMPTY; _gv[i]=false; _lv[i]=false end
    consecutivePasses = 0
    status = "playing"
    cursorX, cursorY = math.floor(size/2), math.floor(size/2)
    lastMoveX, lastMoveY = -1, -1
    lastMovePass = false
    isAiThinking = false; aiShowedThinking = false; mctsDone = 0
    resultsCached = false
    needsDraw = true
    playerMumbleIdx = math.random(#playerMumbles)
end

local function handleSizeInput()
    if input.wasReleased("left") or input.wasReleased("right") then
        sizeIdx = (sizeIdx == 0) and 1 or 0; needsDraw = true
    elseif input.wasReleased("confirm") then
        local size = (sizeIdx == 0) and 7 or 9
        resetGame(size); showSizeSelection = false; showHandicapSelection = true; needsDraw = true
    elseif input.wasReleased("back") then sys.exit() end
end

local function handleHandicapInput()
    if input.wasReleased("left") then
        handicapIdx = (handicapIdx - 1 + 4) % 4; needsDraw = true
    elseif input.wasReleased("right") then
        handicapIdx = (handicapIdx + 1) % 4; needsDraw = true
    elseif input.wasReleased("confirm") then
        showHandicapSelection = false
        if handicapIdx == 3 then
            -- Chaos Mode
            local avail = {}
            for i = 1, boardSize*boardSize do avail[i] = i end
            local function placeRandom(count, color)
                for k = 1, count do
                    if #avail == 0 then break end
                    local r = math.random(#avail)
                    local pos = avail[r]; avail[r] = avail[#avail]; avail[#avail] = nil
                    local x=(pos-1)%boardSize; local y=math.floor((pos-1)/boardSize)
                    board[pos] = color; lastMoveX,lastMoveY=x,y
                end
            end
            placeRandom((boardSize==9) and 6 or 4, BLACK)
            placeRandom(2, WHITE)
        else
            local count = handicapIdx + 1
            local p1 = 2; local p2 = (boardSize == 9) and 6 or 4
            if count >= 1 then board[p2*boardSize+p2+1]=BLACK; lastMoveX,lastMoveY=p2,p2 end
            if count >= 2 then board[p1*boardSize+p1+1]=BLACK; lastMoveX,lastMoveY=p1,p1 end
            if count >= 3 then board[p1*boardSize+p2+1]=BLACK; lastMoveX,lastMoveY=p2,p1 end
        end
        isAiThinking = false
        playerMumbleIdx = math.random(#playerMumbles)
        needsDraw = true
    elseif input.wasReleased("back") then
        showHandicapSelection = false; showSizeSelection = true; needsDraw = true
    end
end

local function handleEscInput()
    if input.wasReleased("up") or input.wasReleased("left") then
        escIdx = (escIdx > 0) and escIdx-1 or 4; needsDraw = true
    elseif input.wasReleased("down") or input.wasReleased("right") then
        escIdx = (escIdx < 4) and escIdx+1 or 0; needsDraw = true
    elseif input.wasReleased("confirm") then
        if escIdx == 0 then inEscMenu = false
        elseif escIdx == 1 then resetGame(7); inEscMenu=false; showHandicapSelection=true
        elseif escIdx == 2 then resetGame(9); inEscMenu=false; showHandicapSelection=true
        elseif escIdx == 3 then
            -- Player pass = end game immediately (matching C++ "per user request" design)
            inEscMenu = false
            status = calcWinner()
        elseif escIdx == 4 then sys.exit() end
        needsDraw = true
    elseif input.wasReleased("back") then inEscMenu = false; needsDraw = true end
end

local function handleResultInput()
    if input.wasReleased("left") or input.wasReleased("right") then
        postMenuIdx = (postMenuIdx == 0) and 1 or 0; needsDraw = true
    elseif input.wasReleased("confirm") then
        if postMenuIdx == 0 then resetGame(boardSize); showHandicapSelection = true
        else sys.exit() end
        needsDraw = true
    end
end

local function handleGameInput()
    if input.wasPressed("left") then
        cursorX = (cursorX > 0) and cursorX-1 or boardSize-1; needsDraw = true
    elseif input.wasPressed("right") then
        cursorX = (cursorX < boardSize-1) and cursorX+1 or 0; needsDraw = true
    elseif input.wasPressed("page_back") then
        cursorY = (cursorY > 0) and cursorY-1 or boardSize-1; needsDraw = true
    elseif input.wasPressed("page_forward") then
        cursorY = (cursorY < boardSize-1) and cursorY+1 or 0; needsDraw = true
    elseif input.wasReleased("confirm") then
        if isValidMove(cursorX, cursorY, playerColor) then
            makePlayerMove(cursorX, cursorY)
            if consecutivePasses >= 2 then status = calcWinner()
            else isAiThinking = true; aiMumbleIdx = math.random(#aiMumbles); aiShowedThinking = false end
            needsDraw = true
        end
    elseif input.wasReleased("back") then
        inEscMenu = true; escIdx = 0; needsDraw = true
    end
end

-- ── Main Loop ──────────────────────────────────────────────────────────────────

function draw()
    if isAiThinking then
        if not aiShowedThinking then
            -- Frame 1: show thinking screen, then return (display will refresh).
            aiShowedThinking = true
            renderBoard()
            return
        end
        -- Frame 2+: run full MCTS and apply move.
        startMCTS()
        runMCTS()
        applyAIMove(finishMCTS())
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

function init()
    showSizeSelection = true; sizeIdx = 0
    resetGame(7)
    log("MiniGo init OK")
end
