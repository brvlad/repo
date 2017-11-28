/*
   Geiger.ino

   This code interacts with the Alibaba RadiationD-v1.1 (CAJOE) Geiger counter board
   and reports readings in CPM (Counts Per Minute).

   Author: Andreas Spiess

   Based on initial work of Mark A. Heckler (@MkHeck, mark.heckler@gmail.com)

   License: MIT License

   Please use freely with attribution. Thank you!
*/

//#include <WiFi.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <IFTTTMaker.h>

#define WEMOS_OLED

#ifndef WEMOS_OLED
#include <SSD1306.h>
#else
#include <Adafruit_SSD1306.h>
#endif


#include <credentials.h> // or define mySSID and myPASSWORD and THINGSPEAK_API_KEY

#define LOG_PERIOD 20000 //Logging period in milliseconds
#define MINUTE_PERIOD 60000
//#define LOG_PERIOD 2000 //Logging period in milliseconds
//#define MINUTE_PERIOD 5000

// IFTTT
#define EVENT_NAME "RadioactivityCPM" // Name of your event name, set when you are creating the applet

// ThingSpeak Settings
const int channelID = THINGSPEAK_CHANNEL_ID;
const char* thingspeak_server = "api.thingspeak.com";

WiFiClient client;
WiFiClientSecure secure_client;
IFTTTMaker ifttt(IFTTT_KEY, secure_client);

volatile unsigned long counts = 0;                       // Tube events
unsigned long cpm = 0;                                   // CPM
unsigned long previousMillis;                            // Time measurement

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

void setup() {
  Serial.begin(115200);
  
  displayInit();
  //displayString("Welcome", 64, 15);
  Serial.println("Connecting to Wi-Fi");

  WiFi.begin(mySSID, myPASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.println("Wi-Fi Connected @: " + WiFi.localIP());
  
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

    postThingspeak(cpm);
    if (cpm > 100 ) IFTTT( EVENT_NAME, cpm);
  }
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


