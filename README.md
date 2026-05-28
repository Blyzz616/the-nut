# the nut

A smart poker card guard with a round touch display, blind timer, game timer, animated screens, and battery indicator — built on an ESP32-C6 in a 3D-printed enclosure.

---

## The Idea

Keep your cards protected and stay on top of the game. **the nut** sits on your hole cards and shows you what matters: how long until the blinds go up, how long you've been at the table, sick animations — all on a circular IPS touch display, in a puck-sized enclosure.

---

## Hardware

### Microcontroller

[Seeed Studio XIAO ESP32-C6](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32C6-p-5884.html)

Thumb-sized ESP32-C6 with Wi-Fi 6, BLE 5.0, Zigbee, and Thread. Onboard USB-C charging circuit handles the LiPo directly — no extra charging module needed.

- **MCU**: ESP32-C6 (RISC-V single-core, 160MHz)
- **Flash**: 4MB
- **Wireless**: Wi-Fi 6 · BLE 5.0 · Zigbee · Thread
- **Charging**: USB-C, onboard TP4054 IC
- **Size**: 21 × 17.5mm

### Display

[EC Buying 1.28" Round Touch TFT Module](https://www.amazon.ca/dp/B0DLN33VQ5)

1.28-inch circular IPS display with capacitive touch. SPI display interface, I2C touch interface. No SD card slot — animations are procedural or cached to LittleFS.

- **Display driver**: GC9A01 (SPI)
- **Touch driver**: CST816D (I2C)
- **Resolution**: 240 × 240
- **Colors**: 65K RGB
- **Display area**: Ø32.4mm
- **Working voltage**: 3.3V / 5V
- **Module size**: 42.1 × 43 × 11.5mm
- **Interface**: MX1.25-8P (display) + MX1.25-4P (touch)

### Battery

[1100mAh 3.7V LiPo](https://www.amazon.ca/dp/B093G9K1D6)

Soldered directly to the `BAT+` and `GND` pads on the underside of the XIAO. Charges via USB-C through the onboard IC — no unplugging required.

- **Capacity**: 1100mAh
- **Voltage**: 3.7V nominal
- **Connection**: Direct solder to XIAO BAT pads

---

## Wiring

| Display pin | XIAO ESP32-C6 | Notes |
|-------------|---------------|-------|
| VIN | 3V3 | Regulated 3.3V from onboard LDO |
| GND | GND | |
| SCL | GPIO6 | SPI clock |
| SDA | GPIO7 | SPI MOSI |
| RES | GPIO2 | Display reset |
| DC | GPIO3 | Data/command select |
| CS | GPIO20 | Chip select |
| BLK | GPIO21 | Backlight (or tie to 3V3 for always-on) |
| TP_SCL | GPIO22 | Touch I2C clock |
| TP_SDA | GPIO23 | Touch I2C data |
| TP_RST | GPIO1 | Touch reset |
| TP_INT | GPIO0 | Touch interrupt |

> **Note:** Battery connects to BAT+ and GND pads on the underside of the XIAO — not the 3V3 pin. Use the 3V3 pin only for powering the display.

---

## Display Screens

All timers run continuously in the background regardless of which screen is active. Swipe or double-tap to move between screens.

| Screen | Description |
|--------|-------------|
| **Blinds timer** | Large countdown arc showing time until next blind level, current and next level |
| **Game timer** | Total session elapsed time, start time, estimated end |
| **Animated screen** | Looping procedural animation — cards, chips, flames, lava-lamp, matrix rain, etc |
| **Session stats** | Hands per hour, time at table, break reminders |
| **Clock** | Large watch-face style clock, NTP synced via Wi-Fi |
| **Battery** | Circular arc gauge, percentage, voltage, estimated hours remaining |

A permanent thin battery arc is drawn at the outer edge of the display on every screen, cycling green → yellow → red.

---

## Touch Gestures

| Gesture | Action |
|---------|--------|
| Single tap | Interact / confirm |
| Double tap | Next screen |
| Swipe | Previous / next screen |
| Long press | In-app menu / settings |
| 7-second hold on boot | Start reboot |
| Hold screen during boot | Enter bootloader mode (for firmware flashing) |

---

## Boot Sequence

If the screen is being touched, reboot into boot-loader for updating firmware.

If normal power up - no touch, the device tries each configured Wi-Fi network in order, stopping as soon as one connects:

```cpp
const char* networks[][2] = {
  {"HomeSSID",     "password1"},
  {"PhoneHotspot", "password2"},
  {"VenueWifi",    "password3"},
};
```

Once connected:
- NTP time sync
- Fetch updated config JSON (blind schedule, settings) from configured URL
- Fall back to locally cached LittleFS config if offline

---

## Firmware Update (Bootloader Mode)

Hold finger on screen at power-on. The backlight will blink 3 times to confirm, then the device enters bootloader mode. Flash normally via Arduino IDE or `esptool.py` over USB-C.

---

## Software Stack

- **[LVGL](https://lvgl.io)** — UI framework, arc widgets, animation engine, touch input handling
- **[SquareLine Studio](https://squareline.io)** — drag-and-drop UI layout, exports LVGL C code
- **LittleFS** — local filesystem for config and cached assets
- **ArduinoOTA** — over-the-air firmware updates over Wi-Fi
- **NTP** — time sync on connect

---

## Enclosure

3D printed. Target dimensions: ~50mm diameter, ~15–18mm tall. USB-C port flush to the side wall.

**Interior stack (bottom to top):**
1. Battery
2. XIAO ESP32-C6
3. Display module (face up, flush with top surface)

Material: Black PLA. Snap-fit or M2 screws for the lid.

---

## Expected Investment

| Item | Cost (CAD) |
|------|-----------|
| [XIAO ESP32-C6](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32C6-p-5884.html) | ~$8 |
| [1.28" Round Touch Display](https://www.amazon.ca/dp/B0DLN33VQ5) | ~$38 |
| [1100mAh LiPo Battery](https://www.amazon.ca/dp/B093G9K1D6) | ~$12 |
| 3D printed enclosure (filament + iterations) | ~$2 |
| Miscellaneous (wire, solder, connectors) | ~$2 |
| **Total** | **~$62** |

---

## Project Status

- [x] Hardware selected
- [x] Wiring defined
- [x] Boot sequence designed
- [x] Screen layout planned
- [ ] Enclosure design
- [ ] LVGL screen implementation
- [ ] Wi-Fi / NTP / OTA
- [ ] Blind schedule config format
- [ ] Animation screens
- [ ] Final assembly
