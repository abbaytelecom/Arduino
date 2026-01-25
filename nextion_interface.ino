/**
 * Nextion Display Interface for Arduino GIGA R1
 *
 * This module handles communication with a Nextion HMI display using
 * the Hardware Serial (Serial1) available on the Arduino GIGA R1.
 *
 * Hardware Connection:
 * - Nextion TX -> GIGA R1 Pin 0 (RX1)
 * - Nextion RX -> GIGA R1 Pin 1 (TX1)
 * - GND -> GND
 */

// --- System State (Consistent with HVAC Logic) ---
enum SystemMode {
  MODE_HEATING,
  MODE_COOLING,
  MODE_DEFROST,
  MODE_ERROR
};

SystemMode currentMode = MODE_HEATING;
bool solarPumpRunning = false; // Represents DHW mode status

// Nextion protocol termination sequence
const uint8_t nextionTerminator[] = { 0xFF, 0xFF, 0xFF };

// ====================== Setup ====================== //
void setup() {
  // USB Serial for debugging
  Serial.begin(9600);

  // Hardware Serial 1 for Nextion Display
  Serial1.begin(9600);

  Serial.println(F("Nextion GIGA R1 Interface Initialized"));
}

// ====================== Main Loop ====================== //
void loop() {
  // Handle incoming data from Nextion
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
 * If in Cooling mode, sends "Cooling". If in Heating mode, sends "Heating".
 */
void updateHmiStatus() {
  // Update main mode text component (e.g., t0)
  if (currentMode == MODE_HEATING) {
    updateNextionText("t0", "Heating");
  } else if (currentMode == MODE_COOLING) {
    updateNextionText("t0", "Cooling");
  } else if (currentMode == MODE_ERROR) {
    updateNextionText("t0", "ERROR");
  }

  // Update DHW status component (e.g., t1)
  // DHW can be on at the same time as cooling or heating.
  if (solarPumpRunning) {
    updateNextionText("t1", "DHW: ON");
  } else {
    updateNextionText("t1", "DHW: OFF");
  }

  Serial.println(F("Sent status update to Nextion"));
}

// ====================== Outgoing Communication Helpers ====================== //

/**
 * Sends a raw command to Nextion via Serial1.
 */
void sendNextionCommand(String cmd) {
  Serial1.print(cmd);
  Serial1.write(nextionTerminator, 3);
}

/**
 * Updates a text component on the Nextion display.
 */
void updateNextionText(String component, String value) {
  String cmd = component + ".txt=\"" + value + "\"";
  sendNextionCommand(cmd);
}

// ====================== Incoming Communication ====================== //

/**
 * Reads data from Nextion and parses specific testing commands.
 * Handles: "cooling", "Heating", "DHWandHeating", "DHWandcooling"
 */
void receiveNextionData() {
  static String inputBuffer = "";

  while (Serial1.available()) {
    char c = Serial1.read();

    // Ignore Nextion termination bytes in the string buffer
    if ((uint8_t)c == 0xFF) continue;

    // Handle standard Nextion return codes (e.g. Touch Event 0x65)
    if ((uint8_t)c == 0x65) {
      uint8_t touchBuffer[6];
      Serial1.readBytes(touchBuffer, 6);
      Serial.println(F("Received Touch Event from Nextion"));
      continue;
    }

    // Process string-based testing commands
    if (c == '\n' || c == '\r') {
      if (inputBuffer.length() > 0) {
        processHmiCommand(inputBuffer);
        inputBuffer = "";
      }
    } else {
      inputBuffer += c;
    }

    // Check for direct matches if no newline is sent
    if (inputBuffer == "cooling") { processHmiCommand("cooling"); inputBuffer = ""; }
    else if (inputBuffer == "Heating") { processHmiCommand("Heating"); inputBuffer = ""; }
    else if (inputBuffer == "DHWandHeating") { processHmiCommand("DHWandHeating"); inputBuffer = ""; }
    else if (inputBuffer == "DHWandcooling") { processHmiCommand("DHWandcooling"); inputBuffer = ""; }
  }
}

/**
 * Processes the testing commands and updates internal state.
 */
void processHmiCommand(String cmd) {
  cmd.trim();
  Serial.print(F("HMI Command Received: "));
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

  // Refresh display immediately
  updateHmiStatus();
}
