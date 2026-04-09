# Home Automation

An ESP32-based home automation project with Wi-Fi setup, relay control, timer scheduling, and MQTT-based remote operation.

## Overview

This project turns an ESP32 into a practical automation node for controlling electrical loads. It supports local configuration, scheduled switching, and remote integration through MQTT, making it useful as a learning project for embedded IoT systems.

## Features

- ESP32 firmware built with PlatformIO
- Relay switching for a connected appliance or load
- Wi-Fi configuration and reconnect handling
- MQTT publish and subscribe support
- Stored device settings using Preferences
- Timer-based ON and OFF scheduling
- LittleFS support for local file-backed resources

## Getting Started

1. Install [PlatformIO](https://platformio.org/).
2. Open the repository in VS Code.
3. Build the project to install dependencies.
4. Flash the firmware to the ESP32.
5. Connect to the setup access point and configure Wi-Fi and MQTT details.

## Safety

Use proper relay isolation and mains safety practices before connecting any real household load.

## Author

Avinash R
