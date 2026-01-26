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
const int CIRCULATOR_PUMP_PIN = 7;
const int TWO_WAY_VALVE_PIN = 8;
const int DEFROST_INPUT_PIN = 9;
const int HEAT_PUMP_FAIL_PIN = 11;
const int SOLAR_CIRCULATOR_PUMP_PIN = 14;
const int OVERHEAT_VALVE_PIN = 15;

// System states
enum SystemMode {
  MODE_OFF,
  MODE_HEAT_PUMP_COOLING,
  MODE_HEAT_PUMP_CH,
  MODE_HEAT_PUMP_DHW,
  MODE_BOILER_CH,
  MODE_SOLAR_DHW,
  MODE_DEFROST,
  MODE_DHW_OVERHEAT,
  MODE_ERROR
};

// Global variables
SystemMode currentMode = MODE_OFF;
bool solarPumpRunning = false;
bool manualOverride = false; // Prevents sensor logic from stomping on manual tests

// Sensor Data
float tankOutletTemp = 0;
float tankInletTemp = 0;
float outsideTempF = 0;
float dhwTankTemp = 0;
float solarCollectorTemp = 0;
float utilityHumidity = 0;

// Temperature sensor addresses
DeviceAddress tankOutlet = { 0x28, 0xFF, 0x64, 0x1E, 0x3C, 0x14, 0x01, 0x0A };
DeviceAddress tankInlet = { 0x28, 0xFF, 0x64, 0x1E, 0x3C, 0x14, 0x01, 0x0B };
DeviceAddress outsideTemp = { 0x28, 0xFF, 0x64, 0x1E, 0x3C, 0x14, 0x01, 0x0C };
DeviceAddress dhwTankSensor = { 0x28, 0xFF, 0x64, 0x1E, 0x3C, 0x14, 0x01, 0x0D };
DeviceAddress solarCollectorSensor = { 0x28, 0xFF, 0x64, 0x1E, 0x3C, 0x14, 0x01, 0x0E };

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
  pinMode(CIRCULATOR_PUMP_PIN, OUTPUT);
  pinMode(TWO_WAY_VALVE_PIN, OUTPUT);
  pinMode(SOLAR_CIRCULATOR_PUMP_PIN, OUTPUT);
  pinMode(OVERHEAT_VALVE_PIN, OUTPUT);

  pinMode(DEFROST_INPUT_PIN, INPUT_PULLUP);
  pinMode(HEAT_PUMP_FAIL_PIN, INPUT_PULLUP);

  setHeatPumpOff(); // Ensure initial state is OFF
  Serial.println(F("Integrated HVAC Controller Initialized (GIGA R1)"));
}

// ====================== Main Loop ====================== //
void loop() {
  receiveNextionData();

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
  tankOutletTemp = sensors.getTempF(tankOutlet);
  tankInletTemp = sensors.getTempF(tankInlet);
  outsideTempF = sensors.getTempF(outsideTemp);
  dhwTankTemp = sensors.getTempF(dhwTankSensor);
  solarCollectorTemp = sensors.getTempF(solarCollectorSensor);
  utilityHumidity = dht.readHumidity();

  // Only apply automatic logic if not in manual override
  if (!manualOverride) {
    // Mode Logic based on Outside Temp (15C threshold)
    float outsideTempC = (outsideTempF - 32.0) * 5.0 / 9.0;
    if (outsideTempF != -196.6) { // Check for sensor presence
      if (outsideTempC > 15.0) {
        setHeatPumpCooling();
      } else {
        setHeatPumpCH();
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
}

// ====================== Input Handling ====================== //

void receiveNextionData() {
  while (Serial1.available()) {
    char c = Serial1.read();
    if ((c >= '1' && c <= '6') || ((uint8_t)c >= 1 && (uint8_t)c <= 6)) {
      char cmd = (c >= '1' && c <= '6') ? c : (c + '0');
      processManualCommand(cmd);
    }
    if ((uint8_t)c == 0x65) { // Flush touch events
      uint8_t buf[6]; Serial1.readBytes(buf, 6);
    }
  }
}

void processManualCommand(char cmd) {
  manualOverride = true; // Engage manual override when HMI command received
  Serial.print(F("Manual Override Activated: ")); Serial.println(cmd);

  switch (cmd) {
    case '1': setHeatPumpCH(); break;
    case '2': setHeatPumpCooling(); break;
    case '3': currentMode = MODE_DEFROST; setHeatPumpOff(); break;
    case '4': currentMode = MODE_ERROR;   setHeatPumpOff(); break;
    case '5':
      solarPumpRunning = true;
      digitalWrite(SOLAR_CIRCULATOR_PUMP_PIN, HIGH);
      break;
    case '6':
      solarPumpRunning = false;
      digitalWrite(SOLAR_CIRCULATOR_PUMP_PIN, LOW);
      break;
  }
  updateHmiDisplay();
}
