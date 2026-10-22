#include <SoftwareSerial.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

const byte ESP32_UART_RX_PIN = A0;
SoftwareSerial espUart(ESP32_UART_RX_PIN, A1);
LiquidCrystal_I2C lcd(0x27, 16, 2);

String uartMessage;
bool discardingMessage = false;

void handleUartMessage(const String& message) {
  if (!message.startsWith("SIGN:")) return;

  const String label = message.substring(5);
  bool validLabel = label.length() > 0 && label.length() <= 20;
  for (size_t i = 0; i < label.length(); i++) {
    const char character = label.charAt(i);
    if (!((character >= 'A' && character <= 'Z') ||
          (character >= 'a' && character <= 'z') ||
          character == ' ' || character == '-')) {
      validLabel = false;
      break;
    }
  }
  if (!validLabel) return;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Detected sign:");
  lcd.setCursor(0, 1);
  lcd.print(label.substring(0, 16));
  Serial.print("Displayed sign: ");
  Serial.println(label);
}

void readEsp32Uart() {
  while (espUart.available() > 0) {
    const char incoming = static_cast<char>(espUart.read());
    if (incoming == '\n') {
      if (!discardingMessage && uartMessage.length() > 0) {
        handleUartMessage(uartMessage);
      }
      uartMessage = "";
      discardingMessage = false;
    } else if (incoming != '\r' && !discardingMessage && uartMessage.length() < 26) {
      uartMessage += incoming;
    } else if (incoming != '\r') {
      uartMessage = "";
      discardingMessage = true;
    }
  }
}

void setup() {
  Serial.begin(115200);
  espUart.begin(9600);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Waiting for");
  lcd.setCursor(0, 1);
  lcd.print("sign...");
  Serial.println("Arduino ready; waiting for sign messages.");
}

void loop() {
  readEsp32Uart();
}
