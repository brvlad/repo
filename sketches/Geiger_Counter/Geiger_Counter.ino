/*
   Geiger.ino

   This code interacts with the Alibaba RadiationD-v1.1 (CAJOE) Geiger counter board
   and reports readings in CPM (Counts Per Minute).

   Author: Andreas Spiess

   Based on initial work of Mark A. Heckler (@MkHeck, mark.heckler@gmail.com)

   License: MIT License

   Please use freely with attribution. Thank you!
*/

#include "ESPHelper.h"
#include <credentials.h>
#include <IFTTTMaker.h>

#define WEMOS_OLED

//using Adafruit_SSD1306-esp8266-64x48 lib for wemos OLED.
//Adafruit_SSD1306_master don't work...
#ifndef WEMOS_OLED
#include <SSD1306.h>
#else
#include <Adafruit_SSD1306.h>
#endif


#define LOG_PERIOD 20000 //Logging period in milliseconds
#define MINUTE_PERIOD 60000
//#define LOG_PERIOD 2000 //Logging period in milliseconds
//#define MINUTE_PERIOD 5000

// IFTTT
#define EVENT_NAME "RadioactivityCPM" // Name of your event name, set when you are creating the applet

#define GEIGER_TOPIC_BASE "hata/geiger/"
const char* HOSTNAME = "esp_geiger";  

#define OUT_TOPIC  GEIGER_TOPIC_BASE "output/"
//output topic nodes
#define PUB_DEBUG  OUT_TOPIC "debug"
#define PUB_GEIGER OUT_TOPIC "cpm"
#define PUB_ID     OUT_TOPIC "id"

#define CBUF_SZ 256  //char buffer size for posting MQTT

// ThingSpeak Settings
const int channelID = THINGSPEAK_CHANNEL_ID;
const char* thingspeak_server = "api.thingspeak.com";

WiFiClient client;
WiFiClientSecure secure_client;
IFTTTMaker ifttt(IFTTT_KEY, secure_client);

netInfo homeNet = {	.mqttHost = RPI_MQTT_IP,			//can be blank if not using MQTT
					.mqttUser = "", 	//can be blank
					.mqttPass = "", 	//can be blank
					.mqttPort = 1883,					//default port for MQTT is 1883 - only chance if needed.
					.ssid = mySSID, 
					.pass = myPASSWORD};
ESPHelper myESP(&homeNet);


volatile unsigned long counts = 0;                       // Tube events
unsigned long cpm = 0;                                   // CPM
unsigned long previousMillis;                            // Time measurement
bool initDone = false;  

//GPIO 0 mapped to D3
const int inputPin = D5;

//Display setup:
//SDA: Serial Data Line (D2/GPIO4 on the Wemos D1 Mini)
//SCL: Serial Clock Line (D1/GPIO5 on the Wemos D1 Mini)

#ifndef WEMOS_OLED
SSD1306  display(0x78, D2, D1);   //i2c addr, SDA, SCL
#else
Adafruit_SSD1306 display(0); ///gpio0
#endif

void ISR_impulse() { // Captures count of events from Geiger counter board
  counts++;
}

void setup() 
{
  Serial.begin(115200);
  
  displayInit();
  //displayString("Welcome", 64, 15);
  Serial.println("Connecting to Wi-Fi");

  //setup ESPHelper
	myESP.OTA_enable();
	myESP.OTA_setPassword(OTA_PASS);
	myESP.OTA_setHostname(HOSTNAME);
	myESP.begin();		//start ESPHelper
  
  // Clear the buffer.
  display.clearDisplay();
  //display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println("WiFi on @: ");  
  display.println(WiFi.localIP());
  display.display();
  delay(1000);

  pinMode(inputPin, INPUT);                                        // Set pin for capturing Tube events
  attachInterrupt(inputPin, ISR_impulse, FALLING); // Define interrupt on falling edge
  Serial.println("Interrupt in: ");
  Serial.println(inputPin);

}

void loop() {
  unsigned long currentMillis = millis();
  char postBuff[CBUF_SZ];
  String postStr;

	//run the loop() method as often as possible - this keeps the network services running
  if(myESP.loop() >= WIFI_ONLY)
  {
    if(!initDone)
    {
      initDone = true;
      publishStr(PUB_ID, getEspID());
    }
  }
  
  if (currentMillis - previousMillis > LOG_PERIOD) {
    previousMillis = currentMillis;
    cpm = counts * MINUTE_PERIOD / LOG_PERIOD;
    Serial.print("counts: ");
    Serial.println(counts);
    counts = 0;
    //display.clear();
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0,0);
    //display.print("IP:");
    //display.println(WiFi.localIP());
    display.setTextSize(3);
    display.println("CPM");  
    display.println(cpm);
    display.display();
    //displayString("CPM:", 10, 20);
    //displayInt(cpm, 64, 40);

    Serial.print("Radioactivity (CPM): ");
    Serial.println(cpm);

    postStr = "Radioactivity (CPM): ";
    postStr += cpm;
    postStr.toCharArray(postBuff, CBUF_SZ);
    publishDebug(postBuff);

    itoa(cpm, postBuff, 10);
    publish(PUB_GEIGER, postBuff);

    postThingspeak(cpm);
    if (cpm > 100 ) IFTTT( EVENT_NAME, cpm);
  }
  
  delay(500);
}

void postThingspeak(int postValue) {
  if (client.connect(thingspeak_server, 80)) {

    // Construct API request body
    String body = "field1=";
    body += String(postValue);

    Serial.print("Radioactivity (CPM): ");
    Serial.println(postValue);

    client.print("POST /update HTTP/1.1\n");
    client.print("Host: api.thingspeak.com\n");
    client.print("Connection: close\n");
    client.print("X-THINGSPEAKAPIKEY: " + String(WRITE_API_KEY) + "\n");
    client.print("Content-Type: application/x-www-form-urlencoded\n");
    client.print("Content-Length: ");
    client.print(body.length());
    client.print("\n\n");
    client.print(body);
    client.print("\n\n");

  }
  client.stop();
}

void IFTTT(String event, int postValue) {
  if (ifttt.triggerEvent(EVENT_NAME, String(postValue))) {
    Serial.println("Successfully sent to IFTTT");
  } else
  {
    Serial.println("IFTTT failed!");
  }
}

void displayInit() {
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 64x48)
  //display.init();
  //display.flipScreenVertically();
  //display.setFont(ArialMT_Plain_24);
}

void displayInt(int dispInt, int x, int y) {
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(x,y);
  display.println(dispInt);  

  /*display.setColor(WHITE);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(x, y, String(dispInt));
  display.setFont(ArialMT_Plain_24);
  */
  display.display();
}

void displayString(String dispString, int x, int y) {
  
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(x,y);
  display.println(dispString);  

  /*display.setColor(WHITE);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.drawString(x, y, dispString);
  display.setFont(ArialMT_Plain_24);*/
  
  display.display();
}


//return hostname:IP pair string
String getEspID()
{
  String printStr;
  
  printStr = HOSTNAME;
  printStr += ": ";
  printStr += myESP.getIP();
  
  return printStr;
}

//publish to topic & dump to serial
void publish(const char* topic, const char* text){

  myESP.publish(topic, text, true);
#ifdef DEBUG_VER
  Serial.println(text);
#endif    
}


//publish to topic & dump to serial
void publishStr(const char* topic, String str){
  char text[CBUF_SZ];
  
  str.toCharArray(text, CBUF_SZ);
  myESP.publish(topic, text, true);
#ifdef DEBUG_VER
  Serial.println(text);
#endif    
}

//Dump to debug topic and serial
void publishDebug(const char* text)
{
  String pubString = String(HOSTNAME);
  char message[128];

  pubString += " : ";
  pubString += text;
  //conver the String into a char*
  pubString.toCharArray(message, 128);
  myESP.publish(PUB_DEBUG, message, true);
#ifdef DEBUG_VER
  Serial.println(pubString);
#endif    
}

