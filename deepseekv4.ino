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
  pinMode(HEAT_PUMP_OFF_PIN, OUTPUT);
  pinMode(BOILER_PIN, OUTPUT);
  pinMode(CIRCULATOR_PUMP_PIN, OUTPUT);
  pinMode(TWO_WAY_VALVE_PIN, OUTPUT);
  pinMode(SOLAR_CIRCULATOR_PUMP_PIN, OUTPUT);
  pinMode(OVERHEAT_VALVE_PIN, OUTPUT);

  pinMode(DEFROST_INPUT_PIN, INPUT_PULLUP);
  pinMode(HEAT_PUMP_FAIL_PIN, INPUT_PULLUP);

  Serial.println(F("HVAC Controller with Nextion Initialized (GIGA R1)"));
}

// ====================== Main Loop ====================== //
void loop() {
  // Handle HMI input
  receiveNextionData();

  // Periodic Hardware Update
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 3000) {
    readSensors();
    updateHmiDisplay();
    lastUpdate = millis();
  }
}

// ====================== Sensor Acquisition ====================== //

/**
 * Acquires data from physical DS18B20 and DHT11 sensors.
 * Replaces random simulation.
 */
void readSensors() {
  sensors.requestTemperatures();
  tankOutletTemp = sensors.getTempF(tankOutlet);
  tankInletTemp = sensors.getTempF(tankInlet);
  outsideTempF = sensors.getTempF(outsideTemp);
  dhwTankTemp = sensors.getTempF(dhwTankSensor);
  solarCollectorTemp = sensors.getTempF(solarCollectorSensor);

  // Read DHT sensor
  utilityHumidity = dht.readHumidity();
  utilityTempF = dht.readTemperature(true);

  // Check for basic sensor disconnects
  if (outsideTempF == -196.6 || tankOutletTemp == -196.6) {
    isSensorError = true;
    currentMode = MODE_ERROR;
    return;
  } else {
    isSensorError = false;
  }

  // Logic: if OutsideTemp > 15 C (59 F) -> Cooling, else Heating
  // Conversion: (T(°F) - 32) × 5/9 = T(°C)
  float outsideTempC = (outsideTempF - 32.0) * 5.0 / 9.0;
  if (outsideTempC > 15.0) {
    currentMode = MODE_COOLING;
  } else {
    currentMode = MODE_HEATING;
  }

  // Logic: if Solar 120-140 F -> DHW ON
  if (solarCollectorTemp >= 120.0 && solarCollectorTemp <= 140.0) {
    solarPumpRunning = true;
  } else {
    solarPumpRunning = false;
  }

  Serial.print(F("Real Data - Outside: ")); Serial.print(outsideTempC); Serial.println(F("C"));
}

// ====================== HMI Communication ====================== //

/**
 * Updates Nextion components:
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
    updateNextionText("t0", "HeatPumpCooling");
  } else if (currentMode == MODE_HEATING) {
    updateNextionText("t0", "HeatPumpHeating");
  } else {
    updateNextionText("t0", "HP OFF");
  }

  // t1: Boiler status
  if (digitalRead(BOILER_PIN) == HIGH) {
    updateNextionText("t1", "Boiler Heating");
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

void receiveNextionData() {
  while (Serial1.available()) {
    char c = Serial1.read();

    if (c >= '1' && c <= '6') {
      processManualCommand(c);
    }
    else if ((uint8_t)c >= 1 && (uint8_t)c <= 6) {
      processManualCommand(c + '0');
    }

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
