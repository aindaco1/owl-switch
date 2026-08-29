local assdraw = require("mp.assdraw")
local timeout = tonumber(mp.get_opt("screensaver-timeout") or "60") or 60
local overlay = mp.create_osd_overlay("ass-events")
local active = false
local paused_seconds = 0
local x, y, vx, vy = 20, 20, 2, 2
local logo_w, logo_h = 180, 48
local animation

local dismiss_keys = {
    "SPACE", "ENTER", "KP_ENTER", "ESC", "LEFT", "RIGHT", "UP", "DOWN",
    "PLAYPAUSE", "STOP", "FORWARD", "REWIND", "NEXT", "PREV",
    "VOLUME_UP", "VOLUME_DOWN", "MUTE"
}

local function draw()
    local width, height = mp.get_osd_size()
    if width == 0 or height == 0 then return end
    local ass = assdraw.ass_new()
    ass:new_event()
    ass:append(string.format("{\\an7\\pos(0,0)\\bord0\\shad0\\1c&H000000&\\p1}m 0 0 l %d 0 l %d %d l 0 %d{\\p0}", width, width, height, height))
    ass:new_event()
    ass:append(string.format("{\\an7\\pos(%d,%d)\\fnVCR OSD Mono\\fs48\\1c&HFFFFFF&\\bord0\\shad0}OWLSWITCH", x, y))
    overlay.res_x = width
    overlay.res_y = height
    overlay.data = ass.text
    overlay:update()
end

local function dismiss()
    if not active then return end
    active = false
    paused_seconds = 0
    overlay:remove()
    animation:kill()
    for _, key in ipairs(dismiss_keys) do
        pcall(mp.remove_key_binding, "screensaver-dismiss-" .. key)
    end
end

local function activate()
    if active then return end
    active = true
    local width, height = mp.get_osd_size()
    x = math.random() * math.max(1, width - logo_w)
    y = math.random() * math.max(1, height - logo_h)
    for _, key in ipairs(dismiss_keys) do
        mp.add_forced_key_binding(key, "screensaver-dismiss-" .. key, dismiss)
    end
    draw()
    animation:resume()
end

mp.add_periodic_timer(1, function()
    if active then return end
    if mp.get_property_native("pause") then
        paused_seconds = paused_seconds + 1
        if paused_seconds >= timeout then activate() end
    else
        paused_seconds = 0
    end
end)

animation = mp.add_periodic_timer(0.016, function()
    if not active then return end
    local width, height = mp.get_osd_size()
    x = x + vx; y = y + vy
    if x + logo_w >= width then x = width - logo_w; vx = -math.abs(vx)
    elseif x <= 0 then x = 0; vx = math.abs(vx) end
    if y + logo_h >= height then y = height - logo_h; vy = -math.abs(vy)
    elseif y <= 0 then y = 0; vy = math.abs(vy) end
    draw()
end)
animation:kill()
