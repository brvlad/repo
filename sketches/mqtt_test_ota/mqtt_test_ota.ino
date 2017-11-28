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

const char* HOSTNAME = "esp_test";
#define TEST_TOPIC "hata/temp/mqtest"
#define TEST_CTRL_TOPIC "hata/temp/mqtest_ctrl"

bool initDone = false;  

void setup() {
	
	Serial.begin(115200);	//start the serial line
	delay(500);

	Serial.println("Starting Up, Please Wait...");

	myESP.OTA_enable();
	myESP.OTA_setPassword(OTA_PASS);
	myESP.OTA_setHostnameWithVersion(HOSTNAME);
	myESP.addSubscription(TEST_CTRL_TOPIC);
	myESP.setMQTTCallback(callback);
  myESP.enableHeartbeat(BUILTIN_LED);
	myESP.begin();
	
}

void loop(){
	//run the loop() method as often as possible - this keeps the network services running
  if(myESP.loop() >= WIFI_ONLY)
  {
    if(!initDone)
    {
      initDone = true;
      postTimeStamp(" Up n running.");
    }
  }
	//Put application code here
  delay (1000);
	yield();
  delay (1000);
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
    
    if(topicStr.equals(TEST_CTRL_TOPIC))
    {
      if(newPayload[0] == '1')
      {
        printStr = "Got 1";
        printStr.toCharArray(message, CBUF_SZ);   
        myESP.publish(TEST_TOPIC, message, true);  
        Serial.println(printStr);    
      }
      else if(newPayload[0] == '0')
      {
        printStr = "Got 0";
        printStr.toCharArray(message, CBUF_SZ);   
        myESP.publish(TEST_TOPIC, message, true);
        Serial.println(printStr);
      }
    }
}





//take a char* and use it as a message with an appended timestamp
void postTimeStamp(const char* text){
  char timeStamp[30];
  createTimeString(timeStamp, 30);


  String pubString = String(HOSTNAME);
  pubString += " : ";
  pubString += text;
  pubString += " - ";
  pubString += timeStamp;

  //conver the String into a char*
  char message[128];
  pubString.toCharArray(message, 128);
  myESP.publish(TEST_TOPIC, message, true);
#ifdef USE_SERIAL
  Serial.println(pubString);
#endif    
}

//create a timestamp string
void createTimeString(char* buf, int length){
    time_t t = now();
    String timeString = String(hour(t));
    timeString += ":";
    timeString += minute(t);
    timeString += ":";
    timeString += second(t);
    timeString += " ";
    timeString += month(t);
    timeString += "/";
    timeString += day(t);
    timeString += "/";
    timeString += year(t);
    timeString.toCharArray(buf, length);
}
