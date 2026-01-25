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
 */
void updateHmiStatus() {
  // Update main mode text component (t0)
  switch (currentMode) {
    case MODE_HEATING: updateNextionText("t0", "Heating"); break;
    case MODE_COOLING: updateNextionText("t0", "Cooling"); break;
    case MODE_DEFROST: updateNextionText("t0", "Defrost"); break;
    case MODE_ERROR:   updateNextionText("t0", "ERROR");   break;
  }

  // Update DHW status component (t1)
  if (solarPumpRunning) {
    updateNextionText("t1", "DHW: ON");
  } else {
    updateNextionText("t1", "DHW: OFF");
  }

  Serial.println(F("Sent status update to Nextion"));
}

// ====================== Outgoing Communication Helpers ====================== //

void sendNextionCommand(String cmd) {
  Serial1.print(cmd);
  Serial1.write(nextionTerminator, 3);
}

void updateNextionText(String component, String value) {
  String cmd = component + ".txt=\"" + value + "\"";
  sendNextionCommand(cmd);
}

// ====================== Incoming Communication ====================== //

/**
 * Reads data from Nextion and parses numeric commands '1' through '6'.
 */
void receiveNextionData() {
  while (Serial1.available()) {
    char c = Serial1.read();

    // Check for standard Nextion return codes (e.g. Touch Event 0x65)
    if ((uint8_t)c == 0x65) {
      uint8_t touchBuffer[6];
      Serial1.readBytes(touchBuffer, 6);
      Serial.println(F("Received Touch Event from Nextion"));
      continue;
    }

    // Process numeric commands '1'-'6' (sent as ASCII or raw bytes)
    if (c >= '1' && c <= '6') {
      processNumericCommand(c);
    }
    else if ((uint8_t)c >= 1 && (uint8_t)c <= 6) {
      processNumericCommand(c + '0');
    }
  }
}

/**
 * Maps numeric inputs to system state changes.
 * 1: Heating, 2: Cooling, 3: Defrost, 4: Error
 * 5: DHW ON, 6: DHW OFF
 */
void processNumericCommand(char cmdChar) {
  Serial.print(F("Numeric Command Received: "));
  Serial.println(cmdChar);

  switch (cmdChar) {
    case '1': currentMode = MODE_HEATING; break;
    case '2': currentMode = MODE_COOLING; break;
    case '3': currentMode = MODE_DEFROST; break;
    case '4': currentMode = MODE_ERROR;   break;
    case '5': solarPumpRunning = true;    break;
    case '6': solarPumpRunning = false;   break;
    default: return; // Should not happen due to caller check
  }

  // Synchronize HMI immediately
  updateHmiStatus();
}
