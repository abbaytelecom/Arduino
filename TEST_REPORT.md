# Professional HVAC & Solar Control System - Test Report

This report documents the verification of the control logic within `deepseekv4.ino` through a granular technical logic trace for five outside temperature scenarios.

## Test Scenario 1: Extreme Cold
*   **Input Conditions:** Ambient = `-10.0°F`, Tank Inlet = `80°F`, Tank Outlet = `75°F`.
*   **Logic Path:**
    1.  Ambient is below `Config::HEATING_THRESHOLD` (65°F).
    2.  Ambient is below `Config::HP_CRITICAL_LOW` (-4°F). `hpOk` is FALSE.
    3.  `processHeating` enters the boiler fallback branch.
    4.  Tank Outlet (75°F) is below `Config::HEATING_MIN_OUTLET` (100°F).
*   **Expected Results:**
    *   **HP Central Heating (Pin 3):** `LOW`
    *   **Boiler (Pin 6):** `HIGH`
    *   **System Mode:** `BOILER_HEATING`

## Test Scenario 2: Cold
*   **Input Conditions:** Ambient = `30.0°F`, Delta T = `30.0°F` (Inlet 70, Outlet 100).
*   **Logic Path:**
    1.  Ambient is below `Config::HEATING_THRESHOLD` (65°F).
    2.  Ambient is within Heat Pump operating range (5°F to 65°F). `hpOk` is TRUE.
    3.  Delta T (30°F) exceeds `Config::DELTA_T_HEATING_ON` (25°F).
*   **Expected Results:**
    *   **HP Central Heating (Pin 3):** `HIGH`
    *   **Boiler (Pin 6):** `HIGH`
    *   **System Mode:** `HP_HEATING`

## Test Scenario 3: Normal (Deadband)
*   **Input Conditions:** Ambient = `68.0°F`.
*   **Logic Path:**
    1.  Ambient is between `Config::HEATING_THRESHOLD` (65°F) and `Config::COOLING_THRESHOLD` (70°F).
    2.  `executeControlLogic` enters the System OFF branch.
    3.  `performSafeShutdown()` is executed.
*   **Expected Results:**
    *   **HP / Boiler / Pumps:** All `LOW`
    *   **System Mode:** `OFF`

## Test Scenario 4: Hot
*   **Input Conditions:** Ambient = `85.0°F`, Humidity = `50%`, Tank Inlet = `60°F`, Tank Outlet = `75°F`.
*   **Logic Path:**
    1.  Ambient exceeds `Config::COOLING_THRESHOLD` (70°F).
    2.  Dew point calculated as `~64.8°F`.
    3.  Outlet (75°F) is safe (`> 64.8 + 2.0`).
    4.  Delta T (15°F) exceeds `Config::DELTA_T_COOLING_ON` (10°F).
*   **Expected Results:**
    *   **HP Cooling (Pin 4):** `HIGH`
    *   **Boiler (Pin 6):** `LOW`
    *   **System Mode:** `HP_COOLING`

## Test Scenario 5: Extreme Hot
*   **Input Conditions:** Ambient = `105.0°F`, Tank Inlet = `70°F`.
*   **Logic Path:**
    1.  Ambient exceeds `Config::COOLING_THRESHOLD` (70°F).
    2.  Tank Inlet (70°F) exceeds `Config::COOLING_MAX_INLET` (65°F).
    3.  `processCooling` determines cooling is unsafe/inefficient.
*   **Expected Results:**
    *   **HP Cooling (Pin 4):** `LOW`
    *   **Circulators (Pins 7/8):** `HIGH` (Maintains flow)
    *   **System Mode:** `OFF` (Wait for thermal recovery)

## Conclusion
The logic walkthrough confirms that the firmware correctly prioritizes equipment safety (Dew Point protection, Ambient limits) and successfully manages multi-stage heating resources.
