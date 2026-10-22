#include <WiFi.h>
#include <WebSocketsServer.h>
#include <WebServer.h>

const char* ssid = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";
const uint16_t WEBSOCKET_PORT = 81;

WebSocketsServer webSocket(WEBSOCKET_PORT);
WebServer httpServer(80);
String lastSign = "None yet";
uint32_t signsReceived = 0;
bool wasConnected = false;

bool isValidLabel(const String& label) {
  if (label.length() == 0 || label.length() > 20) return false;

  for (size_t i = 0; i < label.length(); i++) {
    const char character = label.charAt(i);
    if (!((character >= 'A' && character <= 'Z') ||
          (character >= 'a' && character <= 'z') ||
          character == ' ' || character == '-')) {
      return false;
    }
  }
  return true;
}

void handleHome() {
  String page =
      "<!doctype html><html><head><meta name='viewport' "
      "content='width=device-width,initial-scale=1'><title>ESP32 Sign Receiver"
      "</title></head><body><h1>ESP32 Sign Receiver</h1><p>WebSocket: <code>ws://";
  page += WiFi.localIP().toString();
  page +=
      ":81</code></p><p>Last sign: <strong id='sign'>Loading...</strong></p>"
      "<p>Signs received: <span id='count'>0</span></p>"
      "<script>async function update(){try{const r=await fetch('/status');"
      "const d=await r.json();document.getElementById('sign').textContent=d.sign;"
      "document.getElementById('count').textContent=d.count;"
      "}catch(e){document.getElementById('sign').textContent='ESP32 unavailable';}}"
      "update();setInterval(update,1000);</script></body></html>";
  httpServer.send(200, "text/html", page);
}

void handleStatus() {
  const String response =
      "{\"sign\":\"" + lastSign + "\",\"count\":" + String(signsReceived) + "}";
  httpServer.send(200, "application/json", response);
}

void webSocketEvent(uint8_t client, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.printf("[WS] Client %u connected.\n", client);
    return;
  }
  if (type == WStype_DISCONNECTED) {
    Serial.printf("[WS] Client %u disconnected.\n", client);
    return;
  }
  if (type != WStype_TEXT) return;

  String text;
  text.reserve(length);
  for (size_t i = 0; i < length; i++) {
    text += static_cast<char>(payload[i]);
  }
  if (!text.startsWith("SIGN:")) {
    Serial.println("[WS] Ignored message; expected SIGN:<label>.");
    return;
  }

  const String label = text.substring(5);
  if (!isValidLabel(label)) {
    Serial.println("[WS] Rejected invalid sign label.");
    return;
  }

  lastSign = label;
  signsReceived++;
  Serial.printf("[WS] Received sign: %s\n", label.c_str());
  Serial2.println("SIGN:" + label);
  Serial.printf("[UART] Sent to Arduino: SIGN:%s\n", label.c_str());
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, 16, 17);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  Serial.printf("[WiFi] Connecting to %s\n", ssid);
  WiFi.begin(ssid, password);

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  httpServer.on("/", HTTP_GET, handleHome);
  httpServer.on("/status", HTTP_GET, handleStatus);
  httpServer.begin();
  Serial.println("[Setup] WebSocket and status page started.");
}

void loop() {
  webSocket.loop();
  httpServer.handleClient();

  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected && !wasConnected) {
    Serial.print("[WiFi] Status page: http://");
    Serial.println(WiFi.localIP());
    Serial.print("[WS] Website URL: ws://");
    Serial.print(WiFi.localIP());
    Serial.println(":81");
  } else if (!connected && wasConnected) {
    Serial.println("[WiFi] Disconnected; reconnecting...");
  }
  wasConnected = connected;
}
