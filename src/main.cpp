#include <Arduino.h>
#include <HTTPClient.h>
#include <LilyGo_AMOLED.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UptimeCore.h>
#include <time.h>
#include <vector>

namespace {

constexpr uint32_t WIFI_TIMEOUT_MS = 20000;
constexpr uint32_t WIFI_RETRY_MS = 15000;
constexpr uint32_t HTTP_TIMEOUT_MS = 8000;
constexpr uint32_t UI_REFRESH_MS = 1000;
constexpr int16_t DASHBOARD_WIDTH = 536;
constexpr int16_t DASHBOARD_HEIGHT = 240;

constexpr uint16_t COLOR_BG = 0x0021;
constexpr uint16_t COLOR_PANEL = 0x0863;
constexpr uint16_t COLOR_GRID = 0x10C6;
constexpr uint16_t COLOR_CYAN = 0x07FF;
constexpr uint16_t COLOR_MAGENTA = 0xF81F;
constexpr uint16_t COLOR_GREEN = 0x07E0;
constexpr uint16_t COLOR_RED = 0xF800;
constexpr uint16_t COLOR_AMBER = 0xFD20;
constexpr uint16_t COLOR_MUTED = 0x7BEF;

using uptime_core::SiteState;
using uptime_core::Orientation;

struct Site {
  String name;
  String url;
  SiteState state = SiteState::Unknown;
  int statusCode = 0;
};

struct Settings {
  String ssid;
  String password;
  uint16_t refreshMinutes = 5;
  uint8_t displayBrightnessPercent = 70;
  Orientation orientation = Orientation::Right;
  std::vector<Site> sites;
};

LilyGo_Class amoled;
TFT_eSPI tft;
TFT_eSprite canvas(&tft);
TFT_eSprite portraitCanvas(&tft);
Settings settings;

uint32_t lastRefreshMs = 0;
uint32_t lastUiDrawMs = 0;
uint32_t lastWifiAttemptMs = 0;
bool hasRefreshed = false;
bool isScanning = false;
String scanLabel;
String fatalMessage;

String valueAfterEquals(const String &line) {
  const int separator = line.indexOf('=');
  if (separator < 0) {
    return String();
  }

  String value = line.substring(separator + 1);
  value.trim();
  return value;
}

bool loadSettings() {
  if (!LittleFS.begin(false)) {
    fatalMessage = "FILESYSTEM OFFLINE";
    return false;
  }

  File file = LittleFS.open("/settings.ini", "r");
  if (!file) {
    fatalMessage = "SETTINGS.INI MISSING";
    return false;
  }

  String section;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();

    if (line.isEmpty() || line.startsWith(";") || line.startsWith("#")) {
      continue;
    }
    if (line.startsWith("[") && line.endsWith("]")) {
      section = line.substring(1, line.length() - 1);
      section.toLowerCase();
      continue;
    }

    const int separator = line.indexOf('=');
    if (separator < 1) {
      continue;
    }

    String key = line.substring(0, separator);
    key.trim();
    String value = valueAfterEquals(line);

    if (section == "wifi") {
      if (key == "ssid") {
        settings.ssid = value;
      } else if (key == "password") {
        settings.password = value;
      }
    } else if (section == "sites" && !key.isEmpty() && !value.isEmpty()) {
      Site site;
      site.name = key;
      site.url = value;
      settings.sites.push_back(site);
    } else if (section == "config") {
      if (key == "refresh_minutes") {
        const long minutes = value.toInt();
        if (minutes >= 1 && minutes <= 1440) {
          settings.refreshMinutes = static_cast<uint16_t>(minutes);
        }
      } else if (key == "display_brightness_percent") {
        const long percent = value.toInt();
        if ((percent > 0 || value == "0") && percent <= 100) {
          settings.displayBrightnessPercent = static_cast<uint8_t>(percent);
        }
      } else if (key == "orientation") {
        value.toLowerCase();
        settings.orientation = uptime_core::parseOrientation(value.c_str());
      }
    }
  }

  file.close();
  if (settings.ssid.isEmpty()) {
    fatalMessage = "WIFI SSID MISSING";
    return false;
  }
  if (settings.sites.empty()) {
    fatalMessage = "NO SITES CONFIGURED";
    return false;
  }
  return true;
}

void drawBackground() {
  canvas.fillSprite(COLOR_BG);
  for (int x = 8; x < canvas.width(); x += 24) {
    canvas.drawFastVLine(x, 34, canvas.height() - 42, COLOR_GRID);
  }
  for (int y = 42; y < canvas.height(); y += 24) {
    canvas.drawFastHLine(0, y, canvas.width(), COLOR_GRID);
  }
  canvas.drawFastHLine(0, 32, canvas.width(), COLOR_CYAN);
  canvas.drawFastHLine(0, 34, canvas.width(), COLOR_MAGENTA);
}

void drawPanel(TFT_eSprite &target, int x, int y, int width, int height, uint16_t accent) {
  target.fillRect(x, y, width, height, COLOR_PANEL);
  target.drawRect(x, y, width, height, accent);
  target.fillRect(x, y, 4, height, accent);
  target.drawFastHLine(x + 9, y + 5, 20, accent);
}

String currentTimeText() {
  struct tm timeInfo {};
  if (!getLocalTime(&timeInfo, 10)) {
    return "--:--:-- UTC";
  }

  char buffer[16];
  strftime(buffer, sizeof(buffer), "%H:%M:%S UTC", &timeInfo);
  return String(buffer);
}

String refreshAgeText() {
  if (!hasRefreshed) {
    return "LAST SYNC // NEVER";
  }

  const uint32_t seconds = (millis() - lastRefreshMs) / 1000;
  if (seconds < 60) {
    return "LAST SYNC // " + String(seconds) + " SEC AGO";
  }
  return "LAST SYNC // " + String(seconds / 60) + " MIN AGO";
}

void countStates(size_t &up, size_t &down) {
  uptime_core::StateCounts counts;
  for (const Site &site : settings.sites) {
    uptime_core::addState(counts, site.state);
  }
  up = counts.up;
  down = counts.down;
}

String truncateToWidth(const String &text, int maxWidth, uint8_t font) {
  if (canvas.textWidth(text, font) <= maxWidth) {
    return text;
  }

  const String ellipsis = "...";
  String truncated = text;
  while (truncated.length() > 0 && canvas.textWidth(truncated + ellipsis, font) > maxWidth) {
    truncated.remove(truncated.length() - 1);
  }
  return truncated + ellipsis;
}

bool isPortraitOrientation() {
  return settings.orientation == Orientation::Down || settings.orientation == Orientation::Up;
}

void drawLandscapeStatusPanels(size_t up, size_t down) {
  drawPanel(canvas, 12, 48, 164, 128,
            WiFi.status() == WL_CONNECTED ? COLOR_CYAN : COLOR_AMBER);
  canvas.setTextDatum(TL_DATUM);
  canvas.setTextColor(COLOR_MUTED, COLOR_PANEL);
  canvas.drawString("NETWORK", 25, 60, 2);
  canvas.setTextColor(WiFi.status() == WL_CONNECTED ? COLOR_CYAN : COLOR_AMBER, COLOR_PANEL);
  canvas.drawString(WiFi.status() == WL_CONNECTED ? "LINKED" : "OFFLINE", 25, 85, 4);
  canvas.setTextColor(TFT_WHITE, COLOR_PANEL);
  const String networkDetail = WiFi.status() == WL_CONNECTED
      ? String(WiFi.RSSI()) + " dBm"
      : "RETRYING";
  canvas.drawString(networkDetail, 25, 130, 2);
  canvas.setTextColor(COLOR_MUTED, COLOR_PANEL);
  canvas.drawString(String(settings.sites.size()) + " NODES", 25, 152, 2);

  drawPanel(canvas, 188, 48, 160, 128, COLOR_GREEN);
  canvas.setTextColor(COLOR_MUTED, COLOR_PANEL);
  canvas.drawString("SYSTEMS UP", 201, 60, 2);
  canvas.setTextColor(COLOR_GREEN, COLOR_PANEL);
  canvas.setTextDatum(MC_DATUM);
  canvas.drawNumber(up, 268, 119, 7);

  drawPanel(canvas, 360, 48, 164, 128, COLOR_RED);
  canvas.setTextDatum(TL_DATUM);
  canvas.setTextColor(COLOR_MUTED, COLOR_PANEL);
  canvas.drawString("SYSTEMS DOWN", 373, 60, 2);
  canvas.setTextColor(COLOR_RED, COLOR_PANEL);
  canvas.setTextDatum(MC_DATUM);
  canvas.drawNumber(down, 442, 119, 7);
}

void drawPortraitNetworkPanel(int x, int y) {
  drawPanel(portraitCanvas, x, y, 128, 164,
            WiFi.status() == WL_CONNECTED ? COLOR_CYAN : COLOR_AMBER);
  portraitCanvas.setTextDatum(TL_DATUM);
  portraitCanvas.setTextColor(COLOR_MUTED, COLOR_PANEL);
  portraitCanvas.drawString("NETWORK", x + 13, y + 14, 2);
  portraitCanvas.setTextColor(
      WiFi.status() == WL_CONNECTED ? COLOR_CYAN : COLOR_AMBER, COLOR_PANEL);
  portraitCanvas.drawString(
      WiFi.status() == WL_CONNECTED ? "LINKED" : "OFFLINE", x + 13, y + 42, 4);
  portraitCanvas.setTextColor(TFT_WHITE, COLOR_PANEL);
  const String networkDetail = WiFi.status() == WL_CONNECTED
      ? String(WiFi.RSSI()) + " dBm"
      : "RETRYING";
  portraitCanvas.drawString(networkDetail, x + 13, y + 105, 2);
  portraitCanvas.setTextColor(COLOR_MUTED, COLOR_PANEL);
  portraitCanvas.drawString(String(settings.sites.size()) + " NODES", x + 13, y + 133, 2);
}

void drawPortraitCountPanel(int x, int y, int height, const char *label,
                            size_t count, uint16_t accent) {
  drawPanel(portraitCanvas, x, y, 128, height, accent);
  portraitCanvas.setTextDatum(TL_DATUM);
  portraitCanvas.setTextColor(COLOR_MUTED, COLOR_PANEL);
  portraitCanvas.drawString(label, x + 13, y + 14, 2);
  portraitCanvas.setTextColor(accent, COLOR_PANEL);
  portraitCanvas.setTextDatum(MC_DATUM);
  portraitCanvas.drawNumber(count, x + 64, y + 96, 7);
}

void rotateDashboardToPortrait() {
  const auto *source = reinterpret_cast<const uint16_t *>(canvas.getPointer());
  auto *target = reinterpret_cast<uint16_t *>(portraitCanvas.getPointer());
  const bool clockwise = settings.orientation == Orientation::Down;

  for (int16_t y = 0; y < DASHBOARD_HEIGHT; ++y) {
    for (int16_t x = 0; x < DASHBOARD_WIDTH; ++x) {
      const int16_t targetX = clockwise ? DASHBOARD_HEIGHT - 1 - y : y;
      const int16_t targetY = clockwise ? x : DASHBOARD_WIDTH - 1 - x;
      target[targetY * DASHBOARD_HEIGHT + targetX] = source[y * DASHBOARD_WIDTH + x];
    }
  }
}

void pushFrame(bool redrawPortraitPanels, size_t up = 0, size_t down = 0) {
  if (!isPortraitOrientation()) {
    amoled.pushColors(0, 0, canvas.width(), canvas.height(),
                      reinterpret_cast<uint16_t *>(canvas.getPointer()));
    return;
  }

  rotateDashboardToPortrait();
  if (redrawPortraitPanels) {
    const bool clockwise = settings.orientation == Orientation::Down;
    const int panelX = clockwise ? 64 : 48;
    drawPortraitNetworkPanel(panelX, clockwise ? 12 : 360);
    drawPortraitCountPanel(panelX, 188, 160, "SYSTEMS UP", up, COLOR_GREEN);
    drawPortraitCountPanel(panelX, clockwise ? 360 : 12, 164,
                           "SYSTEMS DOWN", down, COLOR_RED);
  }
  amoled.pushColors(0, 0, portraitCanvas.width(), portraitCanvas.height(),
                    reinterpret_cast<uint16_t *>(portraitCanvas.getPointer()));
}

void drawDashboard() {
  drawBackground();

  canvas.setTextDatum(TL_DATUM);
  canvas.setTextColor(COLOR_CYAN, COLOR_BG);
  canvas.drawString("UPTIME // GRID", 12, 7, 2);
  canvas.setTextColor(TFT_WHITE, COLOR_BG);
  canvas.setTextDatum(TR_DATUM);
  canvas.drawString(currentTimeText(), canvas.width() - 12, 7, 2);

  size_t up = 0;
  size_t down = 0;
  countStates(up, down);
  drawLandscapeStatusPanels(up, down);

  canvas.setTextDatum(TL_DATUM);
  canvas.setTextColor(isScanning ? COLOR_MAGENTA : COLOR_CYAN, COLOR_BG);
  const String cycle = "CYCLE " + String(settings.refreshMinutes) + " MIN";
  const int footerMaxWidth = canvas.width() - 36 - canvas.textWidth(cycle, 2);
  const String footer = truncateToWidth(
      isScanning ? "SCANNING // " + scanLabel : refreshAgeText(), footerMaxWidth, 2);
  canvas.drawString(footer, 12, 194, 2);

  canvas.setTextDatum(TR_DATUM);
  canvas.setTextColor(COLOR_MUTED, COLOR_BG);
  canvas.drawString(cycle, canvas.width() - 12, 194, 2);

  const uint32_t intervalMs = static_cast<uint32_t>(settings.refreshMinutes) * 60000UL;
  const int progress = hasRefreshed
      ? min<int>(canvas.width() - 24, static_cast<uint64_t>(millis() - lastRefreshMs) * (canvas.width() - 24) / intervalMs)
      : 0;
  canvas.drawRect(12, 222, canvas.width() - 24, 5, COLOR_GRID);
  canvas.fillRect(12, 222, progress, 5, COLOR_MAGENTA);

  pushFrame(true, up, down);
}

void drawFatalError() {
  drawBackground();
  canvas.setTextDatum(MC_DATUM);
  canvas.setTextColor(COLOR_RED, COLOR_BG);
  canvas.drawString("CONFIG FAULT", canvas.width() / 2, 85, 4);
  canvas.setTextColor(TFT_WHITE, COLOR_BG);
  canvas.drawString(fatalMessage, canvas.width() / 2, 130, 2);
  canvas.setTextColor(COLOR_MUTED, COLOR_BG);
  canvas.drawString("FLASH LITTLEFS AND REBOOT", canvas.width() / 2, 160, 2);
  pushFrame(false);
}

void connectWifi() {
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(settings.ssid.c_str(), settings.password.c_str());

  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < WIFI_TIMEOUT_MS) {
    drawDashboard();
    delay(250);
  }
  lastWifiAttemptMs = millis();
}

bool checkSite(Site &site) {
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setUserAgent("UptimeGrid/1.0");

  int statusCode = -1;
  if (site.url.startsWith("https://")) {
    WiFiClientSecure client;
    client.setInsecure();
    if (http.begin(client, site.url)) {
      statusCode = http.GET();
      http.end();
    }
  } else if (site.url.startsWith("http://")) {
    WiFiClient client;
    if (http.begin(client, site.url)) {
      statusCode = http.GET();
      http.end();
    }
  }

  site.statusCode = statusCode;
  site.state = uptime_core::classifyHttpStatus(statusCode);
  return site.state == SiteState::Up;
}

void refreshSites() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  isScanning = true;
  for (Site &site : settings.sites) {
    scanLabel = site.name;
    drawDashboard();
    checkSite(site);
    delay(1);
  }
  isScanning = false;
  scanLabel = String();
  hasRefreshed = true;
  lastRefreshMs = millis();
  drawDashboard();
}

}  // namespace

void setup() {
  Serial.begin(115200);

  if (!amoled.beginAMOLED_191(false)) {
    while (true) {
      delay(1000);
    }
  }
  const bool settingsLoaded = loadSettings();
  amoled.setRotation(uptime_core::displayRotation(settings.orientation));

  if (!canvas.createSprite(DASHBOARD_WIDTH, DASHBOARD_HEIGHT)) {
    while (true) {
      delay(1000);
    }
  }
  canvas.setSwapBytes(true);
  if (isPortraitOrientation()) {
    if (!portraitCanvas.createSprite(amoled.width(), amoled.height())) {
      while (true) {
        delay(1000);
      }
    }
    portraitCanvas.setSwapBytes(true);
  }

  if (!settingsLoaded) {
    drawFatalError();
    return;
  }
  amoled.setBrightness(uptime_core::brightnessToByte(settings.displayBrightnessPercent));

  drawDashboard();
  connectWifi();
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  refreshSites();
}

void loop() {
  if (!fatalMessage.isEmpty()) {
    delay(1000);
    return;
  }

  const uint32_t now = millis();
  if (WiFi.status() != WL_CONNECTED &&
      uptime_core::intervalElapsed(now, lastWifiAttemptMs, WIFI_RETRY_MS)) {
    connectWifi();
  }

  const uint32_t refreshIntervalMs = static_cast<uint32_t>(settings.refreshMinutes) * 60000UL;
  if (WiFi.status() == WL_CONNECTED &&
      (!hasRefreshed || uptime_core::intervalElapsed(now, lastRefreshMs, refreshIntervalMs))) {
    refreshSites();
  }

  if (uptime_core::intervalElapsed(now, lastUiDrawMs, UI_REFRESH_MS)) {
    lastUiDrawMs = now;
    drawDashboard();
  }
  delay(10);
}