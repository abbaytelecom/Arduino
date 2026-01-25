# User Instructions for Nextion Display Project

## Hardware Target
- The user is using an **Arduino Giga R1**.
- Communication with the Nextion display must use Hardware **Serial1**.

## HMI Communication Requirements
- Implement a function to randomly generate simulated sensor data for HMI testing:
  - InletTemperature -> n0
  - OutletTempature -> n1
  - DHWTemperature -> n2
  - OutsideTempreature -> n3
  - Solar Temperature -> n4
  - Humidity -> n5
- If simulated OutsideTempreature is above 15°C, set mode to Cooling.
- If simulated OutsideTempreature is below 15°C, set mode to Heating.
- If simulated Solar Temperature is between 120°F and 140°F, set DHW to ON.
- The HMI sends numeric commands for mode control:
  - 1: Heating
  - 2: Cooling
  - 3: Defrost
  - 4: Error
  - 5: DHW ON
  - 6: DHW OFF (User specified 5 to 4, but 6 is the logical OFF).
