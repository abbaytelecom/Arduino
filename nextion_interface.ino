/**
 * Nextion Display Interface for Arduino GIGA R1
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
bool solarPumpRunning = false;

// --- Simulated Sensor Variables ---
int inletTempF = 0;
int outletTempF = 0;
int dhwTempF = 0;
int outsideTempC = 0;
int solarTempF = 0;
int humidityPct = 0;

// Nextion protocol termination sequence
const uint8_t nextionTerminator[] = { 0xFF, 0xFF, 0xFF };

// ====================== Setup ====================== //
void setup() {
  Serial.begin(9600);
  Serial1.begin(9600); // GIGA R1 Hardware Serial 1

  // Seed random for testing
  randomSeed(analogRead(0));

  Serial.println(F("Nextion GIGA Simulation Interface Initialized"));
}

// ====================== Main Loop ====================== //
void loop() {
  // Handle incoming numeric commands from Nextion
  receiveNextionData();

  // Periodic simulation update
  static unsigned long lastSimulation = 0;
  if (millis() - lastSimulation > 5000) {
    generateSimulatedData();
    updateHmiDisplay();
    lastSimulation = millis();
  }
}

// ====================== Simulation Logic ====================== //

/**
 * Randomly generates sensor values and updates system state based on thresholds.
 * As per AGENTS.md:
 * - Outside > 15C -> Cooling, else Heating.
 * - Solar 120-140F -> DHW ON.
 */
void generateSimulatedData() {
  inletTempF = random(40, 80);
  outletTempF = random(40, 120);
  dhwTempF = random(50, 150);
  outsideTempC = random(5, 35); // 5C to 35C
  solarTempF = random(80, 160);
  humidityPct = random(20, 95);

  // Apply Logic based on Outside Temp (Celsius)
  if (outsideTempC > 15) {
    currentMode = MODE_COOLING;
  } else {
    currentMode = MODE_HEATING;
  }

  // Apply Logic based on Solar Temp (Fahrenheit)
  if (solarTempF >= 120 && solarTempF <= 140) {
    solarPumpRunning = true;
  } else {
    solarPumpRunning = false;
  }

  Serial.println(F("Generated new simulation data"));
}

// ====================== Status Reporting ====================== //

/**
 * Updates Nextion numeric and text components.
 */
void updateHmiDisplay() {
  // Update Numeric components n0 to n5
  updateNextionNumber("n0", inletTempF);
  updateNextionNumber("n1", outletTempF);
  updateNextionNumber("n2", dhwTempF);
  updateNextionNumber("n3", outsideTempC);
  updateNextionNumber("n4", solarTempF);
  updateNextionNumber("n5", humidityPct);

  // Update Status Text (t0)
  switch (currentMode) {
    case MODE_HEATING: updateNextionText("t0", "Heating"); break;
    case MODE_COOLING: updateNextionText("t0", "Cooling"); break;
    case MODE_DEFROST: updateNextionText("t0", "Defrost"); break;
    case MODE_ERROR:   updateNextionText("t0", "ERROR");   break;
  }

  // Update DHW Text (t1)
  if (solarPumpRunning) {
    updateNextionText("t1", "DHW: ON");
  } else {
    updateNextionText("t1", "DHW: OFF");
  }

  Serial.println(F("HMI Display Synchronized"));
}

// ====================== Communication Helpers ====================== //

void sendNextionCommand(String cmd) {
  Serial1.print(cmd);
  Serial1.write(nextionTerminator, 3);
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
 * Parses numeric commands 1-6 from Nextion.
 */
void receiveNextionData() {
  while (Serial1.available()) {
    char c = Serial1.read();

    // Check for numeric commands (ASCII or raw)
    if (c >= '1' && c <= '6') {
      processCommand(c);
    }
    else if ((uint8_t)c >= 1 && (uint8_t)c <= 6) {
      processCommand(c + '0');
    }

    // Standard Touch Event (0x65)
    if ((uint8_t)c == 0x65) {
      uint8_t buf[6];
      Serial1.readBytes(buf, 6);
    }
  }
}

void processCommand(char cmd) {
  Serial.print(F("Manual Command: "));
  Serial.println(cmd);

  switch (cmd) {
    case '1': currentMode = MODE_HEATING; break;
    case '2': currentMode = MODE_COOLING; break;
    case '3': currentMode = MODE_DEFROST; break;
    case '4': currentMode = MODE_ERROR;   break;
    case '5': solarPumpRunning = true;    break;
    case '6': solarPumpRunning = false;   break;
  }

  // Update HMI to show manual change
  updateHmiDisplay();
}
