/*
Simple Deep Sleep with Timer Wake Up
=====================================
ESP32 offers a deep sleep mode for effective power
saving as power is an important factor for IoT
applications. In this mode CPUs, most of the RAM,
and all the digital peripherals which are clocked
from APB_CLK are powered off. The only parts of
the chip which can still be powered on are:
RTC controller, RTC peripherals ,and RTC memories
This code displays the most basic deep sleep with
a timer to wake it up and how to store data in
RTC memory to use it over reboots
This code is under Public Domain License.
Author:
Pranav Cherukupalli <cherukupallip@gmail.com>
*/
#define LED 2
#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
#define ENDIAN_CHANGE_U16(x) ((((x)&0xFF00) >> 8) + (((x)&0xFF) << 8))
#define MAX_NO_DEV 5
#define MQTT_MSG_BUF 128

//#define TIME_TO_SLEEP  15        /* Time ESP32 will go to sleep (in seconds) */
#include <stdio.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEEddystoneURL.h>
#include <BLEEddystoneTLM.h>
#include <BLEBeacon.h>
#include <WiFi.h>
#include <PubSubClient.h>


 
RTC_DATA_ATTR int TIME_TO_SLEEP = 15;
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR BLEScan* pBLEScan;
RTC_DATA_ATTR const char* ssid     = "AndroidAPE6C9";
RTC_DATA_ATTR const char* password = "123456789";
RTC_DATA_ATTR int rssi = 0;
RTC_DATA_ATTR char rssi_buf[5];
RTC_DATA_ATTR const char* mqtt_server = "192.168.43.195";
RTC_DATA_ATTR const char* pubTopic = "fromDEV3"; //wrt to the node red flow
RTC_DATA_ATTR const char* tsleep = "toDEV3_tsleep";  //wrt to the node red flow
RTC_DATA_ATTR int scanTime = 3; //BLE scan period In seconds
RTC_DATA_ATTR int uq_devct =0;
RTC_DATA_ATTR String detectedUUID[MAX_NO_DEV][7]; //{UUID;RSSI0;RSSI1;RSSI2;RSSI3;RANGE}
//={{{"00"},{"00"}},{{"00"},{"00"}},{{"00"},{"00"}},{{"00"},{"00"}},{{"00"},{"00"}}};
RTC_DATA_ATTR String knownUUID[6]={"80c350a9-f603-26b0-ae4d-67292f81dab9","0a8ff39a-1689-409b-8d4c-14444b06d438","6a408c2b-d200-03b3-c249-1cd0da2de6af","af8d5bfc-ce32-3cab-a24a-649478a08f11","43ade1c5-8f74-758a-0c47-2425c116befd","9c5aeb7f-620d-b293-ea4a-c8e1c306ba7b"};
RTC_DATA_ATTR bool match;
RTC_DATA_ATTR bool known;
RTC_DATA_ATTR bool inRange;
RTC_DATA_ATTR int SSlimit = -100;
RTC_DATA_ATTR bool match_dev = false;
RTC_DATA_ATTR bool IN_RANGE = false;                      // in range flag
RTC_DATA_ATTR int dev_count =0;
RTC_DATA_ATTR unsigned int cBufIdx[6]={0,0,0,0,0,0};
RTC_DATA_ATTR unsigned int calibrate = 0;

RTC_DATA_ATTR WiFiClient espClient;
RTC_DATA_ATTR PubSubClient MQTTclient(espClient);

//SETUP WIFI
void setup_wifi() {
  int status = WL_IDLE_STATUS;
  WiFi.mode(WIFI_STA);
  delay(10);
  // We start by connecting to a WiFi network
  status = WiFi.begin(ssid, password);
  while (status != WL_CONNECTED){
    Serial.printf("Attempting WiFi Connection to %s \n",ssid);
    status = WiFi.begin(ssid, password);
    delay(3000);
    }
  if (strcmp("0.0.0.0",WiFi.localIP().toString().c_str())==0){
    setup_wifi();
    }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

// MQTT CONNECTION ////////////////////////////////////////////////////////////////////////////////////////
void MQTTcnct() {
  int attempts =0;
  // Loop until we're reconnected
  while (!MQTTclient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "EN2560Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (MQTTclient.connect(clientId.c_str(),"dev1","group_6$")) {
      Serial.println("MQTT broker connected");
      // Once connected, publish an announcement...
      MQTTclient.publish(pubTopic, "hello world");
      // ... and resubscribe
      MQTTclient.subscribe(tsleep,1);
      MQTTclient.subscribe("calibration",1);
      MQTTclient.setCallback(MQTTcallback);
    } else {                                //this part handles if MQTT connection fails after a successful WiFi connection
      if(attempts >=5){
        setup_wifi();
        }
      if(attempts >10){
        ESP.restart();
        }
      Serial.print("failed, rc=");
      Serial.print(MQTTclient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      attempts++;
      delay(3000);
      
    }
  }
}
//////////////////////// MQTT CALLBACK /////////////////////////////////////////
void MQTTcallback(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic,"toDEV3_tsleep")==0){
    digitalWrite(LED,HIGH);
    char temp[length];
    for (int i =0; i<length; i++){
      temp[i] = payload[i];
      }
    TIME_TO_SLEEP = atoi(temp);
    // whatever you want for this topic
  }
 if (strcmp(topic,"calibration")==0){
    calibrate ^= 1;
      }


    // whatever you want for this topic
  }


///////////////////// BLE callback on beacon receive///////////////////////////////////////////////

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
          unsigned int BufIdx;
      //iBeacon
              if (advertisedDevice.haveManufacturerData() == true)
        {
          std::string strManufacturerData = advertisedDevice.getManufacturerData();

          uint8_t cManufacturerData[100];
          strManufacturerData.copy((char *)cManufacturerData, strManufacturerData.length(), 0);

          if (strManufacturerData.length() == 25 && cManufacturerData[0] == 0x4C && cManufacturerData[1] == 0x00)
          { dev_count++;
            Serial.println("Found an iBeacon!");
            BLEBeacon oBeacon = BLEBeacon();
            oBeacon.setData(strManufacturerData);
            Serial.printf("iBeacon Frame\n");
            Serial.printf("ID: %04X Major: %d Minor: %d UUID: %s Power: %d\n", oBeacon.getManufacturerId(), ENDIAN_CHANGE_U16(oBeacon.getMajor()), ENDIAN_CHANGE_U16(oBeacon.getMinor()), oBeacon.getProximityUUID().toString().c_str(), oBeacon.getSignalPower());
          

        for (int i = 0; i < 5; i++) { //check if this is a known device and set match true
        if (strcmp(oBeacon.getProximityUUID().toString().c_str() , knownUUID[i].c_str()) == 0) 
        {
          match = true; 
          BufIdx = cBufIdx[i]&0b00000011; //index repeats over 0-3; 4 samples RSSI0,1,2,3
          cBufIdx[i]++;
          Serial.println("Match");
          break;
        }
        else{
          match =false;
          }
        }
  int pass_index =0;
        if(match){
          match_dev = true;
         for (int j = 0; j < 5; j++){
            if (strcmp(oBeacon.getProximityUUID().toString().c_str(), detectedUUID[j][0].c_str()) == 0 )  
            {
              known = true;
              pass_index = j;
              Serial.println("known");
              
              break;
            }
            else{
            known = false;
            }
            }
        }
      if ((match)&&(!known)){ //its a device in the list we havent seen before
        //Serial.print("DEBUG ");
        char temp_buf[50];
        //String temp = oBeacon.getProximityUUID().toString().c_str();
        snprintf(temp_buf,50,"%s",oBeacon.getProximityUUID().toString().c_str());
        /*for(int i=0;i<37;i++){
          temp_buf[i] = temp[i];
          }*/
        //Serial.println(temp_buf);
        detectedUUID[uq_devct][0] = temp_buf;
        //detectedUUID[uq_devct][0] = oBeacon.getProximityUUID().toString().c_str(); //add to the UUID section of the detected array
        rssi = advertisedDevice.getRSSI();
        if(rssi> SSlimit){
          detectedUUID[uq_devct][2] = "IN";
          }
        else{
          detectedUUID[uq_devct][2] = "OUT";
          }
        snprintf(rssi_buf,sizeof(rssi_buf),"%d",rssi);
        detectedUUID[uq_devct][BufIdx+1]= rssi_buf;             ////add to the RSSI section of the detected array
        BufIdx++;
        Serial.println(detectedUUID[uq_devct][0]);
        Serial.println(detectedUUID[uq_devct][1]);
        if(uq_devct <4){
          uq_devct++;
          }
        else{
          uq_devct =0;
          }
        }

        
      if(match&known){ //device in the list marked as known, just have to update its RSSI
          rssi = advertisedDevice.getRSSI();
           if(rssi> SSlimit){
          detectedUUID[pass_index][5] = "IN";
          }
          else{
          detectedUUID[pass_index][2] = "OUT";
          }
          snprintf(rssi_buf,sizeof(rssi_buf),"%d",rssi);
          detectedUUID[pass_index][BufIdx+1] = rssi_buf; //RSSIs start at index 1
          BufIdx++;
        }
          }
          }
    }
};


/*
Method to print the reason by which ESP32
has been awaken from sleep
*/
void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case 1  : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case 2  : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case 3  : Serial.println("Wakeup caused by timer"); break;
    case 4  : Serial.println("Wakeup caused by touchpad"); break;
    case 5  : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.println("Wakeup was not caused by deep sleep"); break;
  }
}

void in_rangechk(void){
  for(int i =0;i<MAX_NO_DEV;i++){
      char tempArray[5];
      detectedUUID[i][5].toCharArray(tempArray,5);
      if(strcmp(tempArray,"IN")==0){
      IN_RANGE = true;
      break;
      }
      else{
        IN_RANGE = false;
        }
    }
  
  }
///////////////////////////////CALIBRATE////////////////////////////////////////////
void calb(){

  if(calibrate==1){
    pubTopic = "clbrte03";
    }
  else{
    pubTopic = "fromDEV3";
    }
  
  }
void setup(){
  Serial.begin(115200);
  pinMode(LED,OUTPUT);
  digitalWrite(LED,LOW);
  setCpuFrequencyMhz(80);
  delay(1000); //Take some time to open up the Serial Monitor
  Serial.println(getCpuFrequencyMhz());
  
  //Increment boot number and print it every reboot
  ++bootCount;
  Serial.println("Boot number: " + String(bootCount));

  //Print the wakeup reason for ESP32
  print_wakeup_reason();

  /*
  First we configure the wake up source
  We set our ESP32 to wake up every 5 seconds
  */
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  Serial.println("Setup ESP32 to sleep for every " + String(TIME_TO_SLEEP) +
  " Seconds");

  /*
  Next we decide what all peripherals to shut down/keep on
  By default, ESP32 will automatically power down the peripherals
  not needed by the wakeup source, but if you want to be a poweruser
  this is for you. Read in detail at the API docs
  http://esp-idf.readthedocs.io/en/latest/api-reference/system/deep_sleep.html
  Left the line commented as an example of how to configure peripherals.
  The line below turns off all RTC peripherals in deep sleep.
  */
  //esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
  //Serial.println("Configured all RTC Peripherals to be powered down in sleep");

  /*
  Now that we have setup a wake cause and if needed setup the
  peripherals state in deep sleep, we can now start going to
  deep sleep.
  In the case that no wake up sources were provided but deep
  sleep was started, it will sleep forever unless hardware
  reset occurs.
  */
  ////////////////////////////////////BLE SCAN INITIALIZATION/////////////////////////////////////////////
  BLEScan *pBLEScan;
  BLEDevice::init("DEV_2");             //Initialize BLE 
  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks()); //like in MQTT, if a beacon is found run the passed callback function
  pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);  // less or equal setInterval value

  ////////////////////////////////////////////////////////////////////////////////////////////////////////
  
  Serial.println("BLE Scanning...");
  BLEScanResults foundDevices = pBLEScan->start(3, false); //perform the actual scan, false is to clear already scanned devices
  Serial.print("Devices found: ");
  Serial.println(foundDevices.getCount());
  Serial.println("Scan done!");
  pBLEScan->clearResults();
  Serial.println(dev_count);
  if (dev_count==0){
    match_dev =false;
    Serial.println("No BLE devices in the area, I'm going to sleep");
    esp_deep_sleep_start();
    }
  in_rangechk();
  
//If dev_count is 0, the program will never pass this point.
  
    
if(IN_RANGE){
  setup_wifi();
  MQTTclient.setServer(mqtt_server, 1883);  
  MQTTcnct();
  
  }
  
while(IN_RANGE&(dev_count!=0)){
    dev_count=0;
    calb();
    for(int i=0;i<MAX_NO_DEV;i++){   //publishing MQTT MSGS for devices found.
      Serial.println("In da loop");
      char pubmsg[MQTT_MSG_BUF];
      char temp1[37];
      detectedUUID[i][0].toCharArray(temp1,37);
      Serial.println("");
      //Serial.println(detectedUUID[i][0]);
      int meanRSSI = (detectedUUID[i][1].toInt()+detectedUUID[i][2].toInt()+detectedUUID[i][3].toInt()+detectedUUID[i][4].toInt())/4;
      snprintf(pubmsg, MQTT_MSG_BUF,"{\"UUID\":\"%s\",\"SCANNER\":\"3\",\"RSSI\":\"%d\",\"RANGE\":\"%s\"}",temp1,meanRSSI,detectedUUID[i][5]);
      Serial.printf("pubmsg %s\n",pubmsg);
      MQTTclient.publish(pubTopic,pubmsg);
      uint32_t loopStart = millis();         //wait a moment for the publish to be success; 
      while (millis() - loopStart < 1) {   //MQTTclient.loop must be called regularly to maintain connection
          if (!MQTTclient.connected()) { 
            MQTTcnct(); 
          } 
          else MQTTclient.loop(); 
      } 
    }
   BLEScanResults foundDevices = pBLEScan->start(2, false);
   Serial.println(dev_count);
   in_rangechk();
   Serial.println(IN_RANGE);
}

 /* uint32_t loopStart = millis(); 
  while (millis() - loopStart < 15000) { 
    if (!MQTTclient.connected()) { 
      MQTTcnct(); } 
    else MQTTclient.loop(); 
    } 
  for(int i =0; i <5;i++){
    Serial.printf("RSSI %d is %s and is %s range \n",i,detectedUUID[i][1],detectedUUID[i][2]);
    }
    */
  Serial.println("Going to sleep now");
  for(int i =0; i<3;i++){
  char lastwill[MQTT_MSG_BUF];
  char temp2[37];
  knownUUID[i].toCharArray(temp2,37);
  snprintf(lastwill, MQTT_MSG_BUF,"{\"UUID\":\"%s\",\"SCANNER\":\"3\",\"RSSI\":\"-100\"}",temp2);
  Serial.printf("pubmsg %s\n",lastwill);
  MQTTclient.publish(pubTopic,lastwill);
  MQTTclient.loop();
  delay(10);
    }
  
  Serial.flush();
  esp_deep_sleep_start();
  Serial.println("This will never be printed");
}

void loop(){
  //This is not going to be called
}
