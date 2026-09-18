#include <Arduino.h>
#include <HTTPClient.h>
#include <LilyGo_AMOLED.h>
#include <LittleFS.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <vector>

namespace {

constexpr uint32_t WIFI_TIMEOUT_MS = 20000;
constexpr uint32_t WIFI_RETRY_MS = 15000;
constexpr uint32_t HTTP_TIMEOUT_MS = 8000;
constexpr uint32_t UI_REFRESH_MS = 1000;

constexpr uint16_t COLOR_BG = 0x0021;
constexpr uint16_t COLOR_PANEL = 0x0863;
constexpr uint16_t COLOR_GRID = 0x10C6;
constexpr uint16_t COLOR_CYAN = 0x07FF;
constexpr uint16_t COLOR_MAGENTA = 0xF81F;
constexpr uint16_t COLOR_GREEN = 0x07E0;
constexpr uint16_t COLOR_RED = 0xF800;
constexpr uint16_t COLOR_AMBER = 0xFD20;
constexpr uint16_t COLOR_MUTED = 0x7BEF;

enum class SiteState { Unknown, Up, Down };

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
  std::vector<Site> sites;
};

LilyGo_Class amoled;
TFT_eSPI tft;
TFT_eSprite canvas(&tft);
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

void drawPanel(int x, int y, int width, int height, uint16_t accent) {
  canvas.fillRect(x, y, width, height, COLOR_PANEL);
  canvas.drawRect(x, y, width, height, accent);
  canvas.fillRect(x, y, 4, height, accent);
  canvas.drawFastHLine(x + 9, y + 5, 20, accent);
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
  up = 0;
  down = 0;
  for (const Site &site : settings.sites) {
    up += site.state == SiteState::Up;
    down += site.state == SiteState::Down;
  }
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

  drawPanel(12, 48, 164, 128, WiFi.status() == WL_CONNECTED ? COLOR_CYAN : COLOR_AMBER);
  canvas.setTextDatum(TL_DATUM);
  canvas.setTextColor(COLOR_MUTED, COLOR_PANEL);
  canvas.drawString("NETWORK", 25, 60, 2);
  canvas.setTextColor(WiFi.status() == WL_CONNECTED ? COLOR_CYAN : COLOR_AMBER, COLOR_PANEL);
  canvas.drawString(WiFi.status() == WL_CONNECTED ? "LINKED" : "OFFLINE", 25, 85, 4);
  canvas.setTextColor(TFT_WHITE, COLOR_PANEL);
  String networkDetail = WiFi.status() == WL_CONNECTED
      ? String(WiFi.RSSI()) + " dBm"
      : "RETRYING";
  canvas.drawString(networkDetail, 25, 130, 2);
  canvas.setTextColor(COLOR_MUTED, COLOR_PANEL);
  canvas.drawString(String(settings.sites.size()) + " NODES", 25, 152, 2);

  drawPanel(188, 48, 160, 128, COLOR_GREEN);
  canvas.setTextColor(COLOR_MUTED, COLOR_PANEL);
  canvas.drawString("SYSTEMS UP", 201, 60, 2);
  canvas.setTextColor(COLOR_GREEN, COLOR_PANEL);
  canvas.setTextDatum(MC_DATUM);
  canvas.drawNumber(up, 268, 119, 7);

  drawPanel(360, 48, 164, 128, COLOR_RED);
  canvas.setTextDatum(TL_DATUM);
  canvas.setTextColor(COLOR_MUTED, COLOR_PANEL);
  canvas.drawString("SYSTEMS DOWN", 373, 60, 2);
  canvas.setTextColor(COLOR_RED, COLOR_PANEL);
  canvas.setTextDatum(MC_DATUM);
  canvas.drawNumber(down, 442, 119, 7);

  canvas.setTextDatum(TL_DATUM);
  canvas.setTextColor(isScanning ? COLOR_MAGENTA : COLOR_CYAN, COLOR_BG);
  const String footer = isScanning ? "SCANNING // " + scanLabel : refreshAgeText();
  canvas.drawString(footer, 12, 194, 2);

  canvas.setTextDatum(TR_DATUM);
  canvas.setTextColor(COLOR_MUTED, COLOR_BG);
  canvas.drawString("CYCLE " + String(settings.refreshMinutes) + " MIN", canvas.width() - 12, 194, 2);

  const uint32_t intervalMs = static_cast<uint32_t>(settings.refreshMinutes) * 60000UL;
  const int progress = hasRefreshed
      ? min<int>(canvas.width() - 24, static_cast<uint64_t>(millis() - lastRefreshMs) * (canvas.width() - 24) / intervalMs)
      : 0;
  canvas.drawRect(12, 222, canvas.width() - 24, 5, COLOR_GRID);
  canvas.fillRect(12, 222, progress, 5, COLOR_MAGENTA);

  amoled.pushColors(0, 0, canvas.width(), canvas.height(),
                    reinterpret_cast<uint16_t *>(canvas.getPointer()));
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
  amoled.pushColors(0, 0, canvas.width(), canvas.height(),
                    reinterpret_cast<uint16_t *>(canvas.getPointer()));
}

void connectWifi() {
  lastWifiAttemptMs = millis();
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(settings.ssid.c_str(), settings.password.c_str());

  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < WIFI_TIMEOUT_MS) {
    drawDashboard();
    delay(250);
  }
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
  site.state = statusCode >= 200 && statusCode < 400 ? SiteState::Up : SiteState::Down;
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
  amoled.setRotation(0);

  if (!canvas.createSprite(amoled.width(), amoled.height())) {
    while (true) {
      delay(1000);
    }
  }
  canvas.setSwapBytes(true);

  if (!loadSettings()) {
    drawFatalError();
    return;
  }
  const uint8_t brightness = static_cast<uint8_t>(
      (static_cast<uint16_t>(settings.displayBrightnessPercent) * 255U + 50U) / 100U);
  amoled.setBrightness(brightness);

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
  if (WiFi.status() != WL_CONNECTED && now - lastWifiAttemptMs >= WIFI_RETRY_MS) {
    connectWifi();
  }

  const uint32_t refreshIntervalMs = static_cast<uint32_t>(settings.refreshMinutes) * 60000UL;
  if (WiFi.status() == WL_CONNECTED &&
      (!hasRefreshed || now - lastRefreshMs >= refreshIntervalMs)) {
    refreshSites();
  }

  if (now - lastUiDrawMs >= UI_REFRESH_MS) {
    lastUiDrawMs = now;
    drawDashboard();
  }
  delay(10);
}