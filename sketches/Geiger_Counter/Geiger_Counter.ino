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

//using Adafruit_SSD1306-esp8266-64x48 lib for wemos OLED.
//Adafruit_SSD1306_master don't work...
#include <SSD1306.h>

#define LOG_PERIOD 20000 //Logging period in milliseconds
#define MINUTE_PERIOD 60000

// IFTTT
#define EVENT_NAME "RadioactivityCPM" // Name of your event name, set when you are creating the applet

//#define GEIGER_TOPIC_BASE "hata/temp/geiger/"
#define GEIGER_TOPIC_BASE "hata/geiger/"
const char* HOSTNAME = "esp_geiger";  

#define OUT_TOPIC  GEIGER_TOPIC_BASE "output/"
//output topic nodes
#define PUB_DEBUG  OUT_TOPIC "debug"
#define PUB_GEIGER OUT_TOPIC "cpm"
#define PUB_MQ9    OUT_TOPIC "mq9"
#define PUB_ID     OUT_TOPIC "id"

#define CBUF_SZ 256  //char buffer size for posting MQTT


#define R0_VAL 1.08   //MQ9 gas sensor calibration val
#define LOGIC_LEVEL 3.3   //3.3V logic

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

#define LGREEN D7
#define LRED   D6
#define LBLUE  D8

//GPIO 0 mapped to D5
const int inputPin = D5;

//Display setup:
//SDA: Serial Data Line (D2/GPIO4 on the Wemos D1 Mini)
//SCL: Serial Clock Line (D1/GPIO5 on the Wemos D1 Mini)
SSD1306  display(0x3c, D2, D1);

void ISR_impulse() { // Captures count of events from Geiger counter board
  counts++;
}

void setup() 
{
  Serial.begin(115200);
  displayInit();
  Serial.println("Connecting to Wi-Fi");

  //setup LEDs
  pinMode(LGREEN, OUTPUT);
  pinMode(LBLUE, OUTPUT);
  pinMode(LRED, OUTPUT);
  analogWrite(LGREEN, 0);
  analogWrite(LBLUE, 255);
  analogWrite(LRED, 0);
  
  //setup ESPHelper
	myESP.OTA_enable();
	myESP.OTA_setPassword(OTA_PASS);
	myESP.OTA_setHostname(HOSTNAME);
	myESP.begin();		//start ESPHelper
  
  // Clear the buffer.
  display.clear();
  display.drawString(0, 0,"WiFi on @: ");  
  display.drawString(0, 16, WiFi.localIP().toString());
  display.display();
  delay(1000);

  //setup analog and interrupt pins
  pinMode(A0, INPUT);
  pinMode(inputPin, INPUT);                           // Set pin for capturing Tube events
  attachInterrupt(inputPin, ISR_impulse, FALLING); // Define interrupt on falling edge
  Serial.println("Interrupt in: ");
  Serial.println(inputPin);
}


void loop() {
  unsigned long currentMillis = millis();
  char postBuff[CBUF_SZ];
  String postStr;
  float sensor_volt;
  float RsRo_air; //  Get the value of RS via in a clear air
  float R0;  // Get the value of R0 via in LPG
  float ratio = 0; // Get ratio RS_GAS/RsRo_air
  static int cnt = 0;  //averaged num of gas samples
  static float avgMq9 = 0; //average mq9 readings

	//run the loop() method as often as possible - this keeps the network services running
  if(myESP.loop() >= WIFI_ONLY)
  {
    if(!initDone)
    {
      initDone = true;
      publishStr(PUB_ID, getEspID());
    }
  }

/***************  MQ9 sensor  handling ********************/
  //get sensor reading and scale to [0-5] V
  sensor_volt = analogRead(A0);
  sensor_volt = (sensor_volt/1024)*LOGIC_LEVEL;
  RsRo_air = (LOGIC_LEVEL-sensor_volt)/sensor_volt; // omit * RL
  R0 = RsRo_air/9.9; // The ratio of RS/R0 is 9.9 in LPG gas from Graph (Found using WebPlotDigitizer)

  ratio = RsRo_air/R0_VAL;  // ratio = RS/R0
  cnt++;
  avgMq9 = (avgMq9*(cnt-1) + ratio)/cnt;
  //rsair (RsRo of air) = 2 ~ 200ppm, 0.3 ~ 10000ppm
  if (ratio < 4)
  {
    Serial.print("Danger zone! Over 500ppm. ([ 10-2] is less than 200ppm, [0.3] ~ 10000ppm) RsR0 == ");
    Serial.println(ratio);
  }
  Serial.print("sensor_volt = ");
  Serial.print(sensor_volt);
  Serial.print(" raw RsRo = ");
  Serial.print(RsRo_air);
  Serial.print(" adj Rs/R0 = ");
  Serial.println(ratio);


  if (currentMillis - previousMillis > LOG_PERIOD) 
  {
    previousMillis = currentMillis;
    cpm = counts * MINUTE_PERIOD / LOG_PERIOD;
    Serial.print("counts: ");
    Serial.println(counts);
    counts = 0;

    Serial.print("Radioactivity (CPM): ");
    Serial.println(cpm);

    postStr = "Radioactivity (CPM): ";
    postStr += cpm;
    postStr.toCharArray(postBuff, CBUF_SZ);
    publishDebug(postBuff);

    itoa(cpm, postBuff, 10);
    publish(PUB_GEIGER, postBuff);

    //publish and clear average MQ9 readings for sample period
    dtostrf(avgMq9, 10, 2, postBuff);
    publish(PUB_MQ9, postBuff);
    cnt = 0;
    avgMq9 = 0;
    
    postThingspeak(cpm);
    if (cpm > 100 ) IFTTT( EVENT_NAME, cpm);
  }

//Now display stuff:
  display.clear();
  postStr = "\u2622 CPM: ";
  postStr += cpm;
  display.drawString(0, 0, postStr);
  postStr = "Gas/CO: ";
  postStr += ratio;
  display.drawString(0, 24, postStr);  
  if (ratio < 3.5)
  {
    setColor(255,0,0);
    postStr = "Air quality is BAD!";
  }
  else if (ratio > 7)
  {
    setColor(0,255,0);
    postStr = "Air quality is good";
  }
  else
  {
    setColor(255,196,0);
    postStr = "Air quality is blah...";
  }
  display.drawString(0, 48, postStr);  
  display.display();
  
/***************   ********************/
  delay(1000);
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

void displayInit() 
{
  display.init();
  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_16);
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



void setColor (int R, int G, int B)
{
  analogWrite(LRED, R);
  analogWrite(LGREEN, G);
  analogWrite(LBLUE, B);
}

/*
 //measuring gas calibration
 void loop() {

    float sensor_volt;
    float RS_gas; // Get value of RS in a GAS
    float ratio; // Get ratio RS_GAS/RsRo_air
    int sensorValue = analogRead(A0);
    sensor_volt=(float)sensorValue/1024*5.0;
    RS_gas = (5.0-sensor_volt)/sensor_volt; // omit *RL

    //Replace the name "R0" with the value of R0 in the demo of First Test
    ratio = RS_gas/R0_VAL;  // ratio = RS/R0

    Serial.print("sensor_volt = ");
    Serial.println(sensor_volt);
    Serial.print("RS_ratio = ");
    Serial.println(RS_gas);
    Serial.print("Rs/R0 = ");
    Serial.println(ratio);

    Serial.print("\n\n");

    delay(1000);
}
*/

