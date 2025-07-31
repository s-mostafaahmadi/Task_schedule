
# Task Scheduler for ESP32 🧠⚡

A cyberpunk-themed task scheduler for ESP32 with NTP-based time synchronization, daily plan management, and a responsive web interface.

## 🔧 Features
- Manage multiple daily task plans
- Cyberpunk-styled HTML/CSS web UI
- NTP time synchronization with fallback browser time
- EEPROM-based persistent storage
- Multiple task entries per plan (up to 30 per plan)
- Switch between plans via UI

## 📦 Hardware Requirements
- ESP32 Dev Board (e.g., DOIT ESP32 DEVKIT V1)
- 3.3V Power Supply (via USB or external)
- Wi-Fi 2.4GHz access

## 🌐 Network Requirements
- Wi-Fi SSID/password must be hardcoded or configured
- Internet access for NTP server
- Optional: Local network access for browser time fallback

## ⏱️ Time Synchronization and Simulation
- Primary: syncs time from NTP every 30 minutes
- Secondary: falls back to time received from user's browser
- Time-based execution loop runs every 10 seconds

## 💾 EEPROM Storage
- Total EEPROM size: 4096 bytes
- Stores plans as serialized structures
- Automatically persists after changes

## 🖥️ Web Interface
- Hosted on ESP32 using WebServer on port 80
- UI allows:
  - Viewing/Editing tasks
  - Switching between saved plans
  - Viewing current synchronized time

## 🚀 Getting Started
1. Flash firmware to ESP32
2. Connect to configured Wi-Fi
3. Access device IP in browser (shown in serial monitor)
4. Set or edit your task plans via the UI

## 🛠️ Future Improvements
- Task notification via buzzer or LED
- OTA updates and plan import/export
- Dark/light theme toggle

## 🧾 License
This project is licensed under the MIT License.
