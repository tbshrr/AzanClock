
# ESP32 Azan Clock

This project is an ESP32-based Real-Time Azan Reminder with multiple functionalities accessible through the Boot button. Follow the instructions below to configure and operate the device.

## Configuration

1. Open the `constant.h` file in your project.
2. Set your Wi-Fi username and password as follows:
   ```cpp
   const char* WIFI_SSID = "Your_WiFi_SSID";
   const char* WIFI_PASSWORD = "Your_WiFi_Password";
   ```

## Boot Button Functions

The Boot button on the ESP32 performs different actions based on the number of presses:

1. **Single Press**: Changes the display screen to the next available option.
2. **Double Press**: Fetches the latest Azan times from the internet.
3. **Triple Press**: Clears the saved Wi-Fi credentials.
4. **Four Presses**: Fetches the latest time from the internet using the NTP server.

## Notes

- Ensure the device is connected to a stable Wi-Fi network for internet-based functionalities.
- After clearing the Wi-Fi credentials, you will need to reconfigure them in the `constant.h` file or use your preferred Wi-Fi provisioning method.

## Features

- Real-time Azan reminders.
- Pre-Azan alerts with a buzzer.
- OLED display for prayer times and current time.
- Automatic time synchronization with NTP servers.

---

Feel free to contribute or report issues to enhance this project!
