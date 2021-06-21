/*
   Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleScan.cpp
   Ported to Arduino ESP32 by Evandro Copercini
*/

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <WiFi.h>
#include <PubSubClient.h>


const char* ssid     = "malmi_villa";
const char* password = "86467223E";
const char* mqtt_server = "test.mosquitto.org";
const char* pubTopic = "fromDEV1"; //wrt to the node red flow
const char* subTopic = "toDEV1";  //wrt to the node red flow


int rssi = 0;
char rssi_buf[5];
WiFiClient espClient;
PubSubClient MQTTclient(espClient);

//SETUP WIFI
void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to %s");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    //esp_restart();
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}


// MQTT CONNECTION ////////////////////////////////////////////////////////////////////////////////////////
void MQTTcnct() {
  // Loop until we're reconnected
  while (!MQTTclient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "EN2560Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (MQTTclient.connect(clientId.c_str())) {
      Serial.println("MQTT broker connected");
      // Once connected, publish an announcement...
      //client.publish("ransara69", "hello world");
      // ... and resubscribe
      MQTTclient.subscribe(subTopic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(MQTTclient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

int scanTime = 60; //In seconds
BLEScan* pBLEScan;

//BLE SCAN /////////////////////////////////////////////////////////////////////////////////////////////////
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      Serial.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str());
      rssi = advertisedDevice.getRSSI();
      Serial.println(rssi);
      snprintf(rssi_buf,sizeof(rssi_buf),"%d",rssi);
      MQTTclient.publish("ransara69", rssi_buf);
    }
};

void setup() {
  Serial.begin(115200);
  setCpuFrequencyMhz(80);
  Serial.println("CPU Frequency is ");
  Serial.println(getCpuFrequencyMhz());
  Serial.println("MHz");
  setup_wifi();
  MQTTclient.setServer(mqtt_server, 1883);
  //client.setCallback(callback);

  Serial.println("Scanning BLE beacons...");
  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(false); //active scan uses more power, but get results faster
  pBLEScan->setInterval(5000);
  pBLEScan->setWindow(99);  // less or equal setInterval value
}

void loop() {
  // put your main code here, to run repeatedly:
  //BEGIN MQTT CONNECTION 
    if (!MQTTclient.connected()) {
    MQTTcnct();
  }
  MQTTclient.loop();

  //
  BLEScanResults foundDevices = pBLEScan->start(scanTime, false);
  Serial.print("Devices found: ");
  Serial.println(foundDevices.getCount());
  Serial.println("Scan done!");
  pBLEScan->clearResults();   // delete results fromBLEScan buffer to release memory
  delay(2000);
}
