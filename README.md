# Loxone panel for Guition ESP32-4848S040C_I_Y_1

Firmware for the Guition 4.0 inch 480×480 capacitive wall panel. It draws a Loxone home screen and talks to a local Miniserver over HTTP. The one-way relay is a local switch on the Schalter screen.

The photo on this unit reads SKU 10103003 (2444), model `ESP32-4848S040C_I_Y_1`, and barcode `C3713-1`. `10103003` and `C3713-1` do not appear in Guition's specification or product page checked for this project. The model string does.

## What the board is

Guition (Shenzhen Jingcai Intelligent) sells this as a 4.0 inch IPS module in an enclosure, built for an 86 mm wall box.

| Item | Value | Where it comes from |
| --- | --- | --- |
| Model | ESP32-4848S040C_I_Y_1 | Guition product page and specification body. This is the one-relay SKU. |
| Three-relay sibling | ESP32-4848S040C_I_Y_3 | Same specification table |
| MCU | ESP32-S3-WROOM-1 / ESP32-S3R8, dual core 240 MHz | Guition specification and product page |
| Flash / PSRAM | 16 MB flash, 8 MB PSRAM | Guition specification |
| Display | 4.0 inch IPS, 480×480, 16-bit RGB, driver ST7701 | Guition specification names ST7701 |
| Touch | Capacitive | Guition specification. The touch IC name is not in that PDF; see below |
| Relay | One channel on this SKU | Guition specification: "One way relay: ESP32-4848S040C_I_Y_1". Product page: "Relay Output: 1-channel switch" |
| Power | 5 V | Guition specification |
| USB | USB-C on a CH340 USB-UART bridge | ha5dzs pin header and board notes. UART0 is GPIO43 TX / GPIO44 RX |

The specification cover prints `ESP32-4848S043C_I_Y_1`. The page body and the product page use `ESP32-4848S040C_I_Y_1`, which matches the label. Treat the cover `S043` as a typo.

Guition does not define the letters in `C_I_Y_1` one by one. The specification only maps `_Y_1` to the one-way relay and `_Y_3` to the three-way relay. The Chinese retail line "带外壳1位输入" (enclosure, one input) is the same one-channel unit. This firmware drives that one relay from the Schalter screen. GPIO1 and GPIO2 stay unused.

The "Interface Description" page of the Guition PDF is a product photo (TF card, battery pads, USB-C). It is not a GPIO table. The pinout below is from published working configs that agree with each other, not from that drawing.

## Known versus assumed

**Named by Guition**

- ESP32-S3, 16 MB flash, 8 MB PSRAM, 480×480 IPS, capacitive touch
- Display driver ST7701
- `ESP32-4848S040C_I_Y_1` is the one-relay SKU

**High confidence, but not printed in the Guition PDF text**

These pins are the same in Arduino_GFX's `ESP32_4848S040_86BOX_GUITION` device, the ha5dzs PlatformIO header, the NorthernMan54 GPIO table, the ESPHome device page, and HomeDing:

- ST7701 RGB bus, SPI init pins, backlight GPIO38
- GT911 on I2C SDA GPIO19, SCL GPIO45
- Relay 1 on GPIO40. The three-relay SKU adds GPIO2 and GPIO1
- TF card chip select GPIO42, MISO GPIO41, shared MOSI GPIO47 and SCK GPIO48
- USB-C is a CH340 on UART0, not the ESP32-S3 native USB pins. GPIO19 and GPIO20 are touch SDA and a green data line, so USB-CDC must stay off

**Assumed, and not verified on this exact unit**

- The touch controller is a GT911. Guition only says "capacitive". Every published working config for this capacitive line uses a GT911. This firmware probes I2C `0x5D` (HomeDing) and then `0x14` (one field report).
- GT911 INT and RST are not assigned in those GPIO tables. ha5dzs sets both to `-1`. This firmware does the same and does not invent pins. GPIO35, GPIO36, and GPIO37 are blank in the NorthernMan54 table; they are not used here.
- Display rotation is `1`, copied from the Arduino_GFX device entry (the flush in `main/panel_hw.cpp` rotates into the frame buffer). Touch uses the TAMC_GT911 `ROTATION_RIGHT` mapping (`x = raw_y`, `y = 480 - raw_x`), which is the ha5dzs mapping for that display rotation. If taps land in the wrong place, change that pair. ESPHome notes that a 270° rotation is used when the USB socket faces down; that was not applied here.
- Pixel byte order follows Arduino_GFX `useBigEndian = false` and `LV_COLOR_16_SWAP 0`. If red and blue are swapped on the glass, set `CONFIG_LV_COLOR_16_SWAP=y` in `sdkconfig.defaults`.

## Pinout

| Function | GPIO | Notes |
| --- | --- | --- |
| LCD R0–R4 | 11, 12, 13, 14, 0 | ST7701 RGB |
| LCD G0–G5 | 8, 20, 3, 46, 9, 10 | GPIO20 is also the module USB D+ pin; used as green data |
| LCD B0–B4 | 4, 5, 6, 7, 15 | |
| DE, VSYNC, HSYNC, PCLK | 18, 17, 16, 21 | |
| ST7701 SPI CS, SCK, MOSI | 39, 48, 47 | Init only. No DC, no MISO |
| Backlight | 38 | Driven high. Not PWM |
| GT911 SDA, SCL | 19, 45 | GPIO45 is a strapping pin; used as SCL after boot, as in the published configs |
| GT911 INT, RST | not connected | Passed to the touch library as `-1` |
| TF CS, MISO | 42, 41 | Slot is not mounted by this firmware |
| UART TX, RX | 43, 44 | CH340, USB flashing and serial log |
| Relay 1 | 40 | This SKU. Active high: HIGH on, LOW off. Off at boot |
| Relay 2, relay 3 | 2, 1 | Three-way SKU only. **Not configured, not driven** |

RGB timings copied from the Arduino_GFX device: PCLK 12 MHz, hsync polarity 1, front porch 10, pulse 8, back porch 50, vsync polarity 1, front porch 10, pulse 8, back porch 20. The ST7701 init sequence is `st7701_type9_init_operations` from that library ("480x480 square (86 Box) GUITION ESP32-4848S040").

Relay polarity is the ha5dzs setup write: `digitalWrite(RELAY1, LOW)` with the comment "All off, by default" (`src/main.cpp` in the ha5dzs port, `RELAY1` is GPIO40). HIGH turns the relay on. The ESPHome device page lists the same pin with `inverted: true`, which would mean the opposite. This firmware follows the ha5dzs off write, not that catalog flag.

## What the firmware does

Native ESP-IDF v6.1 project: `esp_lcd` RGB panel with the ST7701 init sequence from the Arduino_GFX device entry, GT911 over `i2c_master`, LVGL 8.4.

The screen is 480×480. The look follows the dark [Home Assistant visionOS theme](https://github.com/Nezz/homeassistant-visionos-theme): warm black background, translucent cards, 24 px corners, and the Apple system colors from that theme (orange, yellow, cyan, purple, green). LVGL on this panel cannot blur the backdrop, so the glass is a translucent card over a soft gradient.

Room names, control names, and the weather text use DejaVu Sans covering Latin-1 (U+00A0–U+00FF), including ä, ö, ü, Ä, Ö, Ü, and ß.

- With no `main/panel_config.h`, or with the Wi-Fi SSID, Miniserver host, or user left empty, the screen says it is not configured. It does not pretend the house is online.
- With a config file, it joins Wi-Fi and GETs `/data/LoxAPP3.json` with HTTP basic auth.
- The home screen shows the weather widget and five icon-only squircle tiles (no text, no counts), each with its own accent gradient and glow: Favoriten (star, yellow), Licht (lightbulb, orange), Storen (blinds_2, cyan), Schalter (toggle_on, purple) and Einstellungen (settings, grey). They flash only on a tap, not during a swipe. Weather comes from Open-Meteo over HTTPS with no API key, for the location chosen in Einstellungen. The default is Buchs SG (`47.1674 N, 9.4779 E`, from the Open-Meteo geocoder). A location saved in NVS takes priority. The Miniserver itself stays on local HTTP.
- Einstellungen has two entries. WLAN scans on the core-0 net task and lists SSIDs with signal bars. You pick one, enter the password on the on-screen keyboard and connect. Success stores SSID and password in NVS (namespace `panel`). On failure it reconnects with the previous credentials. Wetter-Ort searches the Open-Meteo geocoding API (`language=de`, `count=10`). Picking a result stores name, lat and lon in NVS and refreshes the weather immediately. Bildschirm sets the screen timeout (Aus, 30 s, 1, 2, 5, 10 min; default 2 min) and "Hinweis anzeigen" (default on). Both are stored in NVS. After the timeout a black layer covers the UI, so LVGL redraws almost nothing. The backlight (GPIO38, LEDC PWM, 600 ms fade) drops to 4 % with a small dim "wake me up" that moves every 45 s, or to 0 % with the hint off. Any touch wakes the screen at once. That touch is swallowed until release, so it never triggers a control or a swipe. Network and state updates keep running. Swipe up from the bottom edge goes home; swipe right in a sub-screen goes back to Einstellungen.
- Icons are Material Symbols Rounded (Google, Apache License 2.0), instanced at FILL 1 / wght 500 and converted with lv_font_conv to only the glyphs used (`main/font_icons_{16,24,32,38,80}.c`). Text uses Inter 4.0 (SIL OFL 1.1, `main/font_inter_*.c`, `main/font_clock_150.c`).
- Tap a category to open it. Swipe left or right to move between pages in that category. A swipe that starts on the right edge and moves left goes back to the home screen. A swipe that starts on the bottom edge and moves up also goes home.
- Schalter (formerly Sonstiges) is one of the home buttons. Its first tile is Relais, even when Loxone has no pushbuttons. Relais is not a Loxone control and does not send a Miniserver command. It toggles relay 1 on GPIO40. Boot writes the pin low, which is off, and the pin changes only when that tile is tapped. GPIO1 and GPIO2 are not configured.
- LoxAPP3.json has no live values. Opening a screen reads each light and shutter on it, first with `GET /jdev/sps/io/<uuidAction>/all` and, if that has no number, with `GET /jdev/sps/io/<state uuid>/state`. Until the read succeeds the tile stays unknown (`...`), not Aus. Coming back to a screen reads it again.
- A light tap sends `On` or `Off` from the last known value, via `GET /jdev/sps/io/<uuidAction>/<command>`. Storen Auf sends `FullUp` and Ab sends `FullDown`. Position 0 is fully open and 1 is fully closed.
- Authorization goes through `LoxoneAuthorizer` (`main/loxone_auth.h`). `LoxoneBasicAuthorizer` is what runs. `LoxoneTokenAuthorizer` is the seam for a later `getkey2` / `gettoken` flow. Set `LOXONE_AUTH` to `"token"` and it refuses to connect instead of sending the password. The client does not know which scheme `apply()` implements.
- Wi-Fi credentials from `panel_config.h` are the fallback. Only credentials chosen in Einstellungen and successfully connected are stored in NVS. Serial logs the host and the auth scheme name, not the user or the password.

The Miniserver client is HTTP only. Use the Miniserver's local HTTP port (usually 80). Weather is the one HTTPS call, to Open-Meteo.

## Configure

```sh
cp main/panel_config.example.h main/panel_config.h
```

Edit `main/panel_config.h`. That file is gitignored. `LOXONE_HOST` is a host or IP only, with no `http://` and no path.

```c
#define WIFI_SSID "your-ssid"
#define WIFI_PASSWORD "your-wifi-password"
#define LOXONE_HOST "192.168.1.77"
#define LOXONE_PORT 80
#define LOXONE_USER "your-user"
#define LOXONE_PASSWORD "your-password"
#define LOXONE_AUTH "basic"
```

Do not commit `main/panel_config.h`.

## Build and flash over USB (ESP-IDF)

The firmware is a native ESP-IDF project (v6.x, tested with v6.1): `main/` is the app component, `components/ArduinoJson` is the vendored header-only JSON library, and LVGL 8.4 comes from the Component Registry (`main/idf_component.yml`, pinned in `dependencies.lock`). No Arduino core is used.

The USB-C port is a CH340 serial adapter on GPIO43/GPIO44, so the board shows up as a normal USB serial port (`/dev/ttyUSB0` or `COMx`), not as the ESP32-S3 USB-Serial/JTAG device.

Windows PowerShell:

```powershell
. $HOME\esp\v6.1\esp-idf\export.ps1
idf.py build
idf.py -p COM14 flash monitor
```

Linux/macOS: `. ~/esp/esp-idf/export.sh`, then the same `idf.py` commands.

Configuration lives in `sdkconfig.defaults` (16 MB QIO flash, 8 MB octal PSRAM, 240 MHz, coredump to flash, watchdogs, LVGL). Do not edit the generated `sdkconfig`; after changing the defaults run `idf.py reconfigure` or delete `build/`. Project options are under "Loxone panel" in `idf.py menuconfig`.

### Test firmware

The serial test harness (`SHOT`, `TAP`, `SWIPE`, `STATE`, ...) is `CONFIG_PANEL_TEST_HARNESS`. `sdkconfig.test` turns it on and sets the console to 460800 baud (921600 drops bytes on the CH340):

```powershell
idf.py -B build-test -DSDKCONFIG="build-test/sdkconfig" -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.test" build
idf.py -B build-test -DSDKCONFIG="build-test/sdkconfig" -p COM14 flash
python tools/swipe_test.py --port COM14 --out shots
python tools/press_test.py --port COM14 --out shots
python tools/weather_test.py --port COM14 --out shots
python tools/settings_test.py --port COM14 --out shots
python tools/screen_test.py --port COM14 --out shots
python tools/v4_test.py --port COM14 --out shots
python tools/panel_test.py --port COM14 shot
```

Extra harness commands: `POS <name>` returns a widget centre (`cat:fav`, `cat:settings`, `set:wifi`, `wifi:N`, `loc:N`, ...), `LIST` dumps list items, `TYPE <text>` and `ENTER` drive the keyboard text area, and `LOCRESET` restores Buchs SG, `SCRTO <s>` sets the screen timeout for this boot only (0 = never; the test tools send `SCRTO 0` on connect), and `REBOOT` restarts the panel. STATE ends with `scr=on|off bl=<backlight %> to=<timeout s>`. `screen_test.py` covers the timeout, the hint, waking by tap and by swipe (nothing fires, no paging) and settings persistence across a reboot. At the end it restores 2 min with the hint on. `settings_test.py` never connects to another WLAN. It searches and selects a location, then restores Buchs SG.

Host unit test for the weather icon rules: `g++ -std=c++17 -I main tools/test_weather_derive.cpp main/weather_derive.cpp -o t && ./t`.

If flashing never starts, hold BOOT, start the flash, then release BOOT. A build with no `main/panel_config.h` still compiles and shows the not-configured screen.

Partitions (`partitions.csv`): NVS, PHY, a 4 MB factory app, and a 256 KB `coredump` partition (`idf.py coredump-info` after a crash).

### Tasks

- `net` on core 0: Wi-Fi, Miniserver HTTP (`esp_http_client`, basic auth), Open-Meteo (HTTPS, certificate bundle), JSON parsing (yields every 16 KB).
- `ui` on core 1: LVGL, GT911 touch (`i2c_master`), the RGB panel (`esp_lcd`), the test harness. It is the only task that touches LVGL; it sends one request at a time to `net` over a queue and gets the results back on another queue.

## Sources

- Material Symbols (Apache License 2.0): <https://github.com/google/material-design-icons>

- Guition product page for this model: <https://www.guition.com/esp32-display-module/openhasp-screen>
- Guition specification PDF (ST7701, one-way relay SKU, cover typo S043): <https://www.guition.com/icms/upload/fb081940d6fc11f09850077a33e1404f/file/productmanager-productfile/19de32eaebc34ca68869b7da70d56e0b/Directory/ESP32-4848S040%20Specifications-EN_1776827930404.pdf>
- Arduino_GFX device `ESP32_4848S040_86BOX_GUITION` and `st7701_type9_init_operations`: <https://github.com/moononournation/Arduino_GFX/blob/v1.6.0/examples/PDQgraphicstest/Arduino_GFX_dev_device.h> and <https://github.com/moononournation/Arduino_GFX/blob/v1.6.0/src/display/Arduino_RGB_Display.h>
- ha5dzs PlatformIO port (reference for the original PlatformIO build) (pin header, GT911 with INT/RST `-1`, relays, CH340, `qio_opi`): <https://github.com/ha5dzs/Guition-ESP32-4848S040-platformio>
- NorthernMan54 GPIO table: <https://github.com/NorthernMan54/ESP32-4848S040/blob/main/README.md>
- ESPHome device (ST7701S, GT911, relay GPIO40 / GPIO2 / GPIO1, backlight GPIO38): <https://devices.esphome.io/devices/guition-esp32-s3-4848s040/>
- HomeDing (GT911 address `0x5D`, same RGB bus, relay GPIO40): <https://homeding.github.io/boards/esp32s3/panel-4848S040.htm>

## UI "Dark Glass" (v4)

- Navy gradient background, glass cards (#1B2333 to #131926, 1 px #2A3448 border), Material Symbols weather glyphs. Home: date and time header, one weather card (now, then Heute and Morgen under a divider; tap a day for the day view) and five icon-only buttons.
- Bildschirm: timeout, plus Bildschirmschoner Schwarz (optionally with the dim "wake me up"), Digital (large clock and date, 40 % backlight, small drift every minute) or Analog (Swiss railway clock: white dial, black bar marks and hands, red second hand with a disc at its tip that sweeps in 58.5 s and rests at 12 until the minute hand jumps). The saver is its own LVGL screen. The analog clock invalidates only the old and new second-hand rectangles (about 12 fps) and the whole dial once a minute. Any touch wakes the panel and is swallowed.
- Uhrzeit: NTP server (ch.pool.ntp.org default, presets or your own) and time zone (Europe/Zurich `CET-1CEST,M3.5.0,M10.5.0/3` default), stored in NVS (`ntp_srv`, `tz_name`, `tz_posix`). SNTP uses `esp_netif_sntp` (`main/clock_sync.cpp`) and is restarted when the server changes.

## Kameras (Windy webcams)

Home button "Kameras" (videocam) shows one Windy webcam per page: swipe left/right to page, swipe up for home. The overlay has the title, place, capture age ("vor X Min"), the webcam page (windy.com/webcams/<id>) and "Webcams provided by windy.com" (Windy API terms). Pictures are refreshed every 2 min while visible. Per Windy's terms images are never upscaled: the free tier only delivers the 400x224 preview, so it is shown 1:1 and centred.

- Settings > Kameras: add by Windy ID, place search (Open-Meteo geocoding, then webcams within 25 km), delete. Only the IDs are stored in NVS; image URLs are tokenised and fetched fresh before every download, never stored.
- API key: Settings > Kameras > API-Key takes the user's own key (NVS, masked display, can be cleared). Without one the build default `CONFIG_PANEL_WINDY_API_KEY` is used. Put it in the gitignored `sdkconfig.defaults.local` (`CONFIG_PANEL_WINDY_API_KEY="..."`); CMakeLists.txt merges that file automatically. Free key: https://api.windy.com/keys (Webcams API).
- Test: `python tools/cam_test.py --port COM14 --out v5shots` (test firmware).
