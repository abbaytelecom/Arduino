# Professional HVAC & Solar Control System Documentation

This document provides a comprehensive technical overview of the HVAC and Solar controller firmware implemented for the **Arduino GIGA R1**.

## 1. System Overview
The system is designed to provide high-efficiency, multi-stage climate control by managing a Heat Pump, a Backup Boiler, and a Solar Thermal DHW (Domestic Hot Water) system. It features real-time telemetry reporting and status visualization via a Nextion HMI display.

## 2. Hardware Interface (Arduino GIGA R1)
The controller utilizes various digital and analog-ready pins for sensors and actuators:

| Pin | Function | Type | Description |
|:---:|:---|:---:|:---|
| 0 | Nextion RX1 | UART | Data received from HMI |
| 1 | Nextion TX1 | UART | Data sent to HMI |
| 2 | OneWire Bus | Input | DS18B20 Temperature Sensors |
| 3 | Heat Pump CH | Output | Activates Central Heating Mode |
| 4 | Heat Pump Cool | Output | Activates Cooling Mode |
| 6 | Boiler | Output | Backup heating support |
| 7 | Circulator 1 | Output | First-floor fluid circulation |
| 8 | Circulator 2 | Output | Second-floor fluid circulation |
| 9 | Defrost | Input | HP Defrost signal (Active LOW) |
| 10 | DHT Sensor | Input | DHT11 Humidity/Temp Sensor |
| 11 | HP Fail | Input | HP Fault signal (Active LOW) |
| 14 | Solar Pump | Output | Solar collector circulation |
| 15 | Overheat Valve | Output | Safety thermal release |

## 3. Configuration Parameters
Key thresholds are managed within the `Config` namespace to ensure system efficiency:

*   **Heating season:** Activated when ambient temperature is `< 65.0°F`.
*   **Cooling season:** Activated when ambient temperature is `> 70.0°F`.
*   **Heat Pump Limits:** Operates between `5.0°F` and `-4.0°F` (Fallback to Boiler below `-4.0°F`).
*   **Delta T Triggers:**
    *   Heating ON: `25.0°F` differential.
    *   Cooling ON: `10.0°F` differential.
*   **Solar Logic:** Activates when collector is `30.0°F` warmer than DHW tank.

## 4. Operational Logic
The firmware implements a sophisticated state-machine approach:

### Heating Mode (`processHeating`)
- Monitors Delta T (Outlet - Inlet).
- **HP Priority:** Prioritizes Heat Pump if ambient conditions are favorable (> 5.0°F).
- **Boiler Takeover:** If the Heat Pump struggles (Delta T >= 25.0°F), the system triggers a **complete takeover**: Heat Pump is forced OFF and the Boiler is forced ON.
- **Anti-Short Cycle:** Once a Boiler Takeover is triggered, the system enforces a **10-minute minimum runtime** for the Boiler before allow any transitions back to Heat Pump mode.
- **Backup Mode:** Activates Boiler as the primary source during extreme cold (< -4.0°F) or if a Heat Pump hardware failure is detected.
- Maintains minimum outlet temperature of `100.0°F`.

### Cooling Mode (`processCooling`)
- Uses a **Magnus-Tetens** dew point calculation based on DHT11 data.
- Ensures the cooling outlet temperature stays `2.0°F` above the calculated dew point to prevent condensation.
- Operates within a tank inlet range of `42.0°F - 65.0°F`.

### Solar DHW (`processSolar`)
- Independent logic loop comparing solar collector and DHW tank temperatures.
- Includes a safety high-limit cutoff at `140.0°F` (`DHW_MAX_TEMP`).

## 5. Fail-Safe & Safety Features
The system prioritizes equipment longevity and home safety:

- **Continuous Monitoring:** Active polling of `PIN_HP_FAIL` and `PIN_DEFROST`.
- **Sensor Integrity:** Automatically detects disconnected Dallas sensors (`DEVICE_DISCONNECTED_F`) and enters a safe state.
- **Safe Shutdown:** A dedicated `performSafeShutdown()` routine forces all high-power outputs to `LOW` in the event of a critical error.

## 6. HMI Interface Protocol
The Nextion display is updated every 3 seconds. The firmware utilizes an **optimized iteration routine** to update telemetry fields, reducing serial overhead. Simultaneously, the system outputs a full telemetry report to the **Serial Monitor** at 9600 baud for debugging purposes, using the exact same status strings as the HMI for all fields (`t0`-`t3`).

### Telemetry (Numeric)
- `n0`: Ambient Outside Temp
- `n1`: Tank Inlet Temp
- `n2`: Tank Outlet Temp
- `n3`: DHW Tank Temp
- `n4`: Solar Collector Temp
- `n5`: Utility Humidity (%)
- `n6`: Dew Point Temp

### Status (Text)
- `t0`: Main System Mode (SYSTEM OFF, HP COOLING, HP HEATING, etc.)
- `t1`: DHW Status (DHW ON, DHW OFF, DHW OVERHEAT)
- `t2`: Boiler Status (BOILER ACT, BOILER OFF)
- `t3`: System Health Status (HEALTH OK, SYS FAULT)

### Actuator Logic (Circulator Pumps)
The **First Floor Circulator (Pin 7)** and **Second Floor Circulator (Pin 8)** are interlocked with the system mode. They are only active when the system is in an active heating or cooling state. If the System Mode is **SYSTEM OFF**, both circulators are forced **OFF** to conserve energy.

### Safety & Protection Logic
- **Boiler High-Limit:** The Boiler will automatically shut off if the Tank Outlet exceeds `HEATING_MIN_OUTLET + 10°F` (110°F), even during a takeover lock.
- **Overheat Valve:** The **Overheat Valve (Pin 15)** is automatically activated (HIGH) if the DHW Tank exceeds `DHW_MAX_TEMP` (140°F).
- **Cooling Hysteresis:** The system uses `DELTA_T_COOLING_OFF` to prevent rapid cycling of the heat pump in cooling mode.

### Command Dispatcher
The `dispatchHmiCommand` function processes incoming commands from the Nextion display.

#### Control Commands
- `0`: Clears system errors and returns to `SYSTEM_OFF`.
- `SYNC`: Requests the Arduino to transmit current thresholds to the HMI (`n10`-`n15`).
- `FACTORY`: Triggers a reset of all thresholds to hardcoded factory defaults.

#### Threshold Update Protocol
Nextion updates are sent in the format: `SET:[ID]:[VAL]\n`.
- **ID 0**: Heating Threshold
- **ID 1**: Cooling Threshold
- **ID 2**: HP Min Ambient
- **ID 3**: DHW Max Temp
- **ID 4**: Solar Delta T
- **ID 5**: Boiler Min Runtime (Mins)
