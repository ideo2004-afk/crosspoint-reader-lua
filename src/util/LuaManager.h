#pragma once

#include <string>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

class GfxRenderer;
class MappedInputManager;

class LuaManager {
public:
    static LuaManager& getInstance();

    bool begin(GfxRenderer* renderer = nullptr, MappedInputManager* input = nullptr);
    void end();

    bool runPlugin(const std::string& pluginName);
    bool callFunction(const char* funcName);

    // sys.exit() support
    void setWantsExit() { wantsExit = true; }
    bool checkAndClearWantsExit() { bool r = wantsExit; wantsExit = false; return r; }

    lua_State* getState() { return L; }

private:
    LuaManager() = default;
    ~LuaManager() { end(); }
    LuaManager(const LuaManager&) = delete;
    LuaManager& operator=(const LuaManager&) = delete;

    lua_State* L = nullptr;
    bool initialized = false;
    bool wantsExit = false;

    void registerBindings();
};
