/* This code is writen by Eric Nientker - 1267027 for his FBP 08/06/2021. 
 * The goal of this code is to run "nutriband" a platform that can process Fluid intake data from a MyBeaker
 * 
 * The section of code responsible for uploading data to the google sheet is derived from @unreeeal
 * https://github.com/unreeeal/esp32-google-sheets
 *
 * The section of code responsible for managin BLE connectivity is derived from@nkolban
 * https://github.com/nkolban/ESP32_BLE_Arduino
 * 
 */

#include <Arduino.h>
#include "BLEDevice.h"
#include "BLEScan.h"
#include "SPIFFS.h"

#include <WiFi.h>
#include <HTTPClient.h>

// #define fullData 1
#define proto 1
#define LED_pin 15

bool ledOn = false;

#define on 30
#define off 0

uint fluidMax = 0;
uint fluidMin = 1000;

//----------------------- Wifi init ----------------------------------
const char *ssid = "ericfi";
const char *password = "12345678";

uint8_t csvFieldSize = 20;

String GOOGLE_SCRIPT_ID = "AKfycbzn7O-i6NBLSGU1Jqr41MdVDNAaoS1q5Vs7Dr_KQ185ZbyscU_MCSlp2gar47CgwoFz"; // Replace by your GAS service id
const int sendInterval = 996 * 5;

char *root_ca =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDujCCAqKgAwIBAgILBAAAAAABD4Ym5g0wDQYJKoZIhvcNAQEFBQAwTDEgMB4G\n"
    "A1UECxMXR2xvYmFsU2lnbiBSb290IENBIC0gUjIxEzARBgNVBAoTCkdsb2JhbFNp\n"
    "Z24xEzARBgNVBAMTCkdsb2JhbFNpZ24wHhcNMDYxMjE1MDgwMDAwWhcNMjExMjE1\n"
    "MDgwMDAwWjBMMSAwHgYDVQQLExdHbG9iYWxTaWduIFJvb3QgQ0EgLSBSMjETMBEG\n"
    "A1UEChMKR2xvYmFsU2lnbjETMBEGA1UEAxMKR2xvYmFsU2lnbjCCASIwDQYJKoZI\n"
    "hvcNAQEBBQADggEPADCCAQoCggEBAKbPJA6+Lm8omUVCxKs+IVSbC9N/hHD6ErPL\n"
    "v4dfxn+G07IwXNb9rfF73OX4YJYJkhD10FPe+3t+c4isUoh7SqbKSaZeqKeMWhG8\n"
    "eoLrvozps6yWJQeXSpkqBy+0Hne/ig+1AnwblrjFuTosvNYSuetZfeLQBoZfXklq\n"
    "tTleiDTsvHgMCJiEbKjNS7SgfQx5TfC4LcshytVsW33hoCmEofnTlEnLJGKRILzd\n"
    "C9XZzPnqJworc5HGnRusyMvo4KD0L5CLTfuwNhv2GXqF4G3yYROIXJ/gkwpRl4pa\n"
    "zq+r1feqCapgvdzZX99yqWATXgAByUr6P6TqBwMhAo6CygPCm48CAwEAAaOBnDCB\n"
    "mTAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUm+IH\n"
    "V2ccHsBqBt5ZtJot39wZhi4wNgYDVR0fBC8wLTAroCmgJ4YlaHR0cDovL2NybC5n\n"
    "bG9iYWxzaWduLm5ldC9yb290LXIyLmNybDAfBgNVHSMEGDAWgBSb4gdXZxwewGoG\n"
    "3lm0mi3f3BmGLjANBgkqhkiG9w0BAQUFAAOCAQEAmYFThxxol4aR7OBKuEQLq4Gs\n"
    "J0/WwbgcQ3izDJr86iw8bmEbTUsp9Z8FHSbBuOmDAGJFtqkIk7mpM0sYmsL4h4hO\n"
    "291xNBrBVNpGP+DTKqttVCL1OmLNIG+6KYnX3ZHu01yiPqFbQfXf5WRDLenVOavS\n"
    "ot+3i9DAgBkcRcAtjOj4LaR0VknFBbVPFd5uRHg5h6h+u/N5GJG79G+dwfCMNYxd\n"
    "AfvDbbnvRG15RjF+Cv6pgsH/76tuIMRQyV+dTZsXjAzlAcmgQWpzU/qlULRuJQ/7\n"
    "TBj0/VLZjmmx6BEP3ojY+x1J96relc8geMJgEtslQIxq/H5COEBkEveegeGTLg==\n"
    "-----END CERTIFICATE-----\n";

WiFiClientSecure client;

void sendData(String params)
{

  HTTPClient http;
  String url = "https://script.google.com/macros/s/" + GOOGLE_SCRIPT_ID + "/exec?" + params;

  Serial.println("starting stream");
  http.begin(url, root_ca);
  http.GET();
  Serial.print("Sending: ");
  http.end();
}

bool WiFi_connect(const char *ssidIn, const char *passIn)
{

  esp_bt_controller_disable();
  esp_bt_controller_deinit();

  uint8_t count = 0;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssidIn, passIn);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.println("Connecting to WiFi..");
    if (count > 15)
      return false;
    count++;
  }

  Serial.println("Connected to the WiFi network");
  return true;
}

//----------------------- BLE init ----------------------------------

static BLEUUID UART_serviceUUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
static BLEUUID charUUID_RX("6E400002-B5A3-F393-E0A9-E50E24DCCA9E"); // RX Characteristic
static BLEUUID charUUID_TX("6E400003-B5A3-F393-E0A9-E50E24DCCA9E"); // TX Characteristic

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = false;
// static BLERemoteCharacteristic *pRemoteCharacteristic;
static BLEAdvertisedDevice *myDevice;

static BLERemoteCharacteristic *pTXCharacteristic;
static BLERemoteCharacteristic *pRXCharacteristic;

const uint8_t notificationOn[] = {0x1, 0x0};

//----------------------- SPIFFS init ----------------------------------

File file_write;
File file_read;

//----------------------- functions ----------------------------------

class MyClientCallback : public BLEClientCallbacks
{
  void onConnect(BLEClient *pclient)
  {
  }

  void onDisconnect(BLEClient *pclient)
  {
    connected = false;
    Serial.println("onDisconnect");
  }
};

bool connectToServer()
{
  Serial.print("Forming a connection to ");
  Serial.println(myDevice->getAddress().toString().c_str());

  BLEClient *pClient = BLEDevice::createClient();
  Serial.println(" - Created client");

  pClient->setClientCallbacks(new MyClientCallback());

  // Connect to the remove BLE Server.
  pClient->connect(myDevice); // if you pass BLEAdvertisedDevice instead of address, it will be recognized type of peer device address (public or private)
  Serial.println(" - Connected to server");

  // Obtain a reference to the service we are after in the remote BLE server.
  BLERemoteService *pRemoteService = pClient->getService(UART_serviceUUID);
  if (pRemoteService == nullptr)
  {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(UART_serviceUUID.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our service");

  // Obtain a reference to the characteristic in the service of the remote BLE server.
  pTXCharacteristic = pRemoteService->getCharacteristic(charUUID_TX);
  if (pTXCharacteristic == nullptr)
  {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(charUUID_TX.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our characteristic");

  pRXCharacteristic = pRemoteService->getCharacteristic(charUUID_RX);
  if (pRXCharacteristic == nullptr)
  {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(charUUID_RX.toString().c_str());
    pClient->disconnect();
    return false;
  }
  Serial.println(" - Found our characteristic");

  pTXCharacteristic->getDescriptor(BLEUUID((uint16_t)0x2902))->writeValue((uint8_t *)notificationOn, 2, true);
  byte myByteArr[] = {0x67, 0x65, 0x74, 0x20, 0x73, 0x74, 0x61, 0x74, 0x75, 0x73, 0x0A};
  pRXCharacteristic->writeValue(myByteArr, 11, false);

  // Read the value of the characteristic.
  if (pTXCharacteristic->canNotify())
  {
    std::string value = pTXCharacteristic->readValue();
    Serial.print("The characteristic value was: ");
    Serial.println(value.c_str());
  }
  else
  {
    Serial.println("cant find value");
  }

  connected = true;
}

/**
 * Scan for BLE servers and find the first one that advertises the service we are looking for.
 */
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
  /**
   * Called for each advertising BLE server.
   */
  void onResult(BLEAdvertisedDevice advertisedDevice)
  {
    Serial.print("BLE Advertised Device found: ");
    Serial.println(advertisedDevice.toString().c_str());
    Serial.print("RSSI: ");
    Serial.println(advertisedDevice.getRSSI());

    // We have found a device, let us now see if it contains the service we are looking for.
    if (advertisedDevice.haveServiceUUID() && advertisedDevice.isAdvertisingService(UART_serviceUUID) && advertisedDevice.getRSSI() > -60)
    {

#ifdef proto
      ledcWrite(0, on);
      delay(500);
      ledcWrite(0, off);
      delay(50);
      ledcWrite(0, on);
      delay(500);
      ledcWrite(0, off);
      delay(50);
      ledcWrite(0, on);
      delay(500);
      ledcWrite(0, off);
#endif

      BLEDevice::getScan()->stop();
      myDevice = new BLEAdvertisedDevice(advertisedDevice);
      doConnect = true;
      doScan = true;

    } // Found our server
  }   // onResult
};    // MyAdvertisedDeviceCallbacks

void scan_connect()
{
  int run = 0;
  while (!doConnect)
  {
    Serial.println("restarting scan");
    run++;

#ifdef proto

    for (int i = 0; i < run; i++)
    {
      ledcWrite(0, on);
      delay(50);
      ledcWrite(0, off);
      delay(50);
    }

    if (run == 5)
    {
      ESP.restart();
    }

#endif

    BLEScan *pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->start(3);
  }
}

void SPIFFS_SETUP()
{

  // if(SPIFFS.format()){
  //   Serial.println("\n\nSuccess formatting");
  // }else{
  //   Serial.println("\n\nError formatting");
  // }

  if (SPIFFS.begin())
  {
    Serial.println(F("SPIFFS mounted correctly."));
  }
  else
  {
    Serial.println(F("!An error occurred during SPIFFS mounting"));
  }
  file_write = SPIFFS.open("/Data/data.csv", "w");

  if (!file_write)
  {
    // File not found
    Serial.println("Failed to open test file");
    return;
  }
  else
  {
    Serial.println("sucsesfull connection to CSV file");
  }

  Serial.println("Starting Arduino BLE Client application...");
}

void setup()
{
  Serial.begin(115200);

  ledcSetup(0, 5000, 8);
  ledcAttachPin(15, 0);

  ledcWrite(0, off);

  SPIFFS_SETUP();

#ifdef proto
  ledcWrite(0, on);
  delay(50);
  ledcWrite(0, off);
#endif

  BLEDevice::init("");
  // Retrieve a Scanner and set the callback we want to use to be informed when we
  // have detected a new device.  Specify that we want active scanning and start the
  // scan to run for 5 seconds.
  scan_connect();
} // End of setup.

// This is the Arduino main loop function.
void loop()
{

  // If the flag "doConnect" is true then we have scanned for and found the desired
  // BLE Server with which we wish to connect.  Now we connect to it.  Once we are
  // connected we set the connected flag to be true.
  if (doConnect == true)
  {
    if (connectToServer())
    {
      Serial.println("We are now connected to the BLE Server.");
    }
    else
    {
      Serial.println("We have failed to connect to the server; there is nothin more we will do.");
    }
    doConnect = false;
  }

  // If we are connected to a peer BLE Server, update the characteristic each time we are reached
  // with the current time since boot.
  if (connected)
  {
    Serial.println("IN LOOP: ");
    std::string value = pTXCharacteristic->readValue();
    Serial.print("The characteristic value was: ");
    Serial.println(value.c_str());

    file_write.print(value.c_str() + String(","));

    byte myByteArr[] = {0x67, 0x65, 0x74, 0x20, 0x73, 0x74, 0x61, 0x74, 0x75, 0x73, 0x0A};
    pRXCharacteristic->writeValue(myByteArr, 11, false);
  }
  else if (doScan) //if beaker and jewelery get seperated
  {

    file_write.close();

      ledcWrite(0, on);
      delay(50);
      ledcWrite(0, off);
      delay(50);
      ledcWrite(0, on);
      delay(50);
      ledcWrite(0, off);
      delay(50);
      ledcWrite(0, on);
      delay(50);
      ledcWrite(0, off);

    while (!WiFi_connect(ssid, password))
    {
      ;
    }

    file_read = SPIFFS.open("/Data/data.csv");
    if (!file_read)
    {
      Serial.println("Failed to open file for reading");
      return;
    }
    Serial.println("File Content:");

    //initiate the csv output
    String builder = "";
    uint counter = 0;
    char csvField[csvFieldSize];
    char temp;
    bool use = false;

    uint16_t int_arr[16] = {};

#ifndef fullData
    while (file_read.available())
    {
      for (int i = 0; i < csvFieldSize; i++)
      {
        temp = file_read.read();
        csvField[i] = temp;

        if (temp == '-')
        {

          for (int j = 15; j > 0; j--)
          {
            int_arr[j] = int_arr[j - 1];
          }

          use = false;
          counter++;

          Serial.println();
          Serial.print("Builder: ");
          Serial.println(builder);
          Serial.print("interger: ");
          Serial.println(builder.toInt());
          Serial.print("counter: ");
          Serial.println(counter);

          int_arr[0] = builder.toInt();

          builder = "";

          // changing max and min values
          if (counter > 10)
          {
            bool stable = true;

            for (int j = 0; j < 10; j++)
            {
              if (abs(int_arr[0] - int_arr[j]) > 10)
              {
                Serial.println(String("check : ") + String(int_arr[0]) + String("  Test : ") + String(int_arr[j]) + String("  @  " + String(j)));
                stable = false;
                break;
              }
            }

            if (stable == true)
            {
              Serial.println("STABEL");
              if (int_arr[0] > fluidMax)
              {
                fluidMax = int_arr[0];
                Serial.print("new fluid max : ");
                Serial.println(int_arr[0]);
              }
              if (int_arr[0] < fluidMin)
              {
                fluidMin = int_arr[0];
                Serial.print("new fluid min : ");
                Serial.println(int_arr[0]);
              }
            }
          }
          break;
        }

        if (use == true)
        {
          Serial.write(temp);
          builder = builder + String(temp);
        }

        if (temp == '=')
        {
          use = true;
        }
      }
    }

    Serial.println(String("fluid given") + String("=") + String(fluidMax, DEC));
    Serial.println(String("fluid taken") + String("=") + String(fluidMin, DEC));
    Serial.println(String("fluid drank") + String("=") + String(fluidMax - fluidMin, DEC));

    builder = String("fluid_given") + String("=") + String(fluidMax) + String("&");
    builder = builder + String("fluid_taken") + String("=") + String(fluidMin) + String("&");
    builder = builder + String("fluid_drank") + String("=") + String(fluidMax - fluidMin);
#endif

#ifdef fullData
    while (file_read.available())
    {
      for (int i = 0; i < csvFieldSize; i++)
      {
        temp = file_read.read();
        csvField[i] = temp;

        if (counter == 60)
        {
          sendData(builder);
          counter = 0;
        }

        if (temp == '-')
        {
          use = false;
          counter++;
          break;
        }

        if (use == true)
        {
          Serial.write(temp);
          builder = builder + String(temp);
        }

        if (temp == '=')
        {
          if (counter != 0)
          {
            builder = builder + String("&") + String(counter) + String("=");
          }
          else
          {
            builder = String(counter) + String("=");
          }
          use = true;
        }
      }
    }
#endif

    if (counter > 0)
    {
      sendData(builder);
    }

    file_read.close();

    delay(1000);

    /* HOST DATA ONLINE DELETE IT FROM MEMORY (RESTARTING ESP) */

    ESP.restart();
  }

  if (ledOn){
    ledcWrite(0, off);
    ledOn = false;
  } else {
    ledcWrite(0, on);
    ledOn = true;
  }

  delay(1000); // Delay a second between loops.
} // End of loop