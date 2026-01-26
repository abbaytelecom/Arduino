# Logic Update - 2025-05-24

## New Requirement: Boiler Takeover
- When Ambient is within the Heat Pump operating range and the Heat Pump struggles (high Delta T), the Boiler must take over completely.
- Implementation: Turn OFF Heat Pump, Turn ON Boiler.
- Requirement: Avoid short cycle between turnover of Heat Pump and Boiler.

## Test Scenarios to Verify:
- Extreme Cold
- Cold
- Normal
- Hot
- Extreme Hot
