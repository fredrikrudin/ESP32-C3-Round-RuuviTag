from PIL import Image, ImageDraw, ImageFont
import math

S = 4                      # supersampling
OUT = 2                    # final scale: 240 logical px -> 480 px
PAD = 10                   # bezel padding (logical px)
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
FONTB = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"

def F(px, bold=False):
    return ImageFont.truetype(FONTB if bold else FONT, int(px * S * 0.9))

BG_MAIN=(5,10,25); BG_PAGE=(10,15,35); PANEL=(15,35,75)
ACCENT=(0,190,255); TITLE=(0,150,255); SOFT=(180,210,255)

def new():
    im = Image.new("RGB", (240*S, 240*S), BG_MAIN)
    return im, ImageDraw.Draw(im)

def T(d, x, y, s, font, fill, anchor="ma"):
    d.text((x*S, y*S), s, font=font, fill=fill, anchor=anchor)

def rr(d, x0, y0, x1, y1, r, fill, outline=None, w=1):
    d.rounded_rectangle([x0*S, y0*S, x1*S, y1*S], radius=r*S, fill=fill, outline=outline, width=w*S)

def arc(d, cx, cy, r, w, a0, a1, col):
    box = [(cx-r)*S, (cy-r)*S, (cx+r)*S, (cy+r)*S]
    d.arc(box, a0, a1, fill=col, width=int(w*S))
    for a in (a0, a1):                                   # rounded caps like LVGL
        rad = r - w/2
        px = cx + rad*math.cos(math.radians(a)); py = cy + rad*math.sin(math.radians(a))
        d.ellipse([(px-w/2)*S, (py-w/2)*S, (px+w/2)*S, (py+w/2)*S], fill=col)

def battery(d, x, y, col, frac=1.0):
    d.rectangle([x*S, y*S, (x+15)*S, (y+8)*S], outline=col, width=int(1.2*S))
    d.rectangle([(x+15)*S, (y+2.5)*S, (x+17)*S, (y+5.5)*S], fill=col)
    d.rectangle([(x+1.8)*S, (y+1.8)*S, (x+1.8+11.4*frac)*S, (y+6.2)*S], fill=col)

def wifi_icon(d, x, y, col):
    for r in (3, 6, 9):
        d.arc([(x-r)*S, (y+8-r)*S, (x+r)*S, (y+8+r)*S], 225, 315, fill=col, width=int(1.6*S))
    d.ellipse([(x-1.3)*S, (y+6.7)*S, (x+1.3)*S, (y+9.3)*S], fill=col)

def check(d, x, y, col):
    d.line([(x-4)*S, (y+1)*S, (x-1)*S, (y+4)*S, (x+5)*S, (y-4)*S], fill=col, width=int(2*S))

def diamond(d, x, y, col):
    d.polygon([(x*S,(y-4)*S),((x+3.5)*S,y*S),(x*S,(y+4)*S),((x-3.5)*S,y*S)], fill=col)

WIFI_ON=(140,150,170); WIFI_OFF=(45,52,68)

def wifi_indicator(d, level=3):
    """Wi-Fi symbol + 4 signal bars, dim grey, under the clock (matches gui_ruuvi.cpp)."""
    x0, y0 = 101, 57                       # 38x14 box, top-mid aligned
    wifi_icon(d, x0 + 7, y0 + 1, WIFI_ON if level >= 0 else WIFI_OFF)
    for i, h in enumerate((4, 7, 10, 13)):
        x = x0 + 19 + i*5
        col = WIFI_ON if i < level else WIFI_OFF
        d.rounded_rectangle([x*S, (y0+14-h)*S, (x+3)*S, (y0+14)*S], radius=1*S, fill=col)

def footer(d, left, right, mid, mid_col=(120,140,180)):
    """Bottom strip: '<' , middle text, '>' (matches gui_ruuvi.cpp)."""
    if left:   T(d, 120-54, 208, "<", F(16, True), TITLE)
    if right:  T(d, 120+54, 208, ">", F(16, True), TITLE)
    T(d, 120, 208, mid, F(14), mid_col)

def checkbox(d, x, y, checked):
    d.rounded_rectangle([x*S, y*S, (x+16)*S, (y+16)*S], radius=3*S,
                        fill=ACCENT if checked else PANEL, outline=ACCENT, width=int(1.5*S))
    if checked:
        d.line([(x+3.5)*S, (y+8.5)*S, (x+7)*S, (y+12)*S, (x+13)*S, (y+4.5)*S], fill=(255,255,255), width=int(2*S))

def title(d, s):
    T(d, 120, 12, s, F(16), TITLE)

def page_main(hum=45, temp="21.4°", tcol=(255,220,130), clock="12:34", sensor="Ruuvi 2233"):
    im, d = new()
    arc(d, 120, 120, 114-5, 10, 135, 405, (0,50,100))                       # gauge background
    arc(d, 120, 120, 114-5, 10, 135, 135 + 270*hum/100, ACCENT)             # humidity
    T(d, 120, 36, clock, F(16), SOFT)
    wifi_indicator(d, 3)
    T(d, 120, 96, temp, F(48, True), tcol, "mm")
    T(d, 120, 138, f"{hum}% RH", F(14), ACCENT, "mm")
    rr(d, 60, 154, 180, 198, 15, PANEL)
    T(d, 120, 166, "1013.2 hPa", F(14), (200,220,255), "mm")
    tw = d.textlength("2.98 V", font=F(14)) / S
    bx = 120 - (19 + 5 + tw)/2
    battery(d, bx, 180, (200,220,255))
    T(d, bx + 24, 184.5, "2.98 V", F(14), (200,220,255), "lm")
    footer(d, True, True, sensor, SOFT)
    return im

def page_settings():
    im, d = new(); d.rectangle([0,0,240*S,240*S], fill=BG_PAGE)
    title(d, "Settings")
    rr(d, 45, 32, 195, 62, 5, PANEL)
    T(d, 55, 47, "Scan every 5 s", F(14), (255,255,255), "lm")
    d.polygon([(178*S,44*S),(188*S,44*S),(183*S,51*S)], fill=(255,255,255))
    T(d, 120, 72, "Night dim (hours)", F(14), SOFT)
    for cx, vals in ((120-42, ("21","22","23")), (120+42, ("05","06","07"))):
        x0, x1 = cx-28, cx+28
        rr(d, x0, 92, x1, 148, 5, PANEL)
        rr(d, x0, 106, x1, 134, 3, (30,120,220))
        T(d, cx, 98, vals[0], F(14), (150,170,210), "mm")
        T(d, cx, 120, vals[1], F(14, True), (255,255,255), "mm")
        T(d, cx, 143, vals[2], F(14), (150,170,210), "mm")
    T(d, 120, 120, "›", F(18, True), (255,255,255), "mm")
    T(d, 82, 172, "Wi-Fi sleep", F(14), (255,255,255), "mm")
    rr(d, 139, 160, 185, 184, 12, (70,80,100))
    d.ellipse([141*S, 162*S, 161*S, 182*S], fill=(255,255,255))
    footer(d, False, True, "Back", TITLE)
    return im

def page_wifi():
    im, d = new(); d.rectangle([0,0,240*S,240*S], fill=BG_PAGE)
    title(d, "Wi-Fi")
    T(d, 120, 34, "Connected: HomeNet", F(14), SOFT)
    rr(d, 65, 56, 175, 84, 5, (0,100,200))
    T(d, 120, 70, "Scan Wi-Fi", F(14), (255,255,255), "mm")
    rr(d, 35, 92, 205, 190, 4, BG_MAIN)
    d.rectangle([35*S, 92*S, 205*S, 190*S], fill=BG_MAIN)
    for i, name in enumerate(("HomeNet", "Neighbour-5G", "Guest")):
        y = 92 + i*32
        if y + 32 > 190: 
            y_c = y + 14
        wifi_icon(d, 52, y+8, (255,255,255))
        T(d, 68, y+16, name, F(14), (255,255,255), "lm")
        d.line([35*S, (y+32)*S, 205*S, (y+32)*S], fill=(30,40,65), width=S)
    footer(d, True, True, "Back", TITLE)
    return im

def page_sensor():
    im, d = new(); d.rectangle([0,0,240*S,240*S], fill=BG_PAGE)
    title(d, "Sensor")
    T(d, 120, 34, "Default: Ruuvi 2233", F(14), SOFT)
    rr(d, 65, 56, 175, 84, 5, (0,100,200))
    T(d, 120, 70, "Refresh", F(14), (255,255,255), "mm")
    rr(d, 32, 92, 208, 190, 4, BG_MAIN)
    rows = [("Auto (strongest)", False), ("Ruuvi 2233 (-58)", True), ("Ruuvi 7E15 (-71)", False)]
    for i, (txt, sel) in enumerate(rows):
        y = 98 + i*30
        checkbox(d, 42, y, sel)
        T(d, 65, y+8, txt, F(14), (255,255,255), "lm")
    footer(d, True, False, "Back", TITLE)
    return im

def to_device(im):
    """Clip to a circle and add a dark bezel; transparent outside."""
    W = (240 + 2*PAD) * S
    canvas = Image.new("RGBA", (W, W), (0,0,0,0))
    cd = ImageDraw.Draw(canvas)
    cd.ellipse([0, 0, W-1, W-1], fill=(28,30,34,255))
    cd.ellipse([2*S, 2*S, W-1-2*S, W-1-2*S], outline=(70,74,82,255), width=S)
    mask = Image.new("L", (240*S, 240*S), 0)
    ImageDraw.Draw(mask).ellipse([0, 0, 240*S-1, 240*S-1], fill=255)
    canvas.paste(im.convert("RGBA"), (PAD*S, PAD*S), mask)
    size = (240 + 2*PAD) * OUT
    return canvas.resize((size, size), Image.LANCZOS)

pages = {"main": page_main(), "settings": page_settings(), "wifi": page_wifi(), "sensor": page_sensor()}
devs = {k: to_device(v) for k, v in pages.items()}
devs["main"].save("preview-main.png")

gap = 16
w = devs["main"].width
sheet = Image.new("RGBA", (w*4 + gap*3, w), (0,0,0,0))
for i, k in enumerate(("main", "settings", "wifi", "sensor")):
    sheet.paste(devs[k], (i*(w+gap), 0), devs[k])
sheet.save("preview-pages.png")
print(devs["main"].size, sheet.size)
