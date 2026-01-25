# User Requirements (Updated 2025-05-24)

## System Modes
The system must support the following exhaustive list of modes in the `SystemMode` enum:
- MODE_OFF
- MODE_HEAT_PUMP_COOLING
- MODE_HEAT_PUMP_CH
- MODE_HEAT_PUMP_DHW
- MODE_BOILER_CH
- MODE_SOLAR_DHW
- MODE_DEFROST
- MODE_DHW_OVERHEAT
- MODE_ERROR

## Operational Logic
- Use real Dallas and DHT sensor data.
- Outside Temp > 15°C -> MODE_HEAT_PUMP_COOLING.
- Outside Temp <= 15°C -> MODE_HEAT_PUMP_CH.
- Solar Temp 120-140°F -> solarPumpRunning = true.

## Nextion Display Mapping
- n0: Inlet Temperature
- n1: Outlet Temperature
- n2: DHW Tank Temperature
- n3: Outside Temperature
- n4: Solar Collector Temperature
- n5: Utility Humidity
- t0: Heat Pump Mode Status (labels: "HeatPumpCooling", "HeatPumpHeating")
- t1: Boiler Status (label: "Boiler Heating")
- t2: DHW Status (labels: "DHW ON", "DHW OFF")

## Manual Commands
- 1: Heat Pump Heating (MODE_HEAT_PUMP_CH)
- 2: Heat Pump Cooling (MODE_HEAT_PUMP_COOLING)
- 3: Defrost (MODE_DEFROST)
- 4: Error (MODE_ERROR)
- 5: DHW ON (MODE_SOLAR_DHW / solarPumpRunning = true)
- 6: DHW OFF (MODE_OFF / solarPumpRunning = false)
