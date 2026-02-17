#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>
#include <EEPROM.h>

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
  float HEATING_THRESHOLD = 65.0f;
  float COOLING_THRESHOLD = 70.0f;
  float HP_MIN_AMBIENT = 5.0f;
  constexpr float HP_CRITICAL_LOW = -4.0f;
  constexpr float DELTA_T_HEATING_OFF = 20.0f;
  constexpr float DELTA_T_HEATING_ON = 25.0f;
  constexpr float DELTA_T_COOLING_OFF = 5.0f;
  constexpr float DELTA_T_COOLING_ON = 10.0f;
  constexpr float DEW_POINT_BUFFER = 2.0f;
  constexpr float HEATING_MIN_OUTLET = 100.0f;
  constexpr float COOLING_MIN_INLET = 42.0f;
  constexpr float COOLING_MAX_INLET = 65.0f;
  float DHW_MAX_TEMP = 140.0f;
  float SOLAR_DELTA_T_ON = 30.0f;
  uint32_t BOILER_MIN_RUNTIME = 600000; // 10 minutes in ms

  const uint32_t MAGIC_ID = 0xDEEB5EE;

  struct Settings {
    uint32_t magic;
    float heating;
    float cooling;
    float minAmbient;
    float dhwMax;
    float solarDelta;
    uint32_t boilerRuntime;
  };

  void save() {
    Settings s = { MAGIC_ID, HEATING_THRESHOLD, COOLING_THRESHOLD, HP_MIN_AMBIENT, DHW_MAX_TEMP, SOLAR_DELTA_T_ON, BOILER_MIN_RUNTIME };
    EEPROM.put(0, s);
  }

  void resetToDefaults() {
    HEATING_THRESHOLD = 65.0f;
    COOLING_THRESHOLD = 70.0f;
    HP_MIN_AMBIENT = 5.0f;
    DHW_MAX_TEMP = 140.0f;
    SOLAR_DELTA_T_ON = 30.0f;
    BOILER_MIN_RUNTIME = 600000;
    save();
  }

  void load() {
    Settings s;
    EEPROM.get(0, s);
    if (s.magic == MAGIC_ID) {
      HEATING_THRESHOLD = s.heating;
      COOLING_THRESHOLD = s.cooling;
      HP_MIN_AMBIENT = s.minAmbient;
      DHW_MAX_TEMP = s.dhwMax;
      SOLAR_DELTA_T_ON = s.solarDelta;
      BOILER_MIN_RUNTIME = s.boilerRuntime;
    } else {
      resetToDefaults();
    }
  }
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

  Config::load();
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
  bool hpOk = (g_data.ambient >= Config::HP_MIN_AMBIENT && g_data.ambient > Config::HP_CRITICAL_LOW);
  bool boilerLocked = (g_boilerStartTime > 0 && (millis() - g_boilerStartTime < Config::BOILER_MIN_RUNTIME));

  // Safety: High-limit thermostat cutoff to prevent runaway conditions
  if (g_data.tankOutlet >= (Config::HEATING_MIN_OUTLET + 10.0f)) {
    digitalWrite(PIN_BOILER, LOW);
    digitalWrite(PIN_HP_CH, LOW);
    digitalWrite(PIN_CIRC_1, LOW);
    digitalWrite(PIN_CIRC_2, LOW);
    g_currentMode = SystemMode::OFF;
    g_boilerStartTime = 0;
    return;
  }

  // 1. Forced Boiler Takeover (High Delta T or Lock-in)
  if (deltaT >= Config::DELTA_T_HEATING_ON || boilerLocked) {
    if (g_boilerStartTime == 0) g_boilerStartTime = millis();

    digitalWrite(PIN_HP_CH, LOW);
    digitalWrite(PIN_HP_COOL, LOW);
    digitalWrite(PIN_BOILER, HIGH);
    digitalWrite(PIN_CIRC_1, HIGH);
    digitalWrite(PIN_CIRC_2, HIGH);
    g_currentMode = SystemMode::BOILER_HEATING;
    return;
  }

  // Reset lock if demand is satisfied or dwell time expired
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
      digitalWrite(PIN_CIRC_1, HIGH);
      digitalWrite(PIN_CIRC_2, HIGH);
    } else {
      // Sufficient temperature or low demand
      digitalWrite(PIN_HP_CH, LOW);
      digitalWrite(PIN_BOILER, LOW);
      digitalWrite(PIN_CIRC_1, LOW);
      digitalWrite(PIN_CIRC_2, LOW);
      g_currentMode = SystemMode::OFF;
    }
  } else {
    // Boiler Backup (Hardware Fail or Critical Cold)
    digitalWrite(PIN_HP_CH, LOW);
    if (g_data.tankOutlet < Config::HEATING_MIN_OUTLET) {
      digitalWrite(PIN_BOILER, HIGH);
      digitalWrite(PIN_CIRC_1, HIGH);
      digitalWrite(PIN_CIRC_2, HIGH);
      if (g_currentMode != SystemMode::ERROR) g_currentMode = SystemMode::BOILER_HEATING;
    } else {
      digitalWrite(PIN_BOILER, LOW);
      digitalWrite(PIN_CIRC_1, LOW);
      digitalWrite(PIN_CIRC_2, LOW);
      g_currentMode = SystemMode::OFF;
    }
  }
}

void processCooling(float deltaT) {
  digitalWrite(PIN_BOILER, LOW);

  bool inRange = (g_data.tankInlet >= Config::COOLING_MIN_INLET && g_data.tankInlet <= Config::COOLING_MAX_INLET);
  bool safeDew = (g_data.tankOutlet >= (g_data.dewPoint + Config::DEW_POINT_BUFFER));

  if (inRange && safeDew) {
    if (deltaT >= Config::DELTA_T_COOLING_ON) {
      g_currentMode = SystemMode::HP_COOLING;
      digitalWrite(PIN_HP_CH, LOW);
      digitalWrite(PIN_HP_COOL, HIGH);
      digitalWrite(PIN_CIRC_1, HIGH);
      digitalWrite(PIN_CIRC_2, HIGH);
    } else if (deltaT <= Config::DELTA_T_COOLING_OFF || g_currentMode != SystemMode::HP_COOLING) {
      // Demand satisfied or hysteresis threshold met
      digitalWrite(PIN_HP_COOL, LOW);
      digitalWrite(PIN_CIRC_1, LOW);
      digitalWrite(PIN_CIRC_2, LOW);
      g_currentMode = SystemMode::OFF;
    }
  } else {
    digitalWrite(PIN_HP_COOL, LOW);
    digitalWrite(PIN_CIRC_1, LOW);
    digitalWrite(PIN_CIRC_2, LOW);
    g_currentMode = SystemMode::OFF;
  }
}

void processSolar() {
  if (g_data.solarCollector == DEVICE_DISCONNECTED_F || g_data.dhwTank == DEVICE_DISCONNECTED_F) {
    digitalWrite(PIN_SOLAR_PUMP, LOW);
    digitalWrite(PIN_OVERHEAT_VALVE, LOW);
    g_solarActive = false;
    return;
  }

  // Overheat Protection
  if (g_data.dhwTank >= Config::DHW_MAX_TEMP) {
    g_solarActive = false;
    digitalWrite(PIN_SOLAR_PUMP, LOW);
    digitalWrite(PIN_OVERHEAT_VALVE, HIGH);
    return;
  } else {
    digitalWrite(PIN_OVERHEAT_VALVE, LOW);
  }

  // Normal Differential Logic
  if ((g_data.solarCollector - g_data.dhwTank) >= Config::SOLAR_DELTA_T_ON) {
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
 * @brief Returns a string representation of the current system mode.
 */
const char* getSystemModeStr() {
  switch (g_currentMode) {
    case SystemMode::HP_COOLING:     return "HP COOLING";
    case SystemMode::HP_HEATING:     return "HP HEATING";
    case SystemMode::BOILER_HEATING: return "BOILER ON";
    case SystemMode::DEFROST:        return "DEFROSTING";
    case SystemMode::ERROR:          return "SYS ERROR";
    default:                         return "SYSTEM OFF";
  }
}

/**
 * @brief Returns a string representation of the DHW status.
 */
const char* getDhwStatusStr() {
  if (g_data.dhwTank >= Config::DHW_MAX_TEMP) {
    return "DHW OVERHEAT";
  } else if (g_solarActive) {
    return "DHW ON";
  }
  return "DHW OFF";
}

/**
 * @brief Returns a string representation of the Boiler status.
 */
const char* getBoilerStatusStr() {
  return (digitalRead(PIN_BOILER) == HIGH) ? "BOILER ACT" : "BOILER OFF";
}

/**
 * @brief Returns a string representation of the System Health status.
 */
const char* getSystemHealthStr() {
  return (g_currentMode == SystemMode::ERROR) ? "SYS FAULT" : "HEALTH OK";
}

/**
 * @brief Prints all current telemetry to the Serial Monitor for debugging.
 */
void printDebugTelemetry() {
  Serial.println(F("--- DEBUG TELEMETRY ---"));

  Serial.print(F("System Mode (t0): ")); Serial.println(getSystemModeStr());
  Serial.print(F("DHW Status (t1): ")); Serial.println(getDhwStatusStr());
  Serial.print(F("Boiler Status (t2): ")); Serial.println(getBoilerStatusStr());
  Serial.print(F("System Health (t3): ")); Serial.println(getSystemHealthStr());

  Serial.print(F("n0 (Ambient): ")); Serial.print(g_data.ambient); Serial.println(F(" F"));
  Serial.print(F("n1 (Inlet): ")); Serial.print(g_data.tankInlet); Serial.println(F(" F"));
  Serial.print(F("n2 (Outlet): ")); Serial.print(g_data.tankOutlet); Serial.println(F(" F"));
  Serial.print(F("n3 (DHW Tank): ")); Serial.print(g_data.dhwTank); Serial.println(F(" F"));
  Serial.print(F("n4 (Solar Coll): ")); Serial.print(g_data.solarCollector); Serial.println(F(" F"));
  Serial.print(F("n5 (Humidity): ")); Serial.print(g_data.humidity); Serial.println(F(" %"));
  Serial.print(F("n6 (Dew Point): ")); Serial.print(g_data.dewPoint); Serial.println(F(" F"));

  Serial.println(F("--- HARDWARE OUTPUTS ---"));
  Serial.print(F("HP CH (Pin 3): ")); Serial.println(digitalRead(PIN_HP_CH) == HIGH ? F("ON") : F("OFF"));
  Serial.print(F("HP COOL (Pin 4): ")); Serial.println(digitalRead(PIN_HP_COOL) == HIGH ? F("ON") : F("OFF"));
  Serial.print(F("BOILER (Pin 6): ")); Serial.println(digitalRead(PIN_BOILER) == HIGH ? F("ON") : F("OFF"));
  Serial.print(F("CIRC 1 (Pin 7): ")); Serial.println(digitalRead(PIN_CIRC_1) == HIGH ? F("ON") : F("OFF"));
  Serial.print(F("CIRC 2 (Pin 8): ")); Serial.println(digitalRead(PIN_CIRC_2) == HIGH ? F("ON") : F("OFF"));
  Serial.print(F("SOLAR PUMP (Pin 14): ")); Serial.println(digitalRead(PIN_SOLAR_PUMP) == HIGH ? F("ON") : F("OFF"));
  Serial.print(F("OVHT VALVE (Pin 15): ")); Serial.println(digitalRead(PIN_OVERHEAT_VALVE) == HIGH ? F("ON") : F("OFF"));
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

/**
 * @brief Sends current configuration thresholds to the HMI settings page.
 */
void syncSettingsToHmi() {
  // Current Active Thresholds (Display Labels)
  sendHmiNum("n10", (int)Config::HEATING_THRESHOLD);
  sendHmiNum("n11", (int)Config::COOLING_THRESHOLD);
  sendHmiNum("n12", (int)Config::HP_MIN_AMBIENT);
  sendHmiNum("n13", (int)Config::DHW_MAX_TEMP);
  sendHmiNum("n14", (int)Config::SOLAR_DELTA_T_ON);
  sendHmiNum("n15", (int)(Config::BOILER_MIN_RUNTIME / 60000));

  // Initialize Input Fields (Editable)
  sendHmiNum("n20", (int)Config::HEATING_THRESHOLD);
  sendHmiNum("n21", (int)Config::COOLING_THRESHOLD);
  sendHmiNum("n22", (int)Config::HP_MIN_AMBIENT);
  sendHmiNum("n23", (int)Config::DHW_MAX_TEMP);
  sendHmiNum("n24", (int)Config::SOLAR_DELTA_T_ON);
  sendHmiNum("n25", (int)(Config::BOILER_MIN_RUNTIME / 60000));
}

void refreshHmiDisplay() {
  // 1. Update Numeric Fields (n0 - n5) using optimized iteration
  const float* telemetryRefs[] = {
    &g_data.ambient, &g_data.tankInlet, &g_data.tankOutlet,
    &g_data.dhwTank, &g_data.solarCollector, &g_data.humidity,
    &g_data.dewPoint
  };

  char cmdBuffer[3] = {'n', '0', '\0'};
  for (uint8_t i = 0; i < 7; i++) {
    cmdBuffer[1] = '0' + i;
    sendHmiNum(cmdBuffer, (int)(*telemetryRefs[i]));
  }

  // 2. Update Status Text Fields (t0 - t3)
  sendHmiTxt("t0", getSystemModeStr());
  sendHmiTxt("t1", getDhwStatusStr());
  sendHmiTxt("t2", getBoilerStatusStr());
  sendHmiTxt("t3", getSystemHealthStr());
}

/**
 * @brief Dispatches commands received from the Nextion HMI.
 * @param cmd Command string
 */
void dispatchHmiCommand(const String& cmd) {
  if (cmd.startsWith(F("SET:"))) {
    // Expected format: SET:ID:VAL
    int firstColon = cmd.indexOf(':');
    int lastColon = cmd.lastIndexOf(':');
    if (firstColon != -1 && lastColon != -1 && firstColon != lastColon) {
      int id = cmd.substring(firstColon + 1, lastColon).toInt();
      float val = cmd.substring(lastColon + 1).toFloat();
      switch (id) {
        case 0: Config::HEATING_THRESHOLD = val; break;
        case 1: Config::COOLING_THRESHOLD = val; break;
        case 2: Config::HP_MIN_AMBIENT = val; break;
        case 3: Config::DHW_MAX_TEMP = val; break;
        case 4: Config::SOLAR_DELTA_T_ON = val; break;
        case 5: Config::BOILER_MIN_RUNTIME = (uint32_t)(val * 60000); break;
      }
      Config::save();
      Serial.print(F("Config updated and saved via HMI: ID ")); Serial.print(id); Serial.print(F(" = ")); Serial.println(val);
    }
  } else if (cmd == F("FACTORY")) {
    Config::resetToDefaults();
    syncSettingsToHmi();
    Serial.println(F("Factory Reset applied via HMI"));
  } else if (cmd == F("SYNC")) {
    syncSettingsToHmi();
  } else if (cmd == F("0")) {
    if (g_currentMode == SystemMode::ERROR) {
      g_currentMode = SystemMode::OFF;
      Serial.println(F("System Error Cleared by HMI"));
    }
  }
}

/**
 * @brief Listens and parses serial data from the Nextion display.
 */
void processHmiInput() {
  static String incomingCmd = "";
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n') {
      incomingCmd.trim();
      if (incomingCmd.length() > 0) dispatchHmiCommand(incomingCmd);
      incomingCmd = "";
    } else if (c >= 32 && c <= 126) {
      incomingCmd += c;
    }
  }
}
