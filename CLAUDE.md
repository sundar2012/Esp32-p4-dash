# CrowPanel ESP32-P4 Face Dashboard — Project Context

## Hardware: Elecrow CrowPanel Advanced 7" ESP32-P4 HMI AI Display

### Core SoC
- **ESP32-P4** (RISC-V dual-core @ 400MHz, configured at 360MHz)
- 32MB hex PSRAM (AP brand, 256Mbit, X16 mode) at 200MHz
- 16MB QIO flash at 80MHz
- Chip revision: v1.3, ECO2

### Display
- **1024x600 IPS** via **MIPI-DSI** (2-lane, 900 Mbps per lane)
- Panel controller: **EK79007** (NOT ILI9881C)
- Pixel clock: **51 MHz**
- DPI timing: HBP=160, HSW=70, HFP=160, VBP=23, VSW=10, VFP=12
- RGB565 color format, double-buffered with DMA2D
- Requires `espressif/esp_lcd_ek79007` component (sends vendor init commands 0x80-0x86 + sleep out via DBI before DPI video starts)
- Without EK79007 driver init commands, the panel stays in sleep mode = blank screen

### Backlight
- **GPIO 31** — LEDC PWM at 30kHz, 11-bit duty resolution
- Active high (BSP_LCD_BACKLIGHT_ON_LEVEL = 1)

### Power (LDO)
- LDO channel 4 → 3300mV (peripherals, display, touch)
- LDO channel 3 → 2500mV (MIPI DSI PHY + camera)

### I2C Bus
- **SCL: GPIO 46, SDA: GPIO 45**
- 400kHz, I2C_NUM_0
- Used by GT911 touch controller

### Touch
- **Goodix GT911** capacitive touch controller
- I2C address: 0x5D
- **RST: GPIO 40, INT: GPIO 42**

### Camera
- 2MP MIPI-CSI camera (640x480 @ 15fps)
- Not yet physically connected by the user

### Wi-Fi Co-processor
- **ESP32-C6** connected via UART (TX=24, RX=25, 115200 baud)
- Requires AT firmware on the C6 (not yet loaded — C6 not responding to AT commands)
- Wi-Fi 6 capable

### USB Serial (for flashing from macOS)
- **CH340** USB-to-serial chip
- Requires CH340 driver from wch-ic.com
- Port: `/dev/cu.wchusbserial210`

## Software Stack

### Build System
- **ESP-IDF v5.4.2** (RISC-V toolchain for ESP32-P4)
- CMake + Ninja
- Toolchain path: `~/esp/esp-idf/export.sh`

### Critical sdkconfig Settings (learned the hard way)
```
# In sdkconfig.defaults (generic):
CONFIG_IDF_EXPERIMENTAL_FEATURES=y    # REQUIRED to unlock 200MHz PSRAM
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y

# In sdkconfig.defaults.esp32p4 (target-specific — MUST be here, not in generic):
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_HEX=y
CONFIG_SPIRAM_SPEED_200M=y            # Without this, PSRAM defaults to 20MHz → DPI underrun
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=360
CONFIG_CACHE_L2_CACHE_128KB=y
CONFIG_CACHE_L2_CACHE_LINE_64B=y
CONFIG_MMU_PAGE_SIZE_64KB=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_SPIRAM_BOOT_INIT=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384
CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768
CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y
```

**Key lesson**: ESP32-P4-specific Kconfig symbols (SPIRAM speed, mode) MUST go in `sdkconfig.defaults.esp32p4`, not the generic `sdkconfig.defaults`. The generic file is parsed before the target is set, so target-specific symbols are silently ignored.

### Partition Table (partitions.csv)
- nvs: 0x9000, 24KB
- phy_init: 0xF000, 4KB
- factory: 0x10000, **12MB** (large app)
- face_db: NVS partition, 1MB (face embeddings)
- storage: SPIFFS, 2MB

### Component Architecture
```
peripheral/
  bsp_display/    — MIPI-DSI + EK79007 + LVGL init (esp_lcd_ek79007 component)
  bsp_i2c/        — I2C master bus
  bsp_extra/      — LDO power + backlight PWM
  bsp_camera/     — MIPI-CSI camera (stub, buffers in PSRAM)
  bsp_uart/       — UART to ESP32-C6

main/
  main.c          — Boot sequence
  app_face.c      — Face recognition (stub — ESP-WHO not yet integrated)
  app_dashboard.c — Screen manager (splash, screensaver, guest, sundar, user2, enrollment, settings)
  app_wifi.c      — Wi-Fi via C6 AT commands
  app_calendar.c  — HTTP calendar fetch (CalDAV/Google Calendar)
  app_shortcuts.c — HTTP triggers for BTT + macOS Shortcuts
  ui_sundar.c     — Sundar's dashboard (calendar + 4x3 Stream Deck grid)
  ui_guest.c      — Guest dashboard
  ui_user2.c      — User2 dashboard
```

### LVGL Configuration
- LVGL v9.2 (from component registry: `lvgl/lvgl ~9.2.0`)
- Color depth: 16-bit (RGB565)
- DPI: 130
- Fonts: Montserrat 14, 16, 20, 24, 28, 32
- Draw buffers: 1024 x 50 x 2 bytes = 100KB each, double-buffered, in PSRAM
- Render mode: partial
- LVGL task pinned to CPU core 0, priority 5
- Tick: 5ms via esp_timer

## User's Use Case

### Owner
- **Sundar** — primary user

### Goal
A face-recognizing smart display that shows a personalized LVGL dashboard per recognized user:

1. **Sundar's dashboard**:
   - Calendar widget (color-coded events fetched via HTTP from CalDAV/Google Calendar)
   - 4x3 Stream Deck-style button grid that sends HTTP requests to trigger:
     - **BetterTouchTool** actions (webserver API on port 12345)
     - **macOS Shortcuts** via a Python bridge server (port 8765)

2. **Guest dashboard**: Generic info display for unrecognized users

3. **Face recognition flow**: Camera → detect face → match embedding → route to user's dashboard

### macOS Integration
- `mac_bridge/shortcuts_bridge.py` — Python HTTP server that triggers macOS Shortcuts
- `mac_bridge/com.crowpanel.shortcuts-bridge.plist` — launchd plist for auto-start
- Mac IP: 192.168.1.50
- BTT port: 12345, Shortcuts bridge port: 8765

## Known Issues / TODO

1. **ESP-WHO face recognition not integrated** — `face_detect_and_recognize()` is a stub that always returns `face_detected = false`. Need to integrate ESP-WHO models (MTMN/blazeface for detection, face recognition for 512-dim embeddings).
2. **Wi-Fi not working** — ESP32-C6 co-processor doesn't respond to AT commands. Likely needs AT firmware flashed to the C6.
3. **Touch not yet tested** — GT911 driver read callback is a stub (always returns released). GPIO pins are now correct (RST=40, INT=42, SCL=46, SDA=45).
4. **Camera not connected** — User hasn't plugged in the MIPI-CSI camera module yet.
5. **Clock shows wrong time** — No NTP sync since Wi-Fi isn't connected.

## Elecrow Official References
- GitHub: https://github.com/Elecrow-RD/CrowPanel-Advanced-7inch-ESP32-P4-HMI-AI-Display-1024x600-IPS-Touch-Screen
- Wiki: https://www.elecrow.com/wiki/CrowPanel_Advanced_7inch_ESP32-P4_HMI_AI_Display
- The official code uses `espressif__esp_lcd_ek79007` component with default init commands
- Their BSP file is `bsp_illuminate.c` / `bsp_illuminate.h`

## Build & Flash Commands
```bash
source ~/esp/esp-idf/export.sh
cd ~/esp32/esp32-p4-dash          # Check actual path on user's Mac
rm -rf build managed_components   # Only needed for clean build / new components
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/cu.wchusbserial210 flash monitor
```
