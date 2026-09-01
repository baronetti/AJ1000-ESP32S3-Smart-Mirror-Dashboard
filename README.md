# 🪞 Philips AJ1000 ESP32-S3 Smart Mirror Dashboard

![Release](https://img.shields.io/badge/version-0.1.0-blue.svg)
![Microcontroller](https://img.shields.io/badge/ESP32--S3-Zero-red.svg)
![Integration](https://img.shields.io/badge/Home%20Assistant-MQTT-green.svg)
![License](https://img.shields.io/badge/license-MIT-brightgreen.svg)

A complete hardware overhaul of the vintage **Philips AJ1000/12** alarm clock, transforming it into a high-density **ESP32-S3-powered Smart Mirror Home Assistant Dashboard**.

It uses a 2.7" SSD1363 OLED display (256x128 resolution) mounted behind the original one-way mirror front panel, featuring adaptive brightness control, NTP synchronization, custom HA weather graphics, and smooth digit transition animations.

---

## 📸 Preview & Current State (v0.1.0)

* **Main Clock View:** High-density 92px vertical font for time (`HH:MM`).
* **Header Bar:** Italian date abbrev, Home Assistant temperature (`°C`), and vector weather icons matching HA weather states.
* **Smart Dimming:** Hardware VEML7700 light sensor with low-pass hysteresis filtering (~0.8s reaction) for glass penetration.
* **Fluid Digit Transitions:** Day-by-day dynamic animations (Vertical Slide, Glitch/Cyber Shift, 3D Split-Flip, Dust Dissolve/Atomization) with fixed slot positions to prevent screen jitter.

---

## ⚡ Hardware Specs & Components

| Component | Model / Specs |
| :--- | :--- |
| **Enclosure** | Stock Philips AJ1000/12 Shell |
| **Microcontroller** | ESP32-S3-Zero |
| **Display** | 2.7" SSD1363 OLED (256x128, 7-pin SPI) |
| **Light Sensor** | VEML7700 (I2C) |
| **Connectivity** | Wi-Fi + Local MQTT (Home Assistant) |

---

## 🔌 Wiring Diagram

> **Note:** Initial prototype scheme (button pins omitted in v0.1.0).

![Wiring Diagram](assets/wiring_diagram_noButtons.png)

### Pinout Mapping
* **OLED Display (SPI):** `CLK -> GPIO12` | `MOSI -> GPIO11` | `CS -> GPIO10` | `DC -> GPIO8` | `RST -> GPIO9` | `VCC -> 5V`
* **VEML7700 Sensor (I2C):** `SCL -> GPIO2` | `SDA -> GPIO1` | `VIN -> 3V3`

---

## 🗺️ Project Roadmap & Dev Log

### 🛠️ In Progress / Planned (Target: v1.0.0 Hardware Integration)
- [ ] **Navigation:** Physical buttons with water-ripple transition animations (Left/Right swipe).
- [ ] **Smart Sleep & Wake Rules:**
  - Auto-sleep after 22:00 if room is dark for > 7 minutes or on button press.
  - Auto-wake on light detection (>2 min), window opened event (HA), or button press.
- [ ] **No Distraction Mode:** 5-hour DND triggered by long-pressing both buttons, with custom corner UI accents.
- [ ] **MQTT Offline Fallback:** Graceful fallback to standalone NTP clock display when Home Assistant is unreachable.
- [ ] **HA Interactive Screens:**
  - Detailed Weather forecast screen.
  - "Now Playing" media control view.
  - Home Statistics Gauges (Power consumption, Temp/Humidity, Internet speed).
  - Mail & Calendar summary.
  - Intercom & Doorbell notifications.
  - DSC Alarm panel status.
  - Alexa request visual sync.
- [ ] **OTA Updates:** Web-based / HA-triggered over-the-air firmware updates.

---

## 🚀 Installation & Build

1. Clone this repository.
2. Open `AJ1000-SmartMirror-Dashboard.ino` in Arduino IDE.
3. Install dependencies via Library Manager:
   * `U8g2` by Oliver Kraus
   * `Adafruit_VEML7700`
   * `PubSubClient` by Nick O'Leary
4. Edit Wi-Fi and MQTT placeholders in the main file:
   ```cpp
   const char* WIFI_SSID     = "YOUR_WIFI_SSID";
   const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
   const char* MQTT_SERVER   = "192.168.1.XXX";
   const char* MQTT_USER     = "YOUR_MQTT_USER";
   const char* MQTT_PASS     = "YOUR_MQTT_PASSWORD";