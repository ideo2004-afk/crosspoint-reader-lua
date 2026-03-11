-- Flashcard TXT Edition
-- DESCRIPTION: Study vocabulary from plain-text decks. [v4.2]
-- File format  : one card per line  →  word|pos|meaning|example_en|example_zh
--                optional first line →  #title:Deck Name
-- Controls:
--   Deck select : Up/Down = scroll, Confirm = open, Back = exit
--   Card view   : Confirm = flip A↔B, Up/Down = sequential,
--                 Left/Right = random, Back = menu

local PLUGIN_PATH = "/plugins/Flashcard"

local STATE_DECK = 1
local STATE_CARD = 2

local state     = STATE_DECK
local decks     = {}
local titles    = {}
local deckIdx   = 1

local cards     = {}   -- {word, pos, meaning, ex_en, ex_zh}
local cardIdx   = 1
local showBack  = false
local deckTitle = ""
local errorMsg  = nil
local needsDraw = true

-- ── Helpers ───────────────────────────────────────────────────────────────────

local function wrapText(font, text, maxW)
    local lines = {}
    local cur   = ""
    -- Use UTF-8 pattern to iterate through each character (including multi-byte Chinese)
    for char in text:gmatch("[\1-\127\194-\244][\128-\191]*") do
        local test = cur .. char
        if gui.getTextWidth(font, test) <= maxW then
            cur = test
        else
            if cur ~= "" then table.insert(lines, cur) end
            cur = (char == " ") and "" or char
        end
    end
    if cur ~= "" then table.insert(lines, cur) end
    return lines
end

-- ── TXT parse ─────────────────────────────────────────────────────────────────

local function parseLine(line)
    -- strip CR
    if line:sub(-1) == "\r" then line = line:sub(1, -2) end
    if line == "" then return end
    if line:sub(1, 1) == "#" then
        local t = line:match("^#title:(.+)$")
        if t then deckTitle = t end
        return
    end
    local f1, f2, f3, f4, f5 = line:match("^([^|]*)|([^|]*)|([^|]*)|([^|]*)|(.*)$")
    if f1 and f1 ~= "" then
        table.insert(cards, {
            word    = f1,
            pos     = f2 or "",
            meaning = f3 or "",
            ex_en   = f4 or "",
            ex_zh   = f5 or "",
        })
    end
end

local function loadDeck(filename)
    errorMsg  = nil
    cards     = {}
    collectgarbage("collect")
    deckTitle = filename:sub(1, -5)
    local path    = PLUGIN_PATH .. "/" .. filename
    local content = fs.readFile(path)

    if not content or content == "" then
        errorMsg = "Cannot read:\n" .. filename
        return
    end

    -- Iterate lines using find() to avoid duplicating the content string
    local pos = 1
    local len = #content
    while pos <= len do
        local nl = content:find("\n", pos, true)
        local line
        if nl then
            line = content:sub(pos, nl - 1)
            pos  = nl + 1
        else
            line = content:sub(pos)
            pos  = len + 1
        end
        parseLine(line)
    end

    if #cards == 0 then
        errorMsg = "No cards found in:\n" .. filename
    end
end

local function loadDeckList()
    local files = fs.listFiles(PLUGIN_PATH)
    local raw = {}
    for _, f in ipairs(files) do
        -- Use sub() to avoid :lower() on non-ASCII filenames (undefined on ESP32)
        if f:sub(-4) == ".txt" then
            table.insert(raw, f)
        end
    end
    table.sort(raw)
    decks  = {}
    titles = {}
    for _, f in ipairs(raw) do
        table.insert(decks,  f)
        table.insert(titles, f:sub(1, -5))  -- strip ".txt"
    end
end

-- ── Rendering ─────────────────────────────────────────────────────────────────

local function drawDeckSelect()
    local sw = gui.width()
    gui.clear()
    gui.drawCenteredText(FONT_BOOKERLY_14, 36, "Flashcard", COLOR_BLACK, STYLE_BOLD)
    gui.drawLine(24, 66, sw - 24, 66, 1, COLOR_BLACK)

    if #decks == 0 then
        gui.drawCenteredText(FONT_UI_10, 260, "No .txt files found in", COLOR_BLACK, STYLE_REGULAR)
        gui.drawCenteredText(FONT_SMALL,  290, PLUGIN_PATH,             COLOR_BLACK, STYLE_REGULAR)
    else
        local y = 90
        for i = 1, #decks do
            local title = titles[i]
            if i == deckIdx then
                gui.fillRoundedRect(24, y - 4, sw - 48, 44, 8, COLOR_BLACK)
                gui.drawCenteredText(FONT_BOOKERLY_12, y + 14, title, COLOR_WHITE, STYLE_BOLD)
            else
                gui.drawRoundedRect(24, y - 4, sw - 48, 44, 1, 8, COLOR_BLACK)
                gui.drawCenteredText(FONT_BOOKERLY_12, y + 14, title, COLOR_BLACK, STYLE_REGULAR)
            end
            y = y + 58
        end
    end

    gui.drawButtonHints("Exit", "Open", "Up", "Down")
    gui.refresh(REFRESH_FAST)
end

local function drawCard()
    local c   = cards[cardIdx]
    local sw  = gui.width()
    local pad = 24
    local maxW = sw - pad * 2
    gui.clear()

    -- Header (shifted down 20px)
    gui.drawText(FONT_UI_10, pad, 36, deckTitle, COLOR_BLACK, STYLE_BOLD)
    local idx_str = tostring(cardIdx) .. " / " .. tostring(#cards)
    local iw = gui.getTextWidth(FONT_UI_10, idx_str)
    gui.drawText(FONT_UI_10, sw - pad - iw, 36, idx_str, COLOR_BLACK, STYLE_REGULAR)
    gui.drawLine(pad, 62, sw - pad, 62, 1, COLOR_BLACK)

    -- Word + pos (same position on both A and B sides)
    local ww = gui.getTextWidth(FONT_BOOKERLY_14, c.word)
    gui.drawText(FONT_BOOKERLY_14, (sw - ww) / 2, 210, c.word, COLOR_BLACK, STYLE_BOLD)
    if c.pos ~= "" then
        gui.drawCenteredText(FONT_BOOKERLY_12, 252, "(" .. c.pos .. ")", COLOR_BLACK, STYLE_REGULAR)
    end

    if showBack then
        -- B-side: meaning + examples below word/pos
        local y = 300
        gui.drawLine(pad, y, sw - pad, y, 1, COLOR_BLACK)
        y = y + 24

        for _, ln in ipairs(wrapText(FONT_BOOKERLY_12, c.meaning, maxW)) do
            gui.drawText(FONT_BOOKERLY_12, pad, y, ln, COLOR_BLACK, STYLE_BOLD)
            y = y + 32
        end
        y = y + 24

        if c.ex_en ~= "" then
            for _, ln in ipairs(wrapText(FONT_BOOKERLY_12, c.ex_en, maxW)) do
                gui.drawText(FONT_BOOKERLY_12, pad, y, ln, COLOR_BLACK, STYLE_REGULAR)
                y = y + 32
            end
        end
        if c.ex_zh ~= "" then
            y = y + 24
            for _, ln in ipairs(wrapText(FONT_BOOKERLY_12, c.ex_zh, maxW)) do
                gui.drawText(FONT_BOOKERLY_12, pad, y, ln, COLOR_BLACK, STYLE_REGULAR)
                y = y + 32
            end
        end
    end

    gui.drawButtonHints("<<", "o", "<", ">")
    gui.refresh(REFRESH_FAST)
end

local function drawError()
    local sw = gui.width()
    gui.clear()
    gui.drawCenteredText(FONT_BOOKERLY_14, 180, "Error", COLOR_BLACK, STYLE_BOLD)
    local y = 230
    for _, ln in ipairs(wrapText(FONT_UI_10, errorMsg, sw - 48)) do
        gui.drawCenteredText(FONT_UI_10, y, ln, COLOR_BLACK, STYLE_REGULAR)
        y = y + 28
    end
    gui.drawButtonHints("Back", "", "", "")
    gui.refresh(REFRESH_FAST)
end

-- ── Lifecycle ─────────────────────────────────────────────────────────────────

function init()
    math.randomseed(sys.millis())
    loadDeckList()
    needsDraw = true
end

function draw()
    -- ── Input handling ────────────────────────────────────────────────────────
    if errorMsg then
        if input.wasReleased("back") then
            errorMsg  = nil
            state     = STATE_DECK
            needsDraw = true
        end
    elseif state == STATE_DECK then
        if input.wasReleased("back") then sys.exit(); return end
        if input.wasReleased("up") or input.wasReleased("page_back") then
            if #decks > 0 then deckIdx = deckIdx > 1 and deckIdx - 1 or #decks end
            needsDraw = true
        end
        if input.wasReleased("down") or input.wasReleased("page_forward") then
            if #decks > 0 then deckIdx = deckIdx % #decks + 1 end
            needsDraw = true
        end
        if input.wasReleased("confirm") and #decks > 0 then
            loadDeck(decks[deckIdx])
            if not errorMsg and #cards > 0 then
                cardIdx  = math.random(#cards)
                showBack = false
                state    = STATE_CARD
            end
            needsDraw = true
        end
    else
        if input.wasReleased("back") then
            -- Free cards while idle at deck select; GC is safe here
            cards     = {}
            collectgarbage("collect")
            state     = STATE_DECK
            showBack  = false
            needsDraw = true
        end
        if input.wasReleased("confirm") then
            showBack  = not showBack
            needsDraw = true
        end
        if input.wasReleased("up") then
            cardIdx  = cardIdx > 1 and cardIdx - 1 or #cards
            showBack = false
            needsDraw = true
        end
        if input.wasReleased("down") then
            cardIdx  = cardIdx % #cards + 1
            showBack = false
            needsDraw = true
        end
        if input.wasReleased("left") or input.wasReleased("right") then
            cardIdx  = math.random(#cards)
            showBack = false
            needsDraw = true
        end
    end

    -- ── Rendering ─────────────────────────────────────────────────────────────
    if not needsDraw then return end
    needsDraw = false

    if errorMsg then
        drawError()
    elseif state == STATE_DECK then
        drawDeckSelect()
    else
        drawCard()
    end
end
