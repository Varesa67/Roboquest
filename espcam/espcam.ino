#include <Arduino.h>
#include "esp_camera.h"
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

// ===========================
// Select camera model in board_config.h
// ===========================
#include "board_config.h"

// ===========================
// Enter your WiFi credentials
// ===========================
const char *ssid = "HONOR X6c";
const char *password = "SabrinoF";
const char *PLATFORM_HOST = "sign-platform-relay.taiga-aisaka.workers.dev";
constexpr uint16_t PLATFORM_PORT = 443;
constexpr size_t MAX_FRAME_BYTES = 250000;
constexpr uint32_t CAMERA_FRAME_UPLOAD_INTERVAL_MS = 333;

void startCameraServer();
void setupLedFlash();
String cameraPairingCode;
bool cameraPaired = false;

Preferences preferences;
String cameraToken;
uint32_t lastEnrollmentAttempt = 0;
uint32_t lastPairingPoll = 0;
uint32_t lastDemandPoll = 0;
uint32_t lastFrameUpload = 0;
uint32_t lastWiFiAttempt = 0;
bool uploadFrames = false;

int readHttpHeaders(WiFiClientSecure &client, int &contentLength) {
  contentLength = 0;
  const String statusLine = client.readStringUntil('\n');
  if (!statusLine.startsWith("HTTP/1.")) return 0;
  const int status = statusLine.substring(9, 12).toInt();
  while (client.connected() || client.available()) {
    const String header = client.readStringUntil('\n');
    if (header == "\r" || header.length() == 0) break;
    if (header.startsWith("Content-Length:")) {
      contentLength = header.substring(15).toInt();
    }
  }
  return status;
}

String readHttpBody(WiFiClientSecure &client, int contentLength, size_t maximum) {
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
  return body.length() == static_cast<size_t>(contentLength) ? body : "";
}

bool jsonStringField(const String &json, const char *field, String &value) {
  const String key = String("\"") + field + "\"";
  const int valueStart = json.indexOf(key);
  if (valueStart < 0) return false;
  int start = valueStart + key.length();
  while (start < json.length() && json[start] <= ' ') start++;
  if (start >= json.length() || json[start++] != ':') return false;
  while (start < json.length() && json[start] <= ' ') start++;
  if (start >= json.length() || json[start++] != '"') return false;
  const int end = json.indexOf('"', start);
  if (end < 0) return false;
  value = json.substring(start, end);
  return value.length() > 0;
}

bool connectPlatform(WiFiClientSecure &client) {
  if (WiFi.status() != WL_CONNECTED ||
      strcmp(PLATFORM_HOST, "your-domain.example") == 0) return false;
  client.useBuiltinCACertBundle();
  client.setTimeout(10000);
  client.setHandshakeTimeout(15);
  return client.connect(PLATFORM_HOST, PLATFORM_PORT);
}

void loadCameraCredentials() {
  preferences.begin("sign-camera", false);
  cameraToken = preferences.getString("token", "");
  cameraPairingCode = preferences.getString("pair", "");
  cameraPaired = preferences.getBool("paired", false);
}

void resetCameraCredentials() {
  preferences.clear();
  cameraToken = "";
  cameraPairingCode = "";
  cameraPaired = false;
  uploadFrames = false;
  lastEnrollmentAttempt = 0;
  Serial.println("[Cloud] Credentials cleared; enrolling again.");
}

bool enrollCamera() {
  WiFiClientSecure client;
  if (!connectPlatform(client)) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("[Cloud] Check PLATFORM_HOST and HTTPS server availability.");
    }
    return false;
  }
  const char *body = "{\"type\":\"camera\"}";
  client.print("POST /api/device/enroll HTTP/1.1\r\nHost: ");
  client.print(PLATFORM_HOST);
  client.print("\r\nUser-Agent: SignPlatform-ESP32CAM/1.0\r\nContent-Type: application/json\r\nContent-Length: ");
  client.print(strlen(body));
  client.print("\r\nConnection: close\r\n\r\n");
  client.print(body);
  int contentLength = 0;
  const int status = readHttpHeaders(client, contentLength);
  const String response = readHttpBody(client, contentLength, 512);
  client.stop();
  if (status != 201 ||
      !jsonStringField(response, "token", cameraToken) ||
      !jsonStringField(response, "pairingCode", cameraPairingCode)) {
    Serial.printf("[Cloud] Camera enrollment failed, HTTP %d.\n", status);
    return false;
  }
  preferences.putString("token", cameraToken);
  preferences.putString("pair", cameraPairingCode);
  preferences.putBool("paired", false);
  cameraPaired = false;
  Serial.println("[Pairing] Open the camera's /pairing page on the local network.");
  Serial.print("[Pairing] ESP32-CAM code: ");
  Serial.println(cameraPairingCode);
  return true;
}

int cameraGet(const char *path, String &body) {
  WiFiClientSecure client;
  if (!connectPlatform(client)) return 0;
  client.print("GET ");
  client.print(path);
  client.print(" HTTP/1.1\r\nHost: ");
  client.print(PLATFORM_HOST);
  client.print("\r\nUser-Agent: SignPlatform-ESP32CAM/1.0\r\nAuthorization: Bearer ");
  client.print(cameraToken);
  client.print("\r\nConnection: close\r\n\r\n");
  int contentLength = 0;
  const int status = readHttpHeaders(client, contentLength);
  body = readHttpBody(client, contentLength, 512);
  client.stop();
  return status;
}

void checkCameraPairing() {
  String response;
  const int status = cameraGet("/api/device/poll", response);
  if (status == 204) {
    if (!cameraPaired) Serial.println("[Cloud] ESP32-CAM paired to account.");
    cameraPaired = true;
    cameraPairingCode = "";
    preferences.putString("pair", "");
    preferences.putBool("paired", true);
  } else if (status == 403) {
    cameraPaired = false;
    preferences.putBool("paired", false);
  } else if (status == 0) {
    Serial.println("[Cloud] Could not reach HTTPS server; will retry.");
  }
}

bool cameraUploadDemand() {
  String response;
  const int status = cameraGet("/api/device/camera/demand", response);
  if (status != 200) {
    if (status == 0) Serial.println("[Cloud] Camera demand check failed; retrying.");
    return false;
  }
  return response.indexOf("\"active\":true") >= 0 ||
      response.indexOf("\"active\": true") >= 0;
}

bool writeCameraBytes(WiFiClientSecure &client, const uint8_t *bytes, size_t length) {
  size_t sent = 0;
  while (sent < length) {
    const size_t written = client.write(bytes + sent, length - sent);
    if (written == 0) return false;
    sent += written;
  }
  return true;
}

bool uploadCameraFrame(const camera_fb_t *frame) {
  if (!frame || frame->format != PIXFORMAT_JPEG ||
      frame->len == 0 || frame->len > MAX_FRAME_BYTES) return false;
  WiFiClientSecure client;
  if (!connectPlatform(client)) return false;
  client.print("POST /api/device/camera/frame HTTP/1.1\r\nHost: ");
  client.print(PLATFORM_HOST);
  client.print("\r\nUser-Agent: SignPlatform-ESP32CAM/1.0\r\nAuthorization: Bearer ");
  client.print(cameraToken);
  client.print("\r\nContent-Type: image/jpeg\r\nContent-Length: ");
  client.print(frame->len);
  client.print("\r\nConnection: close\r\n\r\n");
  if (!writeCameraBytes(client, frame->buf, frame->len)) {
    client.stop();
    return false;
  }
  int responseLength = 0;
  const int status = readHttpHeaders(client, responseLength);
  client.stop();
  return status == 204;
}

void captureAndUploadFrame() {
  camera_fb_t *frame = esp_camera_fb_get();
  if (!frame) {
    Serial.println("[Camera] Frame capture failed.");
    return;
  }
  if (!uploadCameraFrame(frame)) {
    Serial.println("[Cloud] Frame upload failed; will retry.");
  }
  esp_camera_fb_return(frame);
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  //config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);        // flip it back
    s->set_brightness(s, 1);   // up the brightness just a bit
    s->set_saturation(s, -2);  // lower the saturation
  }
  // drop down frame size for higher initial frame rate
  if (config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

// Setup LED FLash if LED pin is defined in camera_pins.h
#if defined(LED_GPIO_NUM)
  setupLedFlash();
#endif

  loadCameraCredentials();
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  Serial.print("WiFi connecting");
  const uint32_t wifiStarted = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStarted < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.print("Camera IP: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi unavailable; retrying in background.");
  }

  startCameraServer();

  if (cameraPairingCode.length()) {
    Serial.print("[Pairing] Camera code: ");
    Serial.println(cameraPairingCode);
  }
  Serial.println("Camera pairing page: http://<camera-ip>/pairing");
}

void loop() {
  while (Serial.available()) {
    const char input = static_cast<char>(Serial.read());
    if (input == 'r' || input == 'R') resetCameraCredentials();
  }

  if (WiFi.status() != WL_CONNECTED) {
    uploadFrames = false;
    if (lastWiFiAttempt == 0 || millis() - lastWiFiAttempt >= 10000) {
      lastWiFiAttempt = millis();
      WiFi.disconnect();
      WiFi.begin(ssid, password);
      Serial.println("[WiFi] Reconnecting...");
    }
    delay(100);
    return;
  }

  if (cameraToken.length() == 0) {
    if (lastEnrollmentAttempt == 0 ||
        millis() - lastEnrollmentAttempt >= 30000) {
      lastEnrollmentAttempt = millis();
      enrollCamera();
    }
    delay(100);
    return;
  }

  if (!cameraPaired) {
    if (lastPairingPoll == 0 || millis() - lastPairingPoll >= 5000) {
      lastPairingPoll = millis();
      checkCameraPairing();
    }
    delay(100);
    return;
  }

  if (millis() - lastDemandPoll >= 3000) {
    lastDemandPoll = millis();
    uploadFrames = cameraUploadDemand();
  }
  if (uploadFrames &&
      millis() - lastFrameUpload >= CAMERA_FRAME_UPLOAD_INTERVAL_MS) {
    lastFrameUpload = millis();
    captureAndUploadFrame();
  }
  delay(10);
}
