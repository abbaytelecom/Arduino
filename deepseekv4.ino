#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>

/**
 * Smart HVAC & Solar Controller (Arduino GIGA R1 Version)
 * with Nextion Display Interface
 *
 * Hardware Connection:
 * - Nextion TX -> GIGA R1 Pin 0 (RX1)
 * - Nextion RX -> GIGA R1 Pin 1 (TX1)
 * - Heat Pump: Pin 3 (CH), Pin 4 (Cooling) -> Two-wire Logic
 * - Boiler: Pin 6
 * - Solar Pump: Pin 14
 */

// Pin definitions
const int ONE_WIRE_BUS = 2;
const int DHT_PIN = 10;
const int HEAT_PUMP_CH_PIN = 3;
const int HEAT_PUMP_COOLING_PIN = 4;
const int BOILER_PIN = 6;
const int FIRST_FLOOR_CIRC_PIN = 7;
const int SECOND_FLOOR_CIRC_PIN = 8;
const int DEFROST_INPUT_PIN = 9;
const int HEAT_PUMP_FAIL_PIN = 11;
const int SOLAR_CIRCULATOR_PUMP_PIN = 14;
const int OVERHEAT_VALVE_PIN = 15;

// System states
enum SystemMode {
  MODE_OFF,
  MODE_HEAT_PUMP_COOLING,
  MODE_HEAT_PUMP_CH,
  MODE_BOILER_CH,
  MODE_SOLAR_DHW,
  MODE_DEFROST,
  MODE_DHW_OVERHEAT,
  MODE_ERROR
};

// Global variables
SystemMode currentMode = MODE_OFF;
bool solarPumpRunning = false;
// Sensor Data
float tankOutletTemp = 0;
float tankInletTemp = 0;
float outsideTempF = 0;
float dhwTankTemp = 0;
float solarCollectorTemp = 0;
float utilityHumidity = 0;

// OneWire and DHT setup
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DHT dht(DHT_PIN, DHT11);

// Nextion protocol termination sequence
const uint8_t nextionTerminator[] = { 0xFF, 0xFF, 0xFF };

// ====================== Setup ====================== //
void setup() {
  Serial.begin(9600);   // Debug
  Serial1.begin(9600);  // Nextion

  sensors.begin();
  dht.begin();

  // Initialize pins
  pinMode(HEAT_PUMP_CH_PIN, OUTPUT);
  pinMode(HEAT_PUMP_COOLING_PIN, OUTPUT);
  pinMode(BOILER_PIN, OUTPUT);
  pinMode(FIRST_FLOOR_CIRC_PIN, OUTPUT);
  pinMode(SECOND_FLOOR_CIRC_PIN, OUTPUT);
  pinMode(SOLAR_CIRCULATOR_PUMP_PIN, OUTPUT);
  pinMode(OVERHEAT_VALVE_PIN, OUTPUT);

  pinMode(DEFROST_INPUT_PIN, INPUT_PULLUP);
  pinMode(HEAT_PUMP_FAIL_PIN, INPUT_PULLUP);

  setHeatPumpOff(); // Ensure initial state is OFF
  Serial.println(F("Integrated HVAC Controller Initialized (GIGA R1)"));
}

// ====================== Main Loop ====================== //
void loop() {
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 3000) {
    readSensors();
    updateHmiDisplay();
    lastUpdate = millis();
  }
}

// ====================== Sensor Acquisition & Logic ====================== //

void readSensors() {
  sensors.requestTemperatures();

  // Use index-based reading to ensure compatibility without known addresses
  tankOutletTemp = sensors.getTempFByIndex(0);
  tankInletTemp = sensors.getTempFByIndex(1);
  outsideTempF = sensors.getTempFByIndex(2);
  dhwTankTemp = sensors.getTempFByIndex(3);
  solarCollectorTemp = sensors.getTempFByIndex(4);

  float h = dht.readHumidity();
  if (!isnan(h)) {
    utilityHumidity = h;
  }

  // Mode Logic based on Outside Temp
  if (outsideTempF != -196.6) { // Check for sensor presence
    if (outsideTempF >= 64.0 && outsideTempF <= 70.0) {
      // OFF Mode range
      setHeatPumpOff();
      digitalWrite(BOILER_PIN, LOW);
      digitalWrite(FIRST_FLOOR_CIRC_PIN, LOW);
      digitalWrite(SECOND_FLOOR_CIRC_PIN, LOW);
      currentMode = MODE_OFF;
    } else if (outsideTempF > 70.0) {
      // Cooling Mode
      setHeatPumpCooling();
        digitalWrite(BOILER_PIN, LOW); // Explicitly ensure boiler is off in cooling
      digitalWrite(FIRST_FLOOR_CIRC_PIN, HIGH);
      digitalWrite(SECOND_FLOOR_CIRC_PIN, HIGH);
    } else {
      // Heating Mode (< 64.0)
      setHeatPumpCH();
      digitalWrite(BOILER_PIN, HIGH); // Boiler ON for heating support
      digitalWrite(FIRST_FLOOR_CIRC_PIN, HIGH);
      digitalWrite(SECOND_FLOOR_CIRC_PIN, HIGH);
    }
  }

  // Solar logic based on 120-140F range
  if (solarCollectorTemp >= 120.0 && solarCollectorTemp <= 140.0) {
    solarPumpRunning = true;
    digitalWrite(SOLAR_CIRCULATOR_PUMP_PIN, HIGH);
  } else {
    solarPumpRunning = false;
    digitalWrite(SOLAR_CIRCULATOR_PUMP_PIN, LOW);
  }
}

// ====================== Heat Pump Control Helpers ====================== //

void setHeatPumpCH() {
  currentMode = MODE_HEAT_PUMP_CH;
  digitalWrite(HEAT_PUMP_COOLING_PIN, LOW);
  digitalWrite(HEAT_PUMP_CH_PIN, HIGH);
}

void setHeatPumpCooling() {
  currentMode = MODE_HEAT_PUMP_COOLING;
  digitalWrite(HEAT_PUMP_CH_PIN, LOW);
  digitalWrite(HEAT_PUMP_COOLING_PIN, HIGH);
}

void setHeatPumpOff() {
  digitalWrite(HEAT_PUMP_CH_PIN, LOW);
  digitalWrite(HEAT_PUMP_COOLING_PIN, LOW);
  if (currentMode != MODE_ERROR && currentMode != MODE_DEFROST) {
    currentMode = MODE_OFF;
  }
}

// ====================== HMI Communication ====================== //

void updateNextionText(String component, String value) {
  Serial1.print(component + ".txt=\"" + value + "\"");
  Serial1.write(nextionTerminator, 3);
}

void updateNextionNumber(String component, int value) {
  Serial1.print(component + ".val=" + String(value));
  Serial1.write(nextionTerminator, 3);
}

void updateHmiDisplay() {
  // Telemetry n0-n5
  updateNextionNumber("n0", (int)tankInletTemp);
  updateNextionNumber("n1", (int)tankOutletTemp);
  updateNextionNumber("n2", (int)dhwTankTemp);
  updateNextionNumber("n3", (int)outsideTempF);
  updateNextionNumber("n4", (int)solarCollectorTemp);
  updateNextionNumber("n5", (int)utilityHumidity);

  // Status t0: Heat Pump
  switch (currentMode) {
    case MODE_HEAT_PUMP_COOLING: updateNextionText("t0", "HeatPumpCooling"); break;
    case MODE_HEAT_PUMP_CH:      updateNextionText("t0", "HeatPumpHeating"); break;
    case MODE_OFF:               updateNextionText("t0", "HP OFF");           break;
    case MODE_ERROR:             updateNextionText("t0", "ERROR");            break;
    default:                     updateNextionText("t0", "HP ACTIVE");        break;
  }

  // Status t1: Boiler
  if (digitalRead(BOILER_PIN) == HIGH) {
    updateNextionText("t1", "Boiler Heating");
  } else {
    updateNextionText("t1", "Boiler OFF");
  }

  // Status t2: DHW
  updateNextionText("t2", solarPumpRunning ? "DHW ON" : "DHW OFF");

  // Status t3: DHW Overheat
  if (dhwTankTemp > 160.0) {
    updateNextionText("t3", "DHWTank Overheating");
  } else {
    updateNextionText("t3", "DHW Normal");
  }
}
