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

#define USE_SERIAL 

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
const unsigned long SECTION_TIME = 3L * MINUTE;
//const unsigned long SECTION_TIME = 10L * SECOND;
const unsigned long CYCLE_TIME = NUM_SPRINKLERS * SECTION_TIME;

const int TRIGGER_HOUR = 17;
const int TRIGGER_MIN = 00;
const int TRIGGER_MAX = 15;

const int TIMEZONE = -8;

const unsigned long DEEP_SLEEP_MS = 10L * MINUTE;

//ESPHelper vars
const char* HOSTNAME = "esp_water";
const char* CB_TOPIC_DEBUG = "hata/watering/debug";
const char* CB_TOPIC_RAIN_DELAY = "hata/watering/raindelay";
const char* CB_TOPIC_MANUAL_RUN = "hata/watering/manualrun";
const char* STATUS_TOPIC = "hata/watering/status";

netInfo homeNet = {	.mqttHost = RPI_MQTT_IP,			//can be blank if not using MQTT
					.mqttUser = "", 	//can be blank
					.mqttPass = "", 	//can be blank
					.mqttPort = 1883,					//default port for MQTT is 1883 - only chance if needed.
					.ssid = mySSID, 
					.pass = myPASSWORD};
ESPHelper myESP(&homeNet);

//NTP setup vars
static const char ntpServerName[] = "us.pool.ntp.org";
const int timeZone = TIMEZONE;
WiFiUDP Udp;
unsigned int localPort = 8888;  // local port to listen for UDP packets
time_t getNtpTime();
void sendNTPpacket(IPAddress &address);

//runCycle & hasStarted are variables that keep track of whether or not a cycle should be running.
//runCycle tells the system that a cycle is running and hasStarted tells the system whether or not it has triggered
//the start to a cycle. Basically runCycle is the overall tracker and hasStarted just keeps track to make sure that we dont
//continually "start" a cycle
bool runCycle = false;
bool hasStarted = false;

//mode determines which section is being watered/which valve is active.
int mode = 0;

//allows user to send a command and the system will skip the next watering cycle
bool skipNext = false;

bool manualRun = false;
//testRunning is flagged when the system is running a test
bool testRunning = false;

//bEnableDeepSleep is cleared when deep sleep is disabled 
bool bEnableDeepSleep = false;

//bisFirstRun is cleared when loop runs in initDone section
bool bisFirstRun = true;


//whether the NTP has been initialized
bool initDone = false;  

//timers for various watering functions
Metro cycleTimer = Metro(CYCLE_TIME);
Metro sectionTimer = Metro(SECTION_TIME);
Metro testTimer = Metro(CYCLE_TIME);
Metro loopTimer = Metro(10 * SECOND);

long uptime_ms;

void setup() {
	//setup the relay pins
	pinMode(relay1, OUTPUT);
	pinMode(relay2, OUTPUT);
	delay(100);
	setValve(0);	//init to valves off
    
  //Connect D0 to RST to wake up from deep sleep
  pinMode(D0, WAKEUP_PULLUP);
  
  //start DHT temp/humidity sensor
  dht.begin();
  
#ifdef USE_SERIAL
  Serial.begin(115200);
#endif   
 
	//setup ESPHelper
	myESP.OTA_enable();
	myESP.OTA_setPassword(OTA_PASS);
	myESP.OTA_setHostname(HOSTNAME);
	//myESP.enableHeartbeat(BUILTIN_LED);
	myESP.setHopping(false);
	myESP.addSubscription(CB_TOPIC_RAIN_DELAY);
  myESP.addSubscription(CB_TOPIC_DEBUG);
  myESP.addSubscription(CB_TOPIC_MANUAL_RUN);
	myESP.setCallback(callback);
	myESP.begin();		//start ESPHelper

  uptime_ms = millis();  //cache uptime
  
	//setup NTP
	Udp.begin(localPort);
	setSyncProvider(getNtpTime);
	setSyncInterval(300);
}


void loop()
{
  float vbatt = 0;
  String postStr;
  char postBuff[CBUF_SZ];
  long curtime_ms = millis();
  float humidity, ctemp, ftemp;
         
	if(myESP.loop() >= WIFI_ONLY){

        //TODO: dont fail init if no time -cache old time before deep sleep
        
		//once we have a WiFi connection trigger some extra setup
		if(!initDone){
			delay(100);
		  	//setup the NTP connection and get the current time
			setupNTP();
			delay(200);

		  	//only set initDone to true if the time is set
			if(timeStatus() != timeNotSet){
				initDone = true;
        //clear loop timer to measure uptime
        loopTimer.reset();
				postTimeStamp("Watering System Started");
			}
		}

		if(initDone){
			//get the current time
			time_t t = now();

			//button2 triggers a valve test
      if (manualRun && !testRunning)
      {
        postTimeStamp("Watering manual run started");
				testRunning = true;
				testTimer.reset();
      }

      //log once for deep sleep or every 60 seconds for normal mode
      if (bisFirstRun || (curtime_ms >= (uptime_ms + MINUTE)))
      {
        bisFirstRun = false;
        //get temp info
        if (!getHumidityTempC(&humidity, &ctemp) )
        {
          postTimeStamp("Failed to read from DHT sensor!");
        }
        else
        {
          ftemp = dht.convertCtoF(ctemp);
          postStr = "Humidity (%): ";
          postStr += humidity;
          postStr += " Temp (C): ";
          postStr += ctemp;
          postStr += " Temp (F): ";
          postStr += ftemp;
          postStr.toCharArray(postBuff, CBUF_SZ);
          postTimeStamp(postBuff);
        }
        
        //get battery voltage
        vbatt = getVbatt();
        postStr = "Battery voltage (V): ";
        postStr += vbatt;
        postStr.toCharArray(postBuff, CBUF_SZ);
        postTimeStamp(postBuff);
        
        uptime_ms = curtime_ms;
      }
      
      //TODO: fix
			//trigger the start of a cycle if needed based on time
			//if(hour(t) == TRIGGER_HOUR && ((minute(t) >= TRIGGER_MIN) && (minute(t) < TRIGGER_MAX) /*(minute(t) < (TRIGGER_MIN + (CYCLE_TIME/MINUTE)))) */&& !runCycle && !skipNext){
      if(hour(t) == TRIGGER_HOUR && ((minute(t) >= TRIGGER_MIN) && (minute(t) < TRIGGER_MAX)) && !runCycle && !skipNext){
				postTimeStamp("Time initiated cycle");
				runCycle = true;
				cycleTimer.reset();
				sectionTimer.reset();
			}
			else if(hour(t) == TRIGGER_HOUR && minute(t) == TRIGGER_MIN + 1 && skipNext){
				postTimeStamp("Cycle Skipped");
				skipNext = false;
			}

			//cycle timer resets/ends a cycle after a set period of time
			if(cycleTimer.check())
      {
        runCycle = false;
      }

			if(testRunning){runTest();}
			else{cycleHandler();}
		}
	}

  //yield for callbacks/etc
  yield();
  
  //if debug flag is not set and no active watering, goto deep sleep
  if (bEnableDeepSleep && !runCycle && loopTimer.check())
  {
    postTimeStamp("Deep sleep right meow...");
    ESP.deepSleep(DEEP_SLEEP_MS * 1000);
  }
  else
  {
    delay (500);
  }
}



//MQTT callback
void callback(char* topic, byte* payload, unsigned int length) {

    //Convert topic to String to make it easier to work with
    //Also fix the payload by null terminating it and also convert that
    String topicStr = topic; 
    char newPayload[CBUF_SZ];
    memcpy(newPayload, payload, length);
    newPayload[length] = '\0';
    String printStr;
    char printBuff[CBUF_SZ];
    
    if(topicStr.equals(CB_TOPIC_RAIN_DELAY)){
    	if(newPayload[0] == '1'){
    		skipNext = true;
    		postTimeStamp("BrokerConfig: Skipping next watering cycle");
    	}
    	else if(newPayload[0] == '0'){
    		skipNext = false;
    		postTimeStamp("BrokerConfig: Not skipping next watering cycle");
    	}
    }
    else if(topicStr.equals(CB_TOPIC_DEBUG)){
    	if(newPayload[0] == '1'){
    		//set debug flag
        bEnableDeepSleep = false;
    		postTimeStamp("BrokerConfig: Debug on: staying on till flag is turned off");
    	}
    	else if(newPayload[0] == '0'){
        bEnableDeepSleep = true;
        printStr = "BrokerConfig: Debug off: will sleep for : ";
        printStr += (DEEP_SLEEP_MS/SECOND);
        printStr += " seconds";
        printStr.toCharArray(printBuff, CBUF_SZ);
    		postTimeStamp(printBuff);
    	}
    }
     else if(topicStr.equals(CB_TOPIC_MANUAL_RUN)){
    	if(newPayload[0] == '1'){
    		//set manual run flag
        manualRun = true;
        bEnableDeepSleep = false;
    		postTimeStamp("BrokerConfig: Manual run enabled");
    	}
    	else if(newPayload[0] == '0'){
        manualRun = false;
    		postTimeStamp("BrokerConfig: Manual run disabled");
    	}
    }
}


void cycleHandler(){

	//if we want to run but have not started yet
	if(runCycle && !hasStarted){
		hasStarted = true;
		mode = 1;
		sectionTimer.reset();
		postTimeStamp("Started Cycle");\
		return;
	}

	//if we dont want to run but we are currently running 
	else if(!runCycle && hasStarted){
		hasStarted = false;
		setValve(0);
		postTimeStamp("Ended Cycle");
		return;
	}



	//manages valves while a watering cycle is running
	if(runCycle){

		if(sectionTimer.check()){
			mode++;
			postTimeStamp("Mode Change");
		}

		if(mode <= (int)NUM_SPRINKLERS){
			setValve(mode);
		}
		else{
			hasStarted = false;
			setValve(0);
			postTimeStamp("Ended Cycle via mode change");
			mode = 0;
			runCycle = false;
		}
	}
}


void runTest (){
	static int testState = 1;

	if(testTimer.check()){
		testState++;
	}

	if(testState <= NUM_SPRINKLERS)
  {
    setValve(testState);
  }
	else
  {
		postTimeStamp("Watering Test Ended");   
    String str = "0";
    char message[128];
    str.toCharArray(message, 128);   
    //publish retained message to signal manual run finish
    myESP.publish(CB_TOPIC_MANUAL_RUN, message, true);    
    manualRun = false;
		testRunning = false;
    bEnableDeepSleep = true;
		setValve(0);
		testState = 3;
	}
}


void setValve(int valveNum){
	static int lastSet = -1;
	if(valveNum == 1 && lastSet != valveNum){
		postTimeStamp("Valve 1 Open");
		digitalWrite(relay1, HIGH);
		delay(100);
		digitalWrite(relay2, LOW);
		lastSet = valveNum;
	}
	else if(valveNum == 2 && lastSet != valveNum){
		postTimeStamp("Valve 2 Open");
		digitalWrite(relay1, LOW);
		delay(100);
		digitalWrite(relay2, HIGH);
		lastSet = valveNum;
	}
	else if(lastSet != valveNum){
		postTimeStamp("All Valves Closed");
		digitalWrite(relay1, LOW);
		digitalWrite(relay2, LOW);
		lastSet = valveNum;
	}
}


float getVbatt()
{  
  int sensorValue = analogRead(A0);
  // Convert the analog reading (which goes from 0 - 1023) to a voltage (0 - 3.2V):
  float voltage = sensorValue * (4.2 / 1023.0);
  
  return voltage;
}


bool getHumidityTempC(float *humidity, float * ctemp)
{
  int cnt = 0;
  
  if (!humidity || !ctemp)
  {
    return false;
  }
  
  //try a few times till get decent reading
  do
  {
    *humidity = dht.readHumidity();
    *ctemp = dht.readTemperature();
     cnt++;
     ///terminate loop and return failure after 10 attempts
     if (cnt > 10)
     {
       return false;
     }
     delay (250);   //250ms DHT11 sensor latency
  }
  while (isnan(*humidity) && isnan(*ctemp));

  return true;
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
  myESP.publish(STATUS_TOPIC, message, true);
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



/*-------- NTP code ----------*/
void setupNTP(){
	delay(200);
	Udp.begin(localPort);
	setSyncProvider(getNtpTime);
	setSyncInterval(300);
}

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime(){
	IPAddress ntpServerIP; // NTP server's ip address

	while (Udp.parsePacket() > 0) ; // discard any previously received packets
	// get a random server from the pool
	WiFi.hostByName(ntpServerName, ntpServerIP);
	sendNTPpacket(ntpServerIP);
	uint32_t beginWait = millis();
	while (millis() - beginWait < 1500) {
		int size = Udp.parsePacket();
		if (size >= NTP_PACKET_SIZE) {
			Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
			unsigned long secsSince1900;
			// convert four bytes starting at location 40 to a long integer
			secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
			secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
			secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
			secsSince1900 |= (unsigned long)packetBuffer[43];
			return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
		}
	}
	return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
	// set all bytes in the buffer to 0
	memset(packetBuffer, 0, NTP_PACKET_SIZE);
	// Initialize values needed to form NTP request
	// (see URL above for details on the packets)
	packetBuffer[0] = 0b11100011;   // LI, Version, Mode
	packetBuffer[1] = 0;     // Stratum, or type of clock
	packetBuffer[2] = 6;     // Polling Interval
	packetBuffer[3] = 0xEC;  // Peer Clock Precision
	// 8 bytes of zero for Root Delay & Root Dispersion
	packetBuffer[12] = 49;
	packetBuffer[13] = 0x4E;
	packetBuffer[14] = 49;
	packetBuffer[15] = 52;
	// all NTP fields have been given values, now
	// you can send a packet requesting a timestamp:
	Udp.beginPacket(address, 123); //NTP requests are to port 123
	Udp.write(packetBuffer, NTP_PACKET_SIZE);
	Udp.endPacket();
}


