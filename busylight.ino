// Load Wi-Fi library
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#define FASTLED_ESP8266_NODEMCU_PIN_ORDER
#include <FastLED.h>

#define LED_PIN     2
#define NUM_LEDS    3
#define BRIGHTNESS  64
#define LED_TYPE    WS2811
#define COLOR_ORDER GRB
#define UPDATES_PER_SECOND 40
#define MILLI_AMPS 100 // Consumption: 1mA (off) / 13.5 (red) / 35mA (white)

#define EEPROM_SSID 0
#define EEPROM_PWD 34

CRGB leds[NUM_LEDS];

// Set web server port number to 80
ESP8266WebServer server(80);

// Assign output variables to GPIO pins
const int output5 = 5;

unsigned int state = 0; /* 0 - WiFi configuration; 1 - normal use */

IPAddress local_IP(192, 168, 0, 70);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);   //optional
IPAddress secondaryDNS(8, 8, 4, 4); //optional

void handleSignChange() {
  String message = "";
  String status = server.arg("status");

  if (status == "on") {
    message = "Turning sign on...";

    fill_solid(leds, NUM_LEDS, CRGB::Red);
    FastLED.show(); 
  } else if (status == "off") {
    message = "Turning sign off...";
 
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show(); 
  } else {  
    message = "Provide a valid status.";
  }

  String payload = server.arg("plain");

  if (!payload.length()) {
    server.send(200, "text/plain", message);
  }
  
  StaticJsonDocument<96> doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  const char* color = doc["color"];
  const char* animation = doc["animation"];

  Serial.print("Color: ");
  Serial.println(color);
  Serial.print("Animation: ");
  Serial.println(animation);
  Serial.println();

  server.send(200, "text/plain", message);
}
  
void handleConfigWifi() {
  String payload = server.arg("plain");
  String message = "";

  StaticJsonDocument<96> doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    errorConfiguringWifi();    
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  const char *ssid_c = doc["ssid"];
  const char *pwd_c = doc["pwd"];  
  const String ssid = String(ssid_c);
  const String pwd = String(pwd_c);
  
  fill_solid(leds, NUM_LEDS, CRGB::Red);
  FastLED.show(); 

  if (ssid.length()) {
    EEPROM.begin(100);
    String savedSsid = readStringFromEEPROM(EEPROM_SSID);
    String savedPwd = readStringFromEEPROM(EEPROM_PWD);

    /* Only saves if some information changed. */
    if (savedSsid != ssid || savedPwd != pwd) {
      writeStringToEEPROM(EEPROM_SSID, ssid);
      writeStringToEEPROM(EEPROM_PWD, pwd);
    }
    EEPROM.end();

    successConfiguringWifi();    
    message = "New SSID (" + ssid + ") saved with success with password (" + pwd + ").";
    
    server.send(200, "text/plain", message);

    delay(3000);
    
    ESP.restart();
  } else {
    errorConfiguringWifi();    
    message = "Provide a valid SSID.";    
    
    server.send(200, "text/plain", message);
  } 
}

void handleResetWifi() {
  EEPROM.begin(100);
  writeStringToEEPROM(EEPROM_SSID, "");
  writeStringToEEPROM(EEPROM_PWD, "");
  EEPROM.end();

  server.send(200, "text/plain", "Wifi configuration reseted");
}

void errorConfiguringWifi() {
  for (int i=0; i<5; i++) {
    fill_solid(leds, NUM_LEDS, CRGB::Red);
    FastLED.show();        
    delay(100);    
    
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
    delay(100);
  }
}

void successConfiguringWifi() {
  fill_solid(leds, NUM_LEDS, CRGB::Red);
  FastLED.show();  
  delay(2000);

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
}

void errorConnectingToWifi() {
  for (int i=0; i<3; i++) {
    fill_solid(leds, NUM_LEDS, CRGB::Red);
    FastLED.show(); 
    delay(100);
    
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show(); 
    delay(100);
  }
}

void successConectingToWifi() {
  fill_solid(leds, NUM_LEDS, CRGB::Red);
  FastLED.show(); 
  delay(100);
  
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
}

void setup() {  
  Serial.begin(115200);

  EEPROM.begin(100);
  checkInitializedEEPROM(100);
  String ssid = readStringFromEEPROM(EEPROM_SSID);
  String pwd = readStringFromEEPROM(EEPROM_PWD);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MILLI_AMPS);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  // Connecting to configured WiFi
  if (ssid.length()) {
    state = 1;
        
    if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
      errorConnectingToWifi();
      
      Serial.println("STA Failed to configure");
    }
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pwd);

    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {       
      successConectingToWifi();
      
      Serial.println();
      Serial.print("WiFi connected. IP: ");
      Serial.println(WiFi.localIP());
      Serial.println();
      
    } else {
      errorConnectingToWifi();
      
      Serial.println();
      Serial.println("Failed to connect to SSID: " + ssid);
      Serial.println();
    }

    ESP.deepSleep(2e6); // 2 seconds;

  // Entering WiFi configuration
  } else {
    state = 0;

    IPAddress local_IP(192,168,0,2);
    IPAddress gateway(192,168,0,1);
    IPAddress subnet(255,255,255,0);
    
    WiFi.softAPConfig(local_IP, gateway, subnet);
    WiFi.softAP("sign1234");
    
    Serial.print("Soft-AP IP address: ");
    Serial.println(WiFi.softAPIP());
    Serial.println();
  }
  EEPROM.end();
  
  server.on("/sign_change", handleSignChange);
  server.on("/config_wifi", handleConfigWifi);
  server.on("/reset_wifi", handleResetWifi);
  server.begin();
  
  Serial.println("Server listening...");
}

void loop(){
  server.handleClient();
}

void writeStringToEEPROM(int addrOffset, const String &strToWrite) {
  byte len = strToWrite.length();
  EEPROM.write(addrOffset, len);
  
  for (int i = 0; i < len; i++) {
    EEPROM.write(addrOffset + 1 + i, strToWrite[i]);
  }
}

String readStringFromEEPROM(int addrOffset) {
  int newStrLen = EEPROM.read(addrOffset);
  char data[newStrLen + 1];
  
  for (int i = 0; i < newStrLen; i++) {
    data[i] = EEPROM.read(addrOffset + 1 + i);
  }
  
  data[newStrLen] = '\0';
  return String(data);
}

void checkInitializedEEPROM(const int validationSize) {
  for (int i = 0; i < validationSize; i++) {
    if (EEPROM.read(i) != 255) {
      return;
    }
  }

  Serial.println("### EEPROM non initialized!");
  initializeEEPROM(validationSize);
}

void initializeEEPROM(const int EEPROMSize) {
  for (int i = 0; i < EEPROMSize; i++) {
    EEPROM.write(i, '\0');
  }
}
