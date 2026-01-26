# User Requirements (Updated 2025-05-24)

## Hardware Changes
- Remove Two-Way Valve (previously Pin 8).
- Split `CIRCULATOR_PUMP_PIN` into:
  - `FIRST_FLOOR_CIRC_PIN` (Pin 7)
  - `SECOND_FLOOR_CIRC_PIN` (Pin 8)

## System Mode Changes
- Remove `MODE_HEAT_PUMP_DHW` from the enum.
- Implement "Off Mode" for the Outside Temperature range **64-70°F**.
- In this range (64-70°F):
  - Heat Pump must be OFF.
  - Boiler must be OFF.
  - First Floor Circulator must be OFF.
  - Second Floor Circulator must be OFF.
  - System state should be `MODE_OFF`.

## Operational Logic Updates
- Below 64°F: Heating Mode (Heat Pump and Boiler as needed, Circulators ON).
- Above 70°F: Cooling Mode (Heat Pump Cooling, Circulators ON).
- Solar DHW logic remains (120-140°F).
