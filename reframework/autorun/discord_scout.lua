-- Discord Scout — Status Dashboard
-- Shows all currently known game parameters live
-- Read-only, no game impact

local function safe(fn) local ok,r=pcall(fn); if ok then return r end; return nil end
local function str(v) if v==nil then return "nil" end; local ok,s=pcall(tostring,v); return ok and s or "?" end

-- ─── DIFFICULTY MAP ───────────────────────────────────────
local DIFF_MAP = { [1200]="Casual", [4200]="Modern", [6200]="Classic", [9000]="Insanity" }

-- ─── MENTAL STATE MAP ─────────────────────────────────────
local MENTAL = {
    {512,"Sick"},{256,"Scared"},{128,"Wheeze"},{64,"Tired"},
    {16,"Panic"},{32,"Hide"},{8,"Agony"},{4,"Pressure"},{2,"Vigilance"},{1,"Calm"},
}
local function mental_name(flags)
    local n = tonumber(flags) or 0
    if n==0 then return "Calm" end
    for _,e in ipairs(MENTAL) do if (n&e[1])~=0 then return e[2] end end
    return "Calm"
end

-- ─── LIVE STATE ───────────────────────────────────────────
local S = {
    in_game=false, -- QuestManager._IsIngameSetupped
    chara="?", section="?", chapter="?", prologue=nil,
    difficulty="?",
    grace_state="?", grace_flags=0,
    leon_hp=nil, leon_armor=nil, leon_hr=nil,
    quest_on=nil,
}

re.on_frame(function()
    -- ── Main Menu detection
    local qm = safe(function() return sdk.get_managed_singleton("app.QuestManager") end)
    S.in_game = qm and safe(function() return qm:get_field("_IsIngameSetupped") end) or false

    if not S.in_game then
        S.chara="?"; S.section="?"; S.chapter="?"; S.quest_on=nil
        S.difficulty="?"; S.grace_state="?"; S.grace_flags=0
        S.leon_hp=nil; S.leon_armor=nil; S.leon_hr=nil
        return
    end

    -- ── GuiManager: prologue flag
    local gm = safe(function() return sdk.get_managed_singleton("app.GuiManager") end)
    if gm then
        S.quest_on = safe(function() return gm:call("get_IsQuestEnable()") end)
    end
    S.prologue = (S.quest_on == false)

    -- ── Difficulty
    local rm = safe(function() return sdk.get_managed_singleton("app.RankManager") end)
    if rm then
        local gs = safe(function() return rm:call("get_GlobalSettings()") end)
        if gs then
            local pts = safe(function() return gs:call("get_DefaultRankPoints()") end)
            S.difficulty = pts and (DIFF_MAP[pts] or ("Rank "..pts)) or "?"
        end
    end

    -- ── Chapter / Section
    local im = safe(function() return sdk.get_managed_singleton("app.ItemManager") end)
    if im then
        local ss = safe(function() return im:get_field("_SectionSetting") end)
        if ss then
            local sid = safe(function() return ss:get_field("_SectionID") end)
            S.section = str(sid)
            local num = S.section:match("sct(%d+)")
            S.chapter = num and tonumber(num) or "Invalid"
        else
            S.section = "nil"
            S.chapter = S.prologue and "PROLOGUE" or "?"
        end
    end

    -- ── Character
    local cm = safe(function() return sdk.get_managed_singleton("app.CharacterManager") end)
    if not cm then S.chara="?"; return end
    local pcf = safe(function() return cm:get_field("<PlayerContextFast>k__BackingField") end)
    if not pcf then S.chara="?"; return end

    local a1 = safe(function() return pcf:get_field("<Cp_A1Unit>k__BackingField") end)
    local a0 = safe(function() return pcf:get_field("<Cp_A0Unit>k__BackingField") end)
    S.chara = a1 and "GRACE" or (a0 and "LEON" or "?")

    local updater = safe(function() return pcf:get_field("<Updater>k__BackingField") end)
    if not updater then return end
    local go    = safe(function() return updater:call("get_GameObject()") end)
    if not go then return end
    local comps = safe(function() return go:call("get_Components()") end)
    if not comps then return end
    local count = tonumber(safe(function() return comps:call("get_Count()") end)) or 0

    if S.chara == "GRACE" then
        -- Mental state from comp[93]
        if count > 93 then
            local msd = safe(function() return comps:call("get_Item(System.Int32)", 93) end)
            if msd then
                local flags = safe(function() return msd:get_field("<StateFlags>k__BackingField") end)
                S.grace_flags = tonumber(flags) or 0
                S.grace_state = mental_name(S.grace_flags)
            end
        end
        -- Reset Leon fields
        S.leon_hp=nil; S.leon_armor=nil; S.leon_hr=nil

    elseif S.chara == "LEON" then
        -- HP from comp[18]
        if count > 18 then
            local spl = safe(function() return comps:call("get_Item(System.Int32)", 18) end)
            if spl then
                local r = tonumber(safe(function() return spl:call("get_HitPointRatio()") end))
                if r then S.leon_hp = math.max(0,math.min(100,math.floor(r*100))) end
            end
        end
        -- Armor from comp[54]
        if count > 54 then
            local hc  = safe(function() return comps:call("get_Item(System.Int32)", 54) end)
            local dhi = hc and safe(function() return hc:get_field("<DamageHitInfo>k__BackingField") end)
            if dhi then
                local hp_b = tonumber(safe(function() return dhi:get_field("<DamageOwnerHitPointBeforeDamaged>k__BackingField") end))
                local dmg  = tonumber(safe(function() return dhi:get_field("<Damage>k__BackingField") end)) or 0
                if hp_b and hp_b > 0 then
                    S.leon_armor = math.floor(math.max(0, hp_b-dmg) / 1200 * 100)
                end
            end
        end
        -- HeartRate from comp[91]
        if count > 91 then
            local hrd  = safe(function() return comps:call("get_Item(System.Int32)", 91) end)
            local hrc  = hrd and safe(function() return hrd:get_field("<HeartRateCore>k__BackingField") end)
            local unit = hrc and safe(function() return hrc:get_field("<Unit>k__BackingField") end)
            if unit then
                local hr = tonumber(safe(function() return unit:call("get_HeartRate()") end))
                if hr then S.leon_hr = hr end
            end
        end
        -- Reset Grace fields
        S.grace_state="?"; S.grace_flags=0
    end
end)

re.on_draw_ui(function()
    imgui.begin_window("RE Requiem — Status Dashboard", true, 0)

    -- ── MAIN MENU ──────────────────────────────
    if not S.in_game then
        imgui.text_colored("  ●  MAIN MENU", 0xFFFFFF44)
        imgui.text_colored("  (QuestManager._IsIngameSetupped = false)", 0xFF888888)
        imgui.end_window()
        return
    end


    -- ── META ──────────────────────────────────────────────
    imgui.text_colored("── Meta ─────────────────────────────", 0xFF888888)

    local quest_str = S.quest_on==nil and "?" or (S.quest_on and "true" or "false")
    local quest_col = S.quest_on and 0xFF44FF44 or 0xFFFF6644
    imgui.text("  IsQuestEnable : ")
    imgui.same_line()
    imgui.text_colored(quest_str, quest_col)

    local section_col = S.section:find("sct") and 0xFF44FFFF or 0xFFFF4444
    imgui.text("  Section       : ")
    imgui.same_line()
    imgui.text_colored(S.section, section_col)

    local ch_str = tostring(S.chapter)
    local ch_col = (ch_str == "PROLOGUE") and 0xFFFFAA00 or
                   (S.chapter ~= "?" and S.chapter ~= "Invalid") and 0xFF44FF44 or 0xFFFF4444
    imgui.text("  Chapter       : ")
    imgui.same_line()
    imgui.text_colored(ch_str, ch_col)

    local diff_col = (S.difficulty ~= "?") and 0xFF44FFFF or 0xFFFF4444
    imgui.text("  Difficulty    : ")
    imgui.same_line()
    imgui.text_colored(S.difficulty, diff_col)

    imgui.separator()

    -- ── CHARACTER ─────────────────────────────────────────
    imgui.text_colored("── Character ────────────────────────", 0xFF888888)

    local chara_col = (S.chara=="GRACE") and 0xFFFF88CC or
                      (S.chara=="LEON")  and 0xFF88CCFF or 0xFFFF4444
    imgui.text("  Character     : ")
    imgui.same_line()
    imgui.text_colored(S.chara, chara_col)

    if S.chara == "GRACE" then
        imgui.separator()
        imgui.text_colored("── Grace ────────────────────────────", 0xFFFF88CC)
        imgui.text(string.format("  MentalState   : %s  (flags=%d)", S.grace_state, S.grace_flags))

    elseif S.chara == "LEON" then
        imgui.separator()
        imgui.text_colored("── Leon ─────────────────────────────", 0xFF88CCFF)

        local hp_str = S.leon_hp and (S.leon_hp.."%") or "unknown (no hit yet)"
        local hp_col = not S.leon_hp and 0xFFFF4444 or
                       (S.leon_hp > 60) and 0xFF44FF44 or
                       (S.leon_hp > 30) and 0xFFFFAA00 or 0xFFFF4444
        imgui.text("  HP            : ")
        imgui.same_line()
        imgui.text_colored(hp_str, hp_col)

        local ar_str = S.leon_armor and (S.leon_armor.."%  (stale until hit)") or "? (no hit yet)"
        local ar_col = S.leon_armor and 0xFF44FFFF or 0xFFFF4444
        imgui.text("  Armor         : ")
        imgui.same_line()
        imgui.text_colored(ar_str, ar_col)

        local hr_str = (S.leon_hr and S.leon_hr > 0) and tostring(S.leon_hr) or "calm (0)"
        local hr_col = (S.leon_hr and S.leon_hr > 0) and 0xFFFFAA00 or 0xFF888888
        imgui.text("  HeartRate     : ")
        imgui.same_line()
        imgui.text_colored(hr_str, hr_col)
    end

    imgui.separator()

    -- ── PREVIEW ───────────────────────────────────────────
    imgui.text_colored("── Discord Preview ──────────────────", 0xFF888888)
    local ch_label = (S.chapter == "PROLOGUE") and "Prologue"
                     or (type(S.chapter)=="number") and ("Chapter "..S.chapter)
                     or "?"
    local details, state
    if S.chara == "GRACE" then
        details = "Grace | " .. S.grace_state
    elseif S.chara == "LEON" then
        local hp_s = S.leon_hp and (S.leon_hp.."%") or "?"
        local ar_s = S.leon_armor and (" AR:"..S.leon_armor.."%") or ""
        local hr_s = (S.leon_hr and S.leon_hr>0) and (" HR:"..S.leon_hr) or ""
        details = "Leon | HP:" .. hp_s .. ar_s .. hr_s
    else
        details = "Playing"
    end
    state = ch_label .. (S.difficulty~="?" and (" | "..S.difficulty) or "")

    imgui.text_colored("  " .. details, 0xFFFFFFFF)
    imgui.text_colored("  " .. state,   0xFFCCCCCC)

    imgui.end_window()
end)
