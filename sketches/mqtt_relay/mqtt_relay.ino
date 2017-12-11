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

netInfo homeNet = {  .mqttHost = RPI_MQTT_IP,      //can be blank if not using MQTT
          .mqttUser = "",   //can be blank
          .mqttPass = "",   //can be blank
          .mqttPort = 1883,         //default port for MQTT is 1883 - only chance if needed.
          .ssid = mySSID, 
          .pass = myPASSWORD};
ESPHelper myESP(&homeNet);

const char* HOSTNAME = "esp_relay-01";

#define RELAY_TOPIC_BASE "hata/lights/outlet1/"

#define OUT_TOPIC  RELAY_TOPIC_BASE "output/"
#define CTRL_TOPIC RELAY_TOPIC_BASE "config/"

//output topic nodes
#define PUB_DEBUG  OUT_TOPIC "debug"
#define PUB_STATE  OUT_TOPIC "state"

//config topic nodes
#define CTRL_TURN_ON CTRL_TOPIC "turn_on"


const int relay_pin = D1;
bool initDone = false;
int ctrl_turn_on = -1;    //mqtt on/off signal, -1 if not init


void setup() {
	
	//setup the relay pin & drive lo
	pinMode(relay_pin, OUTPUT);
	delay(100);
  digitalWrite(relay_pin, LOW);

	myESP.OTA_enable();
	myESP.OTA_setPassword(OTA_PASS);
	myESP.OTA_setHostnameWithVersion(HOSTNAME);
  
  //subscribe to all subnodes
	myESP.addSubscription(CTRL_TURN_ON);
	myESP.setMQTTCallback(callback);
	myESP.begin();	
}

void loop()
{
  static bool isOn = false;

	//run the loop() method as often as possible - this keeps the network services running
  if(myESP.loop() >= WIFI_ONLY)
  {
    if(!initDone)
    {
      initDone = true;
      publish(PUB_STATE, "Online");
    }

    if ((ctrl_turn_on == 1) && !isOn)
    {
      digitalWrite(relay_pin, HIGH);
      isOn = true;
      publish(PUB_STATE, "Relay ON");
    }
    else if ((ctrl_turn_on == 0) && isOn)
    {
      digitalWrite(relay_pin, LOW);
      isOn = false;
      publish(PUB_STATE, "Relay OFF");
    }
  }
  
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

    if(topicStr.equals(CTRL_TURN_ON))
    {
    	if(newPayload[0] == '1')
      {
    		ctrl_turn_on = 1;
    		publish(PUB_DEBUG, "Config: TURN ON");
    	}
    	else if(newPayload[0] == '0')
      {
        ctrl_turn_on = 0;
    		publish(PUB_DEBUG, "Config: TURN OFF");
    	}
    }
}


//publish to topic
void publish(const char* topic, const char* text){

  myESP.publish(topic, text, true);
}
