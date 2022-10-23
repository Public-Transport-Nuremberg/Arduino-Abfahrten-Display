#include <WiFi.h>
#include <SPI.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <TinyGPS++.h>
#include <axp20x.h>  // The TTGO uses a powersuply you can control via code
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "./Icons.h"
#include <Vagapi.h>

#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64  // OLED display height, in pixels

#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

TinyGPSPlus gps;
AXP20X_Class axp;
Vagapi vag;
HardwareSerial GPS(1);

const char* ssid = "";
const char* password = "";

// Varuables to calculate Temps and Battery
float VBAT;
float AXP_TEMP;

// Define Timestamp Format
const char timestamp_format[] = "%d-%d-%dT%d:%d:%d%d";

void setup() {
  Serial.begin(115200);

  // Make a connection to our AXP Power Chip
  Wire.begin(21, 22);
  if (!axp.begin(Wire, AXP192_SLAVE_ADDRESS)) {
    Serial.println("AXP192 Begin PASS");
  } else {
    Serial.println("AXP192 Begin FAIL");
  }

  axp.setPowerOutPut(AXP192_LDO3, AXP202_ON);   // GPS main power
  axp.setPowerOutPut(AXP192_LDO2, AXP202_ON);   // provides power to GPS backup battery
  axp.setPowerOutPut(AXP192_DCDC1, AXP202_ON);  // Power on the OLED Display
  axp.adc1Enable(AXP202_VBUS_VOL_ADC1 | AXP202_VBUS_CUR_ADC1 | AXP202_BATT_CUR_ADC1 | AXP202_BATT_VOL_ADC1, true);

  axp.setDCDC1Voltage(3300);  // Set OLED Display to 3.3V to prevent burn in and regulate powerdraw

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;  // Don't proceed, loop forever
  }

  display.clearDisplay();
  display.display();

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.println("Connecting");
  // Draw WiFi Connecting Symbol
  display.clearDisplay();
  updateDisplayOverlay(true);
  //Wait to connect
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    updateDisplayOverlay(true);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to WiFi network with IP Address: ");
  Serial.println(WiFi.localIP());

  GPS.begin(9600, SERIAL_8N1, 34, 12);  //17-TX 18-RX

  // Set variable in VAG Class
  vag.setTimespan(60); // List only departures in the next 10 minutes
}

static void smartDelay(unsigned long ms) {
  unsigned long start = millis();
  do {
    while (GPS.available())
      gps.encode(GPS.read());
  } while (millis() - start < ms);
}

bool isLeapYear(int yr) {
  if (yr % 4 == 0 && yr % 100 != 0 || yr % 400 == 0) return true;
  else return false;
}


byte daysInMonth(int yr, int m) {
  byte days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (m == 2 && isLeapYear(yr)) return 29;
  else return days[m - 1];
}

// Convert a timestamp char* from API into epoch time
long getEpoch(const char* timestamp) {
  int year, month, day, hour, minute, second, timezone;
  sscanf(timestamp, timestamp_format, &year, &month, &day, &hour, &minute, &second, &timezone);
  if (year < 100) year += 2000;

  long epoch = 0;
  for (int yr = 1970; yr < year; yr++)
    if (isLeapYear(yr)) epoch += 366 * 86400L;
    else epoch += 365 * 86400L;
  for (int m = 1; m < month; m++) epoch += daysInMonth(year, m) * 86400L;
  epoch += (day - 1) * 86400L;
  epoch += hour * 3600L;
  epoch += minute * 60;
  epoch += second;
  epoch -= timezone * 3600L;

  return epoch;
}

// Write all the things on the display that are constant like wifi sigal and battery charge
void updateDisplayOverlay(bool UpdateDisplay) {
  VBAT = axp.getBattVoltage() / 1000;
  AXP_TEMP = axp.getTemp();

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(1);
  display.cp437(true);

  // Draw WiFi Logo
  if (WiFi.status() == WL_CONNECTED) {
    if (WiFi.RSSI() < -80) {
      display.drawBitmap(0, 0, signal1_icon16x16, 16, 16, 1);
    } else if (WiFi.RSSI() < -70) {
      display.drawBitmap(0, 0, signal2_icon16x16, 16, 16, 1);
    } else if (WiFi.RSSI() < -60) {
      display.drawBitmap(0, 0, signal3_icon16x16, 16, 16, 1);
    } else {
      display.drawBitmap(0, 0, signal4_icon16x16, 16, 16, 1);
    }
  } else {
    display.drawBitmap(0, 0, cancel_icon16x16, 16, 16, 1);
  }
  // Draw Satelites
  display.setCursor(18, 0);
  display.print(AXP_TEMP, 2);
  display.drawCircle(50, 1, 1, 1);  // Draw ° Symbol...
  display.setCursor(53, 0);
  display.print("C");
  display.setCursor(18, 8);
  display.print("Sat:");
  display.setCursor(42, 8);
  display.print(gps.satellites.value());

  // Draw Battery Stuff
  if (axp.isBatteryConnect()) {  //Dedect if Battery is connected
    if (VBAT > 4.00) {
      display.drawBitmap(112, -1, fillstate4_icon16x16, 16, 16, 1);
    } else if (VBAT > 3.72) {
      display.drawBitmap(112, -1, fillstate3_icon16x16, 16, 16, 1);
    } else if (VBAT > 3.2) {
      display.drawBitmap(112, -1, fillstate2_icon16x16, 16, 16, 1);
    } else {
      display.drawBitmap(112, -1, fillstate1_icon16x16, 16, 16, 1);
    }
    // Draw Charge / Discharge
    if (axp.isChargeing()) {
      display.setCursor(74, 0);
      display.print("+");
      display.setCursor(80, 0);
      display.print(axp.getBattChargeCurrent(), 0);
      display.setCursor(99, 0);
      display.print("mA");
      display.setCursor(74, 8);
      display.print("+");
      display.setCursor(80, 8);
      display.print(axp.getVbusCurrent(), 0);
      display.setCursor(99, 8);
      display.print("mA");
    } else {
      display.setCursor(74, 0);
      display.print("-");
      display.setCursor(80, 0);
      display.print(axp.getBattDischargeCurrent(), 0);
      display.setCursor(99, 0);
      display.print("mA");
    }
  } else {
    // Draw powerplug Icon
    display.drawBitmap(112, 0, plug_icon16x16, 16, 16, 1);
  }
  if (UpdateDisplay) {
    display.display();
  }
}

// Function to print strings that contain german "Umlaute" to the OLED with default LIBs
// Works with UFT8 Chars
void writeUmlaute(int x_start, int y_start, String text, int size = 1) {
  display.setTextSize(size);

  int letter = 0;

  for (std::string::size_type i = 0; i < text.length(); i++) {
    if (x_start + (letter * 6) >= SCREEN_WIDTH) { break; }
    display.setCursor(x_start + (letter * 6), y_start);
    int letter1 = (unsigned char)text[i];
    int letter2 = (unsigned char)text[i + 1];

    if (letter1 == 195 && letter2 == 164) {  // ä
      display.print(char(0x84));
      i++;
      letter++;
    } else if (letter1 == 195 && letter2 == 132) {  // Ä
      display.print(char(0x8E));
      i++;
      letter++;
    } else if (letter1 == 195 && letter2 == 182) {  // ö
      display.print(char(0x94));
      i++;
      letter++;
    } else if (letter1 == 195 && letter2 == 150) {  // Ö
      display.print(char(0x99));
      i++;
      letter++;
    } else if (letter1 == 195 && letter2 == 188) {  // ü
      display.print(char(0x81));
      i++;
      letter++;
    } else if (letter1 == 195 && letter2 == 156) {  // Ü
      display.print(char(0x9A));
      i++;
      letter++;
    } else if (letter1 == 195 && letter2 == 159) {  // ß
      display.print(char(0xE1));
      i++;
      letter++;
    } else {
      display.print(text[i]);
      letter++;
    }
  }
}

void loop() {
  smartDelay(2000);  // Don´t run faster that that
  // Dedect if we alrady have a GPS Fix or not
  if (gps.location.lat() == 0.00 && gps.location.lng() == 0.00) {
    Serial.println("No GPS Fix...");
    updateDisplayOverlay(false);
    display.setTextSize(2);
    display.setCursor(6, 32);
    display.print("No GPS Fix");
    display.display();
  } else {
    DynamicJsonDocument GPS_request(3072);
    DynamicJsonDocument Dep_request(4096);

    DeserializationError error_GPS_request = deserializeJson(GPS_request, vag.getStopsGPS(gps.location.lat(), gps.location.lng(), 500));

    if (error_GPS_request) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error_GPS_request.c_str());
      return;
    }

    JsonObject Haltestellen_0 = GPS_request["Haltestellen"][0];             // Parse first object
    const char* Metadata_Timestamp = GPS_request["Metadata"]["Timestamp"];  // Used to calculate delays
    int Haltestellen_0_VGNKennung = Haltestellen_0["VGNKennung"];           // Used for next API Request (Abfahren)
    const char* Haltestellenname = Haltestellen_0["Haltestellenname"];      // Displaying Stop name on display

    DeserializationError error_Dep_request = deserializeJson(Dep_request, vag.getDepartures(Haltestellen_0_VGNKennung, 5));

    if (error_Dep_request) {
      Serial.print("deserializeJson() failed: ");
      Serial.println(error_GPS_request.c_str());
      return;
    }
    int Start_Y = 17; // Devine vertical start, note the icons are 16x16

    updateDisplayOverlay(false);
    writeUmlaute(0, Start_Y, Haltestellenname);

    for (JsonObject Abfahrten_item : Dep_request["Abfahrten"].as<JsonArray>()) {
      Start_Y = Start_Y + 8;  // Next line to print
      const char* Abfahrten_item_Linienname = Abfahrten_item["Linienname"];
      const char* Abfahrten_item_Richtungstext = Abfahrten_item["Richtungstext"];
      const char* Abfahrten_item_AbfahrtszeitSoll = Abfahrten_item["AbfahrtszeitSoll"];
      const char* Abfahrten_item_AbfahrtszeitIst = Abfahrten_item["AbfahrtszeitIst"];

      // Calculate Timestamp
      //int delay_in_s = getEpoch(Abfahrten_item_AbfahrtszeitIst) - getEpoch(Abfahrten_item_AbfahrtszeitSoll); // Calculates the delay in s
      //int arrives_in_s = getEpoch(Abfahrten_item_AbfahrtszeitSoll) - getEpoch(Metadata_Timestamp); // Arrives in x seconds based on timetaple
      int arrives_in_s = getEpoch(Abfahrten_item_AbfahrtszeitIst) - getEpoch(Metadata_Timestamp); // Shows the arrival based in timetable + delay

      // Write to display...
      display.setCursor(0, Start_Y);
      display.print(String(arrives_in_s / 60)); // Write arrival in min
      writeUmlaute(8, Start_Y, Abfahrten_item_Linienname); // Write linenumber
      writeUmlaute(24, Start_Y, Abfahrten_item_Richtungstext); // Write direction
    }
    display.display();
  }
  if (millis() > 10000 && gps.charsProcessed() < 10)
    Serial.println(F("No GPS data received: check wiring"));
}
