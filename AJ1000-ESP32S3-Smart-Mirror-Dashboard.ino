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
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <Wire.h>
#include <SPI.h>
#include <U8g2lib.h>
#include <Adafruit_VEML7700.h>
#include <TJpg_Decoder.h>

// ==========================================
// NETWORK CONFIGURATION AND CREDENTIALS
// ==========================================
const char* WIFI_SSID     = "...";
const char* WIFI_PASSWORD = "...";
const char* MQTT_SERVER   = "192.168.1.XX"; 
const int   MQTT_PORT     = 1883;
const char* MQTT_USER     = "...";            
const char* MQTT_PASS     = "...";

// ==========================================
// MQTT TOPICS DEFINITION
// ==========================================
const char* MQTT_TOPIC_STATUS        = "sveglia/Status";
const char* MQTT_TOPIC_TEMPERATURA   = "sveglia/temperatura";
const char* MQTT_TOPIC_METEO         = "sveglia/meteo";
const char* MQTT_TOPIC_MUSIC_STATUS  = "sveglia/musica/Status";
const char* MQTT_TOPIC_MUSIC_COVER   = "sveglia/musica/copertina";
const char* MQTT_TOPIC_MUSIC_TITLE   = "sveglia/musica/titolo";
const char* MQTT_TOPIC_MUSIC_ARTIST  = "sveglia/musica/artista";
const char* MQTT_TOPIC_MUSIC_ALBUM   = "sveglia/musica/album";
const char* MQTT_TOPIC_MUSIC_PLAYLIST= "sveglia/musica/playlist";
const char* MQTT_TOPIC_MUSIC_POS     = "sveglia/musica/posizione";
const char* MQTT_TOPIC_MUSIC_DUR     = "sveglia/musica/durata";
const char* MQTT_TOPIC_MUSIC_VOL     = "sveglia/musica/volume";

// ==========================================
// SCREEN STATE MACHINE (Extensible)
// ==========================================
enum DisplayScreen {
  SCREEN_CLOCK,
  SCREEN_MUSIC
};

DisplayScreen currentScreen = SCREEN_CLOCK;

// ==========================================
// ESP32-S3-ZERO HARDWARE PINS
// ==========================================
#define OLED_SCLK 12
#define OLED_MOSI 11
#define OLED_CS   10
#define OLED_DC    8
#define OLED_RST   9
#define I2C_SDA    1
#define I2C_SCL    2

// ==========================================
// GLOBAL OBJECTS AND BUFFERS
// ==========================================
U8G2_SSD1363_256X128_F_4W_HW_SPI u8g2(U8G2_R0, OLED_CS, OLED_DC, OLED_RST);
Adafruit_VEML7700 veml = Adafruit_VEML7700();
WiFiClient espClient;
PubSubClient mqttClient(espClient);

uint8_t coverBitmap[2048];
uint16_t tempJpegBuffer[128 * 128];
uint16_t scaledW = 64;
uint16_t scaledH = 64;
bool coverLoaded = false;

String rawTemp      = "--";
String meteoState   = "";
String musicStatus  = "idle";
String musicCoverUrl= "";
String musicTitle   = "";
String musicArtist  = "";
String musicAlbum   = "";
String musicPlaylist= "";
long   musicPosSec  = 0;
long   musicDurSec  = 0;
int    musicVolume  = 0;

String lastCoverUrl = "";
bool newCoverNeeded = false;
char prevTimeStr[6] = "";

float smoothLux = 0.0;
float currentContrast = 90.0;
float targetContrast = 90.0;
unsigned long lastLuxRead = 0;
unsigned long lastMqttRetry = 0;

const char* giorni[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* mesi[]   = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// ==========================================
// VECTOR WEATHER ICONS
// ==========================================
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

  if (state == "sunny") drawSunShape(bx + 8, by + 6);
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
  else drawSunShape(bx + 8, by + 7);
}

void drawFormattedTemperature(int x, int y, String tStr) {
  u8g2.setFont(u8g2_font_helvB10_tf);
  String numOnly = "";
  for (size_t i = 0; i < tStr.length(); i++) {
    if (isDigit(tStr[i]) || tStr[i] == '-' || tStr[i] == '.') numOnly += tStr[i];
  }
  if (numOnly == "") return;

  u8g2.setCursor(x, y);
  u8g2.print(numOnly);

  int strW = u8g2.getUTF8Width(numOnly.c_str());
  int circleX = x + strW + 2;
  int circleY = y - 8;
  u8g2.drawCircle(circleX, circleY, 1, U8G2_DRAW_ALL);
  u8g2.setCursor(circleX + 4, y);
  u8g2.print("C");
}

void drawTopBar(const char* dateBuff) {
  u8g2.setFont(u8g2_font_helvB10_tf);
  u8g2.drawUTF8(5, 16, dateBuff);

  bool hasTemp = (rawTemp != "--" && rawTemp.length() > 0);
  bool hasMeteo = (meteoState != "" && meteoState != "none");

  if (hasMeteo) {
    drawWeatherIcon(183, 18, meteoState);
  }
  if (hasTemp) {
    drawFormattedTemperature(205, 16, rawTemp);
  }
}

// ==========================================
// UTF-8 TEXT SCROLLING
// ==========================================
void drawScrollingText(int x, int y, int maxW, String text, const uint8_t* font) {
  u8g2.setFont(font);
  int textW = u8g2.getUTF8Width(text.c_str());

  if (textW <= maxW) {
    u8g2.drawUTF8(x, y, text.c_str());
  } else {
    int spaceW = 30;
    int totalLoopW = textW + spaceW;
    int scrollOffset = (millis() / 35) % totalLoopW;

    u8g2.setClipWindow(x, y - 14, x + maxW, y + 4);
    u8g2.drawUTF8(x - scrollOffset, y, text.c_str());
    u8g2.drawUTF8(x - scrollOffset + totalLoopW, y, text.c_str());
    u8g2.setMaxClipWindow();
  }
}

// ==========================================
// COVER DECODING & DITHERING
// ==========================================
bool tjpgCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  for (int16_t j = 0; j < h; j++) {
    for (int16_t i = 0; i < w; i++) {
      int srcX = x + i;
      int srcY = y + j;
      if (srcX < scaledW && srcY < scaledH) {
        tempJpegBuffer[srcY * scaledW + srcX] = bitmap[j * w + i];
      }
    }
  }
  return true;
}

void downloadAndProcessCover(String url) {
  coverLoaded = false;
  if (url.length() == 0 || !url.startsWith("http")) return;

  if (url.indexOf("ab67616d0000b273") != -1) {
    url.replace("ab67616d0000b273", "ab67616d00004851");
  } else if (url.indexOf("ab67616d00001e02") != -1) {
    url.replace("ab67616d00001e02", "ab67616d00004851");
  }

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(5000);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(5000);

  if (!http.begin(client, url)) return;

  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK || httpCode == 200) {
    int len = http.getSize();
    int maxAlloc = (len > 0) ? len : 15360;

    if (ESP.getFreeHeap() < (maxAlloc + 5000)) {
      http.end();
      return;
    }

    uint8_t* httpBuf = (uint8_t*)malloc(maxAlloc);
    if (!httpBuf) {
      http.end();
      return;
    }

    WiFiClient* stream = http.getStreamPtr();
    int bytesRead = 0;
    unsigned long startRead = millis();

    while (http.connected() && (bytesRead < maxAlloc) && (millis() - startRead < 5000)) {
      size_t avail = stream->available();
      if (avail) {
        int toRead = min((int)avail, maxAlloc - bytesRead);
        bytesRead += stream->readBytes(httpBuf + bytesRead, toRead);
      } else {
        delay(2);
      }
      if (len > 0 && bytesRead >= len) break;
    }

    if (bytesRead > 0) {
      uint16_t jpgW = 0, jpgH = 0;
      TJpgDec.getJpgSize(&jpgW, &jpgH, httpBuf, bytesRead);

      if (jpgW > 0 && jpgH > 0) {
        scaledW = jpgW;
        scaledH = jpgH;

        memset(tempJpegBuffer, 0, sizeof(tempJpegBuffer));
        TJpgDec.setJpgScale(1);
        TJpgDec.setCallback(tjpgCallback);
        TJpgDec.drawJpg(0, 0, httpBuf, bytesRead);

        memset(coverBitmap, 0, sizeof(coverBitmap));
        const uint8_t bayer4x4[4][4] = {
          {  0,  8,  2, 10 },
          { 12,  4, 14,  6 },
          {  3, 11,  1,  9 },
          { 15,  7, 13,  5 }
        };

        for (int py = 0; py < 128; py++) {
          for (int px = 0; px < 128; px++) {
            int srcX = (px * scaledW) / 128;
            int srcY = (py * scaledH) / 128;
            if (srcX >= scaledW) srcX = scaledW - 1;
            if (srcY >= scaledH) srcY = scaledH - 1;

            uint16_t rgb = tempJpegBuffer[srcY * scaledW + srcX];
            uint8_t r = ((rgb >> 11) & 0x1F) << 3;
            uint8_t g = ((rgb >> 5) & 0x3F) << 2;
            uint8_t b = (rgb & 0x1F) << 3;

            uint8_t gray = (r * 11 + g * 16 + b * 5) >> 5;
            uint8_t threshold = bayer4x4[py % 4][px % 4] * 16;

            if (gray > threshold) {
              int byteIdx = (py * 128 + px) / 8;
              int bitIdx = px % 8;
              coverBitmap[byteIdx] |= (1 << bitIdx);
            }
          }
        }
        coverLoaded = true;
      }
    }
    free(httpBuf);
  }
  http.end();
}

// ==========================================
// MUSIC RENDER (Adaptive Layout)
// ==========================================
String formatMMSS(long seconds) {
  if (seconds <= 0) return "00:00";
  long m = seconds / 60;
  long s = seconds % 60;
  char buf[8];
  snprintf(buf, sizeof(buf), "%02ld:%02ld", m, s);
  return String(buf);
}

long parseTimeToSeconds(String val) {
  if (val.indexOf(':') != -1) {
    int sep = val.indexOf(':');
    return val.substring(0, sep).toInt() * 60 + val.substring(sep + 1).toInt();
  }
  long num = val.toInt();
  if (num > 10000) num /= 1000;
  return num;
}

void renderMusicScreen() {
  u8g2.clearBuffer();

  if (coverLoaded) {
    u8g2.drawXBM(0, 0, 128, 128, coverBitmap);

    int rx = 132;
    int rw = 122;

    String headerStr = (musicPlaylist.length() > 0) ? musicPlaylist : "SPOTIFY";
    headerStr.toUpperCase();
    drawScrollingText(rx, 11, rw, headerStr, u8g2_font_4x6_tf);
    u8g2.drawLine(rx, 15, 254, 15);

    String tStr = (musicTitle.length() > 0) ? musicTitle : "Unknown Title";
    drawScrollingText(rx, 31, rw, tStr, u8g2_font_helvB10_tf);

    String aStr = (musicArtist.length() > 0) ? musicArtist : "Unknown Artist";
    drawScrollingText(rx, 48, rw, aStr, u8g2_font_helvB08_tf);

    if (musicAlbum.length() > 0) {
      drawScrollingText(rx, 63, rw, musicAlbum, u8g2_font_helvR08_tf);
    }

    u8g2.drawLine(rx, 68, 254, 68);

    int barY = 76;
    int barH = 6;
    u8g2.drawFrame(rx, barY, rw, barH);
    if (musicDurSec > 0) {
      int fillW = map(constrain(musicPosSec, 0, musicDurSec), 0, musicDurSec, 0, rw - 2);
      if (fillW > 0) u8g2.drawBox(rx + 1, barY + 1, fillW, barH - 2);
    }

    u8g2.setFont(u8g2_font_5x7_tf);
    String timeStr = formatMMSS(musicPosSec) + " / " + formatMMSS(musicDurSec);
    int timeW = u8g2.getUTF8Width(timeStr.c_str());
    u8g2.drawStr(rx + (rw - timeW) / 2, 95, timeStr.c_str());

    u8g2.drawLine(rx, 101, 254, 101);

    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(rx, 118, "VOL");

    int volBarX = rx + 22;
    int volBarW = 68;
    int volBarY = 113;
    int volBarH = 6;

    u8g2.drawFrame(volBarX, volBarY, volBarW, volBarH);
    int volFill = map(musicVolume, 0, 100, 0, volBarW - 2);
    if (volFill > 0) u8g2.drawBox(volBarX + 1, volBarY + 1, volFill, volBarH - 2);

    char volStr[8];
    snprintf(volStr, sizeof(volStr), "%d%%", musicVolume);
    u8g2.drawStr(volBarX + volBarW + 4, 118, volStr);

  } else {
    int rx = 10;
    int rw = 236;

    String headerStr = (musicPlaylist.length() > 0) ? musicPlaylist : "SPOTIFY";
    headerStr.toUpperCase();
    drawScrollingText(rx, 12, rw, headerStr, u8g2_font_6x10_tf);
    u8g2.drawLine(rx, 16, rx + rw, 16);

    String tStr = (musicTitle.length() > 0) ? musicTitle : "Unknown Title";
    drawScrollingText(rx, 35, rw, tStr, u8g2_font_helvB12_tf);

    String aStr = (musicArtist.length() > 0) ? musicArtist : "Unknown Artist";
    drawScrollingText(rx, 53, rw, aStr, u8g2_font_helvB10_tf);

    if (musicAlbum.length() > 0) {
      drawScrollingText(rx, 68, rw, musicAlbum, u8g2_font_helvR08_tf);
    }

    u8g2.drawLine(rx, 73, rx + rw, 73);

    int barY = 81;
    int barH = 7;
    u8g2.drawFrame(rx, barY, rw, barH);

    if (musicDurSec > 0) {
      int fillW = map(constrain(musicPosSec, 0, musicDurSec), 0, musicDurSec, 0, rw - 2);
      if (fillW > 0) u8g2.drawBox(rx + 1, barY + 1, fillW, barH - 2);
    }

    u8g2.setFont(u8g2_font_6x10_tf);
    String timeStr = formatMMSS(musicPosSec) + " / " + formatMMSS(musicDurSec);
    int timeW = u8g2.getUTF8Width(timeStr.c_str());
    u8g2.drawStr(rx + (rw - timeW) / 2, 101, timeStr.c_str());

    u8g2.drawLine(rx, 106, rx + rw, 106);

    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(rx, 122, "VOL");

    int volBarX = rx + 30;
    int volBarW = 165;
    int volBarY = 114;
    int volBarH = 7;

    u8g2.drawFrame(volBarX, volBarY, volBarW, volBarH);
    int volFill = map(musicVolume, 0, 100, 0, volBarW - 2);
    if (volFill > 0) u8g2.drawBox(volBarX + 1, volBarY + 1, volFill, volBarH - 2);

    char volStr[8];
    snprintf(volStr, sizeof(volStr), "%d%%", musicVolume);
    u8g2.drawStr(volBarX + volBarW + 8, 122, volStr);
  }

  u8g2.sendBuffer();
}

// ==========================================
// FIXED SPACES & CLOCK ANIMATIONS
// ==========================================
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
      for (int px = slotX; px < slotX + slotW; px += 2) u8g2.drawPixel(px, py);
    }
  } else if (level == 2) {
    for (int py = 22; py < 126; py++) {
      int startX = slotX + (py % 2);
      for (int px = startX; px < slotX + slotW; px += 2) u8g2.drawPixel(px, py);
    }
  } else if (level == 1) {
    for (int py = 22; py < 126; py++) {
      for (int px = slotX; px < slotX + slotW; px++) {
        if (!((px % 2 == 0) && (py % 2 == 0))) u8g2.drawPixel(px, py);
      }
    }
  }
  u8g2.setDrawColor(1);
}

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

void animatePolverizzazione(const char* oldStr, const char* newStr, const char* dateBuff) {
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

        if (oldLevel > 0) drawDigitWithBrightness(i, oldStr[i], oldLevel);
        if (newLevel > 0) drawDigitWithBrightness(i, newStr[i], newLevel);
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
            if (y < scanY) drawDigitWithBrightness(i, newStr[i], 3);
            else drawDigitWithBrightness(i, oldStr[i], 2);
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
    case 3: animatePolverizzazione(oldStr, newStr, dateBuff); break;
    case 4: animateDitheredCrossFade(oldStr, newStr, dateBuff); break;
    case 5: animateWaveScan(oldStr, newStr, dateBuff); break;
  }
}

void renderClockScreen() {
  struct tm timeinfo;
  bool timeValid = getLocalTime(&timeinfo);

  char timeBuff[6] = "--:--";
  char dateBuff[20] = "--- -- ---";

  if (timeValid) {
    snprintf(timeBuff, sizeof(timeBuff), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    snprintf(dateBuff, sizeof(dateBuff), "%s %d %s", 
             giorni[timeinfo.tm_wday], 
             timeinfo.tm_mday, 
             mesi[timeinfo.tm_mon]);
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

// ==========================================
// MQTT RECEPTION & NETWORK LOGIC
// ==========================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) message += (char)payload[i];
  
  String topStr = String(topic);

  if (topStr == MQTT_TOPIC_TEMPERATURA) rawTemp = message;
  else if (topStr == MQTT_TOPIC_METEO) meteoState = message;
  else if (topStr == MQTT_TOPIC_MUSIC_STATUS) musicStatus = message;
  else if (topStr == MQTT_TOPIC_MUSIC_COVER) {
    musicCoverUrl = message;
    if (musicCoverUrl != lastCoverUrl) newCoverNeeded = true;
  }
  else if (topStr == MQTT_TOPIC_MUSIC_TITLE) musicTitle = message;
  else if (topStr == MQTT_TOPIC_MUSIC_ARTIST) musicArtist = message;
  else if (topStr == MQTT_TOPIC_MUSIC_ALBUM) musicAlbum = message;
  else if (topStr == MQTT_TOPIC_MUSIC_PLAYLIST) musicPlaylist = message;
  else if (topStr == MQTT_TOPIC_MUSIC_POS) musicPosSec = parseTimeToSeconds(message);
  else if (topStr == MQTT_TOPIC_MUSIC_DUR) musicDurSec = parseTimeToSeconds(message);
  else if (topStr == MQTT_TOPIC_MUSIC_VOL) musicVolume = constrain(message.toInt(), 0, 100);
}

void reconnectMQTT() {
  unsigned long now = millis();
  if (!mqttClient.connected() && (now - lastMqttRetry > 5000)) {
    lastMqttRetry = now;
    String clientId = "ESP32_Sveglia_" + String(random(0xffff), HEX);

    bool connected = false;
    if (strlen(MQTT_USER) > 0) {
      connected = mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASS, MQTT_TOPIC_STATUS, 1, true, "offline");
    } else {
      connected = mqttClient.connect(clientId.c_str(), MQTT_TOPIC_STATUS, 1, true, "offline");
    }

    if (connected) {
      mqttClient.publish(MQTT_TOPIC_STATUS, "online", true);

      mqttClient.subscribe(MQTT_TOPIC_TEMPERATURA);
      mqttClient.subscribe(MQTT_TOPIC_METEO);
      mqttClient.subscribe(MQTT_TOPIC_MUSIC_STATUS);
      mqttClient.subscribe(MQTT_TOPIC_MUSIC_COVER);
      mqttClient.subscribe(MQTT_TOPIC_MUSIC_TITLE);
      mqttClient.subscribe(MQTT_TOPIC_MUSIC_ARTIST);
      mqttClient.subscribe(MQTT_TOPIC_MUSIC_ALBUM);
      mqttClient.subscribe(MQTT_TOPIC_MUSIC_PLAYLIST);
      mqttClient.subscribe(MQTT_TOPIC_MUSIC_POS);
      mqttClient.subscribe(MQTT_TOPIC_MUSIC_DUR);
      mqttClient.subscribe(MQTT_TOPIC_MUSIC_VOL);
    }
  }
}

// --- BOOT ANIMATIONS (4 Optimized Effects) ---

// 1. CRT Power-On 
void bootCRTExpand(const char* timeStr, const char* dateBuff) {
  for (int w = 0; w <= 128; w += 8) {
    u8g2.clearBuffer();
    u8g2.setDrawColor(1);
    u8g2.drawBox(128 - w, 63, w * 2, 2);
    u8g2.sendBuffer();
    delay(12);
  }

  for (int h = 2; h <= 64; h += 4) {
    u8g2.clearBuffer();
    u8g2.setClipWindow(0, max(0, 64 - h), 255, min(127, 64 + h));
    
    drawTopBar(dateBuff);
    u8g2.setFont(u8g2_font_logisoso92_tn);
    for (int i = 0; i < 5; i++) {
      char c[2] = {timeStr[i], '\0'};
      u8g2.drawStr(getCharDrawX(i, timeStr[i]), 124, c);
    }
    
    u8g2.setMaxClipWindow();
    u8g2.sendBuffer();
    delay(18);
  }
}

// 2. Sequential Cyber Matrix Lock-In
void bootMatrixLock(const char* timeStr, const char* dateBuff) {
  for (int i = 0; i < 5; i++) {
    if (i == 2) {
      u8g2.clearBuffer();
      drawTopBar(dateBuff);
      u8g2.setFont(u8g2_font_logisoso92_tn);
      for (int j = 0; j <= i; j++) {
        char c[2] = {timeStr[j], '\0'};
        u8g2.drawStr(getCharDrawX(j, timeStr[j]), 124, c);
      }
      u8g2.sendBuffer();
      delay(40);
      continue;
    }

    int targetDigit = timeStr[i] - '0';
    int totalSteps = 10 + targetDigit; 

    for (int step = 0; step <= totalSteps; step++) {
      char currentC[2] = {(char)('0' + (step % 10)), '\0'};

      u8g2.clearBuffer();
      drawTopBar(dateBuff);
      u8g2.setFont(u8g2_font_logisoso92_tn);

      for (int j = 0; j < i; j++) {
        char c[2] = {timeStr[j], '\0'};
        u8g2.drawStr(getCharDrawX(j, timeStr[j]), 124, c);
      }

      u8g2.drawStr(getCharDrawX(i, currentC[0]), 124, currentC);

      u8g2.sendBuffer();
      delay(18); 
    }
  }

  u8g2.clearBuffer();
  drawTopBar(dateBuff);
  u8g2.setFont(u8g2_font_logisoso92_tn);
  for (int i = 0; i < 5; i++) {
    char c[2] = {timeStr[i], '\0'};
    u8g2.drawStr(getCharDrawX(i, timeStr[i]), 124, c);
  }
  u8g2.sendBuffer();
}

// 3. Sequential Drop-In 
void bootSequentialDrop(const char* timeStr, const char* dateBuff) {
  for (int i = 0; i < 5; i++) {
    for (int dy = -60; dy <= 0; dy += 10) {
      u8g2.clearBuffer();
      drawTopBar(dateBuff);
      u8g2.setFont(u8g2_font_logisoso92_tn);

      for (int j = 0; j < i; j++) {
        char c[2] = {timeStr[j], '\0'};
        u8g2.drawStr(getCharDrawX(j, timeStr[j]), 124, c);
      }

      char curC[2] = {timeStr[i], '\0'};
      int slotX = getSlotX(i);
      int slotW = getSlotWidth(i);
      u8g2.setClipWindow(slotX, 22, slotX + slotW, 126);
      u8g2.drawStr(getCharDrawX(i, timeStr[i]), 124 + dy, curC);
      u8g2.setMaxClipWindow();

      u8g2.sendBuffer();
      delay(20);
    }
  }
}

void runBootAnimation(const char* timeStr, const char* dateBuff, int dayOfMonth) {
  int effect = dayOfMonth % 3;

  switch (effect) {
    case 0: bootCRTExpand(timeStr, dateBuff); break;
    case 1: bootMatrixLock(timeStr, dateBuff); break;
    case 2: bootSequentialDrop(timeStr, dateBuff); break;
  }
}

// ==========================================
// INITIAL SETUP (WITH POWER-ON LOCK)
// ==========================================
void setup() {
  Serial.begin(115200);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  SPI.begin(OLED_SCLK, -1, OLED_MOSI, OLED_CS);

  u8g2.begin();
  u8g2.setPowerSave(1); 

  if (veml.begin(&Wire)) {
    veml.setGain(VEML7700_GAIN_1);
    veml.setIntegrationTime(VEML7700_IT_100MS);
    smoothLux = veml.readLux();
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long startWifi = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startWifi < 15000) {
    delay(100);
  }

  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  unsigned long startNtp = millis();
  bool ntpSuccess = false;

  while (millis() - startNtp < 10000) {
    if (getLocalTime(&timeinfo)) {
      ntpSuccess = true;
      break;
    }
    delay(100);
  }

  char bootTimeBuff[10] = "--:--";
  char bootDateBuff[30] = "--- -- ---";

  if (ntpSuccess) {
    snprintf(bootTimeBuff, sizeof(bootTimeBuff), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    snprintf(bootDateBuff, sizeof(bootDateBuff), "%s %d %s", 
             giorni[timeinfo.tm_wday], 
             timeinfo.tm_mday, 
             mesi[timeinfo.tm_mon]);
  }

  u8g2.enableUTF8Print();
  u8g2.setPowerSave(0);

  delay(150);

  if (ntpSuccess) {
    runBootAnimation(bootTimeBuff, bootDateBuff, timeinfo.tm_mday);
  }

  ArduinoOTA.setHostname("Philips-Dashboard-Sveglia");

  ArduinoOTA.onStart([]() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_helvB10_tf);
    u8g2.drawUTF8(30, 45, "OTA Update...");
    u8g2.sendBuffer();
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    if (total == 0) return; 
    
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_helvB10_tf);
    u8g2.drawUTF8(30, 40, "OTA Update...");
    
    int barX = 30;
    int barY = 55;
    int barW = 196;
    int barH = 10;
    
    u8g2.drawFrame(barX, barY, barW, barH);
    
    int fillW = map(progress, 0, total, 0, barW - 2);
    if (fillW > 0) {
      u8g2.drawBox(barX + 1, barY + 1, fillW, barH - 2);
    }
    
    unsigned int percentage = (progress * 100) / total;
    char percStr[12];
    snprintf(percStr, sizeof(percStr), "%u%%", percentage);
    
    u8g2.setFont(u8g2_font_6x10_tf);
    int txtW = u8g2.getUTF8Width(percStr);
    u8g2.drawUTF8(barX + (barW - txtW) / 2, 82, percStr);
    
    u8g2.sendBuffer();
  });

  ArduinoOTA.onEnd([]() {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_helvB10_tf);
    u8g2.drawUTF8(50, 64, "Rebooting...");
    u8g2.sendBuffer();
  });

  ArduinoOTA.begin();

  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setBufferSize(512);
}

// ==========================================
// MAIN LOOP
// ==========================================
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
    targetContrast = constrain(target, 85, 255);
  }

  currentContrast = (currentContrast * 0.85) + (targetContrast * 0.15);
  u8g2.setContrast((uint8_t)currentContrast);

  if (mqttClient.connected() && musicStatus == "playing") {
    currentScreen = SCREEN_MUSIC;
  } else {
    currentScreen = SCREEN_CLOCK;
  }

  switch (currentScreen) {
    case SCREEN_MUSIC:
      if (newCoverNeeded) {
        downloadAndProcessCover(musicCoverUrl);
        lastCoverUrl = musicCoverUrl;
        newCoverNeeded = false;
      }
      renderMusicScreen();
      break;

    case SCREEN_CLOCK:
    default:
      renderClockScreen();
      break;
  }
}