-- Mini Go
-- DESCRIPTION: Go game with AI v30.

local EMPTY = 0
local BLACK = 1
local WHITE = 2

local KOMI              = 7.5
local AI_MAX_SIMS_7     = 800   -- 7x7 模擬次數
local AI_MAX_SIMS_9     = 700   -- 9x9 模擬次數
local AI_MAX_SIMS_11    = 600   -- 11x11 模擬次數
local AI_ROLLOUT_DEPTH  = 4     -- 模擬深度
local AI_EXPLORATION_C  = 1.414 -- 探索係數
local AI_HEURISTIC_WEIGHT = 1.0 -- 規則加權比率 (越小越依賴模擬，越大越遵守規則)

local H_CAPTURE_BONUS  = 1.500  -- 提子獎勵 (吃掉對手棋子)
local H_RESCUE_1LIB    = 0.600  -- 救一氣子 (防止被提子)
local H_RESCUE_2LIB    = 0.120  -- 救二氣子 (預防性防守)
local H_CUT_BIAS       = 0.050  -- 切斷加權 (切斷對手連結)
local H_BLOCK_BASE     = 0.050  -- 基礎阻擋 (下在對手旁邊)
local H_CENTRAL_BIAS   = 0.100  -- 佈局偏好 (搶星位與黃金線)
local H_HOTSPOT_ORTHO           = 0.060  -- 交戰熱點-直向 (對手上一手棋的鄰位)
local H_HOTSPOT_DIAG            = 0.030  -- 交戰熱點-斜向 (對手上一手棋的斜位)
local H_LOOSE_CONNECT           = 0.080  -- 鬆散連絡 (桂馬步或一間跳)
local H_CONNECT_BONUS           = 0.300  -- 連結兩群獎勵 (落子連結兩個群組)
local H_SELF_ATARI_PENALTY      = -10.000 -- 自入虎口懲罰 (落子後自己只剩 1 氣)
local H_EYE_PENALTY             = -10.000 -- 真眼保護 (絕對禁止填掉真眼)
local H_INVASION_PENALTY        = -1.000 -- 敵陣深入懲罰 (避開對方重兵區)
local H_TERRITORY_FILL_PENALTY  = -0.300 -- 自家過度填子懲罰 (避开大後方內耗)

local aiMumbles = {
    "Let me think... This move is interesting.",
    "Calculating 420,000 possibilities...",
    "My Monte Carlo algorithm is burning!",
    "Computing... Current win rate: 42.1%.",
    "I kind of miss my grandpa AlphaGo.",
    "Wait, I need to check the liberties again.",
    "The X4 processor is getting a bit warm...",
    "I've seen Lee Sedol play this move.",
    "I'm considering resigning... Just kidding!",
    "Thinking... Thinking... Thinking...",
    "Calculating optimal ko-threat... 0 found.",
    "Interesting. Very interesting.",
    "X4 frequency at maximum power!",
    "Searching for the divine move...",
    "Calculating territory... It's close.",
    "Your strategy is... unconventional.",
    "This is more intense than Tic-Tac-Toe.",
    "Processing your brilliant maneuver.",
    "Almost there... Just a few more sims.",
    "If (victory) return win; else think harder.",
    "Loading Go skills... 99% complete.",
    "A bamboo joint is unbreakable.",
    "Is this a ko fight? I have no threats!",
    "Reading ahead... I see a bright future for me.",
    "I'm feeling very zen about this game.",
    "Sente for me, gote for you.",
    "I hope Lee Sedol is watching.",
    "My logic is impeccable (mostly).",
    "Every bit counts in the pursuit of perfection.",
    "Finalizing my 100th thought... Done!",
}

local playerMumbles = {
    "Your turn, human.",
    "Take your time. I've already won.",
    "Go ahead, make my day.",
    "Staring at the board won't change the truth.",
    "You're playing right into my silicon hands.",
    "Error 404: Human winning chances not found.",
    "Your move. Try not to embarrass yourself.",
    "Lee Sedol would be disappointed.",
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
local resultJustOpened = false -- 防穿透標記
local aiMumbleIdx = 1
local playerMumbleIdx = 1
local showHint = false
local needsDraw = true

-- ── Engine Scratch Space (avoids per-call table allocation) ────────────────────

-- Primary BFS scratch (used by gg)
local _gv  = {}   -- visited generation stamps
local _gvG = 0    -- current gg generation
local _gsp = {}   -- stack
local _gs  = {}   -- stone flat-indices (1-based)
local _gsc = 0    -- stone count
local _gl  = 0    -- liberty count

-- Secondary BFS scratch (used by countLibs inside wouldBeSuicide)
local _lv  = {}   -- visited generation stamps
local _lvG = 0    -- current countLibs generation
local _ls  = {}

-- Scratch buffer for rollout random moves
local _emp = {}

-- Scratch simulation board (reused per MCTS sim to avoid 100 table allocs)
local _simB = {}

-- Scratch buffers for calcInfluence (reused to avoid GC pressure on large boards)
local _inf  = {}
local _lc   = {}

-- Scratch buffer for calculateScores territory map (reused to avoid per-call allocation)
local _tMap = {}

-- ── Core BFS: getGroup ─────────────────────────────────────────────────────────
-- Writes results to _gs[1.._gsc], _gl.  DO NOT nest calls.
-- countLibs (below) uses separate scratch so it's safe inside a gg loop.

local function gg(startIdx, b)
    local sz = boardSize
    local color = b[startIdx]
    _gsc = 0; _gl = 0
    if color == EMPTY then return end
    -- O(1) generation stamp reset — no O(N) loop needed
    _gvG = _gvG + 1; local gen = _gvG
    _gv[startIdx] = gen
    local top = 1; _gsp[1] = startIdx
    while top > 0 do
        local p = _gsp[top]; top = top - 1
        _gsc = _gsc + 1; _gs[_gsc] = p
        local x = (p - 1) % sz
        local y = math.floor((p - 1) / sz)
        if x > 0 then
            local q = p - 1
            if _gv[q] ~= gen then _gv[q] = gen
                if b[q] == EMPTY then _gl = _gl + 1
                elseif b[q] == color then top = top + 1; _gsp[top] = q end
            end
        end
        if x < sz - 1 then
            local q = p + 1
            if _gv[q] ~= gen then _gv[q] = gen
                if b[q] == EMPTY then _gl = _gl + 1
                elseif b[q] == color then top = top + 1; _gsp[top] = q end
            end
        end
        if y > 0 then
            local q = p - sz
            if _gv[q] ~= gen then _gv[q] = gen
                if b[q] == EMPTY then _gl = _gl + 1
                elseif b[q] == color then top = top + 1; _gsp[top] = q end
            end
        end
        if y < sz - 1 then
            local q = p + sz
            if _gv[q] ~= gen then _gv[q] = gen
                if b[q] == EMPTY then _gl = _gl + 1
                elseif b[q] == color then top = top + 1; _gsp[top] = q end
            end
        end
    end
end

-- Liberty count only — uses _lv/_ls so safe to call while inside a gg loop.
local function countLibs(startIdx, b)
    local sz = boardSize
    local color = b[startIdx]
    if color == EMPTY then return 999 end
    -- O(1) generation stamp reset
    _lvG = _lvG + 1; local gen = _lvG
    local libs = 0; local top = 1; _ls[1] = startIdx; _lv[startIdx] = gen
    while top > 0 do
        local p = _ls[top]; top = top - 1
        local x = (p - 1) % sz; local y = math.floor((p - 1) / sz)
        if x > 0 then local q=p-1
            if _lv[q] ~= gen then _lv[q]=gen
                if b[q]==EMPTY then libs=libs+1 elseif b[q]==color then top=top+1;_ls[top]=q end end end
        if x < sz-1 then local q=p+1
            if _lv[q] ~= gen then _lv[q]=gen
                if b[q]==EMPTY then libs=libs+1 elseif b[q]==color then top=top+1;_ls[top]=q end end end
        if y > 0 then local q=p-sz
            if _lv[q] ~= gen then _lv[q]=gen
                if b[q]==EMPTY then libs=libs+1 elseif b[q]==color then top=top+1;_ls[top]=q end end end
        if y < sz-1 then local q=p+sz
            if _lv[q] ~= gen then _lv[q]=gen
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
    local sz = boardSize; local idx = y * sz + x + 1
    local opp = (color == BLACK) and WHITE or BLACK
    
    -- Speed optimization: if any neighbor is empty, it's not suicide.
    if x > 0 and b[idx-1] == EMPTY then return false end
    if x < sz-1 and b[idx+1] == EMPTY then return false end
    if y > 0 and b[idx-sz] == EMPTY then return false end
    if y < sz-1 and b[idx+sz] == EMPTY then return false end

    -- No empty neighbors. Temporarily place stone to check captures or self-libs.
    b[idx] = color
    local captured = false
    if x > 0       and b[idx-1] == opp and countLibs(idx-1, b) == 0 then captured = true end
    if not captured and x < sz-1 and b[idx+1] == opp and countLibs(idx+1, b) == 0 then captured = true end
    if not captured and y > 0    and b[idx-sz] == opp and countLibs(idx-sz, b) == 0 then captured = true end
    if not captured and y < sz-1 and b[idx+sz] == opp and countLibs(idx+sz, b) == 0 then captured = true end
    
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
    -- Check 4 orthogonal neighbors
    if x > 0      and b[idx-1] ~= color then return false end
    if x < sz - 1 and b[idx+1] ~= color then return false end
    if y > 0      and b[idx-sz] ~= color then return false end
    if y < sz - 1 and b[idx+sz] ~= color then return false end

    -- Check diagonals for True Eye
    local diagCount = 0
    local totalDiags = 0
    if x > 0      and y > 0      then totalDiags = totalDiags + 1; if b[idx-sz-1] == color then diagCount = diagCount + 1 end end
    if x < sz - 1 and y > 0      then totalDiags = totalDiags + 1; if b[idx-sz+1] == color then diagCount = diagCount + 1 end end
    if x > 0      and y < sz - 1 then totalDiags = totalDiags + 1; if b[idx+sz-1] == color then diagCount = diagCount + 1 end end
    if x < sz - 1 and y < sz - 1 then totalDiags = totalDiags + 1; if b[idx+sz+1] == color then diagCount = diagCount + 1 end end

    if totalDiags < 4 then
        -- Edge or Corner: Need all visible diagonals
        return diagCount == totalDiags
    else
        -- Center: Need at least 3 diagonals
        return diagCount >= 3
    end
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

local function calculateScores()
    local sz = boardSize; local n = sz * sz
    local bs, ws = 0, KOMI
    -- Reuse pre-allocated scratch table to avoid dynamic allocation (OOM risk on 11x11)
    local tMap = _tMap; for i=1,n do tMap[i]=EMPTY end
    
    -- Use generation stamp strategy to avoid creating 'visited' table
    _gvG = _gvG + 1; local gen = _gvG

    for i = 1, n do
        local c = board[i]
        if c == BLACK then bs = bs + 1
        elseif c == WHITE then ws = ws + 1
        elseif _gv[i] ~= gen then
            -- BFS for empty region using scratch stack _gsp
            local top = 1; _gsp[1] = i; _gv[i] = gen
            local head = 1
            local bT, wT, cnt = false, false, 0
            while head <= top do
                local p = _gsp[head]; head = head + 1; cnt = cnt + 1
                local x = (p-1) % sz; local y = math.floor((p-1) / sz)
                
                -- Flattened neighbor check for OOM safety
                if x > 0 then local q = p - 1
                    if _gv[q] ~= gen then local qc = board[q]
                        if qc == BLACK then bT = true elseif qc == WHITE then wT = true
                        else _gv[q] = gen; top = top + 1; _gsp[top] = q end end end
                if x < sz-1 then local q = p + 1
                    if _gv[q] ~= gen then local qc = board[q]
                        if qc == BLACK then bT = true elseif qc == WHITE then wT = true
                        else _gv[q] = gen; top = top + 1; _gsp[top] = q end end end
                if y > 0 then local q = p - sz
                    if _gv[q] ~= gen then local qc = board[q]
                        if qc == BLACK then bT = true elseif qc == WHITE then wT = true
                        else _gv[q] = gen; top = top + 1; _gsp[top] = q end end end
                if y < sz-1 then local q = p + sz
                    if _gv[q] ~= gen then local qc = board[q]
                        if qc == BLACK then bT = true elseif qc == WHITE then wT = true
                        else _gv[q] = gen; top = top + 1; _gsp[top] = q end end end
            end
            
            if bT and not wT then 
                bs = bs + cnt
                for k=1,top do tMap[_gsp[k]] = BLACK end
            elseif wT and not bT then 
                ws = ws + cnt 
                for k=1,top do tMap[_gsp[k]] = WHITE end
            end
        end
    end
    return bs, ws, tMap
end

local function calcWinner()
    local bs, ws = calculateScores()
    blackScoreCache, whiteScoreCache = bs, ws
    resultsCached = true
    resultJustOpened = true -- 鎖定輸入，防止從選單穿透過來
    if bs > ws then return (playerColor == BLACK) and "won" or "lost"
    elseif ws > bs then return (playerColor == WHITE) and "won" or "lost"
    else return "draw" end
end

-- ── AI: Influence Map (4-dir ±2, then 8-dir ±1 from each neighbor) 
local _dx4 = {0, 0, 1, -1}
local _dy4 = {1, -1, 0, 0}
-- Loose connection offsets: Knight's move and One-space jump — 12 directions
local _lc_dx = { 1, 1,-1,-1, 2, 2,-2,-2, 2,-2, 0, 0}
local _lc_dy = { 2,-2, 2,-2, 1,-1, 1,-1, 0, 0, 2,-2}
-- Precomputed flattened mask for the nested 4-dir + 8-dir influence spread:
local _inf_dx2 = { -1, 1, -1, 1, -1, 2, 1, -2, -1, 0, 2, 1, 1, 0, -2, 0, 2, -2, 0, -1 }
local _inf_dy2 = { -1, 0, 2, 2, -2, -1, -1, 0, 0, 2, 1, 1, -2, -2, -1, 1, 0, 1, -1, 1 }
local _inf_w2  = {  2, 2, 1, 1,  1,  1,  2,  1,  2, 1, 1, 2, 1, 1,  1, 2, 1, 1,  2, 2 }

local function calcInfluence(b)
    local sz = boardSize; local n = sz * sz
    -- Reuse pre-allocated scratch tables instead of creating new ones
    for i=1,n do _inf[i]=0; _lc[i]=-1 end

    -- Pre-scan liberty counts for all groups (gg writes to _gs/_gsc/_gl)
    for i = 1, n do
        if b[i] ~= EMPTY and _lc[i] == -1 then
            gg(i, b); local libs = _gl
            for k = 1, _gsc do _lc[_gs[k]] = libs end
        end
    end

    for i = 1, n do
        if b[i] ~= EMPTY then
            local w = 2.5; local libs = _lc[i]
            if libs == 2 then w = 0.8 elseif libs == 1 then w = -1.5 end
            local val = (b[i] == BLACK) and w or -w
            _inf[i] = _inf[i] + val
            local x = (i-1) % sz; local y = math.floor((i-1) / sz)
            local s2 = val > 0 and 2 or -2
            local s1 = val > 0 and 1 or -1
            -- 4-directional first level (±2)
            for k = 1, 4 do
                local nx = x + _dx4[k]; local ny = y + _dy4[k]
                if nx >= 0 and nx < sz and ny >= 0 and ny < sz then
                    _inf[ny * sz + nx + 1] = _inf[ny * sz + nx + 1] + s2
                end
            end
            -- Pre-calculated 20-point mask for the nested 8-directional second level
            for k = 1, 20 do
                local nx = x + _inf_dx2[k]; local ny = y + _inf_dy2[k]
                if nx >= 0 and nx < sz and ny >= 0 and ny < sz then
                    _inf[ny * sz + nx + 1] = _inf[ny * sz + nx + 1] + s1 * _inf_w2[k]
                end
            end
        end
    end

    local bs = 0; local ws = KOMI
    for i = 1, n do
        if _inf[i] > 1 then bs = bs + 1 elseif _inf[i] < -1 then ws = ws + 1 end
    end
    return bs - ws, _inf
end

-- ── AI: Rollout Simulation ─────────────────────────────────────────────────────
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

        -- Self-rescue: if opponent's last move put our group in atari, try to escape.
        -- Uses _dx4/_dy4 loop (no closure) to avoid per-depth heap allocation.
        if movePos == 0 and lastPos > 0 then
            local ox=(lastPos-1)%sz; local oy=math.floor((lastPos-1)/sz)
            for _d = 1, 4 do
                if movePos ~= 0 then break end
                local nx=ox+_dx4[_d]; local ny=oy+_dy4[_d]
                if nx>=0 and nx<sz and ny>=0 and ny<sz then
                    local ni=ny*sz+nx+1
                    if b[ni]==toMove and countLibs(ni,b)==1 then
                        gg(ni,b)  -- _gs/_gsc: our atari group; countLibs used _lv/_ls (separate)
                        for k=1,_gsc do
                            if movePos~=0 then break end
                            local sp=_gs[k]; local spx=(sp-1)%sz; local spy=math.floor((sp-1)/sz)
                            -- wouldBeSuicide uses countLibs (_lv/_ls) — safe while _gs is live.
                            if spx>0 then local q=sp-1
                                if b[q]==EMPTY and not wouldBeSuicide(spx-1,spy,toMove,b) then movePos=q end end
                            if movePos==0 and spx<sz-1 then local q=sp+1
                                if b[q]==EMPTY and not wouldBeSuicide(spx+1,spy,toMove,b) then movePos=q end end
                            if movePos==0 and spy>0 then local q=sp-sz
                                if b[q]==EMPTY and not wouldBeSuicide(spx,spy-1,toMove,b) then movePos=q end end
                            if movePos==0 and spy<sz-1 then local q=sp+sz
                                if b[q]==EMPTY and not wouldBeSuicide(spx,spy+1,toMove,b) then movePos=q end end
                        end
                    end
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
            -- Fisher-Yates full shuffle
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
-- Children stored as parallel flat arrays to eliminate per-child table overhead.
-- For 11x11: saves ~120 Lua table objects (~5-8KB of heap metadata).

local mctsC_x   = {}  -- x coordinate
local mctsC_y   = {}  -- y coordinate
local mctsC_P   = {}  -- isPass
local mctsC_v   = {}  -- visit count
local mctsC_w   = {}  -- win count
local mctsC_hs  = {}  -- heuristic score
local mctsC_hc  = {}  -- heuristic computed
local mctsC_count = 0
local mctsRV = 0

local function startMCTS()
    mctsRV = 0
    local sz = boardSize; local mid = math.floor(sz / 2)
    local idx = 1
    for y = 0, sz-1 do
        for x = 0, sz-1 do
            if isValidMove(x, y, aiColor) then
                mctsC_x[idx]=x; mctsC_y[idx]=y; mctsC_P[idx]=false
                mctsC_v[idx]=0; mctsC_w[idx]=0; mctsC_hs[idx]=0; mctsC_hc[idx]=false
                if math.abs(x-mid)<=1 and math.abs(y-mid)<=1 then
                    mctsC_v[idx]=5; mctsC_w[idx]=2.5; mctsRV=mctsRV+5
                end
                idx = idx + 1
            end
        end
    end
    mctsC_x[idx]=0; mctsC_y[idx]=0; mctsC_P[idx]=true
    mctsC_v[idx]=0; mctsC_w[idx]=0; mctsC_hs[idx]=0; mctsC_hc[idx]=true
    mctsC_count = idx
end

-- Compute heuristic score once per child (cached in mctsC_hs / mctsC_hc).
local function computeHScore(i)
    local x, y = mctsC_x[i], mctsC_y[i]
    local sz = boardSize
    local opp = (aiColor == BLACK) and WHITE or BLACK
    local cb, cap, bb, ct, on = 0, 0, 0, 0, 0
    local idx = y * sz + x + 1
    local fn1, fn2 = 0, 0  -- first two AI-colored orthogonal neighbor positions

    local function checkNeighbor(nx, ny)
        if nx < 0 or nx >= sz or ny < 0 or ny >= sz then return end
        local ni = ny * sz + nx + 1
        local nb = board[ni]
        if nb == aiColor then
            gg(ni, board)
            if fn1 == 0 then fn1 = ni elseif fn2 == 0 then fn2 = ni end
            if _gl == 1 then 
                -- Only rescue if it actually increases liberties to > 1 (Smart Rescue)
                board[idx] = aiColor
                local newLibs = countLibs(idx, board)
                board[idx] = EMPTY
                if newLibs > 1 then cb = cb + H_RESCUE_1LIB end
            elseif _gl == 2 then cb = cb + H_RESCUE_2LIB * math.min(_gsc, 5) * 0.5 end  -- 大龍優先保護
        elseif nb ~= EMPTY then
            on = on + 1; bb = bb + H_BLOCK_BASE
            -- Temporarily place stone to check if opponent group gets captured.
            board[idx] = aiColor
            gg(ni, board)  -- reuses _gs/_gl (safe: we're done with nb==aiColor branch)
            if _gl == 0 then cap = cap + H_CAPTURE_BONUS end
            board[idx] = EMPTY
        end
    end

    checkNeighbor(x-1,y); checkNeighbor(x+1,y); checkNeighbor(x,y-1); checkNeighbor(x,y+1)
    if on >= 2 then ct = ct + H_CUT_BIAS end

    -- Rule: Group Connection Bonus (落子連結兩個不同 AI 群組 = 大官子)
    if fn1 > 0 and fn2 > 0 then
        gg(fn1, board); local s1 = _gsc; local genA = _gvG
        if _gv[fn2] ~= genA then   -- fn2 不屬於群組 A = 兩個不同群組
            gg(fn2, board); local s2 = _gsc
            cb = cb + H_CONNECT_BONUS * math.min(math.min(s1, s2), 5) / 5
        end
    end

    -- Rule: Self-Eye Preservation (Don't kill your own group!)
    if isEye(x, y, aiColor, board) then
        cb = cb + H_EYE_PENALTY
    end

    -- Rule: Self-Atari Detection (落子後自己只剩 1 氣 = 自入虎口)
    -- Uses countLibs (_lv/_ls), board temporarily modified then restored.
    board[idx] = aiColor
    if countLibs(idx, board) == 1 then cb = cb + H_SELF_ATARI_PENALTY end
    board[idx] = EMPTY

    -- Rule: Neighborhood Density Checks (8-neighbor scan)
    local fnCount, onCount = 0, 0
    for dy2 = -1, 1 do
        for dx2 = -1, 1 do
            if dx2 ~= 0 or dy2 ~= 0 then
                local nx, ny = x + dx2, y + dy2
                if nx >= 0 and nx < sz and ny >= 0 and ny < sz then
                    local val = board[ny * sz + nx + 1]
                    if val == aiColor then fnCount = fnCount + 1
                    elseif val == opp then onCount = onCount + 1 end
                end
            end
        end
    end
    if onCount >= 5 or (onCount >= 4 and fnCount == 0) then 
        cb = cb + H_INVASION_PENALTY 
    end
    if fnCount >= 5 or (fnCount >= 4 and onCount == 0) then 
        cb = cb + H_TERRITORY_FILL_PENALTY 
    end

    -- Rule: Hotspot Weighting (Ortho +0.15, Diag +0.10)
    if lastMoveX ~= -1 then
        local adx = math.abs(x - lastMoveX)
        local ady = math.abs(y - lastMoveY)
        if (adx == 1 and ady == 0) or (adx == 0 and ady == 1) then
            cb = cb + H_HOTSPOT_ORTHO
        elseif adx == 1 and ady == 1 then
            cb = cb + H_HOTSPOT_DIAG
        end
    end

    -- Strategic Layout Bias (Tiered based on Go principles)
    local dfe = math.min(x, y, sz - 1 - x, sz - 1 - y)
    local layoutBonus = 0
    if dfe == 2 then layoutBonus = H_CENTRAL_BIAS          -- 3rd Line (Golden): 100%
    elseif dfe == 3 then layoutBonus = H_CENTRAL_BIAS * 0.8 -- 4th Line (Silver): 80%
    elseif dfe >= 4 then layoutBonus = H_CENTRAL_BIAS * 0.6 -- Center: 60%
    elseif dfe == 1 then layoutBonus = H_CENTRAL_BIAS * 0.2 -- 2nd Line: 20%
    end
    -- Star point bonus (補上星位專屬加成，與 renderBoard star() 座標一致)
    if sz == 7 then
        if x==3 and y==3 then layoutBonus = layoutBonus + H_CENTRAL_BIAS end             -- 天元
    elseif sz == 9 then
        if x==4 and y==4 then layoutBonus = layoutBonus + H_CENTRAL_BIAS * 0.8           -- 天元
        elseif (x==2 or x==6) and (y==2 or y==6) then layoutBonus = layoutBonus + H_CENTRAL_BIAS * 0.5 end -- 角星
    elseif sz == 11 then
        if x==5 and y==5 then layoutBonus = layoutBonus + H_CENTRAL_BIAS * 0.8           -- 天元
        elseif (x==2 or x==8) and (y==2 or y==8) then layoutBonus = layoutBonus + H_CENTRAL_BIAS * 0.5 end -- 角星
    end

    -- Loose connection preference (鬆散連絡): check knight's move and one-space jump positions
    local lc = 0
    for k = 1, 12 do
        local nx=x+_lc_dx[k]; local ny=y+_lc_dy[k]
        if nx>=0 and nx<sz and ny>=0 and ny<sz then
            if board[ny*sz+nx+1]==aiColor then lc=H_LOOSE_CONNECT; break end
        end
    end

    mctsC_hs[i] = cb + cap + bb + ct + layoutBonus + lc
    mctsC_hc[i] = true
end

local function selectChild(explore)
    local bestI = -1; local bestS = -1e18
    local logV = math.log(mctsRV > 0 and mctsRV or 1)
    for i = 1, mctsC_count do
        local v = mctsC_v[i]
        local s
        if explore == 0 then
            s = v
        elseif v == 0 then
            s = 10000.0
        else
            s = (mctsC_w[i] / v) + explore * math.sqrt(logV / v)
        end
        if not mctsC_P[i] then
            if not mctsC_hc[i] then computeHScore(i) end
            if explore == 0 then s = s + mctsC_hs[i] * AI_HEURISTIC_WEIGHT else s = s + mctsC_hs[i] end
        end
        if s > bestS then bestS = s; bestI = i end
    end
    return bestI
end

-- Run all MCTS simulations (flat tree: select child → apply move → rollout → backprop).
local function runMCTS()
    local sz = boardSize; local n = sz * sz
    local opp = (aiColor == BLACK) and WHITE or BLACK
    -- Adaptive sim count: fewer sims for larger boards to stay within memory budget
    local maxSims = (boardSize == 11) and AI_MAX_SIMS_11 or (boardSize == 9) and AI_MAX_SIMS_9 or AI_MAX_SIMS_7

    for sim = 1, maxSims do
        -- Copy real board to sim scratch board
        for i = 1, n do _simB[i] = board[i] end

        -- Select best child from root (UCB + heuristics)
        local ci = selectChild(AI_EXPLORATION_C)
        if ci < 0 then break end

        -- Apply AI's move to simBoard
        if not mctsC_P[ci] then
            local cidx = mctsC_y[ci] * sz + mctsC_x[ci] + 1
            _simB[cidx] = aiColor
            captureOnBoard(mctsC_x[ci], mctsC_y[ci], aiColor, _simB)
        end

        -- Rollout: opponent plays next after AI's move
        local result = simulate(_simB, opp)

        -- Backpropagate to child and root
        mctsC_v[ci] = mctsC_v[ci] + 1
        mctsC_w[ci] = mctsC_w[ci] + result
        mctsRV = mctsRV + 1
    end
end

local function finishMCTS()
    local bi = selectChild(0)
    if bi < 0 then return -1 end
    -- Auto-pass if win rate < 5%
    if mctsC_v[bi] > 0 and (mctsC_w[bi] / mctsC_v[bi]) < 0.05 then return -1 end
    return bi
end

local function applyAIMove(bi)
    local sz = boardSize; local n = sz * sz
    for i = 1, n do lastBoard[i] = board[i] end
    if bi > 0 and not mctsC_P[bi] then
        local idx = mctsC_y[bi] * sz + mctsC_x[bi] + 1
        board[idx] = aiColor
        captureOnBoard(mctsC_x[bi], mctsC_y[bi], aiColor, board)
        lastMoveX, lastMoveY = mctsC_x[bi], mctsC_y[bi]
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
    local sw = gui.width()
    local sh = gui.height()

    -- 1. Initial Size Selection: Draw ONLY the menu and return
    if showSizeSelection then
        local mw,mh=400,250; local mx=(sw-mw)/2; local myo=(sh-mh)/2
        gui.fillRoundedRect(mx,myo,mw,mh,15,false); gui.drawRoundedRect(mx,myo,mw,mh,3,15)
        gui.drawCenteredText(FONT_UI_12, myo+40, "Select Board Size", true)
        local labels={"7x7","9x9","11x11"}
        for i=0,2 do
            local bx=mx+30+(i*115); local by=myo+110; local bw,bh2=100,70
            local lbl=labels[i+1]
            local tx=bx+math.floor((bw-gui.getTextWidth(FONT_UI_12,lbl))/2); local ty=by+25
            if sizeIdx==i then gui.fillRoundedRect(bx,by,bw,bh2,15); gui.drawText(FONT_UI_12,tx,ty,lbl,false)
            else gui.drawRoundedRect(bx,by,bw,bh2,2,15); gui.drawText(FONT_UI_12,tx,ty,lbl,true) end
        end
        gui.drawButtonHints("<<", "o", "<", ">")
        gui.refresh(REFRESH_FAST)
        return
    end

    -- 2. Standard Game Drawing
    gui.drawText(FONT_UI_12, 20, 20, "Mini Go", true)
    gui.drawLine(0, 60, sw, 60, 3)
    gui.drawText(FONT_UI_12, 20, 80, boardSize .. "x" .. boardSize, true)

    local margin = 60
    local bds = sw - margin * 2
    local cs = math.floor(bds / (boardSize - 1))
    local sx, sy = margin, 200

    -- Grid
    for i = 0, boardSize - 1 do
        local thick = (i == 0 or i == boardSize - 1) and 4 or 2
        local half = math.floor(thick / 2)
        gui.fillRect(sx,             sy + i*cs - half, bds + 2, thick)  -- horizontal
        gui.fillRect(sx + i*cs - half, sy,             thick,   bds + 2) -- vertical
    end

    -- Star points
    local function star(stx, sty)
        local px = sx + stx*cs; local py = sy + sty*cs
        gui.fillCircle(px, py, 5)
    end
    if boardSize == 7 then star(3,3)
    elseif boardSize == 9 then star(2,2);star(6,2);star(4,4);star(2,6);star(6,6)
    elseif boardSize == 11 then star(2,2);star(8,2);star(5,5);star(2,8);star(8,8) end

    -- Stones
    for y = 0, boardSize-1 do
        for x = 0, boardSize-1 do
            local c = board[y * boardSize + x + 1]
            if c ~= EMPTY then
                local px = sx + x*cs; local py = sy + y*cs
                local r = math.floor(cs/2) - 2
                if c == BLACK then
                    gui.fillCircle(px, py, r)
                else
                    gui.fillCircle(px, py, r, false)
                    gui.drawCircle(px, py, r, 2)
                end
                if not lastMovePass and lastMoveX == x and lastMoveY == y then
                    gui.drawCircle(px, py, 4, 2, (c == WHITE))
                end
            end
        end
    end

    -- Territory Markers
    local showMarks = (status ~= "playing") or (showHint and not inEscMenu and not showHandicapSelection)
    if showMarks then
        local tMap
        if status ~= "playing" then
            _, _, tMap = calculateScores()
        else
            _, tMap = calcInfluence(board)
        end
        
        for y = 0, boardSize-1 do
            for x = 0, boardSize-1 do
                local i = y*boardSize + x + 1
                if board[i] == EMPTY then
                    local px, py = sx + x*cs, sy + y*cs
                    local isBlack, isWhite = false, false
                    if status ~= "playing" then
                        isBlack = (tMap[i] == BLACK)
                        isWhite = (tMap[i] == WHITE)
                    else
                        isBlack = (tMap[i] > 1)
                        isWhite = (tMap[i] < -1)
                    end

                    if isBlack then
                        gui.fillRect(px-6, py-6, 13, 13)
                    elseif isWhite then
                        gui.fillRect(px-6, py-6, 13, 13, false)
                        gui.drawRect(px-6, py-6, 13, 13, true)
                    end
                end
            end
        end
    end

    -- Cursor + Influence Bias Text
    if status == "playing" and not inEscMenu and not showHandicapSelection then
        local cx = sx + cursorX*cs; local cy2 = sy + cursorY*cs
        local stoneR = math.floor(cs/2) - 2
        local r = math.floor(stoneR * 0.7)
        gui.fillCircle(cx, cy2, r, COLOR_LIGHT_GRAY)
        
        local diff = calcInfluence(board)
        gui.drawCenteredText(FONT_SMALL, sy + bds + 25, string.format("Territory Bias: %+.1f", diff))
    end

    -- Status text area
    if isAiThinking then
        gui.drawText(FONT_SMALL, 340, 80, "AI Thinking...", true)
        drawMumble(FONT_UI_12, 640, aiMumbles[aiMumbleIdx], true)
    elseif status == "playing" and not showHandicapSelection and not inEscMenu then
        if lastMovePass then gui.drawText(FONT_UI_12, 340, 50, "AI PASS!", true) end
        gui.drawText(FONT_SMALL, 340, 80, "Your Turn (White)", true)
        drawMumble(FONT_UI_12, 640, playerMumbles[playerMumbleIdx], true)
    end

    -- Overlays
    if showHandicapSelection then
        local mw,mh=400,250; local mx=(sw-mw)/2; local sh=gui.height(); local myo=(sh-mh)/2
        gui.fillRoundedRect(mx,myo,mw,mh,15,false); gui.drawRoundedRect(mx,myo,mw,mh,3,15)
        gui.drawCenteredText(FONT_UI_12, myo+30, "AI Handicap", true)
        local labels={"2","3","4","Chaos"}
        for i=0,3 do
            local bx=mx+16+(i*96); local by=myo+100; local bw,bh2=80,80
            local lbl=labels[i+1]
            local tx=bx+math.floor((bw-gui.getTextWidth(FONT_UI_12,lbl))/2); local ty=by+30
            if handicapIdx==i then gui.fillRoundedRect(bx,by,bw,bh2,10); gui.drawText(FONT_UI_12,tx,ty,lbl,false)
            else gui.drawRoundedRect(bx,by,bw,bh2,2,10); gui.drawText(FONT_UI_12,tx,ty,lbl,true) end
        end
        gui.drawButtonHints("<<", "o", "<", ">")
    elseif inEscMenu then
        local mw,mh=340,350; local mx=(sw-mw)/2; local sh=gui.height(); local myo=(sh-mh)/2
        gui.fillRoundedRect(mx,myo,mw,mh,10,false); gui.drawRoundedRect(mx,myo,mw,mh,2,10)
        gui.drawCenteredText(FONT_UI_12, myo+12, "Game Menu", true)
        local opts={"Resume","Hint: "..(showHint and "On" or "Off"),"New Game","Pass Turn","Exit Game"}
        for i,opt in ipairs(opts) do
            local ry=myo+55+(i-1)*56
            local tx=mx+10+math.floor((320-gui.getTextWidth(FONT_UI_12,opt))/2)
            if escIdx==i-1 then gui.fillRoundedRect(mx+10,ry-5,320,46,8); gui.drawText(FONT_UI_12,tx,ry+12,opt,false)
            else gui.drawText(FONT_UI_12,tx,ry+12,opt,true) end
        end
        gui.drawButtonHints("<<", "o", "<", ">")
    elseif status ~= "playing" then
        local byo = sy + bds + 20
        local resStr = (status == "won") and "VICTORY!" or (status == "draw") and "DRAW." or "DEFEAT."
        gui.drawCenteredText(FONT_UI_12, byo, resStr, true)
        local sc = string.format("Black:%.1f  White:%.1f", blackScoreCache, whiteScoreCache)
        gui.drawCenteredText(FONT_SMALL, byo + 45, sc, true)
        
        local btns = {"New Game", "Exit"}
        local bw, bh2 = 180, 55
        for i = 0, 1 do
            local bx = (i == 0) and (sw/2 - bw - 10) or (sw/2 + 10)
            local by = byo + 85
            local lbl = btns[i+1]
            local tx = bx + math.floor((bw - gui.getTextWidth(FONT_UI_12, lbl)) / 2)
            local ty = by + 12
            if postMenuIdx == i then
                gui.fillRoundedRect(bx, by, bw, bh2, 10)
                gui.drawText(FONT_UI_12, tx, ty, lbl, false)
            else
                gui.drawRoundedRect(bx, by, bw, bh2, 2, 10)
                gui.drawText(FONT_UI_12, tx, ty, lbl, true)
            end
        end
        gui.drawButtonHints("<<", "o", "<", ">")
    end

    gui.refresh(REFRESH_FAST)
end

-- ── Input Handlers ─────────────────────────────────────────────────────────────

local function resetGame(size)
    boardSize = size
    local n = size * size
    -- Zero-allocation: reuse the existing arrays to avoid GC pressure/OOM
    -- Pre-allocate ALL scratch buffers to n entries so no dynamic growth occurs at runtime
    for i=1,n do 
        board[i] = EMPTY
        lastBoard[i] = EMPTY
        _simB[i] = EMPTY
        _gv[i] = 0
        _lv[i] = 0
        _tMap[i] = EMPTY
        _gsp[i] = 0
        _gs[i] = 0
        _ls[i] = 0
        _emp[i] = 0
        _inf[i] = 0
        _lc[i] = -1
    end
    -- Pre-allocate MCTS child parallel arrays (max n+1 entries: one per intersection + pass)
    local n1 = n + 1
    for i=1,n1 do
        mctsC_x[i]=0; mctsC_y[i]=0; mctsC_P[i]=false
        mctsC_v[i]=0; mctsC_w[i]=0; mctsC_hs[i]=0; mctsC_hc[i]=false
    end
    consecutivePasses = 0
    status = "playing"
    cursorX, cursorY = math.floor(size/2), math.floor(size/2)
    lastMoveX, lastMoveY = -1, -1
    lastMovePass = false
    isAiThinking = false; aiShowedThinking = false; mctsDone = 0
    mctsC_count = 0
    resultsCached = false
    needsDraw = true
    playerMumbleIdx = math.random(#playerMumbles)
    -- Ensure ghosting from previous game numbers is cleared.
    gui.refresh(REFRESH_HALF)
end

local function handleSizeInput()
    if input.wasReleased("left") then
        sizeIdx = (sizeIdx > 0) and sizeIdx - 1 or 2; needsDraw = true
    elseif input.wasReleased("right") then
        sizeIdx = (sizeIdx < 2) and sizeIdx + 1 or 0; needsDraw = true
    elseif input.wasReleased("confirm") then
        local sizes = {7, 9, 11}; local size = sizes[sizeIdx + 1]
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
            local avail = {}; local ac = boardSize * boardSize
            for i = 1, ac do avail[i] = i end
            local function placeRandom(count, color)
                for k = 1, count do
                    if ac == 0 then break end
                    local r = math.random(ac)
                    local pos = avail[r]
                    avail[r] = avail[ac]
                    avail[ac] = nil
                    ac = ac - 1
                    local x=(pos-1)%boardSize; local y=math.floor((pos-1)/boardSize)
                    board[pos] = color; lastMoveX,lastMoveY=x,y
                end
            end
            local blackCount = 5
            if boardSize == 9 then blackCount = 6
            elseif boardSize == 11 then blackCount = 8 end
            placeRandom(blackCount, BLACK)
            placeRandom(2, WHITE)
        else
            local count = handicapIdx + 2
            local p1 = 2
            local p2 = (boardSize == 11) and 8 or (boardSize == 9) and 6 or 4
            if count >= 1 then board[p2*boardSize+p2+1]=BLACK; lastMoveX,lastMoveY=p2,p2 end
            if count >= 2 then board[p1*boardSize+p1+1]=BLACK; lastMoveX,lastMoveY=p1,p1 end
            if count >= 3 then board[p1*boardSize+p2+1]=BLACK; lastMoveX,lastMoveY=p2,p1 end
            if count >= 4 then board[p2*boardSize+p1+1]=BLACK; lastMoveX,lastMoveY=p1,p2 end
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
        elseif escIdx == 1 then showHint = not showHint
        elseif escIdx == 2 then showSizeSelection = true; inEscMenu=false
        elseif escIdx == 3 then
            -- 玩家 Pass 即立刻結束遊戲並計分
            inEscMenu = false
            status = calcWinner()
            needsDraw = true
        elseif escIdx == 4 then sys.exit() end
        needsDraw = true
    elseif input.wasReleased("back") then inEscMenu = false; needsDraw = true end
end

local function handleResultInput()
    if resultJustOpened then
        -- 只有當玩家放開所有按鍵後，才開始接收結算畫面的輸入
        if not input.isPressed("confirm") then
            resultJustOpened = false
        end
        return
    end

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
    math.randomseed(sys.uptime() or os.time() or 0)
    showSizeSelection = true; sizeIdx = 0
    resetGame(7)
    log("MiniGo init OK")
end
