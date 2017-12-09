/*    
	watering.ino
	Copyright (c) 2016 ItKindaWorks All right reserved.
	github.com/ItKindaWorks

	watering.ino is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	watering.ino is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with watering.ino.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "ESPHelper.h"
#include "Metro.h"
#include <TimeLib.h>
#include <WiFiUdp.h>
#include <credentials.h>
#include "DHT.h"


//some bogus definitions
#ifdef ESP8266
#define D0 BUILTIN_LED
#define D1 BUILTIN_LED
#define D2 BUILTIN_LED
#define D4 BUILTIN_LED
#endif

// Uncomment whatever DHT type you're using!
#define DHTTYPE DHT11  // DHT 11
#define DHTPIN D4     // what pin we're connected to
 
DHT dht(DHTPIN, DHTTYPE);

#define CBUF_SZ 256  //char buffer size for posting MQTT

//setup macros for time
#define SECOND  1000L
#define MINUTE  SECOND * 60L
#define HOUR  MINUTE * 60L

//pin defs
const int relay1 = D2;
const int relay2 = D1;

//consts for the timers for a cycle and section
const unsigned long NUM_SPRINKLERS = 2;

#define DEBUG_VER

#ifdef DEBUG_VER
  #define WATERING_TOPIC_BASE "hata/temp/watering/"
  const char* HOSTNAME = "esp_water_test";
#else
  #define WATERING_TOPIC_BASE "hata/watering/"
  const char* HOSTNAME = "esp_water";  
#endif

#define OUT_TOPIC  WATERING_TOPIC_BASE "output/"
#define CTRL_TOPIC WATERING_TOPIC_BASE "config/"

//output topic nodes
#define PUB_DEBUG  OUT_TOPIC "debug"
#define PUB_STATE  OUT_TOPIC "state"
#define PUB_VBATT  OUT_TOPIC "vbatt"
#define PUB_CTEMP  OUT_TOPIC "ctemp"
#define PUB_FTEMP  OUT_TOPIC "ftemp"
#define PUB_HUMIDITY  OUT_TOPIC "humidity"

//config topic nodes
#define CTRL_DEEP_SLEEP_EN CTRL_TOPIC "deep_sleep_en"
#define CTRL_DEEP_SLEEP_SEC CTRL_TOPIC "deep_sleep_sec"
#define CTRL_ZONE1_ACTIVE CTRL_TOPIC "zone1_on"
#define CTRL_ZONE2_ACTIVE CTRL_TOPIC "zone2_on"
#define CTRL_ZONE1_DURATION CTRL_TOPIC "zone1_duration_min"
#define CTRL_ZONE2_DURATION CTRL_TOPIC "zone2_duration_min"
//#define CTRL_CUR_TIME   "time_val"
#define CTRL_NUM_MESSAGES   6       //how many ctrl settings need to be received

 
struct Sprinkler{
  bool isInitDone = false;
  //bEnableDeepSleep is cleared when deep sleep is disabled 
  bool bEnableDeepSleep = false;
  int deepSleep_sec = -1;
  //enable flag per valve
  bool bValve0En = 0;
  bool bValve1En = 0;
  //valve status
  bool bValve0On = 0;
  bool bValve1On = 0;
  //watering duration per valve
  int valve0EnMin = -1;
  int valve1EnMin = -1;
  //valves left to water
  int valvesLeft = 0;
  
  //valve watering duration timers. Default to 1 min for test
  Metro valve0Timer = Metro(MINUTE);
  Metro valve1Timer = Metro(MINUTE);

  //default constructor
  Sprinkler() {}
  
  bool isInitialized()
  {
    if ( (deepSleep_sec >=0) && (valve0EnMin >=0) && (valve1EnMin >=0))
    {
      return true;
    }
    return false;
  }
  
};
typedef struct Sprinkler Sprinkler;

Sprinkler sprinkler;

//isCycleRunning tells the system that a cycle is running 
bool isCycleRunning = false;
bool initDone  = false;
//bisFirstRun is cleared when loop runs in initDone section
bool bisFirstRun = true;
//keep track of control message count from broker
int ctrl_msg_cnt = 0;
//timers for various watering functions
Metro measureTimer = Metro(MINUTE);
Metro loopTimer = Metro(10 * SECOND);

netInfo homeNet = {	.mqttHost = RPI_MQTT_IP,			//can be blank if not using MQTT
					.mqttUser = "", 	//can be blank
					.mqttPass = "", 	//can be blank
					.mqttPort = 1883,					//default port for MQTT is 1883 - only chance if needed.
					.ssid = mySSID, 
					.pass = myPASSWORD};
ESPHelper myESP(&homeNet);



void setup() 
{
	//setup the relay pins
	pinMode(relay1, OUTPUT);
	pinMode(relay2, OUTPUT);
	delay(100);
	setValve(-1);	//init to valves off

#ifndef ESP8266  
  //Connect D0 to RST to wake up from deep sleep
  pinMode(D0, WAKEUP_PULLUP);
#endif
  
  //start DHT temp/humidity sensor
  dht.begin();
  
#ifdef DEBUG_VER
  Serial.begin(115200);
#endif   
 
	//setup ESPHelper
	myESP.OTA_enable();
	myESP.OTA_setPassword(OTA_PASS);
	myESP.OTA_setHostname(HOSTNAME);

  //control topics settings
  myESP.addSubscription(CTRL_DEEP_SLEEP_EN);
	myESP.addSubscription(CTRL_DEEP_SLEEP_SEC);
  myESP.addSubscription(CTRL_ZONE1_ACTIVE);
	myESP.addSubscription(CTRL_ZONE2_ACTIVE);
  myESP.addSubscription(CTRL_ZONE1_DURATION);
	myESP.addSubscription(CTRL_ZONE2_DURATION);

  //output topics
	/*myESP.addSubscription(PUB_DEBUG);
	myESP.addSubscription(PUB_STATE);
	myESP.addSubscription(PUB_VBATT);
	myESP.addSubscription(PUB_CTEMP);
	myESP.addSubscription(PUB_FTEMP);
	myESP.addSubscription(PUB_HUMIDITY);
*/

#ifdef ESP8266 
  myESP.enableHeartbeat(BUILTIN_LED);
#endif
	myESP.setCallback(callback);
	myESP.begin();		//start ESPHelper
}


void loop()
{
  String postStr;
  char postBuff[CBUF_SZ];
  float vbatt, humidity, ctemp, ftemp;

	if(myESP.loop() >= WIFI_ONLY)
  {
		//once we have a WiFi connection trigger some extra setup
		if(!initDone){
				initDone = true;
        //clear loop timers
        loopTimer.reset();
        measureTimer.reset();
				publishDebug("Watering System Started");
        publish(PUB_STATE, "Started");
		}
		else if(initDone)
    {  
      //log once for deep sleep or every 60 seconds for normal mode
      if (bisFirstRun || measureTimer.check())
      {        
        bisFirstRun = false;
        measureTimer.reset();
        //get temp info
#ifndef ESP8266        
        if (!getHumidityTempF(&humidity, &ftemp) )
#else          
        if (true)
#endif          
        {
          publishDebug("Failed to read from DHT sensor!");
        }
        else
        {
          ctemp = dht.convertFtoC(ftemp);
          postStr = "Humidity (%): ";
          postStr += humidity;
          postStr += " Temp (C): ";
          postStr += ctemp;
          postStr += " Temp (F): ";
          postStr += ftemp;
          postStr.toCharArray(postBuff, CBUF_SZ);
          publishDebug(postBuff);
          
          dtostrf(humidity, 10, 2, postBuff);
          publish(PUB_HUMIDITY, postBuff);
          dtostrf(ctemp, 10, 2, postBuff);
          publish(PUB_CTEMP, postBuff);
          dtostrf(ftemp, 10, 2, postBuff);
          publish(PUB_FTEMP, postBuff);
        }
        
        //get battery voltage
        vbatt = getVbatt();
        postStr = "Battery voltage (V): ";
        postStr += vbatt;
        postStr.toCharArray(postBuff, CBUF_SZ);
        publishDebug(postBuff);
        
        dtostrf(vbatt, 10, 3, postBuff);
        publish(PUB_VBATT, postBuff);
      }

      //if got all control topic settings, check what to do
      if (sprinkler.isInitDone)
      {
        //now check if can start watering
        if (sprinkler.bValve0En && !isCycleRunning)
        {
          publishDebug("Starting Zone 1");
          publish(PUB_STATE, "Zone 1 watering");
          
          sprinkler.bValve0En = false;    //clear En flag to not start cycle again
          sprinkler.bValve0On = true;
          isCycleRunning = true;
          //set duration and reset timer
          sprinkler.valve0Timer.interval(sprinkler.valve0EnMin * MINUTE);
          sprinkler.valve0Timer.reset();
          //open valve 0
          setValve(0);    
        }
        else if( sprinkler.bValve1En && !isCycleRunning)
        {
          publishDebug("Starting Zone 2");
          publish(PUB_STATE, "Zone 2 watering");
          
          sprinkler.bValve1En = false;    //clear En flag to not start cycle again
          sprinkler.bValve1On = true;
          isCycleRunning = true;
          //set duration and reset timer
          sprinkler.valve1Timer.interval(sprinkler.valve1EnMin * MINUTE);
          sprinkler.valve1Timer.reset();
          //open valve 1
          setValve(1);    
        }

        //check if current isCycleRunning is over & close valve/reset vars
        if (isCycleRunning)
        {
          if (sprinkler.bValve0On && sprinkler.valve0Timer.check())
          {
            publishDebug("Stopping Zone 1");
            publish(PUB_STATE, "Zone 1 done");
            sprinkler.bValve0On = false;
            isCycleRunning = false;
            sprinkler.valvesLeft--;
            setValve(-1);
          }
          if (sprinkler.bValve1On && sprinkler.valve1Timer.check())
          {
            publishDebug("Stopping Zone 2");
            publish(PUB_STATE, "Zone 2 done");
            sprinkler.bValve1On = false;
            isCycleRunning = false;
            sprinkler.valvesLeft--;
            setValve(-1);
          }  
        }
        
      }

		}
	}

 
  //if deep sleep enabled and nothing left to water, and 10+ sec elapsed since start, goto deep sleep
  if (sprinkler.bEnableDeepSleep && (sprinkler.valvesLeft <= 0) && loopTimer.check())
  {
    publishDebug("Sleeping...");
    publish(PUB_STATE, "Sleeping...");
    ESP.deepSleep(sprinkler.deepSleep_sec * 1000 * 1000);  //in usec
  }
  else
  {
    //publishDebug("Waiting");
    delay (1000);
  }
}



//MQTT callback
void callback(char* topic, byte* payload, unsigned int length) {

    //Convert topic to String to make it easier to work with
    //Also fix the payload by null terminating it and also convert that
    String topicStr = topic; 
    char newPayload[CBUF_SZ];
    String printStr;
    char printBuff[CBUF_SZ];
    int num = 0;

    memcpy(newPayload, payload, length);
    newPayload[length] = '\0';
    
    
    publishDebug(topic);

            
    if(topicStr.equals(CTRL_DEEP_SLEEP_EN))
    {
      ctrl_msg_cnt++;
      
    	if(newPayload[0] == '0'){
    		//set debug flag
        sprinkler.bEnableDeepSleep = false;
    		publishDebug("Config: Deep sleep OFF");
    	}
    	else if(newPayload[0] == '1'){
        sprinkler.bEnableDeepSleep = true;
    		publishDebug("Config: Deep sleep ON");
    	}
    }
    else if(topicStr.equals(CTRL_DEEP_SLEEP_SEC))
    {
        ctrl_msg_cnt++;
        num = atoi(newPayload);
        if (isnan (num))
        {
          printStr = "Config:ERROR: Deep sleep duration is NaN: ";
          printStr += newPayload;
          printStr.toCharArray(printBuff, CBUF_SZ);
          publishDebug(printBuff);
        }
        else
        {
          sprinkler.deepSleep_sec = num;
          printStr = "Deep sleep duration (sec): ";
          printStr += num;
          printStr.toCharArray(printBuff, CBUF_SZ);
          publishDebug(printBuff);
        }
    }
    else if(topicStr.equals(CTRL_ZONE1_ACTIVE))
    {
      ctrl_msg_cnt++;
    	if(newPayload[0] == '1')
      {
        sprinkler.bValve0En = true;
        sprinkler.valvesLeft++;
    		publishDebug("Config: Zone 1 enabled");
    	}
    	else if(newPayload[0] == '0')
      {
        sprinkler.bValve0En = false;;
    		publishDebug("Config: Zone 1 disabled");
    	}
    }
    else if(topicStr.equals(CTRL_ZONE1_DURATION))
    {
        ctrl_msg_cnt++;
        num = atoi(newPayload);
        
        printStr = num;
        
        if (isnan (num) || (num > 60))
        {
          printStr = "Config:ERROR: Zone 1 duration is invalid or >60 min: ";
          printStr += newPayload;
          printStr.toCharArray(printBuff, CBUF_SZ);
          publishDebug(printBuff);

        }
        else
        {
          sprinkler.valve0EnMin = num;
          printStr = "Config: Zone 1 duration (min): ";
          printStr += num;
          printStr.toCharArray(printBuff, CBUF_SZ);
          publishDebug(printBuff);
        }    
    }
    else if(topicStr.equals(CTRL_ZONE2_ACTIVE))
    {
      ctrl_msg_cnt++;
    	if(newPayload[0] == '1'){
        sprinkler.bValve1En = true;
        sprinkler.valvesLeft++;
    		publishDebug("Config: Zone 2 enabled");
    	}
    	else if(newPayload[0] == '0'){
        sprinkler.bValve1En = false;;
    		publishDebug("Config: Zone 2 disabled");
    	}
    }
    else if(topicStr.equals(CTRL_ZONE2_DURATION))
    {
        ctrl_msg_cnt++;
        num = atoi(newPayload);
        
        printStr = num;
        
        if (isnan (num) || (num > 60))
        {
          printStr = "Config:ERROR: Zone 2 duration is invalid or >60 min: ";
          printStr += newPayload;
          printStr.toCharArray(printBuff, CBUF_SZ);
          publishDebug(printBuff);

        }
        else
        {
          sprinkler.valve1EnMin = num;
          printStr = "Config: Zone 2 duration (min): ";
          printStr += num;
          printStr.toCharArray(printBuff, CBUF_SZ);
          publishDebug(printBuff);
        }    
    }
    
    //if got all ctrl messages, set init done
    //TODO: crappy way, can count same twice
    if (ctrl_msg_cnt >= CTRL_NUM_MESSAGES)
    {
      publish(PUB_STATE, "Initialized");
      sprinkler.isInitDone = true;
    }
}



//Open/close a valve. -1 to close all valves. TODO: it's ugly
void setValve(int valveNum)
{  
  if (valveNum >= (int)NUM_SPRINKLERS)
  {
      publishDebug("Error: valve # invalid");
      return;
  }

  if (valveNum < 0 )
  {
   publishDebug("All Valves Closed");
		digitalWrite(relay1, LOW);
		digitalWrite(relay2, LOW);
  }
	else if(valveNum == 0 )
  {
		publishDebug("Valve 0 Open");
		digitalWrite(relay1, HIGH);
		delay(100);
		digitalWrite(relay2, LOW);
	}
	else if(valveNum == 1)
  {
		publishDebug("Valve 1 Open");
		digitalWrite(relay1, LOW);
		delay(100);
		digitalWrite(relay2, HIGH);
	}
}



float getVbatt()
{  
  int sensorValue = analogRead(A0);
  // Convert the analog reading (which goes from 0 - 1023) to a voltage (0 - 3.2V):
  float voltage = sensorValue * (4.2 / 1023.0);

  return voltage;
}



bool getHumidityTempF(float *humidity, float * ftemp)
{
  int cnt = 0;
  
  if (!humidity || !ftemp)
  {
    publishDebug("NULL ptr for getHumidityTempF");
    return false;
  }
  
  //TODO: why??
  //try a few times till get decent reading
  do
  {
    *humidity = dht.readHumidity();
    *ftemp = dht.readTemperature(true);
     cnt++;
     ///terminate loop and return failure after 10 attempts
     if (cnt > 10)
     {
       return false;
     }
     delay (250);   //250ms DHT11 sensor latency
  }
  while (isnan(*humidity) && isnan(*ftemp));

  return true;
}



//publish to topic & dump to serial
void publish(const char* topic, const char* text){

  myESP.publish(topic, text, true);
#ifdef USE_SERIAL
  Serial.println(text);
#endif    
}



//take a char* and use it as a message
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


