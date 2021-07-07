#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#define FASTLED_ESP8266_NODEMCU_PIN_ORDER
#include <FastLED.h>
#include <PubSubClient.h>

/* LEDs */
#define LED_PIN     2
#define NUM_LEDS    3
#define BRIGHTNESS  64
#define LED_TYPE    WS2811
#define COLOR_ORDER GRB
#define UPDATES_PER_SECOND 40
#define MILLI_AMPS 100 // Consumption: 1mA (off) / 13.5 (red) / 35mA (white)

/* MQTT */
#define MQTT_SERVER      "node02.myqtthub.com"
#define MQTT_PORT        1883
#define TOPIC_SUBSCRIBE  "busylight/camargo"
#define MQTT_DEVICE_NAME "esp123"
#define MQTT_USER        "esp123"
#define MQTT_PASSWORD    "esp123"

/* How much ESP will sleep each cycle. */
#define SLEEP_TIME 2e6

/* State machine */
#define CONFIG_MODE 0 /* Wifi configuration - it enables the internal HTTP server. */
#define NORMAL_MODE 1 /* Normal operation - reads MQTT message from broker. */
unsigned int state = CONFIG_MODE;

/* Position of information in EEPROM. */
#define EEPROM_SSID 0
#define EEPROM_PWD 34

/* Button to reset the EEPROM to factory. */
#define RESET_PIN D5

CRGB leds[NUM_LEDS];

/* Set web server port number to 80. Used only in CONFIG_MODE. */
ESP8266WebServer server(80); 

/* Configure the MQTT client. Used only in NORMAL_MODE. */
WiFiClient espClient;
PubSubClient client(espClient);

/* Wifi SSID. */
String ssid = "";

/* Writes a string to EEPROM. */
void writeStringToEEPROM(int addrOffset, const String &strToWrite) {
  byte len = strToWrite.length();
  EEPROM.write(addrOffset, len);
  
  for (int i = 0; i < len; i++) {
    EEPROM.write(addrOffset + 1 + i, strToWrite[i]);
  }
}

/* Reads a string from EEPROM. */
String readStringFromEEPROM(int addrOffset) {
  int newStrLen = EEPROM.read(addrOffset);
  char data[newStrLen + 1];
  
  for (int i = 0; i < newStrLen; i++) {
    data[i] = EEPROM.read(addrOffset + 1 + i);
  }
  
  data[newStrLen] = '\0';
  return String(data);
}

/* Turn on or off the sign with the selected color. */
void handleSignChange(bool status, int r_channel, int g_channel, int b_channel) {
  if (status) {
    leds[0].setRGB(r_channel, g_channel, b_channel);
    leds[1].setRGB(r_channel, g_channel, b_channel);
    leds[2].setRGB(r_channel, g_channel, b_channel);
    FastLED.show(); 
  } else {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show(); 
  }
}

/* Reads the HTTP post to configure the Wifi. */
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

    message = "New SSID (" + ssid + ") saved with success with password (" + pwd + ").";
    
    server.send(200, "text/plain", message);

    successConfiguringWifi();
    
    ESP.restart();
  } else {
    errorConfiguringWifi();    
    message = "Provide a valid SSID.";    
    
    server.send(200, "text/plain", message);
  } 
}

/* Clean the EEPROM. */
void resetMemory() {
  EEPROM.begin(100);
  writeStringToEEPROM(EEPROM_SSID, "");
  writeStringToEEPROM(EEPROM_PWD, "");
  EEPROM.end();
}

/* Fast blink 5 times if an error was found during the Wifi configuration during CONFIG_MODE. */
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

/* Turn on sign for 2 seconds if the Wifi configuration works during CONFIG_MODE. */
void successConfiguringWifi() {
  fill_solid(leds, NUM_LEDS, CRGB::Red);
  FastLED.show();  
  delay(2000);

  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
}

/* Fast blink 3 times if an error was found during the Wifi connection during NORMAL_MODE. */
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

/* Gets password from EEPROM and tries to connect to the configured Wifi network. */
void connectToWifi() {  
  String pwd = readStringFromEEPROM(EEPROM_PWD);
  
  state = NORMAL_MODE;
    
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pwd);

  uint32_t loopStart = millis();
    
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);

    if (millis() - loopStart > 5000) {
      errorConnectingToWifi();
      ESP.deepSleep(SLEEP_TIME);
    }
  }
}

/* Reads and parse the MQTT message. */
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  
  Serial.println();

  StaticJsonDocument<96> doc;
  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    errorConfiguringWifi();    
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  if (doc["status"].isNull()) {
    Serial.println("Status not present in the message.");
    return;
  }
  
  const bool status = doc["status"];
  const int r_channel = doc["r"].isNull() ? 255 : doc["r"];  
  const int g_channel = doc["g"].isNull() ? 0 : doc["g"];
  const int b_channel = doc["b"].isNull() ? 0 : doc["b"];

  handleSignChange(status, r_channel, g_channel, b_channel);
}

/* Connect to MQTT broker. */
void connectMQTT() {
  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setCallback(callback);
  
  Serial.print("Attempting MQTT connection...");
  
//  // Create a random client ID
//  String clientId = "BusyLightEduardo-";
//  clientId += String(random(0xffff), HEX);
  
  // Attempt to connect
  if (client.connect(MQTT_DEVICE_NAME, MQTT_USER, MQTT_PASSWORD)) {    
    Serial.println("connected");
    
    // ... and subscribe
    client.subscribe(TOPIC_SUBSCRIBE);
  } else {
    Serial.print("failed, rc=");
    Serial.print(client.state());
  }
}

/* Configure the local HTTP server during CONFIG_MODE. */
void configureForInitialUse() {
  state = CONFIG_MODE;

  IPAddress local_IP(192,168,0,2);
  IPAddress gateway(192,168,0,1);
  IPAddress subnet(255,255,255,0);
  
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP("busylight0001");
  
  Serial.print("Soft-AP IP address: ");
  Serial.println(WiFi.softAPIP());
  Serial.println();
}

/* Configure the LED strip. */
void configureLed() {
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, MILLI_AMPS);
}

/* Runs the reset procedure if the RESET_PIN is pressed by more than 3 seconds. */ 
void listenResetButton() {    
  uint32_t loopStart = millis();
  bool buttonPressed = false;
    
  while (digitalRead(RESET_PIN)) {  
    buttonPressed = true;
    fill_solid(leds, NUM_LEDS, CRGB::Orange);
    FastLED.show();
    yield();

    // Wait for 3 seconds
    if (millis() - loopStart > 3000) {
      for (int i=0; i<16; i++) {
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
        delay(50);
        
        fill_solid(leds, NUM_LEDS, CRGB::Orange);
        FastLED.show();
        delay(50);        
      }
      
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      FastLED.show();
      
      resetMemory();
      
      ESP.deepSleep(1e6);
    }
  }

  if (buttonPressed) {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.show();
  }
}

/* Setup function. */
void setup() {  
  Serial.begin(115200);
  configureLed();

  pinMode(RESET_PIN, INPUT_PULLUP);

  listenResetButton();

  EEPROM.begin(100);
  ssid = readStringFromEEPROM(EEPROM_SSID);
  checkInitializedEEPROM(100);

  // Connecting to configured WiFi
  if (ssid.length()) {
    connectToWifi();
    randomSeed(micros());
    connectMQTT();
    
    EEPROM.end();
    
  // Entering WiFi configuration
  } else {
    configureForInitialUse();
    
    EEPROM.end();
    
    server.on("/config_wifi", handleConfigWifi);
    server.begin();
  }
}

/* 
 * During NORMAL_MODE it connects to MQTT broker, reads a message and go to deep sleep by SLEEP_TIME. 
 * During CONFIG_MODE it loads a local HTTP server to wait for the configuration Wifi request. 
 */
void loop(){
  if (state == NORMAL_MODE) {
    listenResetButton();
    
    uint32_t loopStart = millis();
    
    while (millis() - loopStart < 300) { 
      if (!client.connected()) { 
        connectMQTT(); 
       } 
      else client.loop(); 
    }
    
    ESP.deepSleep(SLEEP_TIME);
  } else if (state == CONFIG_MODE) {
    server.handleClient();
  }
}

/* Checks if the EEPROM is initialized. */
void checkInitializedEEPROM(const int validationSize) {
  for (int i = 0; i < validationSize; i++) {
    if (EEPROM.read(i) != 255) {
      return;
    }
  }

  Serial.println("### EEPROM non initialized!");
  initializeEEPROM(validationSize);
}

/* Initializes EEPROM writing \0 to all positions. */
void initializeEEPROM(const int EEPROMSize) {
  for (int i = 0; i < EEPROMSize; i++) {
    EEPROM.write(i, '\0');
  }
}
