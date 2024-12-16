#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>  // Include the time library
#include <RTClib.h>  // Add the RTClib library for RTC
#include "constants.h"
#include <Preferences.h>  


// Create an RTC object
RTC_DS3231 rtc;

String apiUrl;

// OLED display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SSD1306_I2C_ADDRESS  0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Arrays to hold main and other timings
String mainTimingNames[] = {"Fajr", "Sunrise", "Dhuhr", "Asr", "Maghrib", "Isha"};
String mainTimingValues[6];

String otherTimingNames[] = {"Sunset", "Imsak", "Midnight", "1/3rd", "2/3rd"};
String otherTimingValues[5];

String city = "";

bool showMainTimings = false;  // Toggle between screens
bool showOtherTimings = false;  // Toggle between screens
bool showLargeTime = true; // Toggle for the large time screen
bool autoChange = false;
bool changePressed = false;
bool dontChange = false;
bool alwaysConnectWifi = false;

bool fetchingAzanTimes = true;  // Flag to indicate fetching state
unsigned long previousMillis = 0;
const long interval = 1000;  // 1 seconds interval for switching screens
int dotCount = 0;  // Number of dots for animation

// NTP settings
const char* ntpServer = "time.google.com";
const long gmtOffsetSec = 19800;  // GMT offset for IST (in seconds)
const int daylightOffsetSec = 0;  // No daylight savings in India
// Define a time interval for NTP synchronization (e.g., every 60 seconds)
unsigned long previousNtpSyncMillis = 0;
const long ntpSyncInterval = 60000; // 60 seconds

// Buzzer pin
const int BUZZER_PIN = 32; // Replace with your buzzer pin
#define BUZZER_CHANNEL 0 // Use channel 0 for PWM


bool azanTimesUpdated = false;

bool prayerTimeTriggered[6] = {false, false, false, false, false, false};  // Flags for each main prayer time
bool remiderTimeTriggered[6] = {false, false, false, false, false, false};  // Flags for each main prayer time

Preferences preferences;

unsigned long lastButtonPress = 0;
unsigned long debounceDelay = 50;  // Debounce delay in milliseconds
unsigned long patternTimeout = 1000;  // Max time to detect multiple presses
int buttonPressCount = 0;
#define BUTTON_PIN 0  // BOOT button pin (GPIO0 on ESP32)

String latitude = "";
String longitude = "";

// Function declarations
// Declare the syncTimeFromNTP function before using it in loop()
void syncTimeFromNTP();
void connectToWiFi();
// void initializeTime();
void fetchAzanTimes();
void displayTimings();
void displayOtherTimings();
void displayLargeTime();
void showWelcomeMessage();
// void showConnectingMessage();
void displayFetchingAnimation();
String getFormattedDate();
String convertTo12HourFormat(const String &time24);
void soundBuzzer(String prayerTime, String prayerName, String flag); // Function to sound the buzzer
void checkAndTriggerBuzzer(); // Function to check time and trigger buzzer
int getXPos(String text);
int getYPos();
void readAzanTimesFromEEPROM();
void writeAzanTimesFromEEPROM();
void checkForMidnightUpdate();
void clearPreferences();
void getGeoLocation();
String getPublicIP();
void dynamicMessage(String msg1, String msg2 = "");
bool initializeRTC(int maxRetries, int retryDelayMs);
void handleButtonPress();
void whenToBuzzer();
void changeScreen();
void updateDisplay();
void toggleScreens();

void setup() {
    Serial.begin(115200);

    // clearPreferences();
    // Initialize OLED display
    if (!display.begin(SSD1306_I2C_ADDRESS, 0x3C)) {  // Use 0x3C as the I2C address
        Serial.println("SSD1306 allocation failed");
        while (true); // Loop forever
    }
    // display.clearDisplay();
    // display.display();

    // Initialize the RTC
    if (!initializeRTC(3, 500)) {
        Serial.println("RTC failed to initialize. Check connections or replace RTC module.");
        while (1);
    }


    // Set the buzzer pin mode
    pinMode(BUZZER_PIN, OUTPUT);

    // Initialize button pin
    pinMode(BUTTON_PIN, INPUT_PULLUP);

    // Configure the LEDC to generate a PWM signal for the buzzer
    ledcSetup(BUZZER_CHANNEL, 2000, 8); // 2000 Hz frequency, 8-bit resolution
    ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL); // Attach the pin to the channel
    
    // noTone(BUZZER_PIN);

    // Show welcome message
    showWelcomeMessage();
    delay(1000);  // Show for 2 seconds

    if(alwaysConnectWifi){
        connectToWiFi();
    }

    // Check if the RTC lost power and set the time
    if (rtc.lostPower()) {
        Serial.println("RTC lost power, setting the time!");
        syncTimeFromNTP();
    }


    // Read stored Azan times from EEPROM
    readAzanTimesFromEEPROM();

}

void loop() {

    int buzzerState = digitalRead(BUZZER_PIN);

    DateTime now = rtc.now();
    String currentTime = String(now.hour()) + ":" + (now.minute() < 10 ? "0" : "") + String(now.minute());

    checkForMidnightUpdate();

    static int lastButtonState = HIGH;
    int buttonState = digitalRead(BUTTON_PIN);

    // Detect button press (active LOW)
    if (buttonState == LOW && lastButtonState == HIGH && (millis() - lastButtonPress) > debounceDelay) {
        lastButtonPress = millis();
        buttonPressCount++;
        Serial.printf("Button pressed! Count: %d\n", buttonPressCount);
    }
    lastButtonState = buttonState;


    unsigned long currentMillis = millis();


    if (currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;

        // Show the appropriate screen
        if(autoChange){

            // Toggle between screens
            toggleScreens();

            updateDisplay();

        }else if(!dontChange){
            displayLargeTime();
        }

        // Check for timeout between presses
        changeScreen();

        // soundBuzzer();

        whenToBuzzer();

    }

    // Handle manual button press
    if (changePressed) {
        handleButtonPress();
    }

    
}

void updateDisplay() {
    if (showLargeTime) {
        displayLargeTime();
    } else if (showMainTimings) {
        displayTimings();
    } else if (showOtherTimings) {
        displayOtherTimings();
    }
}

void toggleScreens() {
    if (showLargeTime) {
        showLargeTime = false;
        showMainTimings = true;
        showOtherTimings = false;
    } else if (showMainTimings) {
        showLargeTime = false;
        showMainTimings = false;
        showOtherTimings = true;
    } else if (showOtherTimings) {
        showLargeTime = true;
        showMainTimings = false;
        showOtherTimings = false;
    }
}
// Function to handle button presses
void handleButtonPress() {
    changePressed = true;
    toggleScreens();  // Toggle to the next screen
    updateDisplay();  // Refresh the display
    changePressed = false;  // Reset the change flag
}

void changeScreen(){
    if (millis() - lastButtonPress > patternTimeout && buttonPressCount > 0) {
            
            if (buttonPressCount == 1) {
                dynamicMessage("Changing screen");
                handleButtonPress();
                dontChange = true;

            } else if (buttonPressCount == 2) {
                dynamicMessage("Fetching Azan Times");
                
                fetchAzanTimes();
            } else if (buttonPressCount == 3) {
                dynamicMessage("Clearing Wifi", "Preferences");
                clearPreferences();
            } else if (buttonPressCount == 4){
                dynamicMessage("Fetching current time", "from NTP");
                syncTimeFromNTP();
            }else {
                dynamicMessage("Invalid Button");
                Serial.println("Invalid button press pattern.");
            }
            buttonPressCount = 0;  // Reset the counter
        }
}

void whenToBuzzer(){
    // Use a testing variable to simulate the current time for debugging purposes
        int testingHour = 15;   // Set this to your desired hour for testing
        int testingMinute = 27; // Set this to your desired minute for testing

        // Toggle between real and testing time
        bool useTestingTime = false;  // Set to 'true' to use testing time, 'false' for real time


        // Get the current time
        DateTime now = rtc.now();
        String currentTime = String(now.hour()) + ":" + (now.minute() < 10 ? "0" : "") + String(now.minute());

        int currentHour = now.hour();
        int currentMinute = now.minute();

        currentHour = useTestingTime ? testingHour : currentHour;
        currentMinute = useTestingTime ? testingMinute : currentMinute;

        // Calculate the total minutes from midnight for the current time
        int currentTotalMinutes = currentHour * 60 + currentMinute;

        // Check each main prayer time
        for (int i = 0; i < 6; i++) {
            int prayerHour, prayerMinute;
            sscanf(mainTimingValues[i].c_str(), "%d:%d", &prayerHour, &prayerMinute);

            // Convert prayerHour to 24-hour format if necessary
            bool isPM = (mainTimingValues[i].endsWith("PM"));; // You may need to modify this if there's a way to check AM/PM
            if (isPM && prayerHour < 12) {
                prayerHour += 12; // Convert PM hours to 24-hour format
            }
            if (!isPM && prayerHour == 12) {
                prayerHour = 0; // Convert 12:00 AM to 00:00 in 24-hour format
            }


            // Serial.println("Prayer Time: " + String(prayerHour) + ":"+ String(prayerMinute) + " & current time: "+ String(currentHour) + ":" + String(currentMinute) + "prayertimetriggered:" + prayerTimeTriggered[i]);

            int prayerTotalMinutes = prayerHour * 60 + prayerMinute;

            // Reminder 10 minutes before the next prayer time
            if (currentTotalMinutes == prayerTotalMinutes - 10 && !remiderTimeTriggered[i] && (i != 1 && i != 2)) {
                Serial.println("Reminder: " + mainTimingNames[i] + " is coming up!");
                soundBuzzer("Reminder", mainTimingValues[i], "rem");  // Sound buzzer for the reminder
                remiderTimeTriggered[i] = true;
            }

            // Special case: Check for the exact prayer time
            if (currentTotalMinutes == prayerTotalMinutes && !prayerTimeTriggered[i] && i != 1) {
                Serial.println("It's time for: " + mainTimingNames[i]);
                soundBuzzer(mainTimingNames[i], mainTimingValues[i], "time");  // Sound buzzer for the prayer
                prayerTimeTriggered[i] = true;  // Mark this prayer time as triggered
            }

            // Special consideration for Fajr: Use sunrise time for the reminder
            if (i == 0) {  // Fajr is the first prayer
                int sunriseHour, sunriseMinute;
                sscanf(mainTimingValues[1].c_str(), "%d:%d", &sunriseHour, &sunriseMinute);
                int sunriseTotalMinutes = sunriseHour * 60 + sunriseMinute;

                if (currentTotalMinutes == sunriseTotalMinutes - 10 && !remiderTimeTriggered[i]) {
                    Serial.println("Reminder: Fajr is ending soon!");
                    soundBuzzer("Fajr Ending Soon", mainTimingValues[1], "rem");  // Sound buzzer for Fajr reminder
                    remiderTimeTriggered[i] = true;
                }
            }
        }

        // Reset the flags at midnight (00:00) to allow buzzing for the next day's prayer times
        if (currentHour == 0 && currentMinute == 0) {
            memset(prayerTimeTriggered, false, sizeof(prayerTimeTriggered));
            memset(remiderTimeTriggered, false, sizeof(remiderTimeTriggered));
        }
}

// Function to initialize the RTC with retry logic
bool initializeRTC(int maxRetries, int retryDelayMs) {
    for (int attempt = 1; attempt <= maxRetries; attempt++) {
        Serial.printf("Attempt %d to initialize RTC...\n", attempt);
        if (rtc.begin()) {
            Serial.println("RTC initialized successfully.");
            return true;
        }
        Serial.println("RTC initialization failed. Retrying...");
        delay(retryDelayMs);
    }
    Serial.println("RTC initialization failed after maximum retries.");
    return false;
}

// Function to sync time from NTP and update RTC
void syncTimeFromNTP() {
    connectToWiFi();
    configTime(gmtOffsetSec, daylightOffsetSec, ntpServer);
    struct tm timeInfo;
    if (!getLocalTime(&timeInfo)) {
        Serial.println("Failed to obtain time");
    } else {
        // Successfully synchronized, update RTC with the new time
        rtc.adjust(DateTime(timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday, timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec));
        Serial.println("Time synchronized successfully");
    }
}

// Function to display a welcome message
void showWelcomeMessage() {
    display.clearDisplay();
    display.setTextSize(1.5);  // Small font size
    display.setTextColor(SSD1306_WHITE);
    // Display the date below the time
    String msg1 = "Welcome to";
    int xPos = (SCREEN_WIDTH - (msg1.length() * 6)) / 2; // Center the date
    int yPos = SCREEN_HEIGHT /3; // Position the date below the time
    display.setCursor(xPos, yPos);  // Center the text
    display.println(msg1);
    String msg2 = "Azan Reminder!";
    int xPos2 = (SCREEN_WIDTH - (msg2.length() * 6)) / 2; // Center the date
    int yPos2 = SCREEN_HEIGHT /2 + 10; // Position the date below the time
    display.setCursor(xPos2, yPos2);  // Center the text
    display.println(msg2);
    display.display();
}

// Function to display a connecting to WiFi message
void dynamicMessage(String msg1, String msg2) {
    display.clearDisplay();
    display.setTextSize(1.5);  // Small font size
    display.setTextColor(SSD1306_WHITE);
    // Display the date below the time
    display.setCursor(getXPos(msg1), getYPos());  // Center the text
    display.println(msg1);
    display.setCursor(getXPos(msg2), getYPos() + 9);  // Center the text
    display.println(msg2);
    display.display();
}

// Connect to WiFi
void connectToWiFi() {

    preferences.begin("WiFiCreds", false);  // Open Preferences in read-only mode
    preferences.putString("ssid", WIFI_SSID); 
    preferences.putString("password", WIFI_PASSWORD); 
    preferences.end();
    // Add Wifi Name and Secret in Constant
    preferences.begin("WiFiCreds", true);  // Open Preferences in read-only mode
    String savedSSID = preferences.getString("ssid", ""); 
    String savedPassword = preferences.getString("password", ""); 
    preferences.end();

    if (savedSSID.length() > 0) {
        Serial.println("Connecting to saved WiFi...");
        Serial.print("SSID: ");
        Serial.println(savedSSID);

        String msg1 = "Connecting to WiFi...";

        // Display the "Connecting to WiFi..." message on the OLED
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(getXPos(msg1), getYPos());
        display.print(msg1);  // Display the WiFi connection message
        display.display();

        WiFi.begin(savedSSID.c_str(), savedPassword.c_str());

        String msg2 = "Connecting.";
        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
        display.setCursor(getXPos(msg2), getYPos());
        display.print(msg2);
        display.display();
        unsigned long startTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startTime < 20000) {
            delay(500);
            display.print(".");
            display.display();  // Update the display with the new message
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nConnected to WiFi!");
            Serial.print("IP Address: ");
            Serial.println(WiFi.localIP());
            // Once connected, display success message
            display.clearDisplay();
            display.setTextSize(1);
            display.setTextColor(SSD1306_WHITE);
            String msg3 = "WiFi Connected!";
            display.setCursor(getXPos(msg3), getYPos());
            display.println(msg3);  // Show the success message
            display.display();
            delay(2000);  // Show for 2 seconds before continuing
            return;
        } else {
            Serial.println("\nFailed to connect to saved WiFi.");
        }
    }


}



// Function to get the formatted date (DD-MM-YYYY)
String getFormattedDate() {
    struct tm timeInfo;
    DateTime now = rtc.now(); // Get current time from RTC
    char dateBuffer[11];
    snprintf(dateBuffer, sizeof(dateBuffer), "%02d-%02d-%04d", now.day(), now.month(), now.year());
    return String(dateBuffer);
}

// Function to get the formatted date (DD-MM-YYYY)
String getDate() {
    struct tm timeInfo;
    DateTime now = rtc.now(); // Get current time from RTC
    char dateBuffer[11];
    snprintf(dateBuffer, sizeof(dateBuffer), "%02d-%02d-%04d", now.day(), now.month(), now.year());
    return String(dateBuffer);
}



void readAzanTimesFromEEPROM() {
    preferences.begin("azanTimes", false);  // Open Preferences with the namespace "azanTimes"
    Serial.println("Reading Azan times from Preferences...");

    // Read main timings
    for (int i = 0; i < 6; i++) {
        // Create the key by concatenating the base string and the index
        String key = "mainTiming" + String(i);  // Concatenate "mainTiming" with the index
        String timing = preferences.getString(key.c_str(), "");  // Use the key to fetch the timing
        mainTimingValues[i] = timing;
    }

    // Read other timings
    for (int i = 0; i < 5; i++) {
        // Create the key for other timings
        String key = "otherTiming" + String(i);  // Concatenate "otherTiming" with the index
        String timing = preferences.getString(key.c_str(), "");  // Use the key to fetch the timing
        otherTimingValues[i] = timing;
    }

    // Check if the first timing value is empty or invalid
    if (mainTimingValues[0].isEmpty() || mainTimingValues[0] == "0:00") {
        Serial.println("Azan times not found in Preferences, fetching from API...");
        fetchAzanTimes();  // Fetch new Azan times and store in Preferences
    } else {
        Serial.println("Azan times loaded from Preferences.");
        // displayTimings();
    }

    preferences.end();  // Close Preferences
}

void writeAzanTimesToEEPROM() {
    preferences.begin("azanTimes", false);  // Open Preferences with the namespace "azanTimes"
    Serial.println("Writing Azan times to Preferences...");

    // Write main timings
    for (int i = 0; i < 6; i++) {
        String key = "mainTiming" + String(i);  // Create the key
        String timing = mainTimingValues[i];
        preferences.putString(key.c_str(), timing);  // Store the timing in Preferences
    }

    // Write other timings
    for (int i = 0; i < 5; i++) {
        String key = "otherTiming" + String(i);  // Create the key
        String timing = otherTimingValues[i];
        preferences.putString(key.c_str(), timing);  // Store the timing in Preferences
    }

    Serial.println("Azan times successfully written to Preferences.");

    preferences.end();  // Close Preferences
}

void storeCityInPreferences(const String& cityName) {
    preferences.begin("cityData", false);  // Open Preferences with the namespace "cityData"
    preferences.putString("city", cityName);  // Store the city name as a string
    preferences.end();  // Close Preferences
    Serial.println("City name stored in Preferences.");
}

String readCityFromPreferences() {
    preferences.begin("cityData", false);  // Open Preferences with the namespace "cityData"
    String city = preferences.getString("city", "");  // Get the city name, default is empty string if not found
    preferences.end();  // Close Preferences
    return city;
}


// Fetch Azan times from Aladhan API and store them
void fetchAzanTimes() {

    if (WiFi.status() != WL_CONNECTED) {
        connectToWiFi();
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        // Get location and fetch prayer times
            String publicIP = getPublicIP();
        if (publicIP == "") {
            Serial.println("Cannot fetch geolocation without IP.");
            return;
        }

        dynamicMessage("Fetching Geolocation");

        HTTPClient http;
        String url = "http://ip-api.com/json/" + publicIP;
        http.begin(url);
        int locRes = http.GET();

        if (locRes == 200) {
            String payload = http.getString();
            Serial.println("Geolocation Response: " + payload);

            // Parse JSON response
            StaticJsonDocument<1024> doc;
            deserializeJson(doc, payload);

            latitude = doc["lat"].as<String>();
            longitude = doc["lon"].as<String>();
            city = doc["city"].as<String>();

            storeCityInPreferences(city);

            Serial.println("Latitude: " + latitude + ", Longitude: " + longitude + ", City: " + city);
        } else {
            Serial.println("Error fetching geolocation.");
        }
        http.end();

        dynamicMessage("Fetching Latest", "Azan Times");
        // Get the current date and update the API URL
        String currentDate = getFormattedDate();
        // apiUrl = "https://api.aladhan.com/v1/timingsByCity/" + currentDate + "?city=" + String(city) + "&country=" + String(country) + "&method=16";
        apiUrl = "https://api.aladhan.com/v1/timings/" + currentDate + "?latitude=" + latitude + "&longitude=" + longitude + "&method=16";

        Serial.println("API URL"+ apiUrl);
        WiFiClientSecure client;
        client.setInsecure();  // Bypass SSL verification
        HTTPClient https;

        Serial.println("Fetching Azan times...");
        https.begin(client, apiUrl);

        int httpResponseCode = https.GET();
        if (httpResponseCode > 0) {
            String payload = https.getString();
            Serial.println("Azan times fetched successfully:");
            Serial.println(payload);

            // Parse JSON response
            StaticJsonDocument<2000> jsonDoc;
            DeserializationError error = deserializeJson(jsonDoc, payload);

            if (error) {
                Serial.print("JSON deserialization failed: ");
                Serial.println(error.c_str());
                return;
            }

            // Extract and store main timings
            JsonObject timings = jsonDoc["data"]["timings"];
            mainTimingValues[0] = convertTo12HourFormat(timings["Fajr"].as<const char*>());
            mainTimingValues[1] = convertTo12HourFormat(timings["Sunrise"].as<const char*>());
            mainTimingValues[2] = convertTo12HourFormat(timings["Dhuhr"].as<const char*>());
            mainTimingValues[3] = convertTo12HourFormat(timings["Asr"].as<const char*>());
            mainTimingValues[4] = convertTo12HourFormat(timings["Maghrib"].as<const char*>());
            mainTimingValues[5] = convertTo12HourFormat(timings["Isha"].as<const char*>());

            // Extract and store other timings
            otherTimingValues[0] = convertTo12HourFormat(timings["Sunset"].as<const char*>());
            otherTimingValues[1] = convertTo12HourFormat(timings["Imsak"].as<const char*>());
            otherTimingValues[2] = convertTo12HourFormat(timings["Midnight"].as<const char*>());
            otherTimingValues[3] = convertTo12HourFormat(timings["Firstthird"].as<const char*>());
            otherTimingValues[4] = convertTo12HourFormat(timings["Lastthird"].as<const char*>());

            // Write updated timings to EEPROM
            writeAzanTimesToEEPROM();
            readAzanTimesFromEEPROM();
            displayTimings();

            fetchingAzanTimes = false; // Stop fetching animation
        } else {
            Serial.print("Error fetching Azan times: ");
            Serial.println(httpResponseCode);
        }
        https.end();
    } else {
        Serial.println("Not connected to WiFi. Cannot fetch Azan times.");
    }
}


// Function to convert 24-hour format to 12-hour format
String convertTo12HourFormat(const String &time24) {
    int hour = time24.substring(0, 2).toInt();
    int minute = time24.substring(3, 5).toInt();
    String period = "AM";

    if (hour >= 12) {
        period = "PM";
        if (hour > 12) {
            hour -= 12;
        }
    } else if (hour == 0) {
        hour = 12;
    }

    String hourString = String(hour);
    String minuteString = (minute < 10) ? "0" + String(minute) : String(minute);

    return hourString + ":" + minuteString + " " + period;
}

// Function to display fetching animation
void displayFetchingAnimation() {
    display.clearDisplay();
    display.setTextSize(1);  // Small font size
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(20, 24);
    display.print("Fetching Azan Times");
    for (int i = 0; i < dotCount; i++) {
        display.print(".");
    }
    dotCount = (dotCount + 1) % 4;  // Cycle through 0, 1, 2, 3
    display.display();
}


void displayTimings() {
    readAzanTimesFromEEPROM();
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    

    // Define table dimensions
    int tableWidth = 128;  // Display width
    int tableHeight = 64;  // Display height
    int startY = 2;  // Starting Y position for the first row
    int columnSpacing = 60;  // Space between columns
    int rowHeight = 8;  // Height of each row
    int extraRowSpacing = 2;  // Extra space after the line (you can adjust this value)

    int labelColumnX = 5;  // X position for labels
    int valueColumnX = tableWidth / 2 + 5;  // X position for values

    // Draw table borders
    display.drawRect(0, 0, tableWidth, tableHeight, SSD1306_WHITE);
    display.drawLine(tableWidth / 2, 0, tableWidth / 2, tableHeight, SSD1306_WHITE);

    for (int i = 0; i < 6; i++) {
        int labelWidth = mainTimingNames[i].length() * 6;
        int labelX = labelColumnX + (columnSpacing - labelWidth) / 2;

        display.setTextSize(1);
        // Print the timing name (centered)
        display.setCursor(labelX, startY);
        display.print(mainTimingNames[i]);

        String value = mainTimingValues[i];
        int valueWidth = value.length() * 6;
        int valueX = valueColumnX + (columnSpacing - valueWidth) / 2;

        display.setTextSize(1);
        // Print the value (centered)
        display.setCursor(valueX, startY);
        display.print(value);

        // Draw a line under each row for separation, except the last row
        if (i != 5) {  // Skip the last row
            display.drawLine(0, startY + rowHeight, tableWidth, startY + rowHeight, SSD1306_WHITE);
        }

        // Add extra space after the line for subsequent rows
        startY += rowHeight + extraRowSpacing;
    }


    display.display();
}

// Function to display either main or other timings based on the flag
void displayOtherTimings() {
    readAzanTimesFromEEPROM();
     display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    

    // Define table dimensions
    int tableWidth = 128;  // Display width
    int tableHeight = 64;  // Display height
    int startY = 2;  // Starting Y position for the first row
    int columnSpacing = 60;  // Space between columns
    int rowHeight = 8;  // Height of each row
    int extraRowSpacing = 2;  // Extra space after the line (you can adjust this value)

    int labelColumnX = 5;  // X position for labels
    int valueColumnX = tableWidth / 2 + 5;  // X position for values

    // Draw table borders
    display.drawRect(0, 0, tableWidth, tableHeight, SSD1306_WHITE);
    display.drawLine(tableWidth / 2, 0, tableWidth / 2, tableHeight, SSD1306_WHITE);


    for (int i = 0; i < 5; i++) {
        int labelWidth = otherTimingNames[i].length() * 6;
        int labelX = labelColumnX + (columnSpacing - labelWidth) / 2;

        display.setTextSize(1);
        // Print the timing name (centered)
        display.setCursor(labelX, startY);
        display.print(otherTimingNames[i]);

        String value = otherTimingValues[i];
        int valueWidth = value.length() * 6;
        int valueX = valueColumnX + (columnSpacing - valueWidth) / 2;

        display.setTextSize(1);
        // Print the value (centered)
        display.setCursor(valueX, startY);
        display.print(value);

        // Draw a line under each row for separation, except the last row
        if (i != 4) {  // Skip the last row
            display.drawLine(0, startY + rowHeight, tableWidth, startY + rowHeight, SSD1306_WHITE);
        }

        // Add extra space after the line for subsequent rows
        startY += rowHeight + extraRowSpacing;
    }
    

    display.display();
}

void displayLargeTime() {
    display.clearDisplay();
    
    // Set large font for time
    display.setTextSize(2); // Large font size for time
    display.setTextColor(SSD1306_WHITE);

    // Get current time from RTC
    DateTime now = rtc.now();
    int hour = now.hour();
    int minute = now.minute();
    int sec = now.second();
    String period = "AM";

    if (hour >= 12) {
        period = "PM";
        if (hour > 12) hour -= 12;
    } else if (hour == 0) {
        hour = 12;
    }

    // Format the time string
    String hourString = String(hour);
    String minuteString = (minute < 10) ? "0" + String(minute) : String(minute);
    String secString = (sec < 10) ? "0" + String(sec) : String(sec);
    String timeString = hourString + ":" + minuteString + ":" + secString + " ";

    // Calculate position for time
    int timeX = (SCREEN_WIDTH - (timeString.length() * 12)) / 2;
    int timeY = SCREEN_HEIGHT / 3;

    // Display the time at the center of the screen
    display.setCursor(timeX, timeY);
    display.print(timeString);

    // Display the period (AM/PM) as superscript
    display.setTextSize(1); // Smaller font size for period
    int periodX = timeX + timeString.length() * 11; // Position to the right of the time
    int periodY = timeY - 2; // Position above the time
    display.setCursor(periodX, periodY);
    display.print(period);

    // Set small font for the date
    display.setTextSize(1); // Small font size for the date
    String dateString = getFormattedDate();  // Get the current date

    // Calculate position for the date
    int dateX = (SCREEN_WIDTH - (dateString.length() * 6)) / 2;
    int dateY = timeY + 16 + 10; // Position the date below the time

    // Display the date
    display.setCursor(dateX, dateY);
    display.print(dateString);

    // --- Center the prayerTimeTriggered status at the top ---
    int indicatorCount = 6; // Number of indicators
    int totalIndicatorWidth = indicatorCount * 12 - 2; // Total width of all indicators with spacing
    int indicatorX = (SCREEN_WIDTH - totalIndicatorWidth) / 2; // Center the indicators
    int indicatorY = 0; // Top of the screen

    String storedCity = readCityFromPreferences();

    // Set small font for the city
    display.setTextSize(1); // Small font size for the city

    // Calculate position for the city
    int cityX = (SCREEN_WIDTH - (storedCity.length() * 6)) / 2;
    int cityY = 0; // Position at the top of the screen

    // Display the city
    display.setCursor(cityX, cityY);
    display.print(storedCity);

    // Display everything on the screen
    display.display();
}




// Function to calculate x position for centering text
int getXPos(String text) {
    return (SCREEN_WIDTH - (text.length() * 6)) / 2; // Adjust based on character width
}

// Function to calculate y position
int getYPos() {
    return SCREEN_HEIGHT / 2; // You can modify this to adjust the vertical position
}

void checkForMidnightUpdate() {
    DateTime now = rtc.now(); // Get current time from RTC

    if (now.hour() == 1 && now.minute() >= 1 && now.minute() <= 5 && !azanTimesUpdated) { 
        // It's 12:00 AM and Azan times haven't been updated yet
        fetchAzanTimes();  // Fetch new Azan times from the API
        azanTimesUpdated = true;  // Set the flag to prevent multiple updates
        // Read stored Azan times from EEPROM
        readAzanTimesFromEEPROM();
    } 
    else if (now.hour() == 0 && now.minute() >= 6) {
        // Reset the flag after 12:00 AM passes
        azanTimesUpdated = false;
    }
}

bool isValidAzanTime(const String& time) {
    // Simple validation: Check if the time is in valid format (e.g., HH:MM or AM/PM)
    // This assumes that Azan times are in the format like "06:30 AM" or "18:45"
    if (time.length() < 5) return false;  // Minimal length check

    char hourChar = time[0];
    if (hourChar < '0' || hourChar > '9') return false;  // First character should be a digit (hour)
    
    // Check for time format with ':' between hour and minute
    if (time[2] != ':') return false;

    // Check if the time has valid minute and period part (AM/PM)
    if (time.length() > 5 && (time.substring(time.length() - 2) == "AM" || time.substring(time.length() - 2) == "PM")) {
        return true;
    }

    // Otherwise, it's a time format like "06:30"
    return true;
}

// Function to sound the buzzer and display the current prayer time and name
void soundBuzzer(String prayerTime, String prayerName, String flag) {
    Serial.println("Buzzer is sounding!");

    int loopCount = 15;
    int duration = 400;
    if(flag == "rem"){
        loopCount = 5;
        duration = 1000;
    }
    // Flash the prayer time and name on the screen and sound the buzzer
    for (int i = 0; i < loopCount; i++) {  // Three sets of beeps and flashes
        // Sound the buzzer
        tone(BUZZER_PIN, 1000);  // Set buzzer frequency (1000 Hz)
        // Flash the current prayer time and name on the OLED
        display.clearDisplay();  // Clear the display

        // Set large font for the prayer time
        display.setTextSize(2);  // Large font size for the time
        display.setTextColor(SSD1306_WHITE);

        // Calculate position for the time
        int timeX = (SCREEN_WIDTH - (prayerTime.length() * 12)) / 2;
        int timeY = SCREEN_HEIGHT / 3;

        // Display the prayer time at the center of the screen
        display.setCursor(timeX, timeY);
        display.print(prayerTime);

        // Set small font for the prayer name
        display.setTextSize(1);  // Small font size for the prayer name

        // Calculate position for the prayer name
        int nameX = (SCREEN_WIDTH - (prayerName.length() * 6)) / 2;
        int nameY = timeY + 16 + 10;  // Position the name below the time

        // Display the prayer name
        display.setCursor(nameX, nameY);
        display.print(prayerName);

        display.display();     
        delay(duration);              // Beep duration (500 ms)
        // Clear the display to create the flash effect
        display.clearDisplay();
        display.display();
        noTone(BUZZER_PIN);      // Stop the buzzer
        delay(200);              // Pause between beeps

    }
}


void  clearPreferences() {
    preferences.begin("WiFiCreds", false);  // Open the preferences with the same namespace
    preferences.clear();                      // Clear all stored preferences
    preferences.end();                        // Close the preferences
    Serial.println("Preferences cleared.");
    Serial.println("Restarting ESP32");
    ESP.restart();
}

// Function to get public IP
String getPublicIP() {
    delay(500);
    HTTPClient http;
    dynamicMessage("Fetching Public IP");
    http.begin("https://api.ipify.org");  // Use icanhazip.com as an alternative
    int httpResponseCode = http.GET();
    String publicIP = "";

    if (httpResponseCode == 200) {
        publicIP = http.getString();
        dynamicMessage("Public IP Detected", publicIP);
        delay(500);
        Serial.println("Public IP: " + publicIP);
    } else {
        Serial.println("Error getting public IP");
    }
    http.end();
    return publicIP;
}

// Function to fetch geolocation from IP
void getGeoLocation() {
    String publicIP = getPublicIP();
    if (publicIP == "") {
        Serial.println("Cannot fetch geolocation without IP.");
        return;
    }

    HTTPClient http;
    String url = "http://ip-api.com/json/" + publicIP;
    http.begin(url);
    int httpResponseCode = http.GET();

    if (httpResponseCode == 200) {
        String payload = http.getString();
        Serial.println("Geolocation Response: " + payload);

        // Parse JSON response
        StaticJsonDocument<1024> doc;
        deserializeJson(doc, payload);

        latitude = doc["lat"].as<String>();
        longitude = doc["lon"].as<String>();
        Serial.println("Latitude: " + latitude + ", Longitude: " + longitude);
    } else {
        Serial.println("Error fetching geolocation.");
    }
    http.end();
}
