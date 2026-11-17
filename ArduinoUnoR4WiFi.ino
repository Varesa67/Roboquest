#include <LiquidCrystal_I2C.h>
#include <WebSocketsServer.h>
#include <WiFiS3.h>

constexpr bool USE_LCD = false;

const char* ssid = "HONOR X6c";
const char* password = "SabrinoF";

LiquidCrystal_I2C lcd(0x27, 16, 2);
WebSocketsServer webSocket(81);
WiFiServer httpServer(80);

String lastSign = "None yet";
uint32_t signsReceived = 0;
uint32_t lastReconnectAttempt = 0;
bool wasConnected = false;

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
  Serial.print("[SIGN] Detected: ");
  Serial.print(label);
  Serial.print(" (total: ");
  Serial.print(signsReceived);
  Serial.println(")");

  if (!USE_LCD) return;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Detected sign:");
  lcd.setCursor(0, 1);
  lcd.print(label.substring(0, 16));
}

void webSocketEvent(uint8_t client, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.print("[WS] Client connected: ");
    Serial.println(client);
    return;
  }
  if (type == WStype_DISCONNECTED) {
    delay(5000)
    Serial.print("[WS] Client disconnected: ");
    Serial.println(client);
    return;
  }
  if (type != WStype_TEXT) return;

  if (length < 6 || length > 25 || memcmp(payload, "SIGN:", 5) != 0) {
    Serial.println("[WS] Ignored invalid message; expected SIGN:<label>.");
    return;
  }

  const size_t labelLength = length - 5;
  char label[21];
  memcpy(label, payload + 5, labelLength);
  label[labelLength] = '\0';
  if (!isValidLabel(label, labelLength)) {
    Serial.println("[WS] Rejected invalid sign label.");
    return;
  }

  lastSign = label;
  signsReceived++;
  showSign(lastSign);
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
  if (USE_LCD) {
    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Connecting WiFi");
  } else {
    Serial.println("[Setup] LCD disabled; sign results will print here.");
  }

  Serial.println("[WiFi] Connecting to configured network.");
  WiFi.begin(ssid, password);
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  httpServer.begin();
  lastReconnectAttempt = millis();
  Serial.println("[Setup] WebSocket and status page started.");
}

void loop() {
  webSocket.loop();
  handleHttpClient();

  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    Serial.print("[WiFi] Status page: http://");
    Serial.println(WiFi.localIP());
    Serial.print("[WS] Website URL: ws://");
    Serial.print(WiFi.localIP());
    Serial.println(":81");
    Serial.println("[WiFi] Enter the ws:// address above in the website.");
    if (USE_LCD) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Waiting for");
      lcd.setCursor(0, 1);
      lcd.print("sign...");
    }
  } else if (!connected && wasConnected) {
    Serial.println("[WiFi] Disconnected; retrying...");
  } else if (!connected && millis() - lastReconnectAttempt >= 10000) {
    Serial.println("[WiFi] Reconnecting...");
    WiFi.begin(ssid, password);
    lastReconnectAttempt = millis();
  }
  wasConnected = connected;
}
