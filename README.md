# Loxone panel for Guition ESP32-4848S040C_I_Y_1

Firmware for the Guition 4.0 inch 480×480 capacitive wall panel. It draws a Loxone home screen and talks to a local Miniserver over HTTP. It does not drive the onboard relay.

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

Guition does not define the letters in `C_I_Y_1` one by one. The specification only maps `_Y_1` to the one-way relay and `_Y_3` to the three-way relay. The Chinese retail line "带外壳1位输入" (enclosure, one input) is the same one-channel unit. This firmware leaves that channel alone.

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
- Display rotation is `1`, copied from the Arduino_GFX device entry. Touch rotation is `ROTATION_RIGHT`, which is the ha5dzs mapping for that display rotation. If taps land in the wrong place, change that pair. ESPHome notes that a 270° rotation is used when the USB socket faces down; that was not applied here.
- Pixel byte order follows Arduino_GFX `useBigEndian = false` and `LV_COLOR_16_SWAP 0`. If red and blue are swapped on the glass, flip `LV_COLOR_16_SWAP` in `include/lv_conf.h`.

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
| Relay 1 | 40 | This SKU. **Not configured, not driven** |
| Relay 2, relay 3 | 2, 1 | Three-way SKU only. **Not configured, not driven** |

RGB timings copied from the Arduino_GFX device: PCLK 12 MHz, hsync polarity 1, front porch 10, pulse 8, back porch 50, vsync polarity 1, front porch 10, pulse 8, back porch 20. The ST7701 init sequence is `st7701_type9_init_operations` from that library ("480x480 square (86 Box) GUITION ESP32-4848S040").

## What the firmware does

PlatformIO project, Arduino-ESP32, the published Arduino_GFX 1.6.0 device for this panel, LVGL 8.4, and TAMC_GT911 1.0.2. That is the same display stack as the Arduino_GFX device entry and the ha5dzs PlatformIO port.

The screen is 480×480. Light tiles are 222×140 px. A shutter uses the full width of one row, with Auf and Ab buttons at 210×82. Page buttons are 140×72, and the retry button is 400×88.

Room names, control names, and the Miniserver name are drawn with DejaVu Sans covering Latin-1 (U+00A0–U+00FF), including ä, ö, ü, Ä, Ö, Ü, and ß.

- With no `include/panel_config.h`, or with the Wi-Fi SSID, Miniserver host, or user left empty, the home screen is a **Not configured** panel. It tells you which file to copy. It does not pretend the house is online.
- With a config file, it joins Wi-Fi and GETs `/data/LoxAPP3.json` with HTTP basic auth.
- The first screen is only controls marked `isFavorite` in that file. Weiter then shows the rest, one group per screen: Licht (Switch, TimedSwitch, LightController, Dimmer), Storen (Jalousie), then Sonstiges (pushbuttons). Up to 48 controls. Favorites are not repeated in the later groups.
- LoxAPP3.json has no live values. Opening a screen reads each light and shutter on it, first with `GET /jdev/sps/io/<uuidAction>/all` and, if that has no number, with `GET /jdev/sps/io/<state uuid>/state`. Until the read succeeds the tile stays unknown (`...`), not Aus. Coming back to a screen reads it again.
- A light tap sends `On` or `Off` from the last known value, via `GET /jdev/sps/io/<uuidAction>/<command>`. Storen Auf sends `FullUp` and Ab sends `FullDown`. Position 0 is fully open and 1 is fully closed.
- Authorization goes through `LoxoneAuthorizer` (`src/loxone_auth.h`). `LoxoneBasicAuthorizer` is what runs. `LoxoneTokenAuthorizer` is the seam for a later `getkey2` / `gettoken` flow. Set `LOXONE_AUTH` to `"token"` and it refuses to connect instead of sending the password. The client does not know which scheme `apply()` implements.
- Wi-Fi passwords are not written to NVS (`WiFi.persistent(false)`). Serial logs the host and the auth scheme name, not the user or the password.

HTTPS is not implemented. Use the Miniserver's local HTTP port (usually 80).

## Configure

```sh
cp include/panel_config.example.h include/panel_config.h
```

Edit `include/panel_config.h`. That file is gitignored. `LOXONE_HOST` is a host or IP only, with no `http://` and no path.

```c
#define WIFI_SSID "your-ssid"
#define WIFI_PASSWORD "your-wifi-password"
#define LOXONE_HOST "192.168.1.77"
#define LOXONE_PORT 80
#define LOXONE_USER "your-user"
#define LOXONE_PASSWORD "your-password"
#define LOXONE_AUTH "basic"
```

Do not commit `include/panel_config.h`.

## Build and flash over USB

Install [PlatformIO](https://platformio.org/). The USB-C port is a CH340 serial adapter on GPIO43/GPIO44, so the board shows up as a normal USB serial port (`/dev/ttyUSB0`, `/dev/ttyACM0`, or `COMx`), not as the ESP32-S3 USB-Serial/JTAG device.

```sh
pio run -e esp32-4848s040c
pio run -e esp32-4848s040c -t upload
pio device monitor -e esp32-4848s040c
```

Upload speed is 460800. Auto-reset uses the CH340 RTS/DTR lines, which published notes for this board say are wired to EN and GPIO0. If upload never starts, hold BOOT, start the upload, then release BOOT.

A build with no `panel_config.h` still compiles. That binary shows the not-configured screen.

The factory app partition is 4 MB on the 16 MB flash (`partitions.csv`). Flash is QIO and PSRAM is octal (`qio_opi`), which is the ESP32-S3R8 memory type used by the published PlatformIO port.

## Sources

- Guition product page for this model: <https://www.guition.com/esp32-display-module/openhasp-screen>
- Guition specification PDF (ST7701, one-way relay SKU, cover typo S043): <https://www.guition.com/icms/upload/fb081940d6fc11f09850077a33e1404f/file/productmanager-productfile/19de32eaebc34ca68869b7da70d56e0b/Directory/ESP32-4848S040%20Specifications-EN_1776827930404.pdf>
- Arduino_GFX device `ESP32_4848S040_86BOX_GUITION` and `st7701_type9_init_operations`: <https://github.com/moononournation/Arduino_GFX/blob/v1.6.0/examples/PDQgraphicstest/Arduino_GFX_dev_device.h> and <https://github.com/moononournation/Arduino_GFX/blob/v1.6.0/src/display/Arduino_RGB_Display.h>
- ha5dzs PlatformIO port (pin header, GT911 with INT/RST `-1`, relays, CH340, `qio_opi`): <https://github.com/ha5dzs/Guition-ESP32-4848S040-platformio>
- NorthernMan54 GPIO table: <https://github.com/NorthernMan54/ESP32-4848S040/blob/main/README.md>
- ESPHome device (ST7701S, GT911, relay GPIO40 / GPIO2 / GPIO1, backlight GPIO38): <https://devices.esphome.io/devices/guition-esp32-s3-4848s040/>
- HomeDing (GT911 address `0x5D`, same RGB bus, relay GPIO40): <https://homeding.github.io/boards/esp32s3/panel-4848S040.htm>
