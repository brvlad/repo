/*    
OTA.ino
Copyright (c) 2017 ItKindaWorks All right reserved.
github.com/ItKindaWorks

This file is part of ESPHelper

ESPHelper is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

ESPHelper is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with ESPHelper.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "ESPHelper.h"
#include <credentials.h>
#include <TimeLib.h>

#define CBUF_SZ 256
#define USE_SERIAL

netInfo homeNet = {  .mqttHost = RPI_MQTT_IP,      //can be blank if not using MQTT
          .mqttUser = "",   //can be blank
          .mqttPass = "",   //can be blank
          .mqttPort = 1883,         //default port for MQTT is 1883 - only chance if needed.
          .ssid = mySSID, 
          .pass = myPASSWORD};
ESPHelper myESP(&homeNet);

//const char* HOSTNAME = "esp_test_d1";
const char* HOSTNAME = "esp_test-01";

#define TEST_TOPIC_BASE "hata/temp/test/"
#define OUT_TOPIC  TEST_TOPIC_BASE "output/"
#define CTRL_TOPIC TEST_TOPIC_BASE "config/"

//output topic nodes
#define PUB_ACK   OUT_TOPIC "ack"
#define PUB_STATE OUT_TOPIC "state"
#define PUB_TEMP  OUT_TOPIC "temp"
#define PUB_ID    OUT_TOPIC "ID"


#define CTL_VAL1 "val1"
#define CTL_VAL2 "val2"
#define CTL_VAL3 "val3"

#define DEEP_SLEEP_EN
#define DEEP_SLEEP_MS 1000

bool initDone = false;  

bool configDone = false;
int ctrl_val1 = -1;
int ctrl_val2 = -1;

void setup() {
	
	Serial.begin(115200);	//start the serial line
	delay(500);

	Serial.println("Starting Up, Please Wait...");

	myESP.OTA_enable();
	myESP.OTA_setPassword(OTA_PASS);
	myESP.OTA_setHostname(HOSTNAME);
  myESP.setHopping(false);
  
  //subscribe to all subnodes
	myESP.addSubscription(OUT_TOPIC "#");
  myESP.addSubscription(CTRL_TOPIC "#");
	myESP.setMQTTCallback(callback);
  //TODO: remove if pin used
  //myESP.enableHeartbeat(BUILTIN_LED);
	myESP.begin();
	
}

void loop(){

	//run the loop() method as often as possible - this keeps the network services running
  if(myESP.loop() >= WIFI_ONLY)
  {
    if(!initDone)
    {
      initDone = true;
      publish(PUB_STATE, " Up n running.");    
      publishStr(PUB_ID, getEspID());
    }
  }
  if (!configDone)
  {
    Serial.println("not set...");
  }
	//Put application code here
  delay (500);
  if ( !configDone && (ctrl_val1 >= 0) && (ctrl_val2 >= 0))
  {
    configDone = true;
    Serial.println(" #set on 1...");
  }
	yield();
  if ( !configDone && (ctrl_val1 >= 0) && (ctrl_val2 >= 0))
  {
    configDone = true;
    Serial.println("### set on 2...");
  }
  //delay (1000);

#ifdef DEEP_SLEEP_EN    
  if (configDone)
  {
    delay (5000);
    publish(PUB_STATE, "...deep sleep...");
    ESP.deepSleep(DEEP_SLEEP_MS * 1000);
  }
#endif

}

void callback(char* topic, uint8_t* payload, unsigned int length) 
{
    //Convert topic to String to make it easier to work with
    //Also fix the payload by null terminating it and also convert that
    String topicStr = topic; 
    char newPayload[CBUF_SZ];
    memcpy(newPayload, payload, length);
    newPayload[length] = '\0';
    String printStr;
    char message[CBUF_SZ];

    if (topicStr.startsWith(CTRL_TOPIC))
    { 
      if(topicStr.endsWith(CTL_VAL1))
      {
        if(newPayload[0] == '1')
        {
          ctrl_val1 = 1;
          printStr = "Got 1 from " CTL_VAL1;
          printStr.toCharArray(message, CBUF_SZ);   
          myESP.publish(PUB_ACK, message, true);  
          Serial.println(printStr);    
        }
        else if(newPayload[0] == '0')
        {
          ctrl_val1 = 0;
          printStr = "Got 0 from " CTL_VAL1;
          printStr.toCharArray(message, CBUF_SZ);   
          myESP.publish(PUB_ACK, message, true);
          Serial.println(printStr);
        }
      }
      else if(topicStr.endsWith(CTL_VAL2))
      {
        if(newPayload[0] == '1')
        {
          ctrl_val2 = 1;
          printStr = "Got 1 from " CTL_VAL2;
          printStr.toCharArray(message, CBUF_SZ);   
          myESP.publish(PUB_ACK, message, true);  
          Serial.println(printStr);    
        }
        else if(newPayload[0] == '0')
        {
          ctrl_val2 = 0;
          printStr = "Got 0 from " CTL_VAL2;
          printStr.toCharArray(message, CBUF_SZ);   
          myESP.publish(PUB_ACK, message, true);
          Serial.println(printStr);
        }
      }
      else if(topicStr.endsWith(CTL_VAL3))
      {
          myESP.publish(PUB_ACK, newPayload, false);  
          Serial.println(newPayload);    
      }
    }
}

//append extra info
void publish(const char* topic, const char* text){
  //TODO: fix char/string conversions
  String pubString = String(HOSTNAME);
  pubString += " : ";
  pubString += text;

  //conver the String into a char*
  char message[128];
  pubString.toCharArray(message, 128);
  myESP.publish(topic, message, true);
#ifdef USE_SERIAL
  Serial.println(pubString);
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

String getEspID()
{
  String printStr;
  
  printStr = HOSTNAME;
  printStr += ": ";
  printStr += myESP.getIP();
  
  return printStr;
}
