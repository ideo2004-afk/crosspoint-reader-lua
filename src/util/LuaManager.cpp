#include "LuaManager.h"
#include <Arduino.h>
#include <esp_random.h>
#include <HalStorage.h>
#include <GfxRenderer.h>
#include <Bitmap.h>
#include <WiFi.h>
#include "MappedInputManager.h"
#include "Logging.h"
#include "fontIds.h"
#include "components/UITheme.h"
#include "WifiCredentialStore.h"
#include "network/HttpDownloader.h"
#include <HTTPClient.h>
#include <NetworkClient.h>
#include <NetworkClientSecure.h>

// ─── net module state ─────────────────────────────────────────────────────────
namespace {
    enum class NetWifiState { IDLE, CONNECTING, CONNECTED, FAILED };
    NetWifiState s_wifiState = NetWifiState::IDLE;
    int  s_credIdx   = 0;   // index into credentials list being tried
    bool s_ownedWifi = false; // true if net.wifiConnect() started this session
}  // namespace

// ─── Registry helpers ─────────────────────────────────────────────────────────

extern "C" {

static GfxRenderer* get_renderer(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "renderer_ptr");
    auto* r = (GfxRenderer*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return r;
}

static MappedInputManager* get_input(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "input_ptr");
    auto* m = (MappedInputManager*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return m;
}

static MappedInputManager::Button parse_button(const char* name) {
    if (strcmp(name, "confirm") == 0)      return MappedInputManager::Button::Confirm;
    if (strcmp(name, "left") == 0)         return MappedInputManager::Button::Left;
    if (strcmp(name, "right") == 0)        return MappedInputManager::Button::Right;
    if (strcmp(name, "up") == 0)           return MappedInputManager::Button::Up;
    if (strcmp(name, "down") == 0)         return MappedInputManager::Button::Down;
    if (strcmp(name, "page_back") == 0)    return MappedInputManager::Button::PageBack;
    if (strcmp(name, "page_forward") == 0) return MappedInputManager::Button::PageForward;
    return MappedInputManager::Button::Back;
}

// ─── log ─────────────────────────────────────────────────────────────────────

static int l_log(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    logSerial.print("[LUA] ");
    logSerial.println(msg);
    return 0;
}

// ─── gui ─────────────────────────────────────────────────────────────────────

static int l_gui_clear(lua_State* L) {
    auto r = get_renderer(L);
    if (r) r->clearScreen();
    return 0;
}

static int l_gui_refresh(lua_State* L) {
    auto r = get_renderer(L);
    int mode = luaL_optinteger(L, 1, HalDisplay::FAST_REFRESH);
    if (r) r->displayBuffer((HalDisplay::RefreshMode)mode);
    return 0;
}

static Color get_lua_color(lua_State* L, int argIdx, Color defaultColor = Color::Black) {
    if (lua_isnoneornil(L, argIdx)) return defaultColor;
    if (lua_isboolean(L, argIdx)) return lua_toboolean(L, argIdx) ? Color::Black : Color::White;
    return (Color)lua_tointeger(L, argIdx);
}

static int l_gui_draw_rect(lua_State* L) {
    auto r = get_renderer(L);
    if (!r) return 0;
    Color color = get_lua_color(L, 5);
    r->drawRect(luaL_checkinteger(L, 1), luaL_checkinteger(L, 2),
                luaL_checkinteger(L, 3), luaL_checkinteger(L, 4), color != Color::White);
    return 0;
}

static int l_gui_fill_rect(lua_State* L) {
    auto r = get_renderer(L);
    if (!r) return 0;
    Color color = get_lua_color(L, 5);
    r->fillRect(luaL_checkinteger(L, 1), luaL_checkinteger(L, 2),
                luaL_checkinteger(L, 3), luaL_checkinteger(L, 4), color != Color::White);
    return 0;
}

static int l_gui_draw_line(lua_State* L) {
    auto r = get_renderer(L);
    if (!r) return 0;
    int lw = luaL_optinteger(L, 5, 1);
    Color color = get_lua_color(L, 6);
    r->drawLine(luaL_checkinteger(L, 1), luaL_checkinteger(L, 2),
                luaL_checkinteger(L, 3), luaL_checkinteger(L, 4), lw, color != Color::White);
    return 0;
}

static int l_gui_draw_rounded_rect(lua_State* L) {
    auto r = get_renderer(L);
    if (!r) return 0;
    int lw     = luaL_optinteger(L, 5, 2);
    int radius = luaL_optinteger(L, 6, 10);
    Color color = get_lua_color(L, 7);
    r->drawRoundedRect(luaL_checkinteger(L, 1), luaL_checkinteger(L, 2),
                       luaL_checkinteger(L, 3), luaL_checkinteger(L, 4), lw, radius, color != Color::White);
    return 0;
}

static int l_gui_fill_rounded_rect(lua_State* L) {
    auto r = get_renderer(L);
    if (!r) return 0;
    int radius = luaL_optinteger(L, 5, 10);
    Color color = get_lua_color(L, 6);
    r->fillRoundedRect(luaL_checkinteger(L, 1), luaL_checkinteger(L, 2),
                       luaL_checkinteger(L, 3), luaL_checkinteger(L, 4),
                       radius, color);
    return 0;
}

static int l_gui_draw_pixel(lua_State* L) {
    auto r = get_renderer(L);
    if (!r) return 0;
    Color color = get_lua_color(L, 3);
    r->drawPixel(luaL_checkinteger(L, 1), luaL_checkinteger(L, 2), color != Color::White);
    return 0;
}

static int l_gui_draw_circle(lua_State* L) {
    auto r = get_renderer(L);
    if (!r) return 0;
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    int rad = luaL_checkinteger(L, 3);
    int lw = luaL_optinteger(L, 4, 1);
    Color color = get_lua_color(L, 5);
    r->drawCircle(x, y, rad, lw, color != Color::White);
    return 0;
}

static int l_gui_fill_circle(lua_State* L) {
    auto r = get_renderer(L);
    if (!r) return 0;
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    int rad = luaL_checkinteger(L, 3);
    Color color = get_lua_color(L, 4);
    r->fillCircle(x, y, rad, color);
    return 0;
}

static int l_gui_fill_polygon(lua_State* L) {
    auto r = get_renderer(L);
    if (!r) return 0;
    if (!lua_istable(L, 1) || !lua_istable(L, 2)) {
        return luaL_error(L, "fillPolygon expects two tables (xPoints, yPoints)");
    }

    int n1 = lua_rawlen(L, 1);
    int n2 = lua_rawlen(L, 2);
    int numPoints = (n1 < n2) ? n1 : n2;
    if (numPoints < 3) return 0;

    auto* xPoints = static_cast<int*>(malloc(numPoints * sizeof(int)));
    auto* yPoints = static_cast<int*>(malloc(numPoints * sizeof(int)));
    if (!xPoints || !yPoints) {
        free(xPoints); free(yPoints);
        return 0;
    }

    for (int i = 0; i < numPoints; i++) {
        lua_rawgeti(L, 1, i + 1);
        xPoints[i] = lua_tointeger(L, -1);
        lua_rawgeti(L, 2, i + 1);
        yPoints[i] = lua_tointeger(L, -1);
        lua_pop(L, 2);
    }

    Color color = get_lua_color(L, 3);
    r->fillPolygon(xPoints, yPoints, numPoints, color != Color::White);

    free(xPoints);
    free(yPoints);
    return 0;
}

static int l_gui_draw_text(lua_State* L) {
    auto r = get_renderer(L);
    if (!r) return 0;
    Color color = get_lua_color(L, 5);
    int style  = luaL_optinteger(L, 6, (int)EpdFontFamily::REGULAR);
    r->drawText(luaL_checkinteger(L, 1), luaL_checkinteger(L, 2), luaL_checkinteger(L, 3),
                luaL_checkstring(L, 4), color, (EpdFontFamily::Style)style);
    return 0;
}

static int l_gui_draw_centered_text(lua_State* L) {
    auto r = get_renderer(L);
    if (!r) return 0;
    Color color = get_lua_color(L, 4);
    int style  = luaL_optinteger(L, 5, (int)EpdFontFamily::REGULAR);
    r->drawCenteredText(luaL_checkinteger(L, 1), luaL_checkinteger(L, 2),
                        luaL_checkstring(L, 3), color, (EpdFontFamily::Style)style);
    return 0;
}

static int l_gui_get_text_width(lua_State* L) {
    auto r = get_renderer(L);
    int fontId = luaL_checkinteger(L, 1);
    const char* text = luaL_checkstring(L, 2);
    int style = luaL_optinteger(L, 3, (int)EpdFontFamily::REGULAR);
    lua_pushinteger(L, r ? r->getTextWidth(fontId, text, (EpdFontFamily::Style)style) : 0);
    return 1;
}

static int l_gui_get_width(lua_State* L) {
    auto r = get_renderer(L);
    lua_pushinteger(L, r ? r->getScreenWidth() : 480);
    return 1;
}

static int l_gui_get_height(lua_State* L) {
    auto r = get_renderer(L);
    lua_pushinteger(L, r ? r->getScreenHeight() : 800);
    return 1;
}

// gui.drawButtonHints(back, confirm, prev, next) — args are logical roles, remapped to physical positions
static int l_gui_draw_button_hints(lua_State* L) {
    auto r = get_renderer(L);
    auto* m = get_input(L);
    if (!r) return 0;
    const char* back    = luaL_optstring(L, 1, "");
    const char* confirm = luaL_optstring(L, 2, "");
    const char* prev    = luaL_optstring(L, 3, "");
    const char* next    = luaL_optstring(L, 4, "");
    if (m) {
        const auto l = m->mapLabels(back, confirm, prev, next);
        GUI.drawButtonHints(*r, l.btn1, l.btn2, l.btn3, l.btn4);
    } else {
        GUI.drawButtonHints(*r, back, confirm, prev, next);
    }
    return 0;
}

static int l_gui_set_orientation(lua_State* L) {
    auto r = get_renderer(L);
    if (!r) return 0;
    const char* mode = luaL_checkstring(L, 1);
    GfxRenderer::Orientation o = GfxRenderer::Portrait;
    if (strcmp(mode, "landscape_cw") == 0)       o = GfxRenderer::LandscapeClockwise;
    else if (strcmp(mode, "landscape_ccw") == 0) o = GfxRenderer::LandscapeCounterClockwise;
    else if (strcmp(mode, "portrait_inv") == 0)  o = GfxRenderer::PortraitInverted;
    r->setOrientation(o);
    return 0;
}

// gui.drawBmp(path [, x, y, maxW, maxH]) → bool
// x/y default to centered; maxW/maxH default to screen size
static int l_gui_draw_bmp(lua_State* L) {
    auto r = get_renderer(L);
    if (!r) { lua_pushboolean(L, 0); return 1; }

    const char* path = luaL_checkstring(L, 1);

    FsFile file;
    if (!Storage.openFileForRead("LUA", path, file)) {
        LOG_ERR("LUA", "drawBmp: cannot open %s", path);
        lua_pushboolean(L, 0);
        return 1;
    }

    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() != BmpReaderError::Ok) {
        file.close();
        LOG_ERR("LUA", "drawBmp: parse failed for %s", path);
        lua_pushboolean(L, 0);
        return 1;
    }

    int sw = r->getScreenWidth();
    int sh = r->getScreenHeight();
    int x    = luaL_optinteger(L, 2, std::max(0, (sw - bitmap.getWidth())  / 2));
    int y    = luaL_optinteger(L, 3, std::max(0, (sh - bitmap.getHeight()) / 2));
    int maxW = luaL_optinteger(L, 4, sw);
    int maxH = luaL_optinteger(L, 5, sh);

    r->drawBitmap(bitmap, x, y, maxW, maxH);
    file.close();

    lua_pushboolean(L, 1);
    return 1;
}

// ─── input ───────────────────────────────────────────────────────────────────

static int l_input_was_pressed(lua_State* L) {
    auto* m = get_input(L);
    lua_pushboolean(L, m ? m->wasPressed(parse_button(luaL_checkstring(L, 1))) : 0);
    return 1;
}

static int l_input_was_released(lua_State* L) {
    auto* m = get_input(L);
    lua_pushboolean(L, m ? m->wasReleased(parse_button(luaL_checkstring(L, 1))) : 0);
    return 1;
}

static int l_input_is_pressed(lua_State* L) {
    auto* m = get_input(L);
    lua_pushboolean(L, m ? m->isPressed(parse_button(luaL_checkstring(L, 1))) : 0);
    return 1;
}

static int l_input_is_any_pressed(lua_State* L) {
    auto* m = get_input(L);
    lua_pushboolean(L, m ? m->isAnyPressed() : 0);
    return 1;
}

// ─── sys ─────────────────────────────────────────────────────────────────────

static int l_sys_millis(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)millis());
    return 1;
}

static int l_sys_delay(lua_State* L) {
    delay((int)luaL_checkinteger(L, 1));
    return 0;
}

static int l_sys_exit(lua_State* L) {
    // Get LuaManager pointer from registry and set wantsExit
    lua_getfield(L, LUA_REGISTRYINDEX, "lua_manager_ptr");
    LuaManager* mgr = (LuaManager*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (mgr) mgr->setWantsExit();
    return 0;
}

// ─── fs ──────────────────────────────────────────────────────────────────────

// fs.listDirs(path) → table of directory names
static int l_fs_list_dirs(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    lua_newtable(L);
    int idx = 1;

    FsFile root = Storage.open(path);
    if (!root || !root.isDirectory()) {
        if (root) root.close();
        return 1;
    }
    root.rewindDirectory();
    char name[256];
    for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
        if (file.isDirectory()) {
            file.getName(name, sizeof(name));
            if (name[0] != '.') {
                lua_pushstring(L, name);
                lua_rawseti(L, -2, idx++);
            }
        }
        file.close();
    }
    root.close();
    return 1;
}

// fs.listFiles(path) → table of file names
static int l_fs_list_files(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    lua_newtable(L);
    int idx = 1;

    auto files = Storage.listFiles(path, 500);
    for (const auto& f : files) {
        if (!f.startsWith(".")) {
            lua_pushstring(L, f.c_str());
            lua_rawseti(L, -2, idx++);
        }
    }
    return 1;
}

// fs.exists(path) → bool
static int l_fs_exists(lua_State* L) {
    lua_pushboolean(L, Storage.exists(luaL_checkstring(L, 1)));
    return 1;
}

// fs.readFile(path) → string or nil
// Reads into malloc'd buffer, closes file BEFORE any Lua allocation,
// preventing handle leaks if Lua GC or allocation throws.
static int l_fs_read_file(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    FsFile f = Storage.open(path);
    if (!f || f.isDirectory()) {
        if (f) f.close();
        lua_pushnil(L);
        return 1;
    }
    constexpr size_t maxSize = 50000;
    size_t fileSize  = static_cast<size_t>(f.size());
    size_t readSize  = (fileSize < maxSize) ? fileSize : maxSize;
    if (readSize == 0) { f.close(); lua_pushstring(L, ""); return 1; }

    char* buf = static_cast<char*>(malloc(readSize));
    if (!buf) { f.close(); lua_pushnil(L); return 1; }

    size_t total = 0;
    while (total < readSize) {
        int r = f.read(reinterpret_cast<uint8_t*>(buf) + total, readSize - total);
        if (r <= 0) break;
        total += static_cast<size_t>(r);
    }
    f.close();                           // close BEFORE touching Lua stack
    lua_pushlstring(L, buf, total);      // safe: file already closed
    free(buf);
    return 1;
}

// fs.writeFile(path, content) → bool
static int l_fs_write_file(lua_State* L) {
    const char* path    = luaL_checkstring(L, 1);
    const char* content = luaL_checkstring(L, 2);
    lua_pushboolean(L, Storage.writeFile(path, String(content)));
    return 1;
}

// ─── net.* ───────────────────────────────────────────────────────────────────

// net.wifiConnect() — starts async connection using saved credentials.
// Call net.wifiStatus() each frame to poll progress.
static int l_net_wifi_connect(lua_State*) {
    WIFI_STORE.loadFromFile();
    const auto& creds = WIFI_STORE.getCredentials();
    if (creds.empty()) {
        s_wifiState = NetWifiState::FAILED;
        return 0;
    }
    WiFi.mode(WIFI_STA);
    s_credIdx = 0;
    s_ownedWifi = true;
    // Try last-connected first
    const std::string& last = WIFI_STORE.getLastConnectedSsid();
    if (!last.empty()) {
        for (int i = 0; i < (int)creds.size(); i++) {
            if (creds[i].ssid == last) { s_credIdx = i; break; }
        }
    }
    const auto& c = creds[s_credIdx];
    WiFi.begin(c.ssid.c_str(), c.password.empty() ? nullptr : c.password.c_str());
    s_wifiState = NetWifiState::CONNECTING;
    LOG_INF("NET", "Connecting to %s...", c.ssid.c_str());
    return 0;
}

// net.wifiStatus() → "idle" | "connecting" | "connected" | "failed"
static int l_net_wifi_status(lua_State* L) {
    if (s_wifiState == NetWifiState::CONNECTING) {
        wl_status_t ws = WiFi.status();
        if (ws == WL_CONNECTED) {
            s_wifiState = NetWifiState::CONNECTED;
            WIFI_STORE.setLastConnectedSsid(WiFi.SSID().c_str());
            LOG_INF("NET", "Connected: %s", WiFi.localIP().toString().c_str());
        } else if (ws == WL_CONNECT_FAILED || ws == WL_NO_SSID_AVAIL) {
            // Try next credential
            const auto& creds = WIFI_STORE.getCredentials();
            s_credIdx++;
            if (s_credIdx < (int)creds.size()) {
                const auto& c = creds[s_credIdx];
                WiFi.begin(c.ssid.c_str(), c.password.empty() ? nullptr : c.password.c_str());
                LOG_INF("NET", "Trying %s...", c.ssid.c_str());
            } else {
                s_wifiState = NetWifiState::FAILED;
                LOG_ERR("NET", "All credentials failed");
            }
        }
    }
    const char* str = "idle";
    if      (s_wifiState == NetWifiState::CONNECTING) str = "connecting";
    else if (s_wifiState == NetWifiState::CONNECTED)  str = "connected";
    else if (s_wifiState == NetWifiState::FAILED)     str = "failed";
    lua_pushstring(L, str);
    return 1;
}

// net.wifiDisconnect()
static int l_net_wifi_disconnect(lua_State*) {
    if (s_ownedWifi) {
        WiFi.disconnect(false);
        WiFi.mode(WIFI_OFF);
        s_ownedWifi = false;
    }
    s_wifiState = NetWifiState::IDLE;
    return 0;
}

// net.get(url [, headers_table]) → body_string or nil
// headers_table: { ["X-Api-Key"] = "value", ... }
static int l_net_get(lua_State* L) {
    const char* url = luaL_checkstring(L, 1);
    // Optional headers table (arg 2)
    std::vector<std::pair<std::string,std::string>> headers;
    if (lua_istable(L, 2)) {
        lua_pushnil(L);
        while (lua_next(L, 2)) {
            const char* k = lua_tostring(L, -2);
            const char* v = lua_tostring(L, -1);
            if (k && v) headers.push_back({k, v});
            lua_pop(L, 1);
        }
    }
    std::string body;
    // Build a temporary HttpDownloader-style request with extra headers
    // We use fetchUrl which handles HTTP/HTTPS automatically
    if (!headers.empty()) {
        // For custom headers we inline the request
        std::unique_ptr<NetworkClient> client;
        bool isHttps = std::string(url).find("https://") == 0;
        if (isHttps) {
            auto* sc = new NetworkClientSecure(); sc->setInsecure(); client.reset(sc);
        } else {
            client.reset(new NetworkClient());
        }
        HTTPClient http;
        http.begin(*client, url);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.addHeader("User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
        for (auto& h : headers) http.addHeader(h.first.c_str(), h.second.c_str());
        int code = http.GET();
        if (code == HTTP_CODE_OK) {
            body = http.getString().c_str();
            lua_pushstring(L, body.c_str());
        } else {
            LOG_ERR("NET", "GET failed: %d", code);
            lua_pushnil(L);
        }
        http.end();
    } else {
        if (HttpDownloader::fetchUrl(url, body)) {
            lua_pushstring(L, body.c_str());
        } else {
            lua_pushnil(L);
        }
    }
    return 1;
}

// net.urlencode(str) → encoded string
static int l_net_urlencode(lua_State* L) {
    const char* s = luaL_checkstring(L, 1);
    std::string out;
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (isalnum(c) || c=='-' || c=='_' || c=='.' || c=='~') {
            out += (char)c;
        } else {
            char buf[4]; snprintf(buf, sizeof(buf), "%%%02X", c); out += buf;
        }
    }
    lua_pushstring(L, out.c_str());
    return 1;
}

} // extern "C"

// ─── LuaManager ──────────────────────────────────────────────────────────────

LuaManager& LuaManager::getInstance() { static LuaManager instance; return instance; }

bool LuaManager::begin(GfxRenderer* r, MappedInputManager* input) {
    if (initialized) return true;

    wantsExit = false;
    LOG_INF("LUA", "Heap before begin: %d", ESP.getFreeHeap());
    L = luaL_newstate();
    if (!L) { LOG_ERR("LUA", "OOM: Cannot create state"); return false; }

    luaL_openlibs(L);

    // Seed math.random with hardware RNG
    lua_getglobal(L, "math");
    lua_getfield(L, -1, "randomseed");
    lua_pushinteger(L, (lua_Integer)esp_random());
    lua_call(L, 1, 0);
    lua_pop(L, 1);

    // Store pointers in registry
    if (r) {
        lua_pushlightuserdata(L, r);
        lua_setfield(L, LUA_REGISTRYINDEX, "renderer_ptr");
    }
    if (input) {
        lua_pushlightuserdata(L, input);
        lua_setfield(L, LUA_REGISTRYINDEX, "input_ptr");
    }
    lua_pushlightuserdata(L, this);
    lua_setfield(L, LUA_REGISTRYINDEX, "lua_manager_ptr");

    registerBindings();
    LOG_INF("LUA", "VM ready, heap: %d", ESP.getFreeHeap());

    initialized = true;
    return true;
}

void LuaManager::end() {
    if (L) { lua_close(L); L = nullptr; }
    initialized = false;
    wantsExit = false;
    // Clean up WiFi if net module started it
    if (s_ownedWifi) {
        WiFi.disconnect(false);
        WiFi.mode(WIFI_OFF);
        s_ownedWifi = false;
    }
    s_wifiState = NetWifiState::IDLE;
}

void LuaManager::registerBindings() {
    // log()
    lua_pushcfunction(L, l_log);
    lua_setglobal(L, "log");

    // gui.*
    lua_newtable(L);
    lua_pushcfunction(L, l_gui_clear);              lua_setfield(L, -2, "clear");
    lua_pushcfunction(L, l_gui_refresh);            lua_setfield(L, -2, "refresh");
    lua_pushcfunction(L, l_gui_draw_rect);          lua_setfield(L, -2, "drawRect");
    lua_pushcfunction(L, l_gui_fill_rect);          lua_setfield(L, -2, "fillRect");
    lua_pushcfunction(L, l_gui_draw_line);          lua_setfield(L, -2, "drawLine");
    lua_pushcfunction(L, l_gui_draw_rounded_rect);  lua_setfield(L, -2, "drawRoundedRect");
    lua_pushcfunction(L, l_gui_fill_rounded_rect);  lua_setfield(L, -2, "fillRoundedRect");
    lua_pushcfunction(L, l_gui_draw_text);          lua_setfield(L, -2, "drawText");
    lua_pushcfunction(L, l_gui_draw_centered_text); lua_setfield(L, -2, "drawCenteredText");
    lua_pushcfunction(L, l_gui_get_text_width);     lua_setfield(L, -2, "getTextWidth");
    lua_pushcfunction(L, l_gui_get_width);          lua_setfield(L, -2, "width");
    lua_pushcfunction(L, l_gui_get_height);         lua_setfield(L, -2, "height");
    lua_pushcfunction(L, l_gui_set_orientation);    lua_setfield(L, -2, "setOrientation");
    lua_pushcfunction(L, l_gui_draw_bmp);           lua_setfield(L, -2, "drawBmp");
    lua_pushcfunction(L, l_gui_draw_button_hints);  lua_setfield(L, -2, "drawButtonHints");
    lua_pushcfunction(L, l_gui_draw_pixel);         lua_setfield(L, -2, "drawPixel");
    lua_pushcfunction(L, l_gui_draw_circle);        lua_setfield(L, -2, "drawCircle");
    lua_pushcfunction(L, l_gui_fill_circle);        lua_setfield(L, -2, "fillCircle");
    lua_pushcfunction(L, l_gui_fill_polygon);       lua_setfield(L, -2, "fillPolygon");
    
    // Hint constants
    lua_pushstring(L, BaseTheme::HINT_BACK); lua_setfield(L, -2, "HINT_BACK");
    lua_pushstring(L, BaseTheme::HINT_OK);   lua_setfield(L, -2, "HINT_OK");
    lua_pushstring(L, BaseTheme::HINT_PREV); lua_setfield(L, -2, "HINT_PREV");
    lua_pushstring(L, BaseTheme::HINT_NEXT); lua_setfield(L, -2, "HINT_NEXT");
    lua_pushstring(L, BaseTheme::HINT_UP);   lua_setfield(L, -2, "HINT_UP");
    lua_pushstring(L, BaseTheme::HINT_DOWN); lua_setfield(L, -2, "HINT_DOWN");
    
    lua_setglobal(L, "gui");

    // input.*
    lua_newtable(L);
    lua_pushcfunction(L, l_input_was_pressed);    lua_setfield(L, -2, "wasPressed");
    lua_pushcfunction(L, l_input_was_released);   lua_setfield(L, -2, "wasReleased");
    lua_pushcfunction(L, l_input_is_pressed);     lua_setfield(L, -2, "isPressed");
    lua_pushcfunction(L, l_input_is_any_pressed); lua_setfield(L, -2, "isAnyPressed");
    lua_setglobal(L, "input");

    // sys.*
    lua_newtable(L);
    lua_pushcfunction(L, l_sys_millis); lua_setfield(L, -2, "millis");
    lua_pushcfunction(L, l_sys_delay);  lua_setfield(L, -2, "delay");
    lua_pushcfunction(L, l_sys_exit);   lua_setfield(L, -2, "exit");
    lua_setglobal(L, "sys");

    // fs.*
    lua_newtable(L);
    lua_pushcfunction(L, l_fs_list_dirs);  lua_setfield(L, -2, "listDirs");
    lua_pushcfunction(L, l_fs_list_files); lua_setfield(L, -2, "listFiles");
    lua_pushcfunction(L, l_fs_exists);     lua_setfield(L, -2, "exists");
    lua_pushcfunction(L, l_fs_read_file);  lua_setfield(L, -2, "readFile");
    lua_pushcfunction(L, l_fs_write_file); lua_setfield(L, -2, "writeFile");
    lua_setglobal(L, "fs");

    // net.*
    lua_newtable(L);
    lua_pushcfunction(L, l_net_wifi_connect);    lua_setfield(L, -2, "wifiConnect");
    lua_pushcfunction(L, l_net_wifi_status);     lua_setfield(L, -2, "wifiStatus");
    lua_pushcfunction(L, l_net_wifi_disconnect); lua_setfield(L, -2, "wifiDisconnect");
    lua_pushcfunction(L, l_net_get);             lua_setfield(L, -2, "get");
    lua_pushcfunction(L, l_net_urlencode);       lua_setfield(L, -2, "urlencode");
    lua_setglobal(L, "net");

    // Refresh mode constants
    lua_pushinteger(L, HalDisplay::FULL_REFRESH); lua_setglobal(L, "REFRESH_FULL");
    lua_pushinteger(L, HalDisplay::HALF_REFRESH); lua_setglobal(L, "REFRESH_HALF");
    lua_pushinteger(L, HalDisplay::FAST_REFRESH); lua_setglobal(L, "REFRESH_FAST");

    // Color constants
    lua_pushinteger(L, (int)Color::Clear);     lua_setglobal(L, "COLOR_CLEAR");
    lua_pushinteger(L, (int)Color::White);     lua_setglobal(L, "COLOR_WHITE");
    lua_pushinteger(L, (int)Color::LightGray); lua_setglobal(L, "COLOR_LIGHT_GRAY");
    lua_pushinteger(L, (int)Color::DarkGray);  lua_setglobal(L, "COLOR_DARK_GRAY");
    lua_pushinteger(L, (int)Color::Black);     lua_setglobal(L, "COLOR_BLACK");

    // Font ID constants
    lua_pushinteger(L, BOOKERLY_14_FONT_ID); lua_setglobal(L, "FONT_BOOKERLY_14");
    lua_pushinteger(L, BOOKERLY_12_FONT_ID); lua_setglobal(L, "FONT_BOOKERLY_12");
    lua_pushinteger(L, BOOKERLY_16_FONT_ID); lua_setglobal(L, "FONT_BOOKERLY_16");


    // Font style constants
    lua_pushinteger(L, (int)EpdFontFamily::REGULAR); lua_setglobal(L, "STYLE_REGULAR");
    lua_pushinteger(L, (int)EpdFontFamily::BOLD);    lua_setglobal(L, "STYLE_BOLD");
    lua_pushinteger(L, NOTOSANS_12_FONT_ID); lua_setglobal(L, "FONT_NOTOSANS_12");
    lua_pushinteger(L, NOTOSANS_14_FONT_ID); lua_setglobal(L, "FONT_NOTOSANS_14");
    lua_pushinteger(L, NOTOSANS_16_FONT_ID); lua_setglobal(L, "FONT_NOTOSANS_16");
    lua_pushinteger(L, UI_10_FONT_ID);       lua_setglobal(L, "FONT_UI_10");
    lua_pushinteger(L, UI_12_FONT_ID);       lua_setglobal(L, "FONT_UI_12");
    lua_pushinteger(L, SMALL_FONT_ID);       lua_setglobal(L, "FONT_SMALL");
}

bool LuaManager::callFunction(const char* funcName) {
    if (!L) return false;
    lua_getglobal(L, funcName);
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return false; }
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
        LOG_ERR("LUA", "Runtime in %s: %s", funcName, lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }
    return true;
}

namespace {
struct LuaFileReader {
    FsFile file;
    uint8_t buf[512];
};

static const char* lua_chunk_reader(lua_State*, void* ud, size_t* sz) {
    auto* r = static_cast<LuaFileReader*>(ud);
    int n = r->file.read(r->buf, sizeof(r->buf));
    *sz = (n > 0) ? (size_t)n : 0;
    return (*sz > 0) ? reinterpret_cast<const char*>(r->buf) : nullptr;
}
}  // namespace

bool LuaManager::runPlugin(const std::string& pluginName) {
    if (!initialized && !begin()) return false;

    std::string path = "/plugins/" + pluginName + "/main.lua";
    LOG_INF("LUA", "Opening: %s  heap: %d", path.c_str(), ESP.getFreeHeap());

    LuaFileReader reader;
    reader.file = Storage.open(path.c_str());
    if (!reader.file) { LOG_ERR("LUA", "File missing: %s", path.c_str()); return false; }

    std::string chunkName = "@" + pluginName;
    int res = lua_load(L, lua_chunk_reader, &reader, chunkName.c_str(), nullptr);
    reader.file.close();

    if (res != LUA_OK) {
        LOG_ERR("LUA", "Load error: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }

    res = lua_pcall(L, 0, 0, 0);
    if (res != LUA_OK) {
        LOG_ERR("LUA", "Run error: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
        return false;
    }

    LOG_INF("LUA", "Script loaded OK  heap: %d", ESP.getFreeHeap());
    return true;
}
