#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>

/**
 * @file deepseekv4.ino
 * @brief Professional HVAC & Solar Controller (Arduino GIGA R1)
 *
 * Manages Heat Pump, Boiler, and Solar DHW with Nextion HMI interface.
 * Hardware Connection:
 * - Nextion TX -> GIGA RX1 (Pin 0)
 * - Nextion RX -> GIGA TX1 (Pin 1)
 */

// --- Pin Assignments ---
constexpr uint8_t PIN_ONE_WIRE_BUS = 2;
constexpr uint8_t PIN_DHT = 10;
constexpr uint8_t PIN_HP_CH = 3;
constexpr uint8_t PIN_HP_COOL = 4;
constexpr uint8_t PIN_BOILER = 6;
constexpr uint8_t PIN_CIRC_1 = 7;
constexpr uint8_t PIN_CIRC_2 = 8;
constexpr uint8_t PIN_DEFROST = 9;
constexpr uint8_t PIN_HP_FAIL = 11;
constexpr uint8_t PIN_SOLAR_PUMP = 14;
constexpr uint8_t PIN_OVERHEAT_VALVE = 15;

/**
 * @brief System operation modes
 */
enum class SystemMode : uint8_t {
  OFF,
  HP_COOLING,
  HP_HEATING,
  BOILER_HEATING,
  SOLAR_DHW,
  DEFROST,
  DHW_OVERHEAT,
  ERROR
};

// --- Configurable Thresholds (°F) ---
namespace Config {
  constexpr float HEATING_THRESHOLD = 65.0f;
  constexpr float COOLING_THRESHOLD = 70.0f;
  constexpr float HP_MIN_AMBIENT = 5.0f;
  constexpr float HP_CRITICAL_LOW = -4.0f;
  constexpr float DELTA_T_HEATING_OFF = 20.0f;
  constexpr float DELTA_T_HEATING_ON = 25.0f;
  constexpr float DELTA_T_COOLING_OFF = 5.0f;
  constexpr float DELTA_T_COOLING_ON = 10.0f;
  constexpr float DEW_POINT_BUFFER = 2.0f;
  constexpr float HEATING_MIN_OUTLET = 100.0f;
  constexpr float COOLING_MIN_INLET = 42.0f;
  constexpr float COOLING_MAX_INLET = 65.0f;
  constexpr float DHW_MAX_TEMP = 140.0f;
  constexpr float SOLAR_DELTA_T_ON = 30.0f;
  constexpr uint32_t BOILER_MIN_RUNTIME = 600000; // 10 minutes in ms
}

// --- System State ---
SystemMode g_currentMode = SystemMode::OFF;
bool g_solarActive = false;
uint32_t g_boilerStartTime = 0;

struct Telemetry {
  float tankOutlet = 0.0f;
  float tankInlet = 0.0f;
  float ambient = 0.0f;
  float dhwTank = 0.0f;
  float solarCollector = 0.0f;
  float humidity = 0.0f;
  float dewPoint = 0.0f;
} g_data;

// --- Drivers ---
OneWire g_oneWire(PIN_ONE_WIRE_BUS);
DallasTemperature g_sensors(&g_oneWire);
DHT g_dht(PIN_DHT, DHT11);

const uint8_t NEXTION_END[] = { 0xFF, 0xFF, 0xFF };

// =============================================================================
// Helper: Error Handling
// =============================================================================

/**
 * @brief Forces all high-power outputs to a safe (OFF) state.
 */
void performSafeShutdown() {
  digitalWrite(PIN_HP_CH, LOW);
  digitalWrite(PIN_HP_COOL, LOW);
  digitalWrite(PIN_BOILER, LOW);
  digitalWrite(PIN_CIRC_1, LOW);
  digitalWrite(PIN_CIRC_2, LOW);
  digitalWrite(PIN_SOLAR_PUMP, LOW);
}

/**
 * @brief Logs an error to Serial and updates system state.
 * @param msg Error description
 */
void handleSystemError(const __FlashStringHelper* msg) {
  Serial.print(F("CRITICAL ERROR: "));
  Serial.println(msg);
  g_currentMode = SystemMode::ERROR;
  performSafeShutdown();
}

// =============================================================================
// Initialization
// =============================================================================

void setup() {
  Serial.begin(9600);
  Serial1.begin(9600); // Nextion

  g_sensors.begin();
  g_dht.begin();

  const uint8_t outputs[] = {
    PIN_HP_CH, PIN_HP_COOL, PIN_BOILER, PIN_CIRC_1,
    PIN_CIRC_2, PIN_SOLAR_PUMP, PIN_OVERHEAT_VALVE
  };
  for (uint8_t p : outputs) pinMode(p, OUTPUT);

  pinMode(PIN_DEFROST, INPUT_PULLUP);
  pinMode(PIN_HP_FAIL, INPUT_PULLUP);

  performSafeShutdown();
  Serial.println(F("HVAC Control System Online"));
}

// =============================================================================
// Logic Execution
// =============================================================================

void loop() {
  processHmiInput();

  static uint32_t lastTaskTime = 0;
  if (millis() - lastTaskTime >= 3000) {
    updateTelemetry();
    executeControlLogic();
    refreshHmiDisplay();
    printDebugTelemetry();
    lastTaskTime = millis();
  }
}

/**
 * @brief Samples all sensors and validates data.
 */
void updateTelemetry() {
  g_sensors.requestTemperatures();

  g_data.tankOutlet = g_sensors.getTempFByIndex(0);
  g_data.tankInlet = g_sensors.getTempFByIndex(1);
  g_data.ambient = g_sensors.getTempFByIndex(2);
  g_data.dhwTank = g_sensors.getTempFByIndex(3);
  g_data.solarCollector = g_sensors.getTempFByIndex(4);

  float h = g_dht.readHumidity();
  float t = g_dht.readTemperature(true);

  if (!isnan(h) && !isnan(t)) {
    g_data.humidity = h;
    // Magnus-Tetens approximation
    float tempC = (t - 32.0f) * 5.0f / 9.0f;
    const float a = 17.625f, b = 243.04f;
    float alpha = log(h / 100.0f) + (a * tempC) / (b + tempC);
    g_data.dewPoint = ((b * alpha) / (a - alpha) * 9.0f / 5.0f) + 32.0f;
  }
}

/**
 * @brief Core decision making for HVAC and Solar.
 */
void executeControlLogic() {
  // 1. Hardware Safety Check
  if (digitalRead(PIN_HP_FAIL) == LOW) {
    handleSystemError(F("Heat Pump Hardware Failure"));
    return;
  }

  // 2. Sensor Integrity Check
  if (g_data.ambient == DEVICE_DISCONNECTED_F || g_data.tankOutlet == DEVICE_DISCONNECTED_F) {
    handleSystemError(F("Critical Temperature Sensor Lost"));
    return;
  }

  if (digitalRead(PIN_DEFROST) == LOW) {
    g_currentMode = SystemMode::DEFROST;
    performSafeShutdown();
    return;
  }

  const float deltaT = abs(g_data.tankOutlet - g_data.tankInlet);

  // 3. HVAC Seasonal Management
  if (g_data.ambient < Config::HEATING_THRESHOLD) {
    processHeating(deltaT);
  } else if (g_data.ambient > Config::COOLING_THRESHOLD) {
    processCooling(deltaT);
  } else {
    g_currentMode = SystemMode::OFF;
    performSafeShutdown();
  }

  // 4. Independent Solar Logic
  processSolar();
}

void processHeating(float deltaT) {
  digitalWrite(PIN_CIRC_1, HIGH);
  digitalWrite(PIN_CIRC_2, HIGH);

  bool hpOk = (g_data.ambient >= Config::HP_MIN_AMBIENT && g_data.ambient > Config::HP_CRITICAL_LOW);
  bool boilerLocked = (g_boilerStartTime > 0 && (millis() - g_boilerStartTime < Config::BOILER_MIN_RUNTIME));

  // 1. Forced Boiler Takeover (High Delta T or Lock-in)
  if (deltaT >= Config::DELTA_T_HEATING_ON || boilerLocked) {
    if (g_boilerStartTime == 0) g_boilerStartTime = millis();

    digitalWrite(PIN_HP_CH, LOW);
    digitalWrite(PIN_HP_COOL, LOW);
    digitalWrite(PIN_BOILER, HIGH);
    g_currentMode = SystemMode::BOILER_HEATING;
    return;
  }

  // Reset lock if we are below threshold and dwell time expired
  if (deltaT <= Config::DELTA_T_HEATING_OFF) {
    g_boilerStartTime = 0;
  }

  // 2. Normal Priority Logic (HP First)
  if (hpOk && g_currentMode != SystemMode::ERROR) {
    if (g_data.tankOutlet < Config::HEATING_MIN_OUTLET) {
      g_currentMode = SystemMode::HP_HEATING;
      digitalWrite(PIN_HP_COOL, LOW);
      digitalWrite(PIN_HP_CH, HIGH);
      digitalWrite(PIN_BOILER, LOW);
    } else if (deltaT <= Config::DELTA_T_HEATING_OFF) {
      digitalWrite(PIN_HP_CH, LOW);
      digitalWrite(PIN_BOILER, LOW);
      g_currentMode = SystemMode::OFF;
    }
  } else {
    // Boiler Backup (Hardware Fail or Critical Cold)
    digitalWrite(PIN_HP_CH, LOW);
    if (g_data.tankOutlet < Config::HEATING_MIN_OUTLET) {
      digitalWrite(PIN_BOILER, HIGH);
      if (g_currentMode != SystemMode::ERROR) g_currentMode = SystemMode::BOILER_HEATING;
    } else {
      digitalWrite(PIN_BOILER, LOW);
    }
  }
}

void processCooling(float deltaT) {
  digitalWrite(PIN_CIRC_1, HIGH);
  digitalWrite(PIN_CIRC_2, HIGH);
  digitalWrite(PIN_BOILER, LOW);

  bool inRange = (g_data.tankInlet >= Config::COOLING_MIN_INLET && g_data.tankInlet <= Config::COOLING_MAX_INLET);
  bool safeDew = (g_data.tankOutlet >= (g_data.dewPoint + Config::DEW_POINT_BUFFER));

  if (inRange && safeDew) {
    if (deltaT >= Config::DELTA_T_COOLING_ON) {
      g_currentMode = SystemMode::HP_COOLING;
      digitalWrite(PIN_HP_CH, LOW);
      digitalWrite(PIN_HP_COOL, HIGH);
    } else if (deltaT <= Config::DELTA_T_COOLING_OFF) {
      digitalWrite(PIN_HP_COOL, LOW);
      g_currentMode = SystemMode::OFF;
    }
  } else {
    digitalWrite(PIN_HP_COOL, LOW);
  }
}

void processSolar() {
  if (g_data.solarCollector == DEVICE_DISCONNECTED_F || g_data.dhwTank == DEVICE_DISCONNECTED_F) {
    digitalWrite(PIN_SOLAR_PUMP, LOW);
    g_solarActive = false;
    return;
  }

  if ((g_data.solarCollector - g_data.dhwTank) >= Config::SOLAR_DELTA_T_ON &&
      g_data.dhwTank < Config::DHW_MAX_TEMP) {
    g_solarActive = true;
    digitalWrite(PIN_SOLAR_PUMP, HIGH);
  } else {
    g_solarActive = false;
    digitalWrite(PIN_SOLAR_PUMP, LOW);
  }
}

// =============================================================================
// HMI & Debug Communication
// =============================================================================

/**
 * @brief Prints all current telemetry to the Serial Monitor for debugging.
 */
void printDebugTelemetry() {
  Serial.println(F("--- DEBUG TELEMETRY ---"));
  Serial.print(F("Tank Inlet: ")); Serial.print(g_data.tankInlet); Serial.println(F(" F"));
  Serial.print(F("Tank Outlet: ")); Serial.print(g_data.tankOutlet); Serial.println(F(" F"));
  Serial.print(F("DHW Tank: ")); Serial.print(g_data.dhwTank); Serial.println(F(" F"));
  Serial.print(F("Ambient: ")); Serial.print(g_data.ambient); Serial.println(F(" F"));
  Serial.print(F("Solar Coll: ")); Serial.print(g_data.solarCollector); Serial.println(F(" F"));
  Serial.print(F("Humidity: ")); Serial.print(g_data.humidity); Serial.println(F(" %"));
  Serial.print(F("Dew Point: ")); Serial.print(g_data.dewPoint); Serial.println(F(" F"));
  Serial.println(F("-----------------------"));
}

void sendHmiNum(const char* name, int val) {
  Serial1.print(name);
  Serial1.print(F(".val="));
  Serial1.print(val);
  Serial1.write(NEXTION_END, 3);
}

void sendHmiTxt(const char* name, const char* txt) {
  Serial1.print(name);
  Serial1.print(F(".txt=\""));
  Serial1.print(txt);
  Serial1.print(F("\""));
  Serial1.write(NEXTION_END, 3);
}

void refreshHmiDisplay() {
  // 1. Update Numeric Fields (n0 - n5) using optimized iteration
  const float* telemetryRefs[] = {
    &g_data.tankInlet, &g_data.tankOutlet, &g_data.dhwTank,
    &g_data.ambient, &g_data.solarCollector, &g_data.humidity
  };

  char cmdBuffer[3] = {'n', '0', '\0'};
  for (uint8_t i = 0; i < 6; i++) {
    cmdBuffer[1] = '0' + i;
    sendHmiNum(cmdBuffer, (int)(*telemetryRefs[i]));
  }

  // 2. Update Status Text Fields (t0 - t3)
  const char* statusStr = "SYSTEM OFF";
  switch (g_currentMode) {
    case SystemMode::HP_COOLING:     statusStr = "HP COOLING"; break;
    case SystemMode::HP_HEATING:     statusStr = "HP HEATING"; break;
    case SystemMode::BOILER_HEATING: statusStr = "BOILER ON";  break;
    case SystemMode::DEFROST:        statusStr = "DEFROSTING"; break;
    case SystemMode::ERROR:          statusStr = "SYS ERROR";  break;
  }

  sendHmiTxt("t0", statusStr);
  sendHmiTxt("t1", (digitalRead(PIN_BOILER) == HIGH) ? "BOILER ACT" : "BOILER OFF");
  sendHmiTxt("t2", g_solarActive ? "SOLAR ON" : "SOLAR OFF");
  sendHmiTxt("t3", (g_data.dhwTank >= Config::DHW_MAX_TEMP) ? "DHW OVERHEAT" : "DHW NORMAL");
}

/**
 * @brief Dispatches commands received from the Nextion HMI.
 * @param cmd The command character ('0'-'9')
 */
void dispatchHmiCommand(char cmd) {
  Serial.print(F("HMI Command Executing: "));
  Serial.println(cmd);

  switch (cmd) {
    case '0': // Normal / Reset
      if (g_currentMode == SystemMode::ERROR) {
        g_currentMode = SystemMode::OFF;
        Serial.println(F("System Error Cleared by HMI"));
      }
      break;
    default:
      Serial.println(F("Unhandled HMI Command"));
      break;
  }
}

/**
 * @brief Listens and parses serial data from the Nextion display.
 */
void processHmiInput() {
  while (Serial1.available()) {
    uint8_t c = Serial1.read();

    if (c >= '0' && c <= '9') {
      dispatchHmiCommand((char)c);
    } else if (c == 0x65) { // Nextion Touch Event Prefix
      uint8_t buffer[6];
      Serial1.readBytes(buffer, 6);
    }
  }
}
