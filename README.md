# Arduino HVAC Controller

## Target Hardware
- Arduino GIGA R1 WiFi

## Nextion HMI Interface
- Serial Port: Serial1 (Hardware Serial)
- Protocol: Nextion Instruction Set (0xFF termination)

## Testing Commands (Numeric)
The HMI sends the following numeric commands to the Arduino:
1. Heating (MODE_HEATING)
2. Cooling (MODE_COOLING)
3. Defrost (MODE_DEFROST)
4. Error (MODE_ERROR)
5. DHW ON (solarPumpRunning = true)
6. DHW OFF (solarPumpRunning = false)
