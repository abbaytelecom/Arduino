# System Explanation: Smart HVAC & Solar Controller

This document explains how the Arduino-based control system works in plain English.

## 1. Overview
The system manages a home's heating and cooling by coordinating a **Heat Pump**, a **Backup Boiler**, and a **Solar Thermal System**. It automatically switches between heating and cooling based on the outside temperature and ensures everything runs safely.

## 2. Core Logic: How it Decides What to Do
The system constantly checks sensors and follows these rules:

### Seasonal Mode Switching
*   **Heating Season:** If the outside temperature is below a certain threshold (e.g., 65°F), the system enters Heating Mode.
*   **Cooling Season:** If it's warmer outside, the system switches to Cooling Mode.

### Heating Mode (Winter)
*   **Primary Source:** The system tries to use the Heat Pump first because it is more efficient.
*   **Boiler Backup:** If the Heat Pump cannot keep up (measured by the temperature difference between water going in and out), or if the Heat Pump fails, the system automatically turns on the Boiler to help.
*   **Extreme Cold:** If it gets too cold outside (e.g., below -4°F), the Heat Pump is turned off to protect it, and the Boiler takes over.

### Cooling Mode (Summer)
*   **Active Cooling:** The Heat Pump runs in reverse to cool the water circulating through the house.
*   **Condensation Protection:** The system calculates the "Dew Point" (the temperature where water starts to condense). It will never let the cooling water get cold enough to cause "sweating" on your pipes or floors.

## 3. Solar Thermal System
*   **Free Energy:** Whenever the sun makes the solar collector on the roof hotter than the water in your tank, a pump turns on to move that heat into your hot water tank.
*   **Safety:** If the tank is already hot enough (e.g., 140°F), the solar pump stays off to prevent scalding or damage.

## 4. Safety Features
The system is designed to be "Fail-Safe":
*   **Watchdog Timer:** A special internal "heartbeat" monitor. If the computer program ever freezes, this timer will automatically restart the system within 8 seconds.
*   **Overheat Protection:** If the water gets dangerously hot, a safety valve opens and the heaters are shut down.
*   **Freezing Protection:** If it's freezing outside and the pipes are getting too cold, the system circulates water to prevent them from bursting.
*   **Failure Detection:** If the Heat Pump signals an error, the system immediately switches to the Boiler so your house stays warm.

## 5. Remote Control & Monitoring
*   **Bluetooth:** You can connect to the system using a phone or computer via Bluetooth.
*   **Reporting:** It sends regular updates about temperatures and current status.
*   **Settings:** You can remotely change temperature thresholds (like when to switch between heating and cooling) without needing to plug in a keyboard.

---
*This system is designed to maximize comfort and efficiency while providing multiple layers of protection for your home's equipment.*
