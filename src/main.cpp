// ============================================================
// Discord Rich Presence Plugin for Resident Evil Requiem (RE9)
// REFramework Plugin - place in reframework/plugins/
// Reads game state from Lua bridge file for dynamic presence
// ============================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <thread>

// ============================================================
// Configuration
// ============================================================
static constexpr const char* DISCORD_CLIENT_ID  = "1478319086848184383";
static constexpr const char* GAME_NAME          = "Resident Evil: Requiem";
static constexpr int         UPDATE_INTERVAL_MS = 5000;   // 5 seconds

// Path to status file written by Lua bridge script
// Relative to game exe directory
static constexpr const char* STATUS_FILE = "reframework\\data\\DiscordPresence\\discord_status.txt";

// ============================================================
// Lua Self-Extraction
// Embedded Lua source is written to autorun/ on first run or
// when the version tag in the file doesn't match LUA_VERSION.
// Check cost on subsequent launches: one CreateFile + ~30 bytes read.
// ============================================================
static constexpr int         LUA_VERSION     = 1;
static constexpr const char* LUA_VERSION_TAG = "-- DISCORD_PRESENCE_VERSION=1";
static constexpr const char* LUA_FILE        = "reframework\\autorun\\discord_presence.lua";

// Full Lua source embedded at compile time (raw string literal).
// Delimiter LUAEOF does not appear anywhere in the Lua code.
static constexpr const char* LUA_SOURCE = R"LUAEOF(
-- DISCORD_PRESENCE_VERSION=1
-- Author: 1Stalk
-- Discord Presence Bridge for Resident Evil Requiem

-- ================================================================
-- Paths and poll rate
-- ================================================================
local CONFIG_PATH      = "DiscordPresence/config.ini"
local TRANSLATION_PATH = "DiscordPresence/Discord_Presence_RE9_Translation.ini"
local STATUS_PATH      = "DiscordPresence/discord_status.txt"
local POLL_INTERVAL    = 150  -- ~2.5 seconds at 60fps

-- Compiled-in default format strings (used when INI is absent or a key is missing)
local fmt_details = "{character} | {status}"
local fmt_state   = "{chapter} | {difficulty}"

-- ================================================================
-- Default Files Content
-- ================================================================

local DEFAULT_INI = [=====[
; ================================================================
;  Discord Rich Presence - Display Configuration
;  Mod by 1Stalk | Resident Evil Requiem
; ================================================================
;
; AVAILABLE VARIABLES:
;   {character}  - Current character:  Grace / Leon
;   {status}     - Character status:
;                    Grace: mental state  (Calm, Vigilance, Agony...)
;                    Leon:  HP status
;   {chapter}    - Current chapter:    Prologue / Chapter 18
;   {difficulty} - Game difficulty:    Casual / Modern / Classic / Insanity
;
;   Leon-specific (blank when playing as Grace):
;   {hp}         - HP percent:         79%
;
;   Grace-specific (blank when playing as Leon):
;   {mental}     - Mental state:       Vigilance
;
; Leave a line empty to hide it from Discord entirely.
;
; EXAMPLES:
;   details = {character} | {status}
;   details = Playing as {character}
;   state   = {chapter} | {difficulty}
;   state   = {chapter}
; ================================================================

[display]
details = {character} | {status}
state   = {chapter} | {difficulty}
]=====]

local DEFAULT_TRANSLATION_INI = [=====[
; ================================================================
;  Discord Rich Presence RE9 — Translation File
; ================================================================
;
; How to use:
;   Replace the text AFTER the = sign with your translation.
;   Do not change the text BEFORE the =.
;   Save the file, then in REFramework click "Reset Scripts".
;
; Example (Russian):
;   Grace     = Грейс
;   Calm      = Спокойна
;   Chapter   = Глава
; ================================================================

[characters]
Grace    = Grace
Leon     = Leon
Playing  = Playing

[mental_states]
Calm      = Calm
Vigilance = Vigilance
Pressure  = Pressure
Agony     = Agony
Panic     = Panic
Hiding    = Hiding
Tired     = Tired
Wheeze    = Wheeze
Scared    = Scared
Sick      = Sick

[chapters]
Prologue = Prologue
Chapter  = Chapter

[difficulties]
Casual   = Casual
Modern   = Modern
Classic  = Classic
Insanity = Insanity
]=====]

-- ================================================================
-- Utilities
-- ================================================================
local function safe_call(fn)
    local ok, r = pcall(fn)
    if ok then return r end
    return nil
end

-- ================================================================
-- INI Configuration
-- ================================================================
local function init_config()
    local f = io.open(CONFIG_PATH, "r")
    if not f then
        local fw = io.open(CONFIG_PATH, "w")
        if fw then fw:write(DEFAULT_INI); fw:close() end
        return
    end
    -- Parse [display] section
    local in_display = false
    for line in f:lines() do
        local trimmed = line:match("^%s*(.-)%s*$") or ""
        if trimmed:sub(1,1) == ";" or trimmed == "" then
            -- ignore
        elseif trimmed:lower():match("^%[display%]") then
            in_display = true
        elseif trimmed:match("^%[") then
            in_display = false
        elseif in_display then
            local k, v = trimmed:match("^(%a+)%s*=%s*(.*)$")
            if k == "details" and v ~= nil then fmt_details = v end
            if k == "state"   and v ~= nil then fmt_state   = v end
        end
    end
    f:close()
end

-- ================================================================
-- Translation System
-- ================================================================
local T = {}

local function init_translations()
    local f = io.open(TRANSLATION_PATH, "r")
    if not f then
        local fw = io.open(TRANSLATION_PATH, "w")
        if fw then fw:write(DEFAULT_TRANSLATION_INI); fw:close() end
        f = io.open(TRANSLATION_PATH, "r")
        if not f then return end -- failed to create
    end

    for line in f:lines() do
        local trimmed = line:match("^%s*(.-)%s*$") or ""
        if trimmed:sub(1,1) ~= ";" and trimmed ~= "" and not trimmed:match("^%[") then
            -- key = value
            local k, v = trimmed:match("^([^=]+)=(.*)$")
            if k and v then
                local key = k:match("^%s*(.-)%s*$")
                local val = v:match("^%s*(.-)%s*$")
                T[key] = val
            end
        end
    end
    f:close()
end

-- Translation helper: returns translated string, or original if not found
local function tr(key)
    return T[key] or key
end

-- ================================================================
-- Status file writer (new format: two plain lines)
-- ================================================================
local last_written = nil

local function write_mainmenu()
    if last_written == "mainmenu" then return end
    last_written = "mainmenu"
    local f = io.open(STATUS_PATH, "w")
    if f then f:write("mainmenu"); f:close() end
end

local function write_status_lines(details, state)
    details = (details or ""):match("^%s*(.-)%s*$")
    state   = (state   or ""):match("^%s*(.-)%s*$")
    local content = details .. "\n" .. state
    if content == last_written then return end
    last_written = content
    local f = io.open(STATUS_PATH, "w")
    if f then f:write(content); f:close() end
end

-- ================================================================
-- Template expansion: replaces {variable} with value from vars table
-- ================================================================
local function expand(template, vars)
    if not template or template == "" then return "" end
    return (template:gsub("{(%w+)}", function(key)
        return vars[key] or ""
    end))
end

-- ================================================================
-- Game data: Chapter
-- ================================================================
local current_chapter = nil

local function update_chapter()
    local gm = safe_call(function() return sdk.get_managed_singleton("app.GuiManager") end)
    if gm then
        local enabled = safe_call(function() return gm:call("get_IsQuestEnable()") end)
        if enabled == false then
            current_chapter = "prologue"; return
        elseif enabled == true then
            local im  = safe_call(function() return sdk.get_managed_singleton("app.ItemManager") end)
            local ss  = im  and safe_call(function() return im:get_field("_SectionSetting") end)
            local sid = ss  and safe_call(function() return ss:get_field("_SectionID") end)
            if sid then
                local num = tostring(sid):match("sct(%d+)")
                current_chapter = num and tonumber(num) or nil
            else
                current_chapter = "prologue"
            end
            return
        end
    end
    current_chapter = nil
end

local function chapter_str()
    if current_chapter == "prologue" then return tr("Prologue") end
    if current_chapter then return tr("Chapter") .. " " .. current_chapter end
    return ""
end

-- ================================================================
-- Game data: Difficulty
-- ================================================================
local DIFF_MAP = { [1200]="Casual", [4200]="Modern", [6200]="Classic", [9000]="Insanity" }
local current_difficulty = nil

local function update_difficulty()
    local rm  = safe_call(function() return sdk.get_managed_singleton("app.RankManager") end)
    local gs  = rm  and safe_call(function() return rm:call("get_GlobalSettings()") end)
    local pts = gs  and safe_call(function() return gs:call("get_DefaultRankPoints()") end)
    if pts then
        local raw_diff = DIFF_MAP[pts]
        current_difficulty = raw_diff and tr(raw_diff) or ("Rank " .. pts)
    else
        current_difficulty = nil
    end
end

-- ================================================================
-- Game data: Leon (HP)
-- ================================================================
local leon_hp_ratio = nil

local function update_leon(pcf)
    local updater = safe_call(function() return pcf:get_field("<Updater>k__BackingField") end)
    if not updater then return end
    local go    = safe_call(function() return updater:call("get_GameObject()") end)
    if not go then return end
    local comps = safe_call(function() return go:call("get_Components()") end)
    if not comps then return end
    local count = tonumber(safe_call(function() return comps:call("get_Count()") end)) or 0

    if count > 18 then
        local spl = safe_call(function() return comps:call("get_Item(System.Int32)", 18) end)
        if spl then
            local r = tonumber(safe_call(function() return spl:call("get_HitPointRatio()") end))
            if r then leon_hp_ratio = r end
        end
    end
end

local function leon_status_str()
    if not leon_hp_ratio then return tr("Playing") end
    local hp = math.max(0, math.min(100, math.floor(leon_hp_ratio * 100)))
    return "HP:" .. hp .. "%"
end

local function leon_hp_str()
    if not leon_hp_ratio then return "" end
    return math.max(0, math.min(100, math.floor(leon_hp_ratio * 100))) .. "%"
end

-- ================================================================
-- Game data: Grace (MentalState bitmask)
-- ================================================================
local MENTAL_PRIORITY = {
    {512,"Sick"},{256,"Scared"},{128,"Wheeze"},{64,"Tired"},
    {16,"Panic"},{32,"Hiding"},{8,"Agony"},{4,"Pressure"},{2,"Vigilance"},{1,"Calm"},
}

local function get_mental_flags(pcf)
    local updater = safe_call(function() return pcf:get_field("<Updater>k__BackingField") end)
    if not updater then return nil end
    local go    = safe_call(function() return updater:call("get_GameObject()") end)
    if not go then return nil end
    local comps = safe_call(function() return go:call("get_Components()") end)
    if not comps then return nil end
    local msd   = safe_call(function() return comps:call("get_Item(System.Int32)", 93) end)
    if not msd then return nil end
    return safe_call(function() return msd:get_field("<StateFlags>k__BackingField") end)
end

local function flags_to_name(flags)
    local n = tonumber(flags) or 0
    if n == 0 then return tr("Calm") end
    for _, e in ipairs(MENTAL_PRIORITY) do
        if (n & e[1]) ~= 0 then return tr(e[2]) end
    end
    return tr("Calm")
end

-- ================================================================
-- Character detection
-- ================================================================
local function get_pcf()
    local cm = safe_call(function() return sdk.get_managed_singleton("app.CharacterManager") end)
    if not cm then return nil end
    return safe_call(function() return cm:get_field("<PlayerContextFast>k__BackingField") end)
end

local function get_character(pcf)
    local a1 = safe_call(function() return pcf:get_field("<Cp_A1Unit>k__BackingField") end)
    if a1 then return "Grace" end
    local a0 = safe_call(function() return pcf:get_field("<Cp_A0Unit>k__BackingField") end)
    if a0 then return "Leon" end
    return "unknown"
end

-- ================================================================
-- Init: load config and translations once on script startup
-- ================================================================
init_config()
init_translations()

-- ================================================================
-- Main poll loop
-- ================================================================
local frame_count = 0

re.on_pre_application_entry("UpdateScene", function()
    frame_count = frame_count + 1
    if frame_count < POLL_INTERVAL then return end
    frame_count = 0

    -- Main menu check
    local qm = safe_call(function() return sdk.get_managed_singleton("app.QuestManager") end)
    if not qm then write_mainmenu(); return end
    local in_game = safe_call(function() return qm:get_field("_IsIngameSetupped") end)
    if not in_game then write_mainmenu(); return end

    -- Update shared game data
    update_chapter()
    update_difficulty()

    -- Get character
    local pcf   = get_pcf()
    local chara = pcf and get_character(pcf) or "unknown"

    -- Build per-character data
    local char_name  = ""
    local status_str = ""
    local mental_str = ""
    local hp_str     = ""

    if chara == "Grace" then
        char_name  = tr("Grace")
        local flags = get_mental_flags(pcf)
        mental_str  = flags_to_name(flags)
        status_str  = mental_str
        -- Reset Leon state
        leon_hp_ratio = nil
    elseif chara == "Leon" then
        char_name = tr("Leon")
        update_leon(pcf)
        status_str = leon_status_str()
        hp_str     = leon_hp_str()
        -- Reset Grace state
        mental_str = ""
    else
        char_name = tr("Playing")
    end

    -- Build template variable table
    local vars = {
        character  = char_name,
        status     = status_str,
        chapter    = chapter_str(),
        difficulty = current_difficulty or "",
        hp         = hp_str,
        mental     = mental_str,
    }

    write_status_lines(expand(fmt_details, vars), expand(fmt_state, vars))
end)

)LUAEOF";

static std::string read_status_file() {
    // Build full path relative to the process working directory
    char exe_dir[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe_dir, MAX_PATH);

    // Walk back to last backslash to get directory
    char* last_sep = strrchr(exe_dir, '\\');
    if (last_sep) *(last_sep + 1) = '\0';

    std::string path = std::string(exe_dir) + STATUS_FILE;

    HANDLE hFile = CreateFileA(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,  // allow Lua to write while we read
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        return "";  // file not created yet
    }

    char buf[512] = {};
    DWORD bytesRead = 0;
    ReadFile(hFile, buf, sizeof(buf) - 1, &bytesRead, nullptr);
    CloseHandle(hFile);

    return std::string(buf, bytesRead);
}

// ============================================================
// Presence state
// ============================================================

struct PresenceState {
    const char* details;   // line 1: character status (nullptr = not shown)
    const char* state;     // line 2: chapter + difficulty
};

// Persistent buffers for returned c_str pointers
static std::string g_details_buf;
static std::string g_state_buf;

// New bridge format: two plain lines written by Lua after template expansion.
//   Line 1: details text  (empty = Discord hides it)
//   Line 2: state text
// Special value "mainmenu" = show Main Menu, no details.
static PresenceState get_presence_from_status(const std::string& content) {
    if (content.empty() || content == "mainmenu")
        return { nullptr, "Main Menu" };

    auto nl = content.find('\n');
    if (nl == std::string::npos) {
        // Single line — treat as state only
        g_state_buf = content;
        while (!g_state_buf.empty() && (g_state_buf.back() == '\r' || g_state_buf.back() == ' '))
            g_state_buf.pop_back();
        return { nullptr, g_state_buf.c_str() };
    }

    g_details_buf = content.substr(0, nl);
    g_state_buf   = content.substr(nl + 1);

    auto trim_cr = [](std::string& s) {
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n'))
            s.pop_back();
    };
    trim_cr(g_details_buf);
    trim_cr(g_state_buf);

    const char* details = g_details_buf.empty() ? nullptr : g_details_buf.c_str();
    const char* state   = g_state_buf.empty()   ? "Main Menu" : g_state_buf.c_str();
    return { details, state };
}

// ============================================================
// Discord IPC - Named Pipe Protocol
// Each packet: [op:uint32][len:uint32][json:len bytes]
// ============================================================

static HANDLE            g_pipe    = INVALID_HANDLE_VALUE;
static int               g_nonce   = 0;
static std::atomic<bool> g_running { false };
static std::thread       g_thread;

static bool ipc_write(HANDLE pipe, uint32_t op, const char* json, uint32_t json_len) {
    uint8_t hdr[8];
    memcpy(hdr,     &op,       4);
    memcpy(hdr + 4, &json_len, 4);
    DWORD written = 0;
    if (!WriteFile(pipe, hdr,  8,        &written, nullptr) || written != 8)        return false;
    if (!WriteFile(pipe, json, json_len, &written, nullptr) || written != json_len) return false;
    return true;
}

static bool ipc_read(HANDLE pipe, uint32_t& op, std::string& json) {
    uint8_t hdr[8] = {};
    DWORD   n      = 0;
    if (!ReadFile(pipe, hdr, 8, &n, nullptr) || n != 8) return false;

    memcpy(&op, hdr, 4);
    uint32_t len = 0;
    memcpy(&len, hdr + 4, 4);

    if (len == 0 || len > 65536) { json.clear(); return true; }
    json.resize(len);
    if (!ReadFile(pipe, &json[0], len, &n, nullptr) || n != len) return false;
    return true;
}

static void ipc_drain(HANDLE pipe) {
    DWORD avail = 0;
    while (PeekNamedPipe(pipe, nullptr, 0, nullptr, &avail, nullptr) && avail >= 8) {
        uint32_t    op = 0;
        std::string tmp;
        if (!ipc_read(pipe, op, tmp)) break;
    }
}

static HANDLE discord_connect() {
    for (int i = 0; i <= 9; i++) {
        std::string path = "\\\\.\\pipe\\discord-ipc-" + std::to_string(i);
        WaitNamedPipeA(path.c_str(), 1000);

        HANDLE pipe = CreateFileA(
            path.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_EXISTING, 0, nullptr
        );
        if (pipe == INVALID_HANDLE_VALUE) continue;

        // Handshake (op=0)
        std::string hs  = "{\"v\":1,\"client_id\":\"";
        hs             += DISCORD_CLIENT_ID;
        hs             += "\"}";
        if (!ipc_write(pipe, 0, hs.c_str(), static_cast<uint32_t>(hs.size()))) {
            CloseHandle(pipe);
            continue;
        }

        uint32_t    resp_op = 0;
        std::string resp_json;
        if (!ipc_read(pipe, resp_op, resp_json)) {
            CloseHandle(pipe);
            continue;
        }

        return pipe;
    }
    return INVALID_HANDLE_VALUE;
}

// Escape a string for JSON (handles quotes and backslashes)
static std::string json_escape(const char* s) {
    std::string out;
    for (; *s; ++s) {
        if (*s == '"')       out += "\\\"";
        else if (*s == '\\') out += "\\\\";
        else                 out += *s;
    }
    return out;
}

static bool discord_set_activity(HANDLE pipe, const char* details, const char* state, int64_t start_ts) {
    ++g_nonce;

    char json[1024];
    int  len;

    if (details && *details) {
        // With details field (currently unused, kept for future use)
        std::string details_esc = json_escape(details);
        std::string state_esc   = json_escape(state);
        len = snprintf(json, sizeof(json),
            "{"
            "\"cmd\":\"SET_ACTIVITY\","
            "\"args\":{"
                "\"pid\":%lu,"
                "\"activity\":{"
                    "\"details\":\"%s\","
                    "\"state\":\"%s\","
                    "\"timestamps\":{\"start\":%lld}"
                "}"
            "},"
            "\"nonce\":\"%d\""
            "}",
            static_cast<unsigned long>(GetCurrentProcessId()),
            details_esc.c_str(),
            state_esc.c_str(),
            static_cast<long long>(start_ts),
            g_nonce
        );
    } else {
        // Without details — Discord shows app name from Developer Portal, no duplication
        std::string state_esc = json_escape(state);
        len = snprintf(json, sizeof(json),
            "{"
            "\"cmd\":\"SET_ACTIVITY\","
            "\"args\":{"
                "\"pid\":%lu,"
                "\"activity\":{"
                    "\"state\":\"%s\","
                    "\"timestamps\":{\"start\":%lld}"
                "}"
            "},"
            "\"nonce\":\"%d\""
            "}",
            static_cast<unsigned long>(GetCurrentProcessId()),
            state_esc.c_str(),
            static_cast<long long>(start_ts),
            g_nonce
        );
    }

    if (len <= 0 || len >= static_cast<int>(sizeof(json))) return false;

    ipc_drain(pipe);
    if (!ipc_write(pipe, 1, json, static_cast<uint32_t>(len))) return false;

    uint32_t    resp_op = 0;
    std::string resp;
    if (!ipc_read(pipe, resp_op, resp)) return false;

    return true;
}

// ============================================================
// Lua Self-Extraction Helpers
// All calls use plain Windows API — zero dependency on REFramework SDK.
// ============================================================

// Returns the full path to discord_presence.lua relative to the game exe.
static std::string get_lua_path() {
    char exe_dir[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe_dir, MAX_PATH);
    char* sep = strrchr(exe_dir, '\\');
    if (sep) *(sep + 1) = '\0';
    return std::string(exe_dir) + LUA_FILE;
}

// Returns true if the file is missing OR its first line doesn't match LUA_VERSION_TAG.
static bool lua_needs_update(const std::string& path) {
    HANDLE h = CreateFileA(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (h == INVALID_HANDLE_VALUE) return true;  // file doesn't exist yet

    char    buf[64] = {};
    DWORD   n       = 0;
    DWORD   tag_len = static_cast<DWORD>(strlen(LUA_VERSION_TAG));
    ReadFile(h, buf, tag_len, &n, nullptr);
    CloseHandle(h);

    return (n < tag_len) || (strncmp(buf, LUA_VERSION_TAG, tag_len) != 0);
}

// Writes the embedded Lua source to disk.
// Also ensures the autorun directory exists (safe to call even if it's already there).
static void write_lua_file(const std::string& path) {
    // Ensure reframework\autorun\ exists
    std::string dir = path.substr(0, path.rfind('\\'));
    CreateDirectoryA(dir.c_str(), nullptr);  // no-op if already exists

    HANDLE h = CreateFileA(
        path.c_str(), GENERIC_WRITE, 0,
        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (h == INVALID_HANDLE_VALUE) return;

    DWORD src_len = static_cast<DWORD>(strlen(LUA_SOURCE));
    DWORD written = 0;
    WriteFile(h, LUA_SOURCE, src_len, &written, nullptr);
    CloseHandle(h);
}

// Entry point called once from reframework_plugin_initialize.
// Fast path (version matches): one CreateFile + ~30-byte read, then done.
// Also ensures both reframework/autorun/DiscordPresence/ and
// reframework/data/DiscordPresence/ directories exist.
static void extract_lua_if_needed() {
    // Build base exe dir
    char exe_dir[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exe_dir, MAX_PATH);
    char* sep = strrchr(exe_dir, '\\');
    if (sep) *(sep + 1) = '\0';
    std::string base(exe_dir);

    // Ensure data/DiscordPresence/ exists (Lua writes the status file here)
    CreateDirectoryA((base + "reframework\\data\\DiscordPresence").c_str(), nullptr);

    // Extract Lua if missing or outdated
    std::string lua_path = base + LUA_FILE;
    if (lua_needs_update(lua_path)) {
        write_lua_file(lua_path);
    }
}

// ============================================================
// Background Worker Thread
// ============================================================

static void discord_thread_func() {
    const int64_t session_start = static_cast<int64_t>(time(nullptr));
    std::string   last_status   = "";

    while (g_running) {
        // Connect / reconnect
        if (g_pipe == INVALID_HANDLE_VALUE) {
            g_pipe = discord_connect();
            if (g_pipe == INVALID_HANDLE_VALUE) {
                // Retry after 15 seconds
                for (int i = 0; i < 150 && g_running; i++)
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            // Force update on reconnect
            last_status = "__force__";
        }

        // Read current status from Lua bridge file
        std::string status = read_status_file();

        // Only send to Discord if state changed (reduces IPC chatter)
        if (status != last_status) {
            PresenceState presence = get_presence_from_status(status);

            if (!discord_set_activity(g_pipe, presence.details, presence.state, session_start)) {
                CloseHandle(g_pipe);
                g_pipe = INVALID_HANDLE_VALUE;
                continue;
            }
            last_status = status;
        }

        // Sleep UPDATE_INTERVAL_MS in small chunks for fast shutdown
        const int chunks = UPDATE_INTERVAL_MS / 100;
        for (int i = 0; i < chunks && g_running; i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (g_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(g_pipe);
        g_pipe = INVALID_HANDLE_VALUE;
    }
}

// ============================================================
// REFramework Plugin Entry Point
// ============================================================
struct REFrameworkPluginInitializeParam;

extern "C" {
    __declspec(dllexport)
    bool reframework_plugin_initialize(const REFrameworkPluginInitializeParam*) {
        // Self-extract Lua bridge script to reframework/autorun/ if needed.
        // Runs before REFramework scans autorun/, so the script is always present.
        extract_lua_if_needed();

        g_running = true;
        g_thread  = std::thread(discord_thread_func);
        return true;
    }
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH && g_running) {
        g_running = false;
    }
    return TRUE;
}
