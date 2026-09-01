/*
 * PHILIPS AJ1000 ESP32S3 SMART MIRROR DASHBOARD
 * Author: baronetti
 *
 * INSTRUCTIONS:
 * 1. Navigate to the "NETWORK CONFIGURATION" section below.
 * 2. Replace WIFI_SSID and WIFI_PASSWORD with your network credentials.
 * 3. Set MQTT_SERVER to your Home Assistant IP address.
 * 4. Fill in MQTT_USER and MQTT_PASS with your broker credentials.
 * 5. If needed, modify the MQTT_TOPIC_* variables to match your sensors.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>
#include <Wire.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <Adafruit_VEML7700.h>
#include <ArduinoOTA.h>

// --- NETWORK CONFIGURATION ---
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASS";
const char* MQTT_SERVER   = "192.168.X.XX";
const int   MQTT_PORT     = 1883;
const char* MQTT_USER     = "YOUR_MQTT_USER";
const char* MQTT_PASS     = "YOUR_MQTT_PASS";

const char* MQTT_TOPIC_STATUS      = "mirror/status";
const char* MQTT_TOPIC_TEMPERATURE = "mirror/temperature";
const char* MQTT_TOPIC_WEATHER     = "mirror/weather";

// --- HARDWARE PINOUT (ESP32-S3-Zero) ---
#define OLED_SCLK 12
#define OLED_MOSI 11
#define OLED_CS   10
#define OLED_DC    8
#define OLED_RST   9
#define I2C_SDA    1
#define I2C_SCL    2

// --- HARDWARE INITIALIZATION ---
U8G2_SSD1363_256X128_F_4W_HW_SPI u8g2(U8G2_R0, OLED_CS, OLED_DC, OLED_RST);
Adafruit_VEML7700 veml = Adafruit_VEML7700();
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// --- SYSTEM STATE ---
String rawTemp = "--";
String weatherState = "clear-night";
char prevTimeStr[6] = "";

float smoothLux = 0.0;
float currentContrast = 90.0;
float targetContrast = 90.0;
unsigned long lastLuxRead = 0;
unsigned long lastMqttRetry = 0;

const char* days[]   = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};


// --- WEATHER GRAPHICS FUNCTIONS ---

void drawSunShape(int sx, int sy) {
  u8g2.setDrawColor(1);
  u8g2.drawCircle(sx, sy, 2, U8G2_DRAW_ALL);
  u8g2.drawLine(sx, sy - 5, sx, sy - 3);
  u8g2.drawLine(sx, sy + 3, sx, sy + 5);
  u8g2.drawLine(sx - 5, sy, sx - 3, sy);
  u8g2.drawLine(sx + 3, sy, sx + 5, sy);
  u8g2.drawLine(sx + 3, sy - 3, sx + 2, sy - 2);
  u8g2.drawLine(sx - 3, sy - 3, sx - 2, sy - 2);
  u8g2.drawLine(sx + 3, sy + 3, sx + 2, sy + 2);
  u8g2.drawLine(sx - 3, sy + 3, sx - 2, sy + 2);
}

void drawMoonShape(int mx, int my) {
  u8g2.setDrawColor(1);
  u8g2.drawDisc(mx, my, 6, U8G2_DRAW_ALL);
  u8g2.setDrawColor(0);
  u8g2.drawDisc(mx + 4, my - 3, 6, U8G2_DRAW_ALL);
  u8g2.setDrawColor(1);
}

void drawCloudShape(int cx, int cy) {
  u8g2.setDrawColor(1);
  u8g2.drawDisc(cx + 3, cy + 4, 3, U8G2_DRAW_ALL);
  u8g2.drawDisc(cx + 7, cy + 2, 4, U8G2_DRAW_ALL);
  u8g2.drawDisc(cx + 11, cy + 4, 3, U8G2_DRAW_ALL);
  u8g2.drawBox(cx + 2, cy + 4, 11, 4);
}

void eraseCloudMask(int cx, int cy) {
  u8g2.setDrawColor(0);
  u8g2.drawDisc(cx + 3, cy + 4, 4, U8G2_DRAW_ALL);
  u8g2.drawDisc(cx + 7, cy + 2, 5, U8G2_DRAW_ALL);
  u8g2.drawDisc(cx + 11, cy + 4, 4, U8G2_DRAW_ALL);
  u8g2.drawBox(cx + 1, cy + 4, 13, 5);
  u8g2.setDrawColor(1);
}

void drawAsterisk(int ax, int ay) {
  u8g2.setDrawColor(1);
  u8g2.drawLine(ax - 2, ay, ax + 2, ay);
  u8g2.drawLine(ax, ay - 2, ax, ay + 2);
  u8g2.drawLine(ax - 1, ay - 1, ax + 1, ay + 1);
  u8g2.drawLine(ax - 1, ay + 1, ax + 1, ay - 1);
}

void drawWindLines(int wx, int wy) {
  u8g2.setDrawColor(1);
  u8g2.drawLine(wx, wy + 2, wx + 9, wy + 2);
  u8g2.drawLine(wx + 9, wy + 2, wx + 11, wy + 1);
  u8g2.drawLine(wx + 11, wy + 1, wx + 11, wy);
  u8g2.drawLine(wx + 11, wy, wx + 10, wy);
  u8g2.drawLine(wx + 2, wy + 7, wx + 12, wy + 7);
  u8g2.drawLine(wx + 12, wy + 7, wx + 14, wy + 8);
  u8g2.drawLine(wx + 14, wy + 8, wx + 14, wy + 9);
  u8g2.drawLine(wx + 14, wy + 9, wx + 12, wy + 9);
  u8g2.drawLine(wx + 1, wy + 12, wx + 7, wy + 12);
  u8g2.drawLine(wx + 7, wy + 12, wx + 9, wy + 11);
}

void drawLightningBolt(int lx, int ly) {
  u8g2.setDrawColor(1);
  u8g2.drawLine(lx + 2, ly, lx, ly + 4);
  u8g2.drawLine(lx, ly + 4, lx + 3, ly + 4);
  u8g2.drawLine(lx + 3, ly + 4, lx + 1, ly + 8);
}

void drawWeatherIcon(int x, int y, String state) {
  int bx = x;
  int by = y - 14; 

  if (state == "sunny") drawSunShape(bx + 8, by + 8);
  else if (state == "clear-night") drawMoonShape(bx + 8, by + 8);
  else if (state == "cloudy") drawCloudShape(bx + 1, by + 4);
  else if (state == "partlycloudy-day") { drawSunShape(bx + 11, by + 4); eraseCloudMask(bx, by + 5); drawCloudShape(bx, by + 5); }
  else if (state == "partlycloudy-night") { drawMoonShape(bx + 5, by + 4); eraseCloudMask(bx, by + 5); drawCloudShape(bx, by + 5); }
  else if (state == "fog") {
    u8g2.setDrawColor(1);
    u8g2.drawLine(bx, by + 3, bx + 15, by + 3);
    u8g2.drawLine(bx + 2, by + 7, bx + 13, by + 7);
    u8g2.drawLine(bx + 1, by + 11, bx + 16, by + 11);
  }
  else if (state == "rainy") { drawCloudShape(bx + 1, by + 1); u8g2.drawLine(bx + 3, by + 10, bx + 2, by + 13); u8g2.drawLine(bx + 8, by + 10, bx + 7, by + 13); u8g2.drawLine(bx + 13, by + 10, bx + 12, by + 13); }
  else if (state == "pouring") { drawCloudShape(bx + 1, by + 1); u8g2.drawLine(bx + 3, by + 9, bx + 1, by + 14); u8g2.drawLine(bx + 7, by + 9, bx + 5, by + 14); u8g2.drawLine(bx + 11, by + 9, bx + 9, by + 14); u8g2.drawLine(bx + 14, by + 9, bx + 12, by + 14); }
  else if (state == "hail") { drawCloudShape(bx + 1, by + 1); u8g2.drawDisc(bx + 4, by + 11, 1, U8G2_DRAW_ALL); u8g2.drawDisc(bx + 8, by + 13, 1, U8G2_DRAW_ALL); u8g2.drawDisc(bx + 12, by + 10, 1, U8G2_DRAW_ALL); }
  else if (state == "lightning") { drawCloudShape(bx + 1, by + 1); drawLightningBolt(bx + 6, by + 8); }
  else if (state == "lightning-rainy" || state == "exceptional") { drawCloudShape(bx + 1, by + 1); u8g2.drawLine(bx + 3, by + 10, bx + 2, by + 13); drawLightningBolt(bx + 8, by + 8); }
  else if (state == "snowy") { drawCloudShape(bx + 1, by + 1); drawAsterisk(bx + 4, by + 11); drawAsterisk(bx + 12, by + 11); }
  else if (state == "snowy-rainy") { drawCloudShape(bx + 1, by + 1); u8g2.drawLine(bx + 3, by + 10, bx + 2, by + 13); drawAsterisk(bx + 11, by + 11); }
  else if (state == "windy" || state == "windy-variant") drawWindLines(bx, by + 1);
  else drawSunShape(bx + 8, by + 8);
}

void drawFormattedTemperature(int x, int y, String tStr) {
  u8g2.setFont(u8g2_font_helvB10_tr);
  String numOnly = "";
  for (size_t i = 0; i < tStr.length(); i++) {
    if (isDigit(tStr[i]) || tStr[i] == '-' || tStr[i] == '.') numOnly += tStr[i];
  }
  if (numOnly == "") numOnly = "--";

  u8g2.setCursor(x, y);
  u8g2.print(numOnly);

  int strW = u8g2.getStrWidth(numOnly.c_str());
  int circleX = x + strW + 2;
  int circleY = y - 8;
  u8g2.drawCircle(circleX, circleY, 1, U8G2_DRAW_ALL);
  u8g2.setCursor(circleX + 4, y);
  u8g2.print("C");
}

void drawTopBar(const char* dateBuff) {
  u8g2.setFont(u8g2_font_helvB10_tr);
  u8g2.drawStr(5, 16, dateBuff);
  drawWeatherIcon(185, 18, weatherState);
  drawFormattedTemperature(205, 16, rawTemp);
}


// --- FIXED CLOCK SPACING (Anti-Jitter) ---
const int SLOT_W[5] = {52, 52, 22, 52, 52}; 

int getSlotX(int index) {
  int x = 13;
  for (int i = 0; i < index; i++) x += SLOT_W[i];
  return x;
}

int getSlotWidth(int index) {
  return SLOT_W[index];
}

int getCharDrawX(int index, char c) {
  u8g2.setFont(u8g2_font_logisoso92_tn);
  char buf[2] = {c, '\0'};
  int charW = u8g2.getStrWidth(buf);
  return getSlotX(index) + (SLOT_W[index] - charW) / 2;
}


// --- PIXEL BRIGHTNESS ENGINE (DITHERING GRAYSCALE) ---
void drawDigitWithBrightness(int slotIdx, char c, int level) {
  if (level <= 0) return;
  
  int drawX = getCharDrawX(slotIdx, c);
  char str[2] = {c, '\0'};
  
  u8g2.setDrawColor(1);
  u8g2.drawStr(drawX, 124, str);

  if (level >= 4) return; 

  int slotX = getSlotX(slotIdx);
  int slotW = getSlotWidth(slotIdx);

  u8g2.setDrawColor(0);
  if (level == 3) {
    for (int py = 24; py < 126; py += 2) {
      for (int px = slotX; px < slotX + slotW; px += 2) {
        u8g2.drawPixel(px, py);
      }
    }
  } else if (level == 2) {
    for (int py = 22; py < 126; py++) {
      int startX = slotX + (py % 2);
      for (int px = startX; px < slotX + slotW; px += 2) {
        u8g2.drawPixel(px, py);
      }
    }
  } else if (level == 1) {
    for (int py = 22; py < 126; py++) {
      for (int px = slotX; px < slotX + slotW; px++) {
        if (!((px % 2 == 0) && (py % 2 == 0))) {
          u8g2.drawPixel(px, py);
        }
      }
    }
  }
  u8g2.setDrawColor(1);
}


// --- DIGIT TRANSITIONS ---

void animateVerticalSlide(const char* oldStr, const char* newStr, const char* dateBuff) {
  const int STEPS = 12;
  const int FONT_HEIGHT = 92;

  for (int step = 0; step <= STEPS; step++) {
    float t = (float)step / STEPS;
    int dy = (int)(t * FONT_HEIGHT);

    u8g2.clearBuffer();
    drawTopBar(dateBuff);
    u8g2.setFont(u8g2_font_logisoso92_tn);

    for (int i = 0; i < 5; i++) {
      int slotX = getSlotX(i);
      int slotW = getSlotWidth(i);
      char oldC[2] = {oldStr[i], '\0'};
      char newC[2] = {newStr[i], '\0'};
      
      int drawX_old = getCharDrawX(i, oldStr[i]);
      int drawX_new = getCharDrawX(i, newStr[i]);

      if (oldStr[i] == newStr[i]) {
        u8g2.drawStr(drawX_new, 124, newC);
      } else {
        u8g2.setClipWindow(slotX, 22, slotX + slotW, 126);
        u8g2.drawStr(drawX_old, 124 - dy, oldC);
        u8g2.drawStr(drawX_new, 124 - dy + FONT_HEIGHT, newC);
        u8g2.setMaxClipWindow();
      }
    }
    u8g2.sendBuffer();
  }
}

void animateGlitchShift(const char* oldStr, const char* newStr, const char* dateBuff) {
  const int STEPS = 10;
  const int STRIP_H = 12;

  for (int step = 0; step <= STEPS; step++) {
    float t = (float)step / STEPS;

    u8g2.clearBuffer();
    drawTopBar(dateBuff);
    u8g2.setFont(u8g2_font_logisoso92_tn);

    for (int i = 0; i < 5; i++) {
      int slotX = getSlotX(i);
      int slotW = getSlotWidth(i);
      char oldC[2] = {oldStr[i], '\0'};
      char newC[2] = {newStr[i], '\0'};

      if (oldStr[i] == newStr[i] || step == STEPS) {
        u8g2.drawStr(getCharDrawX(i, newStr[i]), 124, newC);
      } else {
        int drawX = getCharDrawX(i, (t < 0.4) ? oldStr[i] : newStr[i]);
        for (int y = 22; y < 126; y += STRIP_H) {
          int shiftX = (random(100) < 65 && t < 0.85) ? random(-8, 9) : 0;
          u8g2.setClipWindow(max(0, slotX - 5), y, min(255, slotX + slotW + 5), min(y + STRIP_H, 126));
          u8g2.drawStr(drawX + shiftX, 124, (t < 0.4) ? oldC : newC);
          u8g2.setMaxClipWindow();
        }
      }
    }
    u8g2.sendBuffer();
    delay(10);
  }
}

void animateSplitFlip3D(const char* oldStr, const char* newStr, const char* dateBuff) {
  const int STEPS = 14;
  const int Y_CENTER = 75;

  for (int step = 0; step <= STEPS; step++) {
    float t = (float)step / STEPS;

    u8g2.clearBuffer();
    drawTopBar(dateBuff);
    u8g2.setFont(u8g2_font_logisoso92_tn);

    for (int i = 0; i < 5; i++) {
      int slotX = getSlotX(i);
      int slotW = getSlotWidth(i);
      char oldC[2] = {oldStr[i], '\0'};
      char newC[2] = {newStr[i], '\0'};

      if (oldStr[i] == newStr[i]) {
        u8g2.drawStr(getCharDrawX(i, newStr[i]), 124, newC);
      } else {
        float scaleY;
        const char* drawChar;
        int drawX;

        if (t < 0.5) {
          scaleY = cos(t * PI);
          drawChar = oldC;
          drawX = getCharDrawX(i, oldStr[i]);
        } else {
          scaleY = sin((t - 0.5) * PI);
          drawChar = newC;
          drawX = getCharDrawX(i, newStr[i]);
        }

        int numStrips = 10;
        int sliceH = 92 / numStrips;

        for (int s = 0; s < numStrips; s++) {
          int origY = 32 + (s * sliceH);
          int distFromCenter = origY - Y_CENTER;
          int targetY = Y_CENTER + (int)(distFromCenter * scaleY);
          int targetH = max(1, (int)(sliceH * scaleY));

          u8g2.setClipWindow(slotX, targetY, slotX + slotW, targetY + targetH);
          u8g2.drawStr(drawX, 124 - (origY - targetY), drawChar);
          u8g2.setMaxClipWindow();
        }
      }
    }
    u8g2.sendBuffer();
  }
}

void animateAtomization(const char* oldStr, const char* newStr, const char* dateBuff) {
  const int STEPS = 15;

  for (int step = 0; step <= STEPS; step++) {
    float t = (float)step / STEPS;

    u8g2.clearBuffer();
    drawTopBar(dateBuff);
    u8g2.setFont(u8g2_font_logisoso92_tn);

    for (int i = 0; i < 5; i++) {
      int slotX = getSlotX(i);
      int slotW = getSlotWidth(i);
      char oldC[2] = {oldStr[i], '\0'};
      char newC[2] = {newStr[i], '\0'};

      if (oldStr[i] == newStr[i] || step == STEPS) {
        u8g2.drawStr(getCharDrawX(i, newStr[i]), 124, newC);
      } else {
        int rows = 12;
        int cols = 8;
        int blockW = max(1, slotW / cols);
        int blockH = 92 / rows;

        int drawX = getCharDrawX(i, (t < 0.5) ? oldStr[i] : newStr[i]);
        const char* drawChar = (t < 0.5) ? oldC : newC;

        for (int r = 0; r < rows; r++) {
          for (int c = 0; c < cols; c++) {
            int bx = slotX + (c * blockW);
            int by = 32 + (r * blockH);

            uint8_t hash = (r * 13 + c * 29 + i * 7) % 50;
            float speed = 0.8 + (hash % 20) * 0.1;
            float sideDrift = ((hash % 11) - 5) * 1.5;

            float t_effect = (t < 0.5) ? (t * 2.0) : ((1.0 - t) * 2.0);

            int offsetY = - (int)(t_effect * t_effect * 35.0 * speed);
            int offsetX = (int)(t_effect * sideDrift);

            float threshold = (float)r / (float)rows;
            bool isDissolved = (t < 0.5) ? (t_effect > (1.0 - threshold * 0.7)) : (t_effect > (threshold * 0.7));

            if (!isDissolved) {
              u8g2.setClipWindow(bx, by, min(255, bx + blockW), min(126, by + blockH));
              u8g2.drawStr(drawX + offsetX, 124 + offsetY, drawChar);
              u8g2.setMaxClipWindow();
            } else if (t_effect > 0.1) {
              u8g2.setDrawColor(1);
              int px = bx + blockW / 2 + offsetX;
              int py = by + blockH / 2 + offsetY;
              if (px >= 0 && px < 256 && py >= 22 && py < 126) {
                u8g2.drawPixel(px, py);
                if (hash % 2 == 0) u8g2.drawPixel(px + 1, py - 1);
              }
            }
          }
        }
      }
    }
    u8g2.sendBuffer();
  }
}

void animateDitheredCrossFade(const char* oldStr, const char* newStr, const char* dateBuff) {
  const int STEPS = 8;

  for (int step = 0; step <= STEPS; step++) {
    float t = (float)step / STEPS;

    u8g2.clearBuffer();
    drawTopBar(dateBuff);
    u8g2.setFont(u8g2_font_logisoso92_tn);

    for (int i = 0; i < 5; i++) {
      if (oldStr[i] == newStr[i]) {
        char c[2] = {newStr[i], '\0'};
        u8g2.drawStr(getCharDrawX(i, newStr[i]), 124, c);
      } else {
        int oldLevel = map((int)(t * 100), 0, 100, 4, 0);
        int newLevel = map((int)(t * 100), 0, 100, 0, 4);

        if (oldLevel > 0) {
          drawDigitWithBrightness(i, oldStr[i], oldLevel);
        }
        if (newLevel > 0) {
          drawDigitWithBrightness(i, newStr[i], newLevel);
        }
      }
    }
    u8g2.sendBuffer();
  }
}

void animateWaveScan(const char* oldStr, const char* newStr, const char* dateBuff) {
  const int STEPS = 14;
  const int Y_MIN = 22;
  const int Y_MAX = 126;

  for (int step = 0; step <= STEPS; step++) {
    float t = (float)step / STEPS;
    int scanY = Y_MIN + (int)(t * (Y_MAX - Y_MIN));

    u8g2.clearBuffer();
    drawTopBar(dateBuff);
    u8g2.setFont(u8g2_font_logisoso92_tn);

    for (int i = 0; i < 5; i++) {
      int slotX = getSlotX(i);
      int slotW = getSlotWidth(i);

      if (oldStr[i] == newStr[i]) {
        char c[2] = {newStr[i], '\0'};
        u8g2.drawStr(getCharDrawX(i, newStr[i]), 124, c);
      } else {
        int sliceH = 8;
        for (int y = Y_MIN; y < Y_MAX; y += sliceH) {
          int h = min(sliceH, Y_MAX - y);
          u8g2.setClipWindow(slotX, y, slotX + slotW, y + h);

          if (y < scanY - 8) {
            drawDigitWithBrightness(i, newStr[i], 4);
          } else if (y <= scanY + 8) {
            if (y < scanY) {
              drawDigitWithBrightness(i, newStr[i], 3);
            } else {
              drawDigitWithBrightness(i, oldStr[i], 2);
            }
          } else {
            drawDigitWithBrightness(i, oldStr[i], 4);
          }
          u8g2.setMaxClipWindow();
        }

        u8g2.setDrawColor(1);
        if (scanY >= Y_MIN && scanY <= Y_MAX) {
          u8g2.drawBox(slotX, scanY - 1, slotW, 3);
        }
      }
    }
    u8g2.sendBuffer();
  }
}

void animateTimeTransition(const char* oldStr, const char* newStr, const char* dateBuff, int dayOfMonth) {
  int effect = dayOfMonth % 6;

  switch (effect) {
    case 0: animateVerticalSlide(oldStr, newStr, dateBuff); break;
    case 1: animateGlitchShift(oldStr, newStr, dateBuff); break;
    case 2: animateSplitFlip3D(oldStr, newStr, dateBuff); break;
    case 3: animateAtomization(oldStr, newStr, dateBuff); break;
    case 4: animateDitheredCrossFade(oldStr, newStr, dateBuff); break;
    case 5: animateWaveScan(oldStr, newStr, dateBuff); break;
  }
}


// --- NETWORK & SYSTEM LOGIC ---

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  
  if (String(topic) == MQTT_TOPIC_TEMPERATURE) rawTemp = message;
  else if (String(topic) == MQTT_TOPIC_WEATHER) weatherState = message;
}

void reconnectMQTT() {
  unsigned long now = millis();
  if (!mqttClient.connected() && (now - lastMqttRetry > 5000)) {
    lastMqttRetry = now;
    String clientId = "ESP32_Mirror_" + String(random(0xffff), HEX);
    
    bool connected = false;
    if (strlen(MQTT_USER) > 0) {
      connected = mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS, 
                                     MQTT_TOPIC_STATUS, 1, true, "offline");
    } else {
      connected = mqttClient.connect(clientId.c_str(), 
                                     MQTT_TOPIC_STATUS, 1, true, "offline");
    }

    if (connected) {
      mqttClient.publish(MQTT_TOPIC_STATUS, "online", true);
      mqttClient.subscribe(MQTT_TOPIC_TEMPERATURE);
      mqttClient.subscribe(MQTT_TOPIC_WEATHER);
    }
  }
}

void setup() {
  Serial.begin(115200);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  SPI.begin(OLED_SCLK, -1, OLED_MOSI, OLED_CS);

  u8g2.begin();

  if (veml.begin(&Wire)) {
    veml.setGain(VEML7700_GAIN_1);
    veml.setIntegrationTime(VEML7700_IT_100MS);
    smoothLux = veml.readLux();
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(250);

  ArduinoOTA.setHostname("Philips-Dashboard-Mirror");
  ArduinoOTA.setPassword("admin");

  ArduinoOTA.onStart([]() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_helvB10_tr);
    u8g2.drawStr(30, 64, "OTA Update...");
    u8g2.sendBuffer();
  });

  ArduinoOTA.onEnd([]() {
    u8g2.clearBuffer();
    u8g2.drawStr(50, 64, "Rebooting...");
    u8g2.sendBuffer();
  });

  ArduinoOTA.begin();

  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);
}

void loop() {
  ArduinoOTA.handle();
  unsigned long now = millis();

  if (!mqttClient.connected()) reconnectMQTT();
  mqttClient.loop();

  if (now - lastLuxRead >= 200) {
    lastLuxRead = now;
    float rawLux = veml.readLux();
    smoothLux = (smoothLux * 0.7) + (rawLux * 0.3);
    float target = map((long)smoothLux, 0, 150, 85, 255);
    targetContrast = constrain(target, 30, 255);
  }

  currentContrast = (currentContrast * 0.85) + (targetContrast * 0.15);
  u8g2.setContrast((uint8_t)currentContrast);

  struct tm timeinfo;
  bool timeValid = getLocalTime(&timeinfo);

  char timeBuff[6] = "--:--";
  char dateBuff[20] = "--- -- ---";

  if (timeValid) {
    snprintf(timeBuff, sizeof(timeBuff), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    snprintf(dateBuff, sizeof(dateBuff), "%s %d %s", 
             days[timeinfo.tm_wday], 
             timeinfo.tm_mday, 
             months[timeinfo.tm_mon]);
  }

  if (timeValid) {
    if (prevTimeStr[0] == '\0') {
      strcpy(prevTimeStr, timeBuff);
    } else if (strcmp(prevTimeStr, timeBuff) != 0) {
      animateTimeTransition(prevTimeStr, timeBuff, dateBuff, timeinfo.tm_mday);
      strcpy(prevTimeStr, timeBuff);
    }
  }

  u8g2.clearBuffer();
  drawTopBar(dateBuff);

  u8g2.setFont(u8g2_font_logisoso92_tn);
  for (int i = 0; i < 5; i++) {
    char c[2] = {timeBuff[i], '\0'};
    u8g2.drawStr(getCharDrawX(i, timeBuff[i]), 124, c);
  }

  u8g2.sendBuffer();
}