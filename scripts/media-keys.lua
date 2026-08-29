local assdraw = require 'mp.assdraw'

local VOLUME_STEP = 5
local SEEK_FORWARD = 30
local SEEK_BACK = 10
local BAR_TIMEOUT = 1.5
local C_WHITE = "&HFFFFFF&"
local A_OPAQUE = "&H00&"
local A_DIM = "&HB0&"
local bar_overlay = mp.create_osd_overlay("ass-events")
local bar_timer = nil

local function hide_bar()
    if bar_timer then bar_timer:kill(); bar_timer = nil end
    bar_overlay:remove()
end

mp.register_script_message("owlswitch-osd-volume-hide", hide_bar)

local function draw_rect(ass, x, y, w, h, colour, alpha)
    ass:new_event()
    ass:pos(x, y)
    ass:append(string.format("{\\bord0\\shad0\\1c%s\\1a%s}", colour, alpha))
    ass:draw_start()
    ass:rect_cw(0, 0, w, h)
    ass:draw_stop()
end

local function draw_text(ass, x, y, anchor, value, size, colour)
    ass:new_event()
    ass:append(string.format(
        "{\\an%d\\pos(%d,%d)\\fnVCR OSD Mono\\fs%d\\1c%s\\1a%s\\shad0\\bord0}%s",
        anchor, x, y, size, colour, A_OPAQUE, value))
end

local function show_volume_bar()
    mp.commandv("script-message", "owlswitch-osd-menu-hide")
    local ww, wh = mp.get_osd_size()
    if ww == 0 or wh == 0 then return end
    local volume = mp.get_property_number("volume", 0) or 0
    local maximum = mp.get_property_number("volume-max", 100) or 100
    local ticks = math.max(1, math.floor(maximum / VOLUME_STEP + 0.5))
    local filled = math.max(0, math.min(math.floor(volume / VOLUME_STEP + 0.5), ticks))
    local font_size = math.floor(wh * 0.0333333)
    local left = math.floor(ww * 0.12)
    local width = math.floor(ww * 0.88) - left
    local height = math.floor(font_size * 2)
    local label_y = math.floor(wh * 0.7979166)
    local bar_y = math.floor(wh * 0.8333333)
    local slot_width = width / ticks
    local gap = math.max(1, math.floor(slot_width * 0.35))
    local tick_width = math.max(1, math.floor(slot_width - gap))
    local dash_height = math.max(2, math.floor(height * 0.15))
    local ass = assdraw.ass_new()
    local label = mp.get_property_bool("mute", false) and "MUTE" or "VOLUME"
    draw_text(ass, left, label_y, 1, label, font_size * 3, C_WHITE)
    local x = left
    for i = 1, ticks do
        if i <= filled then
            draw_rect(ass, math.floor(x), bar_y, tick_width, height, C_WHITE, A_OPAQUE)
        else
            draw_rect(ass, math.floor(x), bar_y + math.floor((height - dash_height) / 2),
                      tick_width, dash_height, C_WHITE, A_DIM)
        end
        x = x + slot_width
    end
    bar_overlay.res_x = ww
    bar_overlay.res_y = wh
    bar_overlay.data = ass.text
    bar_overlay:update()
    if bar_timer then bar_timer:kill() end
    bar_timer = mp.add_timeout(BAR_TIMEOUT, hide_bar)
end

local function change_volume(delta)
    mp.command("no-osd add volume " .. delta)
    show_volume_bar()
end

local function seek_with_menu(command)
    mp.command(command)
    mp.commandv("script-message", "owlswitch-osd-menu-show")
end

mp.add_forced_key_binding("VOLUME_UP", "mk-vol-up", function() change_volume(VOLUME_STEP) end, {repeatable=true})
mp.add_forced_key_binding("VOLUME_DOWN", "mk-vol-down", function() change_volume(-VOLUME_STEP) end, {repeatable=true})
mp.add_forced_key_binding("MUTE", "mk-mute", function() mp.command("no-osd cycle mute"); show_volume_bar() end)
mp.add_forced_key_binding("PLAYPAUSE", "mk-playpause", function() mp.command("cycle pause") end)
mp.add_forced_key_binding("STOP", "mk-stop", function() mp.command("quit") end)
mp.add_forced_key_binding("FORWARD", "mk-forward", function() seek_with_menu("no-osd seek " .. SEEK_FORWARD) end)
mp.add_forced_key_binding("REWIND", "mk-rewind", function() seek_with_menu("no-osd seek -" .. SEEK_BACK) end)
mp.add_forced_key_binding("NEXT", "mk-next", function() seek_with_menu("no-osd add chapter 1") end)
mp.add_forced_key_binding("PREV", "mk-prev", function() seek_with_menu("no-osd add chapter -1") end)
