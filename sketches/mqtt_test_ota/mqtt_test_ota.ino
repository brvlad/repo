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

extern "C" {
#include "user_interface.h"
  uint16 readvdd33(void);
  bool wifi_set_sleep_type(sleep_type_t);
  sleep_type_t wifi_get_sleep_type(void);
}


#define CBUF_SZ 256
//#define USE_SERIAL
#define DEEP_SLEEP_EN
//#define LIGHT_SLEEP_EN
#define DEEP_SLEEP_MS 60000

netInfo homeNet = {  .mqttHost = RPI_MQTT_IP,      //can be blank if not using MQTT
          .mqttUser = "",   //can be blank
          .mqttPass = "",   //can be blank
          .mqttPort = 1883,         //default port for MQTT is 1883 - only chance if needed.
          .ssid = mySSID, 
          .pass = myPASSWORD};
ESPHelper myESP(&homeNet);

//const char* HOSTNAME = "esp_test_d1";
const char* HOSTNAME = "esp_test-02";

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

bool initDone = false;  

bool configDone = false;
int ctrl_val1 = -1;
int ctrl_val2 = -1;

void setup() {

#ifdef USE_SERIAL          	
	Serial.begin(115200);	//start the serial line
	delay(500);
	Serial.println("Starting Up, Please Wait...");
#endif          

	myESP.OTA_enable();
	myESP.OTA_setPassword(OTA_PASS);
	myESP.OTA_setHostname(HOSTNAME);
  
  //subscribe to all subnodes
  myESP.addSubscription(CTRL_TOPIC "#");
	myESP.setMQTTCallback(callback);
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
#ifdef USE_SERIAL          
    Serial.println("not set...");
#endif          
    
  }
  
	//Put application code here
  delay (100);
  if ( !configDone && (ctrl_val1 >= 0) && (ctrl_val2 >= 0))
  {
    configDone = true;
#ifdef USE_SERIAL          
    Serial.println("### set on 1...");
#endif          
  }
	yield();
  if ( !configDone && (ctrl_val1 >= 0) && (ctrl_val2 >= 0))
  {
    configDone = true;
#ifdef USE_SERIAL          
    Serial.println("### set on 2...");
#endif          

    }
  //delay (1000);

  if (configDone)
  {
#ifdef DEEP_SLEEP_EN
    if (ctrl_val1 > 0)
    {
      publish(PUB_STATE, "...deep sleep...");
      ESP.deepSleep(DEEP_SLEEP_MS * 1000);
    }
    else
    {
      delay(500);
    }
#elif defined(LIGHT_SLEEP_EN)
    publish(PUB_STATE, "...light sleep...");
    goToLightSleep();
#endif
  }
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
#ifdef USE_SERIAL          
          Serial.println(printStr);    
#endif     
        }
        else if(newPayload[0] == '0')
        {
          ctrl_val1 = 0;
          printStr = "Got 0 from " CTL_VAL1;
          printStr.toCharArray(message, CBUF_SZ);   
          myESP.publish(PUB_ACK, message, true);
#ifdef USE_SERIAL          
          Serial.println(printStr);    
#endif          
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
#ifdef USE_SERIAL          
          Serial.println(printStr);    
#endif          
        }
        else if(newPayload[0] == '0')
        {
          ctrl_val2 = 0;
          printStr = "Got 0 from " CTL_VAL2;
          printStr.toCharArray(message, CBUF_SZ);   
          myESP.publish(PUB_ACK, message, true);
#ifdef USE_SERIAL          
          Serial.println(printStr);
#endif          
        }
      }
      else if(topicStr.endsWith(CTL_VAL3))
      {
          myESP.publish(PUB_ACK, newPayload, false);  
#ifdef USE_SERIAL
          Serial.println(newPayload);    
#endif
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


void goToLightSleep()
{
#ifdef USE_SERIAL    
    Serial.println("switching off Wifi Modem and CPU");
#endif
    wifi_set_sleep_type(LIGHT_SLEEP_T);
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
    // how do we call this: 
    //gpio_pin_wakeup_enable(GPIO_ID_PIN(switchPin),GPIO_PIN_INTR_NEGEDGE);
    wifi_fpm_open();
    wifi_fpm_do_sleep(10*1000);
    Serial.println("out of light sleep");

    //if(WiFi.forceSleepBegin(26843455)) sleep = true;
}