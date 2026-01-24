#include <SoftwareSerial.h>

/**
 * Nextion Display Interface for Arduino
 *
 * This module handles communication with a Nextion HMI display.
 * It uses SoftwareSerial on pins 16 (A2) and 17 (A3) to avoid
 * conflicts with the existing HVAC controller pin assignments.
 */

// SoftwareSerial pins: RX=16 (A2), TX=17 (A3)
SoftwareSerial nextion(16, 17);

// Nextion protocol termination sequence
const uint8_t nextionTerminator[] = { 0xFF, 0xFF, 0xFF };

// ====================== Setup ====================== //
void setup() {
  // Debug serial
  Serial.begin(9600);

  // Nextion serial
  nextion.begin(9600);

  Serial.println(F("Nextion Interface Initialized"));
}

// ====================== Main Loop ====================== //
void loop() {
  // Handle incoming data from Nextion
  receiveNextionData();

  // Periodic update demonstration
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 5000) {
    updateNextionText("t0", "System OK");
    updateNextionNumber("n0", 25);
    lastUpdate = millis();
    Serial.println(F("Sent periodic update to Nextion"));
  }
}

// ====================== Outgoing Communication ====================== //

/**
 * Sends a raw command string to the Nextion display.
 */
void sendNextionCommand(String cmd) {
  nextion.print(cmd);
  nextion.write(nextionTerminator, 3);
}

/**
 * Updates a text component on the Nextion display.
 * Example: updateNextionText("t0", "Hello") -> t0.txt="Hello"
 */
void updateNextionText(String component, String value) {
  String cmd = component + ".txt=\"" + value + "\"";
  sendNextionCommand(cmd);
}

/**
 * Updates a numeric component on the Nextion display.
 * Example: updateNextionNumber("n0", 123) -> n0.val=123
 */
void updateNextionNumber(String component, int value) {
  String cmd = component + ".val=" + String(value);
  sendNextionCommand(cmd);
}

// ====================== Incoming Communication ====================== //

/**
 * Reads and parses incoming data from the Nextion display.
 * Focuses on parsing Touch Events (0x65).
 */
void receiveNextionData() {
  if (nextion.available() >= 7) {
    if (nextion.peek() == 0x65) {
      uint8_t buffer[7];
      nextion.readBytes(buffer, 7);

      // Verify termination bytes
      if (buffer[4] == 0xFF && buffer[5] == 0xFF && buffer[6] == 0xFF) {
        uint8_t pageId = buffer[1];
        uint8_t componentId = buffer[2];
        uint8_t eventType = buffer[3]; // 0x01 Press, 0x00 Release

        Serial.print(F("Nextion Touch Event: Page "));
        Serial.print(pageId);
        Serial.print(F(", Component "));
        Serial.print(componentId);
        Serial.println(eventType == 0x01 ? F(" Pressed") : F(" Released"));

        // Handle specific component actions here
        handleTouchComponent(pageId, componentId, eventType);
      }
    } else {
      // Clear non-touch event data from buffer to prevent hanging
      nextion.read();
    }
  }
}

/**
 * Example handler for specific touch events.
 */
void handleTouchComponent(uint8_t page, uint8_t component, uint8_t event) {
  // Logic to respond to specific button presses
  if (page == 0 && component == 1 && event == 0x01) {
    Serial.println(F("Button ID 1 on Page 0 was pressed!"));
  }
}
