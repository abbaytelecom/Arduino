#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>

/**
 * @file deepseekv4.ino
 * @brief Smart HVAC & Solar Controller (Arduino GIGA R1)
 *
 * Hardware Connection:
 * - Nextion TX -> GIGA R1 Pin 0 (RX1)
 * - Nextion RX -> GIGA R1 Pin 1 (TX1)
 * - Heat Pump: Pin 3 (CH), Pin 4 (Cooling)
 * - Boiler: Pin 6
 * - Solar Pump: Pin 14
 */

// --- Pin Definitions ---
constexpr uint8_t ONE_WIRE_BUS = 2;
constexpr uint8_t DHT_PIN = 10;
constexpr uint8_t HEAT_PUMP_CH_PIN = 3;
constexpr uint8_t HEAT_PUMP_COOLING_PIN = 4;
constexpr uint8_t BOILER_PIN = 6;
constexpr uint8_t FIRST_FLOOR_CIRC_PIN = 7;
constexpr uint8_t SECOND_FLOOR_CIRC_PIN = 8;
constexpr uint8_t DEFROST_INPUT_PIN = 9;
constexpr uint8_t HEAT_PUMP_FAIL_PIN = 11;
constexpr uint8_t SOLAR_CIRCULATOR_PUMP_PIN = 14;
constexpr uint8_t OVERHEAT_VALVE_PIN = 15;

/**
 * @brief System operation modes
 */
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

// --- Temperature Thresholds (°F) ---
constexpr float OUTSIDE_HEATING_THRESHOLD = 65.0f;
constexpr float OUTSIDE_COOLING_THRESHOLD = 70.0f;
constexpr float HEAT_PUMP_MIN_TEMP = 5.0f;
constexpr float HEAT_PUMP_OFF_TEMP = -4.0f;
constexpr float DELTA_T_HEATING_THRESHOLD = 20.0f;
constexpr float DELTA_T_COOLING_THRESHOLD = 5.0f;
constexpr float DELTA_T_HEATING_ASSIST = 25.0f;
constexpr float DELTA_T_COOLING_ON = 10.0f;
constexpr float DELTA_T_HEATING_ON = 25.0f;
constexpr float DEW_POINT_BUFFER = 2.0f;
constexpr float HEATING_MAX_OUTLET_TEMP = 120.0f;
constexpr float HEATING_MIN_OUTLET_TEMP = 100.0f;
constexpr float COOLING_MAX_INLET_TEMP = 65.0f;
constexpr float COOLING_MIN_INLET_TEMP = 42.0f;
constexpr float DHW_OVERHEAT_TEMP = 140.0f;
constexpr float SOLAR_DHW_DELTA_T = 30.0f;

// --- Global Variables ---
SystemMode currentMode = MODE_OFF;
bool solarPumpRunning = false;

// Telemetry Data
float tankOutletTemp = 0.0f;
float tankInletTemp = 0.0f;
float outsideTempF = 0.0f;
float dhwTankTemp = 0.0f;
float solarCollectorTemp = 0.0f;
float utilityHumidity = 0.0f;
float dewPointF = 0.0f;

// Libraries setup
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
DHT dht(DHT_PIN, DHT11);

const uint8_t NEXTION_TERMINATOR[] = { 0xFF, 0xFF, 0xFF };

// =============================================================================
// Initialization
// =============================================================================

void setup() {
  Serial.begin(9600);   // Debug Console
  Serial1.begin(9600);  // Nextion HMI (Hardware Serial1)

  sensors.begin();
  dht.begin();

  // Pin Configuration
  const uint8_t outputPins[] = {
    HEAT_PUMP_CH_PIN, HEAT_PUMP_COOLING_PIN, BOILER_PIN,
    FIRST_FLOOR_CIRC_PIN, SECOND_FLOOR_CIRC_PIN,
    SOLAR_CIRCULATOR_PUMP_PIN, OVERHEAT_VALVE_PIN
  };
  for (uint8_t pin : outputPins) pinMode(pin, OUTPUT);

  pinMode(DEFROST_INPUT_PIN, INPUT_PULLUP);
  pinMode(HEAT_PUMP_FAIL_PIN, INPUT_PULLUP);

  setHeatPumpOff();
  Serial.println(F("HVAC System Initialized (GIGA R1)"));
}

// =============================================================================
// Main Execution Loop
// =============================================================================

void loop() {
  receiveNextionData();

  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate >= 3000) {
    readTelemetry();
    processHvacLogic();
    updateHmiDisplay();
    lastUpdate = millis();
  }
}

// =============================================================================
// Data Acquisition
// =============================================================================

/**
 * @brief Reads all system sensors and calculates derived values
 */
void readTelemetry() {
  sensors.requestTemperatures();

  tankOutletTemp = sensors.getTempFByIndex(0);
  tankInletTemp = sensors.getTempFByIndex(1);
  outsideTempF = sensors.getTempFByIndex(2);
  dhwTankTemp = sensors.getTempFByIndex(3);
  solarCollectorTemp = sensors.getTempFByIndex(4);

  float h = dht.readHumidity();
  float t = dht.readTemperature(true);
  if (!isnan(h) && !isnan(t)) {
    utilityHumidity = h;
    dewPointF = calculateDewPoint(t, h);
  }
}

// =============================================================================
// Control Logic
// =============================================================================

/**
 * @brief Processes system logic and drives outputs
 */
void processHvacLogic() {
  // Fault and Status checks
  if (digitalRead(HEAT_PUMP_FAIL_PIN) == LOW) {
    currentMode = MODE_ERROR;
  } else if (digitalRead(DEFROST_INPUT_PIN) == LOW) {
    currentMode = MODE_DEFROST;
  }

  // Critical Sensor Check
  if (outsideTempF == DEVICE_DISCONNECTED_F || tankOutletTemp == DEVICE_DISCONNECTED_F ||
      tankInletTemp == DEVICE_DISCONNECTED_F) {
    currentMode = MODE_ERROR;
    runOffMode();
    return;
  }

  const float deltaT = calculateDeltaT(tankOutletTemp, tankInletTemp);

  // Seasonal Logic
  if (outsideTempF < OUTSIDE_HEATING_THRESHOLD) {
    runHeatingSeason(deltaT);
  } else if (outsideTempF >= OUTSIDE_HEATING_THRESHOLD && outsideTempF <= OUTSIDE_COOLING_THRESHOLD) {
    runOffMode();
  } else if (outsideTempF > OUTSIDE_COOLING_THRESHOLD) {
    runCoolingSeason(deltaT);
  }

  // Independent Solar DHW Logic
  processSolarLogic();
}

/**
 * @brief Logic for heating season (<65°F)
 */
void runHeatingSeason(float deltaT) {
  digitalWrite(FIRST_FLOOR_CIRC_PIN, HIGH);
  digitalWrite(SECOND_FLOOR_CIRC_PIN, HIGH);

  const bool hpAllowed = (outsideTempF >= HEAT_PUMP_MIN_TEMP &&
                          outsideTempF > HEAT_PUMP_OFF_TEMP);

  if (hpAllowed && currentMode != MODE_ERROR) {
    if (deltaT >= DELTA_T_HEATING_ON) {
      setHeatPumpCH();
      digitalWrite(BOILER_PIN, HIGH);
    } else if (tankOutletTemp < HEATING_MIN_OUTLET_TEMP) {
      setHeatPumpCH();
      digitalWrite(BOILER_PIN, LOW);
    } else if (deltaT <= DELTA_T_HEATING_THRESHOLD) {
      setHeatPumpOff();
      digitalWrite(BOILER_PIN, LOW);
    }
  } else {
    // Boiler Fallback
    setHeatPumpOff();
    if (deltaT >= DELTA_T_HEATING_ON || tankOutletTemp < HEATING_MIN_OUTLET_TEMP) {
      digitalWrite(BOILER_PIN, HIGH);
      if (currentMode != MODE_ERROR) currentMode = MODE_BOILER_CH;
    } else {
      digitalWrite(BOILER_PIN, LOW);
    }
  }
}

/**
 * @brief Logic for cooling season (>70°F)
 */
void runCoolingSeason(float deltaT) {
  digitalWrite(FIRST_FLOOR_CIRC_PIN, HIGH);
  digitalWrite(SECOND_FLOOR_CIRC_PIN, HIGH);
  digitalWrite(BOILER_PIN, LOW);

  const bool inRange = (tankInletTemp >= COOLING_MIN_INLET_TEMP &&
                        tankInletTemp <= COOLING_MAX_INLET_TEMP);
  const bool aboveDewPoint = (tankOutletTemp >= (dewPointF + DEW_POINT_BUFFER));

  if (inRange && aboveDewPoint) {
    if (deltaT >= DELTA_T_COOLING_ON) {
      setHeatPumpCooling();
    } else if (deltaT <= DELTA_T_COOLING_THRESHOLD) {
      setHeatPumpOff();
    }
  } else {
    setHeatPumpOff();
  }
}

/**
 * @brief Logic for System OFF range (65-70°F)
 */
void runOffMode() {
  setHeatPumpOff();
  digitalWrite(BOILER_PIN, LOW);
  digitalWrite(FIRST_FLOOR_CIRC_PIN, LOW);
  digitalWrite(SECOND_FLOOR_CIRC_PIN, LOW);
  currentMode = MODE_OFF;
}

/**
 * @brief Processes Solar DHW logic
 */
void processSolarLogic() {
  // Sensor safety check
  if (solarCollectorTemp == DEVICE_DISCONNECTED_F || dhwTankTemp == DEVICE_DISCONNECTED_F) {
    digitalWrite(SOLAR_CIRCULATOR_PUMP_PIN, LOW);
    solarPumpRunning = false;
    return;
  }

  if ((solarCollectorTemp - dhwTankTemp) >= SOLAR_DHW_DELTA_T &&
      dhwTankTemp < DHW_OVERHEAT_TEMP) {
    solarPumpRunning = true;
    digitalWrite(SOLAR_CIRCULATOR_PUMP_PIN, HIGH);
  } else {
    solarPumpRunning = false;
    digitalWrite(SOLAR_CIRCULATOR_PUMP_PIN, LOW);
  }
}

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * @brief Activates Heat Pump in Central Heating mode
 */
void setHeatPumpCH() {
  currentMode = MODE_HEAT_PUMP_CH;
  digitalWrite(HEAT_PUMP_COOLING_PIN, LOW);
  digitalWrite(HEAT_PUMP_CH_PIN, HIGH);
}

/**
 * @brief Activates Heat Pump in Cooling mode
 */
void setHeatPumpCooling() {
  currentMode = MODE_HEAT_PUMP_COOLING;
  digitalWrite(HEAT_PUMP_CH_PIN, LOW);
  digitalWrite(HEAT_PUMP_COOLING_PIN, HIGH);
}

/**
 * @brief Deactivates all Heat Pump outputs
 */
void setHeatPumpOff() {
  digitalWrite(HEAT_PUMP_CH_PIN, LOW);
  digitalWrite(HEAT_PUMP_COOLING_PIN, LOW);
  if (currentMode != MODE_ERROR && currentMode != MODE_DEFROST) {
    currentMode = MODE_OFF;
  }
}

/**
 * @brief Calculates absolute temperature difference
 * @param outlet Outlet temperature
 * @param inlet Inlet temperature
 * @return Absolute difference
 */
float calculateDeltaT(float outlet, float inlet) {
  return abs(outlet - inlet);
}

/**
 * @brief Calculates dew point using Magnus-Tetens approximation
 * @param tempF Temperature in Fahrenheit
 * @param humidity Relative humidity in percent
 * @return Dew point in Fahrenheit
 */
float calculateDewPoint(float tempF, float humidity) {
  float tempC = (tempF - 32.0f) * 5.0f / 9.0f;
  const float a = 17.625f;
  const float b = 243.04f;
  float alpha = log(humidity / 100.0f) + (a * tempC) / (b + tempC);
  float dewPointC = (b * alpha) / (a - alpha);
  return (dewPointC * 9.0f / 5.0f) + 32.0f;
}

// =============================================================================
// HMI Communication (Nextion)
// =============================================================================

/**
 * @brief Sends a text update to the Nextion display
 * @param component Nextion component name (e.g., "t0")
 * @param value String to display
 */
void updateNextionText(const String& component, const String& value) {
  Serial1.print(component + ".txt=\"" + value + "\"");
  Serial1.write(NEXTION_TERMINATOR, 3);
}

/**
 * @brief Sends a numeric update to the Nextion display
 * @param component Nextion component name (e.g., "n0")
 * @param value Integer value to display
 */
void updateNextionNumber(const String& component, int value) {
  Serial1.print(component + ".val=" + String(value));
  Serial1.write(NEXTION_TERMINATOR, 3);
}

/**
 * @brief Updates all Nextion HMI components with current telemetry
 */
void updateHmiDisplay() {
  updateNextionNumber("n0", (int)tankInletTemp);
  updateNextionNumber("n1", (int)tankOutletTemp);
  updateNextionNumber("n2", (int)dhwTankTemp);
  updateNextionNumber("n3", (int)outsideTempF);
  updateNextionNumber("n4", (int)solarCollectorTemp);
  updateNextionNumber("n5", (int)utilityHumidity);

  switch (currentMode) {
    case MODE_HEAT_PUMP_COOLING: updateNextionText("t0", "HeatPumpCooling"); break;
    case MODE_HEAT_PUMP_CH:      updateNextionText("t0", "HeatPumpHeating"); break;
    case MODE_OFF:               updateNextionText("t0", "HP OFF");           break;
    case MODE_ERROR:             updateNextionText("t0", "ERROR");            break;
    default:                     updateNextionText("t0", "HP ACTIVE");        break;
  }

  updateNextionText("t1", (digitalRead(BOILER_PIN) == HIGH) ? "Boiler Heating" : "Boiler OFF");
  updateNextionText("t2", solarPumpRunning ? "DHW ON" : "DHW OFF");

  if (dhwTankTemp >= DHW_OVERHEAT_TEMP) {
    updateNextionText("t3", "DHWTank Overheating");
  } else {
    updateNextionText("t3", "DHW Normal");
  }
}

// =============================================================================
// Input Handling
// =============================================================================

/**
 * @brief Listens for serial data from Nextion HMI
 */
void receiveNextionData() {
  while (Serial1.available()) {
    uint8_t c = Serial1.read();
    if (c >= '0' && c <= '9') processCommand(c);
    if (c == 0x65) { // Nextion touch event prefix
      uint8_t buf[6];
      Serial1.readBytes(buf, 6);
    }
  }
}

/**
 * @brief Processes numeric commands from HMI
 * @param cmd Char code '0'-'9'
 */
void processCommand(uint8_t cmd) {
  Serial.print(F("Nextion Command Received: "));
  Serial.println((char)cmd);

  switch (cmd) {
    case '0': Serial.println(F("System Normal")); break;
    case '1': Serial.println(F("Force Heat Mode")); break;
    case '2': Serial.println(F("Force Cool Mode")); break;
    default:  Serial.println(F("Unknown Command")); break;
  }
}
