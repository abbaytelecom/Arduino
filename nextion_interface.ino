#include <SoftwareSerial.h>

/**
 * Nextion Display Interface for HVAC System
 *
 * This module handles communication with a Nextion HMI display,
 * including status reporting for Heating/Cooling/DHW modes
 * and command processing for testing.
 */

// --- System State (Shared with HVAC Controller) ---
enum SystemMode {
  MODE_HEATING,
  MODE_COOLING,
  MODE_DEFROST,
  MODE_ERROR
};

SystemMode currentMode = MODE_HEATING;
bool solarPumpRunning = false; // Represents DHW mode status

// --- Nextion Configuration ---
// SoftwareSerial pins: RX=16 (A2), TX=17 (A3)
SoftwareSerial nextion(16, 17);

// Nextion protocol termination sequence
const uint8_t nextionTerminator[] = { 0xFF, 0xFF, 0xFF };

// ====================== Setup ====================== //
void setup() {
  Serial.begin(9600);
  nextion.begin(9600);
  Serial.println(F("Nextion HVAC Interface Initialized"));
}

// ====================== Main Loop ====================== //
void loop() {
  // Handle incoming commands from Nextion
  receiveNextionData();

  // Periodic status update to Nextion
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 2000) {
    updateHmiStatus();
    lastUpdate = millis();
  }
}

// ====================== Status Reporting ====================== //

/**
 * Transmits the current system mode and DHW status to the Nextion display.
 */
void updateHmiStatus() {
  // Update main mode text
  if (currentMode == MODE_HEATING) {
    updateNextionText("t0", "Heating");
  } else if (currentMode == MODE_COOLING) {
    updateNextionText("t0", "Cooling");
  } else if (currentMode == MODE_ERROR) {
    updateNextionText("t0", "ERROR");
  }

  // Update DHW (Solar) status
  if (solarPumpRunning) {
    updateNextionText("t1", "DHW: ON");
  } else {
    updateNextionText("t1", "DHW: OFF");
  }

  Serial.println(F("HMI Status Updated"));
}

// ====================== Outgoing Communication Helpers ====================== //

void sendNextionCommand(String cmd) {
  nextion.print(cmd);
  nextion.write(nextionTerminator, 3);
}

void updateNextionText(String component, String value) {
  String cmd = component + ".txt=\"" + value + "\"";
  sendNextionCommand(cmd);
}

void updateNextionNumber(String component, int value) {
  String cmd = component + ".val=" + String(value);
  sendNextionCommand(cmd);
}

// ====================== Incoming Communication ====================== //

/**
 * Reads and parses incoming data from the Nextion display.
 */
void receiveNextionData() {
  static String inputBuffer = "";

  while (nextion.available()) {
    char c = nextion.read();

    // Check for Nextion return codes (e.g., Touch Event 0x65)
    if ((uint8_t)c == 0x65) {
      // Handle standard touch event if needed (read next 6 bytes)
      uint8_t touchBuffer[6];
      nextion.readBytes(touchBuffer, 6);
      Serial.println(F("Received Nextion Touch Event"));
      continue;
    }

    // Buffer characters for string commands (testing purpose)
    if (c == '\n' || c == '\r') {
      if (inputBuffer.length() > 0) {
        processHmiCommand(inputBuffer);
        inputBuffer = "";
      }
    } else if ((uint8_t)c != 0xFF) {
      inputBuffer += c;
    }

    // Alternative: check if buffer matches user's specific strings
    if (inputBuffer == "cooling") { processHmiCommand("cooling"); inputBuffer = ""; }
    else if (inputBuffer == "Heating") { processHmiCommand("Heating"); inputBuffer = ""; }
    else if (inputBuffer == "DHWandHeating") { processHmiCommand("DHWandHeating"); inputBuffer = ""; }
    else if (inputBuffer == "DHWandcooling") { processHmiCommand("DHWandcooling"); inputBuffer = ""; }
  }
}

/**
 * Processes commands received from the Nextion for testing purposes.
 */
void processHmiCommand(String cmd) {
  cmd.trim();
  Serial.print(F("Processing HMI Command: "));
  Serial.println(cmd);

  if (cmd == "cooling") {
    currentMode = MODE_COOLING;
    solarPumpRunning = false;
  }
  else if (cmd == "Heating") {
    currentMode = MODE_HEATING;
    solarPumpRunning = false;
  }
  else if (cmd == "DHWandHeating") {
    currentMode = MODE_HEATING;
    solarPumpRunning = true;
  }
  else if (cmd == "DHWandcooling") {
    currentMode = MODE_COOLING;
    solarPumpRunning = true;
  }

  // Immediately update HMI after change
  updateHmiStatus();
}
