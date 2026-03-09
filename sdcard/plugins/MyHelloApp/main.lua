-- DESCRIPTION: Basic Lua API demonstration
local count = 0
local lastPress = 0

function init()
    log("MyHelloApp init")
end

function draw()
    gui.clear()

    gui.drawCenteredText(FONT_UI_12, 60, "Phase 1 API Test", true)
    gui.drawLine(20, 90, gui.width() - 20, 90, 2)

    gui.drawRoundedRect(40, 120, gui.width() - 80, 200, 2, 15)
    gui.fillRoundedRect(60, 150, 80, 80, 10)
    gui.drawLine(200, 150, 360, 230, 3)

    local msg = "Button presses: " .. count
    gui.drawCenteredText(FONT_UI_10, 360, msg)

    gui.drawButtonHints("«", "o", "", "")
    gui.drawCenteredText(FONT_SMALL, 720, "t=" .. sys.millis() .. "ms")

    if input.wasReleased("confirm") then
        count = count + 1
        log("count=" .. count)
    end
end
