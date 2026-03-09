-- Flashcard Plugin
-- Deck structure: /plugins/flashcard/<DeckName>/NNN_a.bmp + NNN_b.bmp
-- Progress:       /plugins/flashcard/<DeckName>/progress.txt (one learned sideA path per line)

local PLUGIN_PATH = "/plugins/flashcard"

local STATE_DECK = 1
local STATE_CARD = 2

local state      = STATE_DECK
local decks      = {}
local deckIdx    = 1

local cards      = {}   -- {sideA, sideB, learned}
local cardIdx    = 1
local showBack   = false
local deckName   = ""
local needsDraw  = true
local learnedMsg = false

-- ── helpers ───────────────────────────────────────────────────────────────────

local function endsWith(s, ext)
    return s:sub(-#ext):lower() == ext
end

local function progressFile()
    return PLUGIN_PATH .. "/" .. deckName .. "/progress.txt"
end

local function saveProgress()
    local lines = {}
    for _, c in ipairs(cards) do
        if c.learned then lines[#lines + 1] = c.sideA end
    end
    fs.writeFile(progressFile(), table.concat(lines, "\n"))
end

local function loadProgress()
    local content = fs.readFile(progressFile())
    if not content then return end
    local learned = {}
    for line in (content .. "\n"):gmatch("([^\n]*)\n") do
        if line ~= "" then learned[line] = true end
    end
    for _, c in ipairs(cards) do
        if learned[c.sideA] then c.learned = true end
    end
end

-- ── deck / card loading ───────────────────────────────────────────────────────

local function loadDecks()
    decks = fs.listDirs(PLUGIN_PATH)
    table.sort(decks)
end

local function loadCards(name)
    deckName  = name
    cards     = {}
    showBack  = false
    local dir = PLUGIN_PATH .. "/" .. name

    local files = fs.listFiles(dir)
    table.sort(files)

    -- collect sideA files
    local sideAs = {}
    local fileSet = {}
    for _, f in ipairs(files) do
        fileSet[f] = true
        if (endsWith(f, ".bmp")) and (f:find("_a%.") or f:find("_A%.")) then
            sideAs[#sideAs + 1] = f
        end
    end

    for _, fa in ipairs(sideAs) do
        local fb = fa:gsub("(_[aA])(%.[Bb][Mm][Pp])$", function(mid, ext)
            return mid:lower():gsub("_a", "_b") .. ext
        end)
        cards[#cards + 1] = {
            sideA   = dir .. "/" .. fa,
            sideB   = fileSet[fb] and (dir .. "/" .. fb) or nil,
            learned = false,
        }
    end

    loadProgress()
    log("Loaded " .. #cards .. " cards from " .. name)
end

-- ── random pick (unlearned) ───────────────────────────────────────────────────

local function pickRandom()
    local pool = {}
    for i, c in ipairs(cards) do
        if not c.learned then pool[#pool + 1] = i end
    end
    if #pool == 0 then return nil end
    return pool[math.random(#pool)]
end

local function countLearned()
    local n = 0
    for _, c in ipairs(cards) do if c.learned then n = n + 1 end end
    return n
end

-- ── rendering ─────────────────────────────────────────────────────────────────

local function drawDeckSelect()
    gui.clear()
    gui.drawCenteredText(FONT_UI_12, 50, "Flashcards - Select Deck")
    gui.drawLine(20, 82, gui.width() - 20, 82, 2)

    local y = 110
    for i, name in ipairs(decks) do
        if i == deckIdx then
            gui.fillRoundedRect(30, y - 6, gui.width() - 60, 44, 8)
            gui.drawText(FONT_UI_10, 50, y + 10, name, false)
        else
            gui.drawText(FONT_UI_10, 50, y + 10, name, true)
        end
        y = y + 55
    end

    gui.drawButtonHints("«", "o", "<", ">")
    gui.refresh(REFRESH_FAST)
end

local function drawCard()
    local card = cards[cardIdx]
    local path = showBack and card.sideB or card.sideA
    if not path then return end

    gui.setOrientation("landscape_ccw")
    gui.clear()
    gui.drawBmp(path)

    -- overlay: deck name top-left, status bottom-left
    local h = gui.height()
    gui.drawText(FONT_UI_10, 10, 8, deckName, true)
    local status = cardIdx .. "/" .. #cards .. "  Learned: " .. countLearned()
    gui.drawText(FONT_SMALL, 10, h - 32, status, true)

    gui.setOrientation("portrait")
    gui.drawButtonHints("«", "o", "<", ">")
    gui.refresh(REFRESH_FAST)
end

local function drawLearnedMsg()
    local card = cards[cardIdx]
    local path = showBack and card.sideB or card.sideA
    if not path then path = card.sideA end

    gui.setOrientation("landscape_ccw")
    gui.clear()
    gui.drawBmp(path)

    local w = gui.width()
    local h = gui.height()

    -- floating popup centered on landscape screen
    local pw = math.floor(w * 0.62)
    local ph = 130
    local px = math.floor((w - pw) / 2)
    local py = math.floor((h - ph) / 2)

    gui.fillRoundedRect(px, py, pw, ph, 14, false)
    gui.drawRoundedRect(px, py, pw, ph, 3, 14)
    gui.drawCenteredText(FONT_UI_12, py + 42, "You have learned this card.")
    gui.drawCenteredText(FONT_SMALL,  py + 88, "Learned: " .. countLearned() .. " / " .. #cards)

    gui.setOrientation("portrait")
    gui.drawButtonHints("«", "o", "", "")
    gui.refresh(REFRESH_FAST)
end

local function drawAllLearned()
    gui.clear()
    gui.drawCenteredText(FONT_UI_12, 340, "Deck Complete!")
    gui.drawCenteredText(FONT_UI_10, 400, "All " .. #cards .. " cards learned.")
    gui.drawButtonHints("«", "", "", "")
    gui.refresh(REFRESH_FAST)
end

local function drawNoCards()
    gui.clear()
    gui.drawCenteredText(FONT_UI_12, 360, "No cards found")
    gui.drawCenteredText(FONT_SMALL, 420, "Add NNN_a.bmp / NNN_b.bmp files")
    gui.drawButtonHints("«", "", "", "")
    gui.refresh(REFRESH_FAST)
end

-- ── main ──────────────────────────────────────────────────────────────────────

function init()
    loadDecks()
    if #decks == 0 then
        log("No decks found in " .. PLUGIN_PATH)
    end
    needsDraw = true
    log("Flashcard init: " .. #decks .. " decks")
end

function draw()
    -- ── input (always checked every loop) ────────────────────────────────────
    if state == STATE_DECK then
        if input.wasReleased("back") then
            sys.exit(); return
        elseif input.wasReleased("up") or input.wasReleased("page_back") then
            deckIdx = math.max(1, deckIdx - 1); needsDraw = true
        elseif input.wasReleased("down") or input.wasReleased("page_forward") then
            deckIdx = math.min(#decks, deckIdx + 1); needsDraw = true
        elseif input.wasReleased("confirm") and #decks > 0 then
            loadCards(decks[deckIdx])
            cardIdx  = pickRandom() or 1
            state    = STATE_CARD
            needsDraw = true
        end

    else -- STATE_CARD
        if learnedMsg then
            -- any key dismisses the message and advances to next card
            if input.wasReleased("back") or input.wasReleased("confirm") or
               input.wasReleased("left") or input.wasReleased("right") or
               input.wasReleased("up") or input.wasReleased("page_back") or
               input.wasReleased("down") or input.wasReleased("page_forward") then
                learnedMsg = false
                local next = pickRandom()
                if next then cardIdx = next end
                showBack = false; needsDraw = true
            end
        else
            if input.wasReleased("back") then
                state = STATE_DECK; needsDraw = true
            elseif input.wasReleased("confirm") then
                -- mark current card as learned, show message
                cards[cardIdx].learned = true
                saveProgress()
                learnedMsg = true; needsDraw = true
            elseif input.wasReleased("left") then
                -- flip card
                if cards[cardIdx].sideB then
                    showBack = not showBack; needsDraw = true
                end
            elseif input.wasReleased("right") then
                -- random unlearned card
                local next = pickRandom()
                if next then cardIdx = next; showBack = false; needsDraw = true end
            elseif input.wasReleased("up") or input.wasReleased("page_back") then
                cardIdx = math.min(#cards, cardIdx + 1); showBack = false; needsDraw = true
            elseif input.wasReleased("down") or input.wasReleased("page_forward") then
                cardIdx = math.max(1, cardIdx - 1); showBack = false; needsDraw = true
            end
        end
    end

    -- ── render (only when needed) ─────────────────────────────────────────────
    if not needsDraw then return end
    needsDraw = false

    if state == STATE_DECK then
        drawDeckSelect()
    elseif #cards == 0 then
        drawNoCards()
    elseif learnedMsg then
        drawLearnedMsg()
    elseif countLearned() == #cards then
        drawAllLearned()
    else
        drawCard()
    end
end
