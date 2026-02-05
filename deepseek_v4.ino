#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>
#include <KVStore.h>
#include <WiFi.h>
#include <ArduinoHttpClient.h>

// Firebase project API key
#define API_KEY "YOUR_API_KEY"

// Firebase project database URL
#define DATABASE_URL "YOUR_DATABASE_URL"
#define FIREBASE_HOST "your-project-id.firebaseio.com"
#define FIREBASE_AUTH "YOUR_DATABASE_SECRET"

// WiFi credentials
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// WiFi and HTTP clients
WiFiClient wifi;
HttpClient client = HttpClient(wifi, FIREBASE_HOST, 443);

// Pin definitions
const int ONE_WIRE_BUS_1 = 2;        // OneWire temperature sensors (Bus 1)
const int ONE_WIRE_BUS_2 = 21;       // OneWire temperature sensors (Bus 2)
const int DHT_PIN = 10;              // DHT22 sensor pin
const int HEAT_PUMP_CH_PIN = 3;      // Heat pump CH (Central Heating) mode
const int HEAT_PUMP_COOLING_PIN = 4; // Heat pump cooling mode
const int HEAT_PUMP_OFF_PIN = 5;     // Heat pump off signal
const int BOILER_PIN = 6;            // Boiler control
const int CONCRETE_CIRCULATOR_PUMP_PIN = 7;   // Circulator pump for concrete
const int FAN_COILS_CIRCULATOR_PUMP_PIN = 8;     // Circulator pump for fan coils
const int DEFROST_INPUT_PIN = 9;     // Defrost mode input signal
const int HEAT_PUMP_FAIL_PIN = 11;   // Heat pump failure input signal
const int SOLAR_CIRCULATOR_PUMP_PIN = 14; // Solar circulator pump control
const int OVERHEAT_VALVE_PIN = 15;   // Overheat one-way valve control
const int DHW_TO_BUFFER_PUMP_PIN = 16; // Pump to transfer heat from DHW to buffer tank
const int HEAT_PUMP_DHW_PIN = 17;    // Heat pump DHW mode

// Temperature thresholds (default values)
const float DEFAULT_OUTSIDE_HEATING_THRESHOLD = 65.0f;  // Heating mode if outside temp < 65°F
const float DEFAULT_HEAT_PUMP_MIN_TEMP = 5.0f;          // Heat pump CH mode minimum outside temp
const float DEFAULT_HEAT_PUMP_OFF_TEMP = -4.0f;         // Heat pump turns off below this outside temp
const float DEFAULT_DELTA_T_HEATING_THRESHOLD = 20.0f;  // Delta T threshold for heating mode
const float DEFAULT_DELTA_T_COOLING_THRESHOLD = 5.0f;   // Delta T threshold for cooling mode
const float DEFAULT_DELTA_T_HEATING_ASSIST = 25.0f;     // Boiler assist threshold in heating mode
const float DEFAULT_DELTA_T_COOLING_ON = 10.0f;         // Delta T threshold to turn on heat pump in cooling mode
const float DEFAULT_DELTA_T_HEATING_ON = 25.0f;         // Delta T threshold to turn on heat pump in heating mode
const float DEFAULT_DEW_POINT_BUFFER = 2.0f;          // 2°F buffer above dew point

// Heating mode temperature limits
const float HEATING_MAX_OUTLET_TEMP = 120.0f;  // Maximum outlet temperature in heating mode
const float HEATING_MIN_OUTLET_TEMP = 100.0f;  // Minimum outlet temperature in heating mode

// Cooling mode temperature limits
const float COOLING_MAX_INLET_TEMP = 65.0f;    // Maximum inlet temperature in cooling mode
const float COOLING_MIN_INLET_TEMP = 42.0f;    // Minimum inlet temperature in cooling mode

// DHW tank and solar collector thresholds
const float DHW_OVERHEAT_TEMP = 140.0f;        // Overheat temperature for DHW tank
const float SOLAR_DHW_DELTA_T = 30.0f;         // Temperature difference to activate solar circulator pump

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

// Configuration structure
struct SystemConfig {
  float outsideHeatingThreshold;
  float heatPumpMinTemp;
  float heatPumpOffTemp;
  float deltaTHeatingThreshold;
  float deltaTCoolingThreshold;
  float deltaTHeatingAssist;
  float deltaTCoolingOn;
  float deltaTHeatingOn;
  float dewPointBuffer;
};

// Global variables
SystemConfig config = {
  DEFAULT_OUTSIDE_HEATING_THRESHOLD,
  DEFAULT_HEAT_PUMP_MIN_TEMP,
  DEFAULT_HEAT_PUMP_OFF_TEMP,
  DEFAULT_DELTA_T_HEATING_THRESHOLD,
  DEFAULT_DELTA_T_COOLING_THRESHOLD,
  DEFAULT_DELTA_T_HEATING_ASSIST,
  DEFAULT_DELTA_T_COOLING_ON,
  DEFAULT_DELTA_T_HEATING_ON,
  DEFAULT_DEW_POINT_BUFFER
};

SystemMode currentMode = MODE_HEATING;
bool isHeatPumpFailed = false;
bool isSensorError = false;
bool solarPumpRunning = false;

// Temperature variables
float tankOutletTemp = -127.0f;
float tankInletTemp = -127.0f;
float outsideTempF = -127.0f;
float dhwTankTemp = -127.0f;
float solarCollectorTemp = -127.0f;
float utilityHumidity = 0.0f;
float utilityTempF = 0.0f;
float dewPointF = 0.0f;

// Temperature sensor addresses (replace with your actual addresses)
DeviceAddress tankOutlet = { 0x28, 0xFF, 0x64, 0x1E, 0x3C, 0x14, 0x01, 0x0A };
DeviceAddress tankInlet = { 0x28, 0xFF, 0x64, 0x1E, 0x3C, 0x14, 0x01, 0x0B };
DeviceAddress outsideTemp = { 0x28, 0xFF, 0x64, 0x1E, 0x3C, 0x14, 0x01, 0x0C };
DeviceAddress dhwTankSensor = { 0x28, 0xFF, 0x64, 0x1E, 0x3C, 0x14, 0x01, 0x0D };
DeviceAddress solarCollectorSensor = { 0x28, 0xFF, 0x64, 0x1E, 0x3C, 0x14, 0x01, 0x0E };

// OneWire and DHT setup
OneWire oneWire1(ONE_WIRE_BUS_1);
DallasTemperature sensors1(&oneWire1);
OneWire oneWire2(ONE_WIRE_BUS_2);
DallasTemperature sensors2(&oneWire2);
DHT dht(DHT_PIN, DHT22);

// Communication with Nextion display via Hardware Serial

// ====================== Function Prototypes ====================== //
void initializePins();
void sendConfigToBluetooth();
void checkSensorErrors();
void controlSolarSystem();
void checkBluetoothCommands();
void checkOverheatingProtection();
void checkFreezingProtection();
void handleHeatPumpFailure();
void setHeatPumpCH();
void setHeatPumpOff();
void setHeatPumpCooling();
void handleDefrostMode();
void handleSensorError();

// ====================== Setup ====================== //
void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);
  sensors1.begin();
  sensors2.begin();
  dht.begin();

  initializePins();

  // Start the mbed OS watchdog timer
  mbed::Watchdog &wdt = mbed::Watchdog::get_instance();
  wdt.start(8000); // 8-second timeout in milliseconds

  KVStore.begin();
  if (KVStore.get("config", config) != 0) {
    // If config doesn't exist, restore defaults and save
    restoreDefaultValues();
  }

  connectToWiFi();
  sendConfigToBluetooth();
}

// ====================== Main Loop ====================== //
void loop() {
  static unsigned long lastFirebaseSend = 0;

  mbed::Watchdog::get_instance().kick();
  readTemperatures();
  checkSensorErrors();
  updateSystemState();
  controlSolarSystem();
  checkBluetoothCommands();
  receiveDataFromNextion();

  printDebugInfo();

  if (millis() - lastFirebaseSend > 300000) {
    sendDataToFirebase();
    lastFirebaseSend = millis();
  }
}

// ====================== Core Functions ====================== //
void readTemperatures() {
  // Bus 1
  sensors1.requestTemperatures();
  tankOutletTemp = sensors1.getTempF(tankOutlet);
  tankInletTemp = sensors1.getTempF(tankInlet);
  outsideTempF = sensors1.getTempF(outsideTemp);

  // Bus 2
  sensors2.requestTemperatures();
  dhwTankTemp = sensors2.getTempF(dhwTankSensor);
  solarCollectorTemp = sensors2.getTempF(solarCollectorSensor);

  if (tankOutletTemp == -127.0f || tankInletTemp == -127.0f || outsideTempF == -127.0f ||
      dhwTankTemp == -127.0f || solarCollectorTemp == -127.0f) {
    isSensorError = true;
  } else {
    isSensorError = false;
  }

  readDHT();
}

void readDHT() {
  static unsigned long lastDHTRead = 0;
  if (millis() - lastDHTRead > 2000) {
    utilityHumidity = dht.readHumidity();
    utilityTempF = dht.readTemperature(true);
    lastDHTRead = millis();

    if (isnan(utilityHumidity) || isnan(utilityTempF)) {
      isSensorError = true;
    } else {
      isSensorError = false;
      dewPointF = calculateDewPoint(utilityTempF, utilityHumidity);
    }
  }
}

// ====================== State Machine ====================== //
void updateSystemState() {
  if (isSensorError) {
    currentMode = MODE_ERROR;
  }

  // Check for mode changes based on sensor readings and input signals
  SystemMode previousMode = currentMode;
  determineOperatingMode();

  if (currentMode != previousMode) {
    sendDataToNextion();
  } else {
    sendSensorDataToNextion();
  }

  switch (currentMode) {
    case MODE_OFF:
      controlOffMode();
      break;
    case MODE_HEAT_PUMP_COOLING:
      controlHeatPumpCoolingMode();
      break;
    case MODE_HEAT_PUMP_CH:
      controlHeatPumpCHMode();
      break;
    case MODE_HEAT_PUMP_DHW:
      controlHeatPumpDHWMode();
      break;
    case MODE_BOILER_CH:
      controlBoilerCHMode();
      break;
    case MODE_SOLAR_DHW:
      controlSolarDHWMode();
      break;
    case MODE_DEFROST:
      handleDefrostMode();
      break;
    case MODE_DHW_OVERHEAT:
      controlDHWOverheatMode();
      break;
    case MODE_ERROR:
      handleSensorError();
      break;
  }
}

void determineOperatingMode() {
  if (isOffMode()) return;
  if (isDefrostMode()) return;
  if (isDHWOverheatMode()) return;
  if (isSolarDHWMode()) return;

  bool boilerCHNeeded = isBoilerCHMode();
  bool heatPumpCHNeeded = isHeatPumpCHMode();

  if (boilerCHNeeded || heatPumpCHNeeded) {
    return;
  }

  if (isHeatPumpDHWMode()) return;
  if (isHeatPumpCoolingMode()) return;
}

bool isOffMode() {
  if (outsideTempF >= 64.0f && outsideTempF <= 70.0f) {
    currentMode = MODE_OFF;
    return true;
  }
  return false;
}

bool isDefrostMode() {
  if (digitalRead(DEFROST_INPUT_PIN) == HIGH) {
    currentMode = MODE_DEFROST;
    return true;
  }
  return false;
}

bool isDHWOverheatMode() {
  if (dhwTankTemp >= DHW_OVERHEAT_TEMP) {
    currentMode = MODE_DHW_OVERHEAT;
    return true;
  }
  return false;
}

bool isSolarDHWMode() {
  if (solarCollectorTemp > 140.0f || (solarCollectorTemp - dhwTankTemp > 40.0f)) {
    currentMode = MODE_SOLAR_DHW;
    return true;
  }
  return false;
}

bool isBoilerCHMode() {
  if (outsideTempF < 20.0f || (isHeatPumpFailed && outsideTempF < 47.0f && tankInletTemp < 80.0f)) {
    currentMode = MODE_BOILER_CH;
    return true;
  }
  return false;
}

bool isHeatPumpDHWMode() {
  if (outsideTempF >= 5.0f && outsideTempF < 72.0f && currentMode != MODE_SOLAR_DHW) {
    currentMode = MODE_HEAT_PUMP_DHW;
    return true;
  }
  return false;
}

bool isHeatPumpCoolingMode() {
  if ((outsideTempF > 72.0f && utilityHumidity > 50.0f) || (outsideTempF > 75.0f && utilityHumidity <= 50.0f)) {
    currentMode = MODE_HEAT_PUMP_COOLING;
    return true;
  }
  return false;
}

bool isHeatPumpCHMode() {
  float deltaT = calculateDeltaT(tankOutletTemp, tankInletTemp);
  if (outsideTempF >= 25.0f && outsideTempF < 32.0f) {
    if (deltaT < 11.0f && tankOutletTemp < 120.0f) {
      currentMode = MODE_HEAT_PUMP_CH;
      return true;
    }
  } else if (outsideTempF >= 32.0f && outsideTempF < 47.0f) {
    if (deltaT < 21.0f && tankOutletTemp < 110.0f) {
      currentMode = MODE_HEAT_PUMP_CH;
      return true;
    }
  } else if (outsideTempF >= 15.0f && outsideTempF < 25.0f) {
    if (deltaT < 31.0f && tankOutletTemp < 100.0f) {
      currentMode = MODE_HEAT_PUMP_CH;
      return true;
    }
  }
  return false;
}

// ====================== Mode Control Functions ====================== //

void controlOffMode() {
  setHeatPumpOff();
  digitalWrite(BOILER_PIN, LOW);
  digitalWrite(CONCRETE_CIRCULATOR_PUMP_PIN, LOW);
  digitalWrite(FAN_COILS_CIRCULATOR_PUMP_PIN, LOW);
  digitalWrite(SOLAR_CIRCULATOR_PUMP_PIN, LOW);
  digitalWrite(DHW_TO_BUFFER_PUMP_PIN, LOW);
  digitalWrite(OVERHEAT_VALVE_PIN, LOW);
}

void controlHeatPumpCoolingMode() {
  float deltaT = calculateDeltaT(tankOutletTemp, tankInletTemp);
  if (deltaT > 5.0f && tankOutletTemp > 57.0f) {
    setHeatPumpCooling();
    if (tankOutletTemp > dewPointF + config.dewPointBuffer) {
      digitalWrite(CONCRETE_CIRCULATOR_PUMP_PIN, HIGH);
      digitalWrite(FAN_COILS_CIRCULATOR_PUMP_PIN, HIGH);
    } else {
      digitalWrite(CONCRETE_CIRCULATOR_PUMP_PIN, LOW);
      digitalWrite(FAN_COILS_CIRCULATOR_PUMP_PIN, LOW);
    }
  } else {
    setHeatPumpOff();
    digitalWrite(CONCRETE_CIRCULATOR_PUMP_PIN, LOW);
    digitalWrite(FAN_COILS_CIRCULATOR_PUMP_PIN, LOW);
  }
}

void controlHeatPumpCHMode() {
  float deltaT = calculateDeltaT(tankOutletTemp, tankInletTemp);
  if (outsideTempF >= 25.0f && outsideTempF < 32.0f) {
    if (deltaT < 11.0f && tankOutletTemp < 120.0f) {
      setHeatPumpCH();
    } else {
      setHeatPumpOff();
    }
  } else if (outsideTempF >= 32.0f && outsideTempF < 47.0f) {
    if (deltaT < 21.0f && tankOutletTemp < 110.0f) {
      setHeatPumpCH();
    } else {
      setHeatPumpOff();
    }
  } else if (outsideTempF >= 15.0f && outsideTempF < 25.0f) {
    if (deltaT < 31.0f && tankOutletTemp < 100.0f) {
      setHeatPumpCH();
    } else {
      setHeatPumpOff();
    }
  } else {
    setHeatPumpOff();
  }
}

void controlHeatPumpDHWMode() {
  // Turn on the heat pump in DHW mode
  digitalWrite(HEAT_PUMP_DHW_PIN, HIGH);
}

void controlBoilerCHMode() {
  digitalWrite(BOILER_PIN, HIGH);
}

void controlSolarDHWMode() {
  digitalWrite(SOLAR_CIRCULATOR_PUMP_PIN, HIGH);
}

void controlDHWOverheatMode() {
  digitalWrite(DHW_TO_BUFFER_PUMP_PIN, HIGH);
  if (tankOutletTemp >= 120.0f || dhwTankTemp >= 150.0f) {
    digitalWrite(OVERHEAT_VALVE_PIN, HIGH);
  } else {
    digitalWrite(OVERHEAT_VALVE_PIN, LOW);
  }
}

// ====================== Utility Functions ====================== //
float calculateDeltaT(float outlet, float inlet) {
  return abs(outlet - inlet);
}

float calculateDewPoint(float tempF, float humidity) {
  // Formula for dew point calculation
  float tempC = (tempF - 32.0f) * 5.0f / 9.0f;
  const float a = 17.625f;
  const float b = 243.04f;
  float alpha = log(humidity / 100.0f) + (a * tempC) / (b + tempC);
  float dewPointC = (b * alpha) / (a - alpha);
  return (dewPointC * 9.0f / 5.0f) + 32.0f;
}

// ====================== Placeholder Functions ====================== //
void initializePins() {
  pinMode(HEAT_PUMP_CH_PIN, OUTPUT);
  pinMode(HEAT_PUMP_COOLING_PIN, OUTPUT);
  pinMode(HEAT_PUMP_OFF_PIN, OUTPUT);
  pinMode(BOILER_PIN, OUTPUT);
  pinMode(CIRCULATOR_PUMP_PIN, OUTPUT);
  pinMode(TWO_WAY_VALVE_PIN, OUTPUT);
  pinMode(SOLAR_CIRCULATOR_PUMP_PIN, OUTPUT);
  pinMode(OVERHEAT_VALVE_PIN, OUTPUT);
  pinMode(DEFROST_INPUT_PIN, INPUT);
  pinMode(HEAT_PUMP_FAIL_PIN, INPUT);
}

void sendConfigToBluetooth() {
  // Placeholder for sending configuration data via Bluetooth
}

void checkSensorErrors() {
  if (isSensorError) {
    currentMode = MODE_ERROR;
  }
}

void controlSolarSystem() {
  // Placeholder for solar system control logic
}

void checkBluetoothCommands() {
  // Placeholder for checking for incoming Bluetooth commands
}

void checkOverheatingProtection() {
  // Placeholder for overheating protection logic
}

void checkFreezingProtection() {
  // Placeholder for freezing protection logic
}

void handleHeatPumpFailure() {
  // Placeholder for handling heat pump failure
}

void setHeatPumpCH() {
  digitalWrite(HEAT_PUMP_CH_PIN, HIGH);
  digitalWrite(HEAT_PUMP_COOLING_PIN, LOW);
  digitalWrite(HEAT_PUMP_OFF_PIN, LOW);
}

void setHeatPumpCooling() {
  digitalWrite(HEAT_PUMP_CH_PIN, LOW);
  digitalWrite(HEAT_PUMP_COOLING_PIN, HIGH);
  digitalWrite(HEAT_PUMP_OFF_PIN, LOW);
}

void setHeatPumpOff() {
  digitalWrite(HEAT_PUMP_CH_PIN, LOW);
  digitalWrite(HEAT_PUMP_COOLING_PIN, LOW);
  digitalWrite(HEAT_PUMP_OFF_PIN, HIGH);
}

void handleDefrostMode() {
  digitalWrite(HEAT_PUMP_DHW_PIN, LOW);
  digitalWrite(BOILER_PIN, LOW);
  digitalWrite(CONCRETE_CIRCULATOR_PUMP_PIN, LOW);
  digitalWrite(FAN_COILS_CIRCULATOR_PUMP_PIN, LOW);
}

void handleSensorError() {
  // Placeholder for sensor error handling
  setHeatPumpOff();
  digitalWrite(BOILER_PIN, LOW);
}

// ====================== Storage Functions ====================== //
void saveConfig() {
  KVStore.put("config", config);
}

void restoreDefaultValues() {
  config.outsideHeatingThreshold = DEFAULT_OUTSIDE_HEATING_THRESHOLD;
  config.heatPumpMinTemp = DEFAULT_HEAT_PUMP_MIN_TEMP;
  config.heatPumpOffTemp = DEFAULT_HEAT_PUMP_OFF_TEMP;
  config.deltaTHeatingThreshold = DEFAULT_DELTA_T_HEATING_THRESHOLD;
  config.deltaTCoolingThreshold = DEFAULT_DELTA_T_COOLING_THRESHOLD;
  config.deltaTHeatingAssist = DEFAULT_DELTA_T_HEATING_ASSIST;
  config.deltaTCoolingOn = DEFAULT_DELTA_T_COOLING_ON;
  config.deltaTHeatingOn = DEFAULT_DELTA_T_HEATING_ON;
  config.dewPointBuffer = DEFAULT_DEW_POINT_BUFFER;
  saveConfig();
}

void changeValue(String key, float value) {
  if (key == "n0") {
    config.outsideHeatingThreshold = value;
  } else if (key == "n1") {
    config.heatPumpMinTemp = value;
  } else if (key == "n2") {
    config.heatPumpOffTemp = value;
  } else if (key == "n3") {
    config.deltaTHeatingThreshold = value;
  } else if (key == "n4") {
    config.deltaTCoolingThreshold = value;
  } else if (key == "n5") {
    config.deltaTHeatingAssist = value;
  } else if (key == "n6") {
    config.deltaTCoolingOn = value;
  } else if (key == "n7") {
    config.deltaTHeatingOn = value;
  } else if (key == "n8") {
    config.dewPointBuffer = value;
  } else if (key == "n9") {
    // HEATING_MAX_OUTLET_TEMP is a const
  } else if (key == "n10") {
    // HEATING_MIN_OUTLET_TEMP is a const
  } else if (key == "n11") {
    // COOLING_MAX_INLET_TEMP is a const
  } else if (key == "n12") {
    // COOLING_MIN_INLET_TEMP is a const
  } else if (key == "n13") {
    // DHW_OVERHEAT_TEMP is a const
  } else if (key == "n14") {
    // SOLAR_DHW_DELTA_T is a const
  }
}

// ====================== Nextion Functions ====================== //
void receiveDataFromNextion() {
  if (Serial1.available()) {
    String command = Serial1.readStringUntil('\n');
    command.trim();

    if (command == "factory_default") {
      restoreDefaultValues();
    } else if (command == "save") {
      for (int i = 0; i <= 14; i++) {
        String command = "get n" + String(i) + ".val";
        sendCommand(command);
        if (Serial1.available()) {
          String response = Serial1.readStringUntil('\n');
          response.trim();
          float value = response.toFloat();
          changeValue("n" + String(i), value);
        }
      }
      saveConfig(); // Save all changes at once after the loop
    } else if (command.startsWith("ssid:")) {
      int separatorIndex = command.indexOf(':');
      String ssid = command.substring(separatorIndex + 1);
      // TODO: Store SSID
    } else if (command.startsWith("password:")) {
      int separatorIndex = command.indexOf(':');
      String password = command.substring(separatorIndex + 1);
      // TODO: Store password
    } else if (command.startsWith("api_key:")) {
      int separatorIndex = command.indexOf(':');
      String apiKey = command.substring(separatorIndex + 1);
      // TODO: Store API key
    } else if (command.startsWith("database_url:")) {
      int separatorIndex = command.indexOf(':');
      String databaseUrl = command.substring(separatorIndex + 1);
      // TODO: Store database URL
    } else {
      int separatorIndex = command.indexOf(':');
      if (separatorIndex != -1) {
        String key = command.substring(0, separatorIndex);
        String valueString = command.substring(separatorIndex + 1);
        int pageNumber = valueString.substring(valueString.lastIndexOf(',') + 1).toInt();
        float value = valueString.substring(0, valueString.lastIndexOf(',')).toFloat();
        changeValue(key, value);
      }
    }
  }
}

// ====================== WiFi Functions ====================== //
/**
 * @brief Connects to the WiFi network.
 */
void connectToWiFi() {
  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print(".");
    delay(5000);
  }
  Serial.println("\nConnected to WiFi!");
}

// ====================== Firebase Functions ====================== //
/**
 * @brief Sends sensor data to the Firebase Realtime Database using the REST API.
 */
void sendDataToFirebase() {
  String jsonObject = "{";
  jsonObject += "\"temperature/outlet\":" + String(tankOutletTemp) + ",";
  jsonObject += "\"temperature/inlet\":" + String(tankInletTemp) + ",";
  jsonObject += "\"temperature/outside\":" + String(outsideTempF) + ",";
  jsonObject += "\"temperature/dhw\":" + String(dhwTankTemp) + ",";
  jsonObject += "\"temperature/solar\":" + String(solarCollectorTemp) + ",";
  jsonObject += "\"humidity\":" + String(utilityHumidity) + ",";
  jsonObject += "\"mode\":" + String(currentMode);
  jsonObject += "}";

  String path = "/data.json?auth=" + String(FIREBASE_AUTH);
  client.beginRequest();
  client.put(path);
  client.sendHeader("Content-Type", "application/json");
  client.sendHeader("Content-Length", jsonObject.length());
  client.write((const byte*)jsonObject.c_str(), jsonObject.length());
  client.endRequest();

  int statusCode = client.responseStatusCode();
  String response = client.responseBody();

  if (statusCode == 200) {
    Serial.println("Data sent to Firebase successfully.");
  } else {
    Serial.print("Error sending data to Firebase. Status code: ");
    Serial.println(statusCode);
    Serial.print("Response: ");
    Serial.println(response);
  }
}

// ====================== Nextion Functions ====================== //
void sendCommand(String command) {
  Serial1.print(command);
  Serial1.write(0xff);
  Serial1.write(0xff);
  Serial1.write(0xff);
}

void sendSensorDataToNextion() {
  // Create a command string
  String command = "";

  // Add the temperature data to the command string
  command = "t0.txt=\"" + String(tankOutletTemp, 2) + "\"";
  sendCommand(command);
  command = "t1.txt=\"" + String(tankInletTemp, 2) + "\"";
  sendCommand(command);
  command = "t2.txt=\"" + String(outsideTempF, 2) + "\"";
  sendCommand(command);
  command = "t3.txt=\"" + String(dhwTankTemp, 2) + "\"";
  sendCommand(command);
  command = "t4.txt=\"" + String(solarCollectorTemp, 2) + "\"";
  sendCommand(command);

  // Add the humidity data to the command string
  command = "h0.txt=\"" + String(utilityHumidity, 2) + "\"";
  sendCommand(command);
}

void sendDataToNextion() {
  sendSensorDataToNextion();

  // Add the mode of operation data to the command string
  String command = "";
  String colorCommand = "";

  // Error
  command = "t19.txt=\"";
  colorCommand = "t19.bco=";
  if (currentMode == MODE_ERROR) {
    command += "ON\"";
    colorCommand += "63488"; // Green
  } else {
    command += "OFF\"";
    colorCommand += "63488"; // Red
  }
  sendCommand(command);
  sendCommand(colorCommand);

  // Defrost
  command = "t17.txt=\"";
  colorCommand = "t17.bco=";
  if (currentMode == MODE_DEFROST) {
    command += "ON\"";
    colorCommand += "63488"; // Green
  } else {
    command += "OFF\"";
    colorCommand += "63488"; // Red
  }
  sendCommand(command);
  sendCommand(colorCommand);

  // Overheat
  command = "t16.txt=\"";
  colorCommand = "t16.bco=";
  if (currentMode == MODE_DHW_OVERHEAT) {
    command += "ON\"";
    colorCommand += "63488"; // Green
  } else {
    command += "OFF\"";
    colorCommand += "63488"; // Red
  }
  sendCommand(command);
  sendCommand(colorCommand);

  // Boiler
  command = "t15.txt=\"";
  colorCommand = "t15.bco=";
  if (currentMode == MODE_BOILER_CH) {
    command += "ON\"";
    colorCommand += "63488"; // Green
  } else {
    command += "OFF\"";
    colorCommand += "63488"; // Red
  }
  sendCommand(command);
  sendCommand(colorCommand);

  // Heat Pump DHW
  command = "t14.txt=\"";
  colorCommand = "t14.bco=";
  if (currentMode == MODE_HEAT_PUMP_DHW) {
    command += "ON\"";
    colorCommand += "63488"; // Green
  } else {
    command += "OFF\"";
    colorCommand += "63488"; // Red
  }
  sendCommand(command);
  sendCommand(colorCommand);

  // Heat Pump Cooling
  command = "t13.txt=\"";
  colorCommand = "t13.bco=";
  if (currentMode == MODE_HEAT_PUMP_COOLING) {
    command += "ON\"";
    colorCommand += "63488"; // Green
  } else {
    command += "OFF\"";
    colorCommand += "63488"; // Red
  }
  sendCommand(command);
  sendCommand(colorCommand);

  // Heat Pump CH
  command = "t8.txt=\"";
  colorCommand = "t8.bco=";
  if (currentMode == MODE_HEAT_PUMP_CH) {
    command += "ON\"";
    colorCommand += "63488"; // Green
  } else {
    command += "OFF\"";
    colorCommand += "63488"; // Red
  }
  sendCommand(command);
  sendCommand(colorCommand);
}

// ====================== Debugging ====================== //
void printDebugInfo() {
  Serial.print("[DEBUG] ");
  Serial.print(millis());
  Serial.print(" | Outlet: ");
  Serial.print(tankOutletTemp);
  Serial.print("F | Inlet: ");
  Serial.print(tankInletTemp);
  Serial.print("F | Outside: ");
  Serial.print(outsideTempF);
  Serial.print("F | DHW: ");
  Serial.print(dhwTankTemp);
  Serial.print("F | Solar: ");
  Serial.print(solarCollectorTemp);
  Serial.print("F | Humidity: ");
  Serial.print(utilityHumidity);
  Serial.print("% | Dew Point: ");
  Serial.print(dewPointF);
  Serial.print("F | Mode: ");

  String modeName = "";
  switch (currentMode) {
    case MODE_OFF:
      modeName = "OFF";
      break;
    case MODE_HEAT_PUMP_COOLING:
      modeName = "HP_COOLING";
      break;
    case MODE_HEAT_PUMP_CH:
      modeName = "HP_CH";
      break;
    case MODE_HEAT_PUMP_DHW:
      modeName = "HP_DHW";
      break;
    case MODE_BOILER_CH:
      modeName = "BOILER_CH";
      break;
    case MODE_SOLAR_DHW:
      modeName = "SOLAR_DHW";
      break;
    case MODE_DEFROST:
      modeName = "DEFROST";
      break;
    case MODE_DHW_OVERHEAT:
      modeName = "DHW_OVERHEAT";
      break;
    case MODE_ERROR:
      modeName = "ERROR";
      break;
  }
  Serial.println(modeName);
}
