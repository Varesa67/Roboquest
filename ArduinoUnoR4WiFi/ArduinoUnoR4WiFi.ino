#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <WebSocketsServer.h>
#include <WiFiS3.h>

constexpr bool USE_LCD = true;
#ifndef USE_TFT
#define USE_TFT 1
#endif

#if USE_TFT
#define USER_SETUP_LOADED
#include "tft_setup.h"
#include <TJpg_Decoder.h>
#include <TFT_eSPI.h>
TFT_eSPI tft;
#endif

const char* platformHost = "sign-platform-relay.taiga-aisaka.workers.dev";
constexpr uint16_t PLATFORM_HTTPS_PORT = 443;
const char* ssid = "HONOR X6c";
const char* password = "SabrinoF";

LiquidCrystal_I2C lcd(0x27, 16, 2);
WebSocketsServer webSocket(81);
WiFiServer httpServer(80);

String lastSign = "None yet";
uint32_t signsReceived = 0;
uint32_t lastReconnectAttempt = 0;
bool wasConnected = false;
uint32_t lastNetworkInfoPrint = 0;
uint32_t lastDisconnectLog = 0;
uint32_t lastCloudPoll = 0;
uint32_t lastEnrollmentAttempt = 0;
uint32_t lastCloudErrorLog = 0;
uint32_t translatedTextShownAt = 0;
bool cloudPairingPending = false;
bool serverConnected = false;
bool translatedTextVisible = false;
bool wifiFailureShown = false;
bool pairingPromptShown = false;
String deviceToken;
String pairingCode;
String deviceId;

struct DeviceSettings {
  char magic[4];
  char token[64];
  char id[40];
  char pairing[16];
};

DeviceSettings deviceSettings;

#if USE_TFT
void showTftHome(const char* message) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(8, 12);
  tft.println("Sign translator");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(8, 48);
  tft.println(message);
}

void showTftSign(const String& label) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(8, 12);
  tft.println("Detected sign");
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(4);
  tft.setCursor(8, 56);
  tft.println(label);
}

void showTftText(const String& text) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(8, 12);
  tft.println("Translation");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(8, 48);
  tft.setTextWrap(true, true);
  tft.println(text);
}
#endif

void showDeviceStatus(const char* line1, const char* line2) {
  translatedTextVisible = false;
  if (USE_LCD) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(String(line1).substring(0, 16));
    lcd.setCursor(0, 1);
    lcd.print(String(line2).substring(0, 16));
  }
#if USE_TFT
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(8, 12);
  tft.println("Sign translator");
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(8, 56);
  tft.println(line1);
  tft.setCursor(8, 88);
  tft.println(line2);
#endif
}

bool isValidLabel(const char* label, size_t length) {
  if (length == 0 || length > 20) return false;

  for (size_t i = 0; i < length; i++) {
    const char character = label[i];
    if (!((character >= 'A' && character <= 'Z') ||
          (character >= 'a' && character <= 'z') ||
          character == ' ' || character == '-')) {
      return false;
    }
  }
  return true;
}

void showSign(const String& label) {
  translatedTextVisible = false;
  Serial.print("[SIGN] Detected: ");
  Serial.print(label);
  Serial.print(" (total: ");
  Serial.print(signsReceived);
  Serial.println(")");

#if USE_TFT
  showTftSign(label);
#endif
  if (!USE_LCD) return;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Detected sign:");
  lcd.setCursor(0, 1);
  lcd.print(label.substring(0, 16));
}

void showTranslatedText(const String& text) {
  Serial.print("[TEXT] Translated: ");
  Serial.println(text);

#if USE_TFT
  showTftText(text);
#endif
  translatedTextShownAt = millis();
  translatedTextVisible = true;
  if (!USE_LCD) return;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(text.substring(0, 16));
  if (text.length() > 16) {
    lcd.setCursor(0, 1);
    lcd.print(text.substring(16, 32));
  }
}

void updateTranslatedTextDisplay() {
  if (!translatedTextVisible || millis() - translatedTextShownAt < 5000) return;
  translatedTextVisible = false;
  showDeviceStatus("Ready", "Waiting for input");
}

void showPairingCode() {
  translatedTextVisible = false;
  cloudPairingPending = pairingCode.length() > 0;
  pairingPromptShown = cloudPairingPending;
  Serial.print("[Pairing] Enter this code in your account: ");
  Serial.println(pairingCode);
  if (USE_LCD) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Pair Arduino:");
    lcd.setCursor(0, 1);
    lcd.print(pairingCode.substring(0, 10));
  }
#if USE_TFT
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(8, 12);
  tft.println("Pair Arduino");
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(3);
  tft.setCursor(8, 72);
  tft.println(pairingCode);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(8, 120);
  tft.println("Enter code on account page");
#endif
}

void loadDeviceSettings() {
  EEPROM.get(0, deviceSettings);
  if (memcmp(deviceSettings.magic, "SGN1", 4) != 0) {
    memset(&deviceSettings, 0, sizeof(deviceSettings));
    return;
  }
  deviceToken = String(deviceSettings.token);
  deviceId = String(deviceSettings.id);
  pairingCode = String(deviceSettings.pairing);
  cloudPairingPending = pairingCode.length() > 0;
}

void saveDeviceSettings() {
  memset(deviceSettings.magic, 0, sizeof(deviceSettings.magic));
  memcpy(deviceSettings.magic, "SGN1", 4);
  memset(deviceSettings.token, 0, sizeof(deviceSettings.token));
  memset(deviceSettings.id, 0, sizeof(deviceSettings.id));
  memset(deviceSettings.pairing, 0, sizeof(deviceSettings.pairing));
  deviceToken.toCharArray(deviceSettings.token, sizeof(deviceSettings.token));
  deviceId.toCharArray(deviceSettings.id, sizeof(deviceSettings.id));
  pairingCode.toCharArray(deviceSettings.pairing, sizeof(deviceSettings.pairing));
  EEPROM.put(0, deviceSettings);
}

void resetCloudDevice() {
  deviceToken = "";
  deviceId = "";
  pairingCode = "";
  cloudPairingPending = false;
  memset(&deviceSettings, 0, sizeof(deviceSettings));
  EEPROM.put(0, deviceSettings);
  lastEnrollmentAttempt = 0;
  Serial.println("[Cloud] Device credentials cleared; a new code will be requested.");
}

bool platformHostConfigured() {
  return platformHost != nullptr && strcmp(platformHost, "your-domain.example") != 0;
}

int readHttpHeaders(WiFiSSLClient& client, int& contentLength, int& frameCount) {
  contentLength = 0;
  frameCount = 0;
  const String statusLine = client.readStringUntil('\n');
  if (!statusLine.startsWith("HTTP/1.")) return 0;
  const int status = statusLine.substring(9, 12).toInt();
  while (client.connected() || client.available()) {
    const String header = client.readStringUntil('\n');
    if (header == "\r" || header.length() == 0) break;
    if (header.startsWith("Content-Length:")) {
      contentLength = header.substring(15).toInt();
    } else if (header.startsWith("X-Frame-Count:")) {
      frameCount = header.substring(14).toInt();
    }
  }
  return status;
}

String readHttpBody(WiFiSSLClient& client, int contentLength, size_t maximum) {
  if (contentLength < 0 || static_cast<size_t>(contentLength) > maximum) return "";
  String body;
  body.reserve(contentLength);
  const uint32_t started = millis();
  while (body.length() < static_cast<size_t>(contentLength) &&
         millis() - started < 10000) {
    while (client.available() && body.length() < static_cast<size_t>(contentLength)) {
      body += static_cast<char>(client.read());
    }
    if (!client.available()) delay(1);
  }
  if (body.length() != static_cast<size_t>(contentLength)) return "";
  return body;
}

bool jsonStringField(const String& json, const char* field, String& value) {
  const String key = String("\"") + field + "\":\"";
  const int valueStart = json.indexOf(key);
  if (valueStart < 0) return false;
  const int start = valueStart + key.length();
  const int end = json.indexOf('"', start);
  if (end < 0) return false;
  value = json.substring(start, end);
  return value.length() > 0;
}

bool enrollDevice() {
  WiFiSSLClient client;
  client.setTimeout(10000);
  if (!client.connect(platformHost, PLATFORM_HTTPS_PORT)) {
    serverConnected = false;
    Serial.println("[Cloud] Secure enrollment connection failed.");
    showDeviceStatus("Couldn't Connect", "To Server. Retry...");
    return false;
  }
  const char* body = "{\"type\":\"uno\"}";
  client.print("POST /api/device/enroll HTTP/1.1\r\nHost: ");
  client.print(platformHost);
  client.print("\r\nUser-Agent: SignPlatform-UNO/1.0\r\nContent-Type: application/json\r\nContent-Length: ");
  client.print(strlen(body));
  client.print("\r\nConnection: close\r\n\r\n");
  client.print(body);

  int contentLength = 0;
  int frameCount = 0;
  const int status = readHttpHeaders(client, contentLength, frameCount);
  const String response = readHttpBody(client, contentLength, 512);
  client.stop();
  if (status != 201) {
    serverConnected = false;
    Serial.print("[Cloud] Enrollment failed, HTTP ");
    Serial.println(status);
    showDeviceStatus("Couldn't Connect", "To Server. Retry...");
    return false;
  }
  if (!jsonStringField(response, "deviceId", deviceId) ||
      !jsonStringField(response, "token", deviceToken) ||
      !jsonStringField(response, "pairingCode", pairingCode)) {
    Serial.println("[Cloud] Enrollment response was incomplete.");
    deviceToken = "";
    pairingCode = "";
    return false;
  }
  serverConnected = true;
  saveDeviceSettings();
  showPairingCode();
  Serial.println("[Cloud] Device enrolled; enter the pairing code in your account.");
  return true;
}

#if USE_TFT
bool tftOutput(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t* bitmap) {
  if (x >= tft.width() || y >= tft.height()) return false;
  tft.pushImage(x, y, width, height, bitmap);
  return true;
}

bool fetchAnimationFrame(uint8_t frame, uint8_t* jpeg, size_t capacity,
                         size_t& jpegLength, int& frameCount) {
  WiFiSSLClient client;
  client.setTimeout(10000);
  if (!client.connect(platformHost, PLATFORM_HTTPS_PORT)) return false;
  client.print("GET /api/device/animation/hello/");
  client.print(frame);
  client.print(" HTTP/1.1\r\nHost: ");
  client.print(platformHost);
  client.print("\r\nUser-Agent: SignPlatform-UNO/1.0\r\nAuthorization: Bearer ");
  client.print(deviceToken);
  client.print("\r\nConnection: close\r\n\r\n");

  int contentLength = 0;
  const int status = readHttpHeaders(client, contentLength, frameCount);
  if (status != 200 || contentLength <= 0 ||
      static_cast<size_t>(contentLength) > capacity) {
    client.stop();
    return false;
  }
  size_t received = 0;
  const uint32_t started = millis();
  while (received < static_cast<size_t>(contentLength) &&
         millis() - started < 10000) {
    while (client.available() && received < static_cast<size_t>(contentLength)) {
      const int count = client.read(jpeg + received, contentLength - received);
      if (count > 0) received += count;
    }
    if (!client.available()) delay(1);
  }
  client.stop();
  jpegLength = received;
  return received == static_cast<size_t>(contentLength);
}

void playHelloAnimation() {
  if (!platformHostConfigured() || deviceToken.length() == 0) {
    Serial.println("[GIF] Pair the Arduino and configure its HTTPS hostname first.");
    showTftHome("Pair device first");
    return;
  }
  static uint8_t jpeg[4096];
  tft.fillScreen(TFT_BLACK);
  TJpgDec.setJpgScale(1);
  for (uint8_t frame = 0; frame < 32; frame++) {
    size_t jpegLength = 0;
    int frameCount = 0;
    if (!fetchAnimationFrame(frame, jpeg, sizeof(jpeg), jpegLength, frameCount)) {
      Serial.println("[GIF] Could not fetch the next animation frame.");
      return;
    }
    if (frame == 0 && frameCount > 0 && frameCount < 32) {
      tft.fillScreen(TFT_BLACK);
    }
    TJpgDec.drawJpg(0, 0, jpeg, jpegLength);
    if (frameCount > 0 && frame + 1 >= frameCount) break;
    delay(85);
  }
}
#endif

void processCommand(const String& command) {
  if (command.startsWith("TEXT:") && command.length() > 5 &&
      command.length() <= 37) {
    const String text = command.substring(5);
    for (size_t i = 0; i < text.length(); i++) {
      if (text[i] < 0x20 || text[i] > 0x7E) {
        Serial.println("[Device] Rejected invalid translated text.");
        return;
      }
    }
    lastSign = text;
    showTranslatedText(lastSign);
    return;
  }

  if (command == "GIF:hello") {
#if USE_TFT
    playHelloAnimation();
#else
    Serial.println("[GIF] Enable USE_TFT to display streamed animation frames.");
#endif
    return;
  }

  if (!command.startsWith("SIGN:")) {
    Serial.println("[Device] Ignored unsupported command.");
    return;
  }
  const String label = command.substring(5);
  if (!isValidLabel(label.c_str(), label.length())) {
    Serial.println("[Device] Rejected invalid sign label.");
    return;
  }
  lastSign = label;
  signsReceived++;
  showSign(lastSign);
}

void pollCloudDevice() {
  if (!platformHostConfigured() || WiFi.status() != WL_CONNECTED) return;
  if (deviceToken.length() == 0) {
    if (lastEnrollmentAttempt == 0 || millis() - lastEnrollmentAttempt >= 30000) {
      lastEnrollmentAttempt = millis();
      enrollDevice();
    }
    return;
  }

  WiFiSSLClient client;
  client.setTimeout(10000);
  if (!client.connect(platformHost, PLATFORM_HTTPS_PORT)) {
    serverConnected = false;
    if (lastCloudErrorLog == 0 || millis() - lastCloudErrorLog >= 15000) {
      Serial.println("[Cloud] Secure connection failed; will retry.");
      lastCloudErrorLog = millis();
      showDeviceStatus("Couldn't Connect", "To Server. Retry...");
    }
    return;
  }
  client.print("GET /api/device/poll HTTP/1.1\r\nHost: ");
  client.print(platformHost);
  client.print("\r\nUser-Agent: SignPlatform-UNO/1.0\r\nAuthorization: Bearer ");
  client.print(deviceToken);
  client.print("\r\nConnection: close\r\n\r\n");
  int contentLength = 0;
  int frameCount = 0;
  const int status = readHttpHeaders(client, contentLength, frameCount);
  const String command = status == 200 ? readHttpBody(client, contentLength, 48) : "";
  client.stop();

  if (status == 200 && command.length() > 0) {
    serverConnected = true;
    processCommand(command);
    pairingCode = "";
    cloudPairingPending = false;
    saveDeviceSettings();
  } else if (status == 204) {
    const bool wasServerConnected = serverConnected;
    serverConnected = true;
    if (!wasServerConnected && pairingCode.length() == 0) {
      showDeviceStatus("Connected", "Successfully");
    }
    if (pairingCode.length() > 0) {
      pairingCode = "";
      cloudPairingPending = false;
      saveDeviceSettings();
      Serial.println("[Cloud] Device is paired and connected.");
      showDeviceStatus("Connected", "Successfully");
    }
  } else if (status == 403) {
    serverConnected = true;
    if (pairingCode.length() > 0 && !pairingPromptShown) showPairingCode();
  } else if (status >= 400 && status != 403) {
    serverConnected = false;
    if (lastCloudErrorLog == 0 || millis() - lastCloudErrorLog >= 15000) {
      Serial.print("[Cloud] Poll failed, HTTP ");
      Serial.println(status);
      lastCloudErrorLog = millis();
      showDeviceStatus("Couldn't Connect", "To Server. Retry...");
    }
  }
}

void webSocketEvent(uint8_t client, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.print("[WS] Client connected: ");
    Serial.println(client);
    return;
  }
  if (type == WStype_DISCONNECTED) {
    if (lastDisconnectLog == 0 || millis() - lastDisconnectLog >= 5000) {
      Serial.print("[WS] Client disconnected: ");
      Serial.println(client);
      lastDisconnectLog = millis();
    }
    return;
  }
  if (type != WStype_TEXT) return;

  if (length == 0 || length > 40) {
    Serial.println("[WS] Ignored invalid message.");
    return;
  }
  char message[41];
  memcpy(message, payload, length);
  message[length] = '\0';
  processCommand(String(message));
}

void sendHttpResponse(WiFiClient& client, const char* contentType, const String& body) {
  client.println("HTTP/1.1 200 OK");
  client.print("Content-Type: ");
  client.println(contentType);
  client.println("Connection: close");
  client.println();
  client.print(body);
  client.stop();
}

void handleHttpClient() {
  WiFiClient client = httpServer.available();
  if (!client) return;

  client.setTimeout(100);
  const String requestLine = client.readStringUntil('\n');
  while (client.connected()) {
    const String header = client.readStringUntil('\n');
    if (header == "\r" || header.length() == 0) break;
  }

  if (requestLine.startsWith("GET /status ")) {
    const String response =
        "{\"sign\":\"" + lastSign + "\",\"count\":" + String(signsReceived) + "}";
    sendHttpResponse(client, "application/json", response);
    return;
  }

  String page =
      "<!doctype html><html><head><meta name='viewport' "
      "content='width=device-width,initial-scale=1'><title>Uno R4 Sign Receiver"
      "</title></head><body><h1>Arduino UNO R4 WiFi Sign Receiver</h1><p>WebSocket: <code>ws://";
  page += WiFi.localIP().toString();
  page +=
      ":81</code></p><p>Last sign: <strong id='sign'>Loading...</strong></p>"
      "<p>Signs received: <span id='count'>0</span></p>"
      "<script>async function update(){try{const r=await fetch('/status');"
      "const d=await r.json();document.getElementById('sign').textContent=d.sign;"
      "document.getElementById('count').textContent=d.count;"
      "}catch(e){document.getElementById('sign').textContent='Board unavailable';}}"
      "update();setInterval(update,1000);</script></body></html>";
  sendHttpResponse(client, "text/html", page);
}

void setup() {
  Serial.begin(115200);
  Serial.println("[Setup] Starting Arduino UNO R4 WiFi sign receiver.");
  loadDeviceSettings();
#if USE_TFT
  tft.init();
  tft.setRotation(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tftOutput);
  showTftHome("Connecting WiFi...");
#endif
  if (USE_LCD) {
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Connecting WiFi");
  } else {
    Serial.println("[Setup] LCD disabled; sign results will print here.");
  }

  Serial.println("[WiFi] Connecting to configured network.");
  if (!platformHostConfigured()) {
    Serial.println("[Cloud] Set platformHost to your stable HTTPS hostname in this sketch.");
  }
  WiFi.begin(ssid, password);
  lastCloudPoll = millis();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  httpServer.begin();
  lastReconnectAttempt = millis();
  Serial.println("[Setup] WebSocket and status page started.");
}

void loop() {
  while (Serial.available()) {
    const char input = static_cast<char>(Serial.read());
    if (input == 'r' || input == 'R') {
      resetCloudDevice();
      while (Serial.available()) Serial.read();
    }
  }
  webSocket.loop();
  handleHttpClient();

  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && (!wasConnected || millis() - lastNetworkInfoPrint >= 10000)) {
    Serial.print("[WiFi] Status page: http://");
    Serial.println(WiFi.localIP());
    Serial.print("[WS] Website URL: ws://");
    Serial.print(WiFi.localIP());
    Serial.println(":81");
    Serial.println("[WiFi] Enter the ws:// address above in the website.");
    lastNetworkInfoPrint = millis();
    if (!wasConnected) {
      wifiFailureShown = false;
      serverConnected = false;
      lastCloudPoll = millis();
      showDeviceStatus("Connected", "Successfully");
      if (!platformHostConfigured()) {
        showDeviceStatus("Set HTTPS host", "in Arduino sketch");
      }
    }
  } else if (!connected && wasConnected) {
    Serial.println("[WiFi] Disconnected; retrying...");
    showDeviceStatus("WiFi Connection", "Failed. Retrying...");
    wifiFailureShown = true;
  } else if (!connected && millis() - lastReconnectAttempt >= 10000) {
    Serial.println("[WiFi] Reconnecting...");
    if (!wifiFailureShown) {
      showDeviceStatus("WiFi Connection", "Failed. Retrying...");
      wifiFailureShown = true;
    }
    WiFi.begin(ssid, password);
    lastReconnectAttempt = millis();
  }
  wasConnected = connected;

  if (connected && millis() - lastCloudPoll >= 3000) {
    lastCloudPoll = millis();
    pollCloudDevice();
  }
  updateTranslatedTextDisplay();
}
