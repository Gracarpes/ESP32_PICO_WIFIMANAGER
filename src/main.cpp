#include <FS.h>
#include <WiFiManager.h>
#include <time.h>
#include <stdio.h>
#include <WiFiUdp.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <Wire.h>
#include "SSD1306Wire.h"
#include "OLEDDisplayUi.h"
#include "time.h"
#include <ArduinoJson.h>
#include "images.h"

#ifdef ESP32
  #include <SPIFFS.h>
#endif

//Parametrización de la sonda de temperatura
#define Sonda_Pin 22                          // Pin donde se conecta la sonda de temperatura
OneWire bus_OnWire(Sonda_Pin);                // Instancia a comunicación onewire
DallasTemperature Sonda_Temp(&bus_OnWire);    // Instancia al sensor de temperatura 
float tempC;                                  // Variable para almecenar la temperatura 

//Inicialización NTP
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;
struct tm timeinfo;
String timenow = "";

//Config Pantalla OLED
SSD1306Wire  display(0x3c, 32, 26); // Inicializa el display oled
OLEDDisplayUi ui (&display);
bool screen = false;

//Config MQTT
WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
unsigned long lastMsg1 = 0;
unsigned long lastMsg2 = 0;
unsigned long clk = 0;
#define MSG_BUFFER_SIZE1  (50)
char msg1[MSG_BUFFER_SIZE1];

//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40];
char mqtt_port[6]  = "1883";
char mqtt_user[32] = "Ruben114";
char mqtt_password[32] = "nmqwyyff";
char mqtt_topic[32] = "Prueba";
char mqtt_time[5] = "50";

//Funciones NTP
int second()
{
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return 0;
  }
  char variable[3];
  strftime(variable,3, "%S", &timeinfo);
  return atoi(variable);
}

int minute()
{
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return 0;
  }
  char variable[3];
  strftime(variable,3, "%M", &timeinfo);
  return atoi(variable);
}

int hour()
{
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return 0;
  }
  char variable[3];
  strftime(variable,3, "%H", &timeinfo);
  return atoi(variable);
}

String twoDigits(int digits){
  if(digits < 10) {
    String i = '0'+String(digits);
    return i;
  }
  else {
    return String(digits);
  }
}

//Funciones SSD1306
void drawImageDemo() {
  delay(1000);
  display.clear();
  delay(1000);
  display.drawXbm(34, 14, WiFi_Logo_width, WiFi_Logo_height, WiFi_Logo_bits);
  display.display();
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str(), mqtt_user, mqtt_password)) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void setupSpiffs(){
  //clean FS, for testing
  // SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_user, json["mqtt_user"]);
          strcpy(mqtt_password, json["mqtt_password"]);
          strcpy(mqtt_topic, json["mqtt_topic"]);
          strcpy(mqtt_time, json["mqtt_time"]);
        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read
}

void setup() {
  //OLED LCD INIT
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
  delay(100);
  drawImageDemo();
  Serial.begin(115200);
  delay(1000);

  setupSpiffs();

  WiFiManager wm;
  // TEST OPTION FLAGS
  bool TEST_CP         = true; // always start the configportal, even if ap found
  int  TESP_CP_TIMEOUT = 300; // test cp timeout

  wm.setSaveConfigCallback(saveConfigCallback);

  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", mqtt_user, 32);
  WiFiManagerParameter custom_mqtt_password("password", "mqtt password", mqtt_password, 32);
  WiFiManagerParameter custom_mqtt_topic("topic", "mqtt topic", mqtt_topic, 32);
  WiFiManagerParameter custom_mqtt_time("time", "mqtt time", mqtt_time, 5);

  //add all your parameters here
  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_port);
  wm.addParameter(&custom_mqtt_user);
  wm.addParameter(&custom_mqtt_password);
  wm.addParameter(&custom_mqtt_topic);
  wm.addParameter(&custom_mqtt_time);

  //reset settings - wipe credentials for testing
  //wm.resetSettings();

  //automatically connect using saved credentials if they exist
  //If connection fails it starts an access point with the specified name
  //here  "AutoConnectAP" if empty will auto generate basedcon chipid, if password is blank it will be anonymous
  //and goes into a blocking loop awaiting configuration
  // set Hostname
  wm.setHostname("JLLOPEZ_MQTT_SETTING");
  wm.setConfigPortalTimeout(300);
  wm.setBreakAfterConfig(true); // needed to use saveWifiCallback
  if (!wm.autoConnect("JLLOPEZ_MQTT_SETTING","JLLOPEZ_2021")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    // if we still have not connected restart and try all over again
    ESP.restart();
    delay(5000);
  }
  else if(TEST_CP) {
    // start configportal always
    delay(1000);
    Serial.println("TEST_CP ENABLED");
    wm.setConfigPortalTimeout(TESP_CP_TIMEOUT);
    wm.startConfigPortal("JLLOPEZ_MQTT_SETTING");
  }

  else{  //if you get here you have connected to the WiFi
    Serial.println("connected...yeey :)");
  }

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());
  strcpy(mqtt_topic, custom_mqtt_topic.getValue());
  strcpy(mqtt_time, custom_mqtt_time.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"]   = mqtt_port;
    json["mqtt_user"]   = mqtt_user;
    json["mqtt_password"]   = mqtt_password;
    json["mqtt_topic"]   = mqtt_topic;
    json["mqtt_time"]   = mqtt_time;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.prettyPrintTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
    shouldSaveConfig = false;
  }
  delay(1000);
  Serial.println("local ip");
  Serial.println(WiFi.localIP());
  Serial.println(WiFi.gatewayIP());

  //DS18B20
  Sonda_Temp.begin();
  
  //MQTT
  client.setServer(mqtt_server, atoi(mqtt_port));

  //init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  unsigned long now = millis();
  if (now - lastMsg > 1000) {
    lastMsg = now;
    timenow = String(twoDigits(hour())+":"+twoDigits(minute())+":"+twoDigits(second()));
    if (timenow == "03:00:00"){
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    }
  }
  if (now - lastMsg2 > atoi(mqtt_time)*1000) {
    lastMsg2 = now;
    snprintf (msg1, MSG_BUFFER_SIZE1,"%.2f", tempC);
    client.publish(mqtt_topic, msg1);
  }
  if (now - lastMsg1 > 5000) {
    lastMsg1 = now;
    tempC = Sonda_Temp.getTempCByIndex(0);
    Sonda_Temp.requestTemperatures();
    Serial.println(tempC);
    screen = !screen;
  }
  if (screen){
    if (now - clk > 1000){
      clk = now;
      display.clear();
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.setFont(ArialMT_Plain_24);
      display.drawString(64, 18, timenow);
      display.display();
    }
   }
  if (!screen){
    if (now - clk > 1000){
      clk = now;
      display.clear();
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.setFont(ArialMT_Plain_24);
      display.drawString(64, 18, String(tempC)+" ºC");
      display.display();
   }
  }
}