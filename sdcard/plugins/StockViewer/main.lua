-- StockViewer
-- DESCRIPTION: Live stock portfolio viewer (Yahoo Finance)

local CONFIG_PATH = "/plugins/StockViewer/stocks.json"

local DEFAULT_STOCKS = {
    {symbol = "2330.TW", name = "TSMC"},
    {symbol = "INTC",    name = "Intel"},
    {symbol = "TSLA",    name = "Tesla"},
}

-- ── State ───────────────────────────────────────────────────────────────────
local STATE_CONNECTING = 1
local STATE_FETCHING   = 2
local STATE_DONE       = 3
local STATE_ERROR      = 4

local state        = STATE_CONNECTING
local fetchIdx     = 1
local fetchShown   = false
local connectTimer = 0
local needsDraw    = true
local stocks       = {}
local results      = {}
local updateTime   = ""

-- ── Config ──────────────────────────────────────────────────────────────────
local function loadConfig()
    local content = fs.readFile(CONFIG_PATH)
    if content then
        local loaded = {}
        for sym, name in content:gmatch(
                '"symbol"%s*:%s*"([^"]+)"%s*,%s*"name"%s*:%s*"([^"]+)"') do
            table.insert(loaded, {symbol = sym, name = name})
        end
        if #loaded > 0 then return loaded end
    end
    return DEFAULT_STOCKS
end

-- ── Fetch one stock (no custom headers — confirmed working) ──────────────────
-- Passing any header forces the inline HTTPClient+setInsecure() path
-- instead of HttpDownloader, which may not recognise our Lua WiFi session.
local FETCH_HEADERS = {["Accept"] = "*/*"}

local function fetchOne(symbol)
    local url  = "https://query1.finance.yahoo.com/v8/finance/chart/"
              .. symbol .. "?interval=1d&range=1d"
    local body = net.get(url, FETCH_HEADERS)
    if not body then return {ok=false, err="fetch:nil"} end

    local function num(key)
        -- Allow optional whitespace around colon
        return tonumber(body:match('"' .. key .. '"%s*:%s*(%-?[%d%.eE+]+)'))
    end

    local price     = num("regularMarketPrice")
    local prevClose = num("chartPreviousClose")
                   or num("previousClose")
                   or num("regularMarketPreviousClose")
    local changePct = num("regularMarketChangePercent")
    local t         = num("regularMarketTime")

    -- On parse failure return first 80 chars for diagnosis
    if not price then return {ok=false, err=body:sub(1,80)} end

    if not changePct and prevClose and prevClose > 0 then
        changePct = (price - prevClose) / prevClose * 100
    end

    local ts = ""
    if t then
        local h = math.floor((t % 86400) / 3600 + 8) % 24
        local m = math.floor((t % 3600) / 60)
        ts = string.format("%02d:%02d", h, m)
    end

    return {
        price     = price,
        prevClose = prevClose or price,
        changePct = changePct or 0,
        time      = ts,
        ok        = true,
    }
end

-- ── Number formatting ────────────────────────────────────────────────────────
local function formatPrice(p)
    local s        = string.format("%.2f", p)
    local int, dec = s:match("^(%-?%d+)(%.%d+)$")
    if not int then return s end
    local neg    = int:sub(1, 1) == "-"
    local digits = neg and int:sub(2) or int
    local n      = #digits
    local parts  = {}
    for i = n, 1, -3 do
        table.insert(parts, 1, digits:sub(math.max(1, i - 2), i))
    end
    return (neg and "-" or "") .. table.concat(parts, ",") .. dec
end

-- ── Layout ───────────────────────────────────────────────────────────────────
local M       = 20
local ROW_H   = 90
local BADGE_W = 116
local BADGE_H = 28
local BADGE_R = 5
local LIST_Y  = 60

local function drawHeader(sub)
    local sw = gui.width()
    gui.drawText(FONT_BOOKERLY_18, M, 12, "STOCKS", false)
    if sub and sub ~= "" then
        local tw = gui.getTextWidth(FONT_SMALL, sub)
        gui.drawText(FONT_SMALL, sw - M - tw, 20, sub, false)
    end
    gui.drawLine(0, LIST_Y - 2, gui.width(), LIST_Y - 2, 1, false)
end

local function renderDone()
    local sw = gui.width()
    local sh = gui.height()
    gui.fillRect(0, 0, sw, sh)
    drawHeader(updateTime ~= "" and ("Updated " .. updateTime) or "")

    local rowY   = LIST_Y
    local badgeX = sw - M - BADGE_W

    for i, s in ipairs(stocks) do
        local r = results[i]

        gui.drawText(FONT_UI_12, M, rowY + 10, s.symbol, false)
        gui.drawText(FONT_SMALL, M, rowY + 50, s.name,   false)

        if r and r.ok then
            local priceStr = formatPrice(r.price)
            local pw = gui.getTextWidth(FONT_BOOKERLY_18, priceStr)
            gui.drawText(FONT_BOOKERLY_18, badgeX - pw - 12, rowY + 6, priceStr, false)

            local up     = r.changePct >= 0
            local pctStr = string.format("%s%.2f%%", up and "+" or "", r.changePct)
            local tw     = gui.getTextWidth(FONT_SMALL, pctStr)
            local tx     = badgeX + math.floor((BADGE_W - tw) / 2)
            local badgeY = rowY + 48

            if up then
                gui.fillRoundedRect(badgeX, badgeY, BADGE_W, BADGE_H, BADGE_R, false)
                gui.drawText(FONT_SMALL, tx, badgeY + 7, pctStr, true)
            else
                gui.drawRoundedRect(badgeX, badgeY, BADGE_W, BADGE_H, 1, BADGE_R, false)
                gui.drawText(FONT_SMALL, tx, badgeY + 7, pctStr, false)
            end
        else
            local msg = (r and r.err) and r.err:sub(1, 40) or "N/A"
            gui.drawText(FONT_SMALL, M, rowY + 50, msg, false)
        end

        if i < #stocks then
            gui.drawLine(M, rowY + ROW_H - 1, sw - M, rowY + ROW_H - 1, 1, false)
        end
        rowY = rowY + ROW_H
    end

    gui.drawButtonHints("<<", "Refresh", "", "")
    gui.refresh(REFRESH_FAST)
end

local function renderProgress()
    local sw = gui.width()
    local sh = gui.height()
    local cy = math.floor(sh * 0.46)
    gui.fillRect(0, 0, sw, sh)
    drawHeader("")

    local sym = stocks[fetchIdx] and stocks[fetchIdx].symbol or "..."
    gui.drawCenteredText(FONT_UI_12, cy - 18,
        string.format("Fetching  %s  (%d / %d)", sym, fetchIdx, #stocks), false)

    local barW = 320
    local barH = 14
    local barX = math.floor((sw - barW) / 2)
    local barY = cy + 16
    local fill = math.floor((fetchIdx - 1) / #stocks * barW)
    gui.drawRoundedRect(barX, barY, barW, barH, 1, 4, false)
    if fill > 3 then
        gui.fillRoundedRect(barX + 1, barY + 1, fill - 2, barH - 2, 3, false)
    end

    gui.drawButtonHints("<<", "", "", "")
    gui.refresh(REFRESH_FAST)
end

local function renderStatus(msg)
    local sw = gui.width()
    local sh = gui.height()
    gui.fillRect(0, 0, sw, sh)
    drawHeader("")
    gui.drawCenteredText(FONT_UI_12, math.floor(sh * 0.46), msg, false)
    gui.drawButtonHints("<<", "", "", "")
    gui.refresh(REFRESH_FAST)
end

-- ── Init / Draw ──────────────────────────────────────────────────────────────
function init()
    stocks       = loadConfig()
    results      = {}
    for i = 1, #stocks do results[i] = {ok = false} end
    updateTime   = ""
    fetchIdx     = 1
    fetchShown   = false
    needsDraw    = true
    state        = STATE_CONNECTING
    connectTimer = sys.millis()
    net.wifiConnect()
end

function draw()
    if input.wasPressed("back") then
        net.wifiDisconnect()
        sys.exit()
        return
    end

    if state == STATE_DONE and input.wasPressed("page_forward") then
        results = {}
        for i = 1, #stocks do results[i] = {ok = false} end
        updateTime   = ""
        fetchIdx     = 1
        fetchShown   = false
        needsDraw    = true
        state        = STATE_CONNECTING
        connectTimer = sys.millis()
        net.wifiConnect()
    end

    if state == STATE_CONNECTING then
        local s = net.wifiStatus()
        if s == "connected" then
            state      = STATE_FETCHING
            fetchIdx   = 1
            fetchShown = false
            return
        elseif s == "failed" then
            state     = STATE_ERROR
            needsDraw = true
        elseif sys.millis() - connectTimer > 15000 then
            net.wifiDisconnect()
            state     = STATE_ERROR
            needsDraw = true
        end

    elseif state == STATE_FETCHING then
        if not fetchShown then
            fetchShown = true
            renderProgress()
            return
        end
        local r = fetchOne(stocks[fetchIdx].symbol)
        results[fetchIdx] = r or {ok=false, err="fetch:nil"}
        if r and r.ok and r.time ~= "" then updateTime = r.time end
        fetchIdx   = fetchIdx + 1
        fetchShown = false
        if fetchIdx > #stocks then
            net.wifiDisconnect()
            state     = STATE_DONE
            needsDraw = true
        end
        return
    end

    if not needsDraw then return end
    needsDraw = false

    if state == STATE_CONNECTING then
        renderStatus("Connecting to WiFi...")
    elseif state == STATE_FETCHING then
        renderProgress()
    elseif state == STATE_DONE then
        renderDone()
    elseif state == STATE_ERROR then
        renderStatus("Connection failed.")
    end
end
