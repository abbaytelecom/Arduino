/**
 * Smart HVAC & Solar Controller (Arduino GIGA R1 Version)
 * with Nextion Display Interface
 *
 * Hardware Connection:
 * - Nextion TX -> GIGA R1 Pin 0 (RX1)
 * - Nextion RX -> GIGA R1 Pin 1 (TX1)
 * - USB -> Debug Serial
 */

// Pin definitions
const int ONE_WIRE_BUS = 2;
const int DHT_PIN = 10;
const int HEAT_PUMP_CH_PIN = 3;
const int HEAT_PUMP_COOLING_PIN = 4;
const int HEAT_PUMP_OFF_PIN = 5;
const int BOILER_PIN = 6;
const int CIRCULATOR_PUMP_PIN = 7;
const int TWO_WAY_VALVE_PIN = 8;
const int DEFROST_INPUT_PIN = 9;
const int HEAT_PUMP_FAIL_PIN = 11;
const int SOLAR_CIRCULATOR_PUMP_PIN = 14;
const int OVERHEAT_VALVE_PIN = 15;

// System states
enum SystemMode {
  MODE_HEATING,
  MODE_COOLING,
  MODE_DEFROST,
  MODE_ERROR
};

// Global variables
SystemMode currentMode = MODE_HEATING;
bool isHeatPumpFailed = false;
bool isSensorError = false;
bool solarPumpRunning = false;

// Sensor Data
float tankOutletTemp = 0;
float tankInletTemp = 0;
float outsideTempF = 0;
float dhwTankTemp = 0;
float solarCollectorTemp = 0;
float utilityHumidity = 0;
float utilityTempF = 0;
float dewPointF = 0;

// Nextion protocol termination sequence
const uint8_t nextionTerminator[] = { 0xFF, 0xFF, 0xFF };

// ====================== Setup ====================== //
void setup() {
  Serial.begin(9600);   // Debug
  Serial1.begin(9600);  // Nextion

  // Initialize pins
  pinMode(HEAT_PUMP_CH_PIN, OUTPUT);
  pinMode(HEAT_PUMP_COOLING_PIN, OUTPUT);
  pinMode(HEAT_PUMP_OFF_PIN, OUTPUT);
  pinMode(BOILER_PIN, OUTPUT);
  pinMode(CIRCULATOR_PUMP_PIN, OUTPUT);
  pinMode(TWO_WAY_VALVE_PIN, OUTPUT);
  pinMode(SOLAR_CIRCULATOR_PUMP_PIN, OUTPUT);
  pinMode(OVERHEAT_VALVE_PIN, OUTPUT);

  pinMode(DEFROST_INPUT_PIN, INPUT_PULLUP);
  pinMode(HEAT_PUMP_FAIL_PIN, INPUT_PULLUP);

  randomSeed(analogRead(0));
  Serial.println(F("HVAC Controller with Nextion Initialized (GIGA R1)"));
}

// ====================== Main Loop ====================== //
void loop() {
  // Handle HMI input
  receiveNextionData();

  // Periodic Simulation & Display Update
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 3000) {
    generateSimulatedData();
    updateHmiDisplay();
    lastUpdate = millis();
  }
}

// ====================== Simulation Logic ====================== //

/**
 * Randomly generates sensor values and updates system state based on user thresholds.
 */
void generateSimulatedData() {
  tankInletTemp = random(40, 80);
  tankOutletTemp = random(40, 120);
  dhwTankTemp = random(50, 150);

  // Simulated Outside Temp in Celsius then convert to F
  int outsideC = random(5, 35);
  outsideTempF = (outsideC * 9.0 / 5.0) + 32.0;

  solarCollectorTemp = random(80, 160);
  utilityHumidity = random(20, 95);

  // Logic: if Outemp > 15 C (59 F) -> Cooling, else Heating
  if (outsideC > 15) {
    currentMode = MODE_COOLING;
  } else {
    currentMode = MODE_HEATING;
  }

  // Logic: if Solar 120-140 F -> DHW ON
  if (solarCollectorTemp >= 120 && solarCollectorTemp <= 140) {
    solarPumpRunning = true;
  } else {
    solarPumpRunning = false;
  }

  Serial.print(F("Simulated - Outside: ")); Serial.print(outsideC); Serial.println(F("C"));
}

// ====================== HMI Communication ====================== //

/**
 * Updates Nextion components as requested:
 * n0-n5 for sensors, t0-t2 for status.
 */
void updateHmiDisplay() {
  // Numeric Readings
  updateNextionNumber("n0", (int)tankInletTemp);
  updateNextionNumber("n1", (int)tankOutletTemp);
  updateNextionNumber("n2", (int)dhwTankTemp);
  updateNextionNumber("n3", (int)outsideTempF);
  updateNextionNumber("n4", (int)solarCollectorTemp);
  updateNextionNumber("n5", (int)utilityHumidity);

  // t0: Heat pump mode status
  if (currentMode == MODE_COOLING) {
    updateNextionText("t0", "HP Cooling");
  } else if (currentMode == MODE_HEATING) {
    updateNextionText("t0", "HP Heating");
  } else {
    updateNextionText("t0", "HP OFF");
  }

  // t1: Boiler status
  if (digitalRead(BOILER_PIN) == HIGH) {
    updateNextionText("t1", "Boiler ON");
  } else {
    updateNextionText("t1", "Boiler OFF");
  }

  // t2: DHW status
  if (solarPumpRunning) {
    updateNextionText("t2", "DHW ON");
  } else {
    updateNextionText("t2", "DHW OFF");
  }

  Serial.println(F("Nextion Display Updated"));
}

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

// ====================== HMI Input Handling ====================== //

/**
 * Parses numeric commands 1-6 for manual testing overrides.
 */
void receiveNextionData() {
  while (Serial1.available()) {
    char c = Serial1.read();

    // Process numeric commands 1-6
    if (c >= '1' && c <= '6') {
      processManualCommand(c);
    }
    else if ((uint8_t)c >= 1 && (uint8_t)c <= 6) {
      processManualCommand(c + '0');
    }

    // Clear standard Nextion return codes
    if ((uint8_t)c == 0x65) {
      uint8_t buf[6];
      Serial1.readBytes(buf, 6);
    }
  }
}

void processManualCommand(char cmd) {
  Serial.print(F("Manual Override: "));
  Serial.println(cmd);

  switch (cmd) {
    case '1': currentMode = MODE_HEATING; break;
    case '2': currentMode = MODE_COOLING; break;
    case '3': currentMode = MODE_DEFROST; break;
    case '4': currentMode = MODE_ERROR;   break;
    case '5': solarPumpRunning = true;    break;
    case '6': solarPumpRunning = false;   break;
  }

  updateHmiDisplay();
}
