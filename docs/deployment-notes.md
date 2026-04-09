# Deployment Notes

## Typical Flow

1. Flash the ESP32 firmware.
2. Join the setup access point exposed by the board.
3. Save Wi-Fi credentials and MQTT settings.
4. Test manual control.
5. Configure timer values and verify the switching schedule.

## Safety Reminder

Use an isolated relay module and keep mains wiring separated from low-voltage logic wiring. If the load is powered from household AC, enclosure quality and insulation matter as much as the firmware.
