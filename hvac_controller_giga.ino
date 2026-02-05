#include <Nextion.h>
#include <OneWire.h>
#include <DallasTemperature.h>

/**
 * Arduino Giga R1 HVAC Controller with Nextion HMI
 *
 * Features:
 * - Nextion HMI integration: Reads a dynamic setpoint and updates a feedback field.
 * - HVAC Modes: Heating, Cooling, Defrost, and Error.
 * - Solar DHW: Automatic control of solar circulation pump.
 * - Hardware: Optimized for Arduino Giga R1 using Hardware Serial1 for Nextion.
 */

// --- Nextion Configuration ---
// Components: [page id, component id, component name]
NexNumber nSetpoint = NexNumber(0, 1, "nSetpoint"); // Input field for Setpoint
NexNumber nStatus   = NexNumber(0, 2, "nStatus");   // Output field for Status/Confirmation

// Register Nextion objects to the listen list
NexTouch *nex_listen_list[] = {
  &nSetpoint,
  NULL
};

// --- Hardware Pins (Arduino Giga R1) ---
const int ONE_WIRE_BUS = 2;          // DS18B20 data line
const int HP_HEATING_PIN = 3;        // Heat Pump Heating Mode
const int HP_COOLING_PIN = 4;        // Heat Pump Cooling Mode
const int HP_OFF_PIN = 5;            // Heat Pump Off Signal
const int BOILER_PIN = 6;            // Backup Boiler
const int CIRC_PUMP_PIN = 7;         // Main Circulation Pump
const int SOLAR_PUMP_PIN = 14;       // Solar DHW Pump (Pin 14 / A0)
const int DEFROST_IN_PIN = 9;        // Input from HP for Defrost Mode
const int HP_FAIL_IN_PIN = 11;       // Input from HP for Error/Failure

// --- System Thresholds ---
float currentSetpoint = 72.0;        // Default Setpoint in Fahrenheit
const float HEATING_THRESHOLD = 65.0; // Outside temp threshold to switch to Heating
const float SOLAR_ON_DELTA = 30.0;   // Temp difference to start solar pump
const float SOLAR_OFF_DELTA = 15.0;  // Temp difference to stop solar pump
const float DHW_MAX_TEMP = 140.0;    // Maximum DHW tank temperature

// --- System State ---
enum SystemMode { MODE_HEATING, MODE_COOLING, MODE_DEFROST, MODE_ERROR };
SystemMode currentMode = MODE_HEATING;
bool solarPumpState = false;

// --- Sensors ---
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Temperature variables
float tempOutside = 70.0;
float tempOutlet = 70.0;
float tempDhwTank = 110.0;
float tempSolarCollector = 100.0;

// --- Nextion Callbacks ---
/**
 * Callback triggered when nSetpoint is released on the Nextion screen.
 */
void nSetpointPopCallback(void *ptr) {
  uint32_t value;
  if (nSetpoint.getValue(&value)) {
    currentSetpoint = (float)value;
    // Update the status field on Nextion to confirm the update
    nStatus.setValue(value);
    Serial.print("Setpoint updated to: ");
    Serial.println(currentSetpoint);
  }
}

// ====================== Setup ====================== //
void setup() {
  // Debug Serial
  Serial.begin(115200);

  // Nextion Communication
  // On Arduino Giga R1, ensure the Nextion library is configured to use Serial1
  // You may need to edit NexConfig.h in the library folder.
  nexInit();

  // Initialize Sensors
  sensors.begin();

  // Configure Pins
  pinMode(HP_HEATING_PIN, OUTPUT);
  pinMode(HP_COOLING_PIN, OUTPUT);
  pinMode(HP_OFF_PIN, OUTPUT);
  pinMode(BOILER_PIN, OUTPUT);
  pinMode(CIRC_PUMP_PIN, OUTPUT);
  pinMode(SOLAR_PUMP_PIN, OUTPUT);

  pinMode(DEFROST_IN_PIN, INPUT_PULLUP);
  pinMode(HP_FAIL_IN_PIN, INPUT_PULLUP);

  // Attach Nextion callback
  nSetpoint.attachPop(nSetpointPopCallback, &nSetpoint);

  Serial.println("System Initialized");
}

// ====================== Main Loop ====================== //
void loop() {
  // Process Nextion events
  nexLoop(nex_listen_list);

  // System update every 2 seconds
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate >= 2000) {
    lastUpdate = millis();

    performReadings();
    updateSystemState();
    controlHVAC();
    controlSolarDHW();
    reportToNextion();
  }
}

// ====================== Core Functions ====================== //

void performReadings() {
  sensors.requestTemperatures();

  // Assumes sensors are connected in a specific order or use specific addresses
  tempOutside = sensors.getTempFByIndex(0);
  tempOutlet = sensors.getTempFByIndex(1);
  tempDhwTank = sensors.getTempFByIndex(2);
  tempSolarCollector = sensors.getTempFByIndex(3);

  // Safety check for disconnected sensors
  if (tempOutside < -100 || tempOutlet < -100) {
    currentMode = MODE_ERROR;
  }
}

void updateSystemState() {
  // Check for Hardware Faults first
  if (digitalRead(HP_FAIL_IN_PIN) == LOW) {
    currentMode = MODE_ERROR;
  }
  // Check for Defrost Signal
  else if (digitalRead(DEFROST_IN_PIN) == LOW) {
    currentMode = MODE_DEFROST;
  }
  // Otherwise, determine mode based on outside temperature
  else {
    if (tempOutside < HEATING_THRESHOLD) {
      currentMode = MODE_HEATING;
    } else {
      currentMode = MODE_COOLING;
    }
  }
}

void controlHVAC() {
  switch (currentMode) {
    case MODE_HEATING:
      if (tempOutlet < (currentSetpoint - 2.0)) {
        // Demand for heat
        digitalWrite(HP_HEATING_PIN, HIGH);
        digitalWrite(HP_COOLING_PIN, LOW);
        digitalWrite(HP_OFF_PIN, LOW);
        digitalWrite(BOILER_PIN, HIGH); // Assist with boiler
      } else if (tempOutlet >= currentSetpoint) {
        // Temperature reached
        digitalWrite(HP_HEATING_PIN, LOW);
        digitalWrite(HP_OFF_PIN, HIGH);
        digitalWrite(BOILER_PIN, LOW);
      }
      break;

    case MODE_COOLING:
      if (tempOutlet > (currentSetpoint + 2.0)) {
        // Demand for cooling
        digitalWrite(HP_COOLING_PIN, HIGH);
        digitalWrite(HP_HEATING_PIN, LOW);
        digitalWrite(HP_OFF_PIN, LOW);
      } else if (tempOutlet <= currentSetpoint) {
        // Temperature reached
        digitalWrite(HP_COOLING_PIN, LOW);
        digitalWrite(HP_OFF_PIN, HIGH);
      }
      digitalWrite(BOILER_PIN, LOW); // Never use boiler in cooling
      break;

    case MODE_DEFROST:
      // Heat pump handles defrost internally, but we might turn on boiler to maintain house temp
      digitalWrite(HP_OFF_PIN, HIGH); // Stop normal HP operation
      digitalWrite(BOILER_PIN, HIGH); // Boiler takes over load
      break;

    case MODE_ERROR:
      // Safety shutdown
      digitalWrite(HP_OFF_PIN, HIGH);
      digitalWrite(HP_HEATING_PIN, LOW);
      digitalWrite(HP_COOLING_PIN, LOW);
      digitalWrite(BOILER_PIN, LOW);
      break;
  }

  // Keep circulator pump running if not in error
  digitalWrite(CIRC_PUMP_PIN, (currentMode != MODE_ERROR) ? HIGH : LOW);
}

void controlSolarDHW() {
  float delta = tempSolarCollector - tempDhwTank;

  if (tempDhwTank < DHW_MAX_TEMP && delta >= SOLAR_ON_DELTA) {
    solarPumpState = true;
  } else if (delta <= SOLAR_OFF_DELTA || tempDhwTank >= DHW_MAX_TEMP) {
    solarPumpState = false;
  }

  digitalWrite(SOLAR_PUMP_PIN, solarPumpState ? HIGH : LOW);
}

void reportToNextion() {
  // You could also update other fields on Nextion here
  // e.g. nTempOutside.setValue((uint32_t)tempOutside);
}
