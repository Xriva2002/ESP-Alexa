#include <FS.h>                                   // This needs to be first, or it all crashes and burns...

// ***********************************************
const bool DEBUG = false;
// ***********************************************

#include <ESP8266WiFi.h>
//needed for library WiFiManager
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

#include <PubSubClient.h>
// ***********************************************
// ***********************************************
#define MQTT_MAX_PACKET_SIZE 1024                 // You must alter the PubSubClient to this to allow config download
// ***********************************************
// ***********************************************
#define ESPALEXA "ESP_ALEXA"                      // Base name (used for MQTT topic)

#include "mqttSettings.h"                         // Contains the below defines
/* 
  #define MQTTSERVER     "IP ADDRESS STRING"
  #define MQTTSERVERPORT PORT NUMBER
  #define MQTTCLIENTID   "ID STRING"
  #define MQTTUSER       "USER STRING"
  #define MQTTPASSWORD   "PASSWORD STRING"
*/
#include <ArduinoJson.h>

//#define ESPALEXA_MAXDEVICES 15                  // set maximum devices add-able to Espalexa
#include <Espalexa.h>

#include <IRremoteESP8266.h>
#include <IRsend.h>
IRsend irsend(4);                                 // Set the GPIO (D2) to be used for IR LED sending the message. (maybe move to config item)

WiFiClient espClient;
PubSubClient mqttClient(espClient);               // Create MQTT object

void deviceChanged(EspalexaDevice* dev);          // Espalexa callback functions

Espalexa espalexa;

struct irDevice 
{
  uint8_t ir_state;                               // Current device state. Only set when on/off codes sent and may not reflect the true state if device does not respond to the IR
  uint8_t ir_type;                                // See decode_type_t
  uint8_t ir_repeat;                              // Number of times to repeat the IR signal
  uint8_t ir_bits;                                // Number of bits in the IR signal (uint32_t max)
  uint32_t ir_onCode;                             // IR on code
  uint32_t ir_offCode;                            // IR off code
  char ir_alexaName[20];                          // IR device Alexa name
};

char myDeviceName[40];                            // Initially store ESP device name (from JSON "devname") then converted to MQTT callback topic
irDevice myDevices[ESPALEXA_MAXDEVICES];          // No more than x devices

void setup()
{
  Serial.begin(115200);
  
  Serial.println();
  Serial.println(F(ESPALEXA));
  Serial.println(F("Sketch = " __FILE__));
  Serial.println(F("Compile Date = " __DATE__ " " __TIME__));
  Serial.println(F("Booting"));
  
  WiFiManager wifiManager;
  //wifiManager.resetSettings();
  
  //fetches ssid and pass from eeprom and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "ESP_Alexa"
  //and goes into a blocking loop awaiting configuration
  wifiManager.autoConnect("ESP_AlexaWM");
  
  //if you get here you have connected to the WiFi
  if(DEBUG){ Serial.print("\nLocal IP = ");}
  if(DEBUG){ Serial.println(WiFi.localIP());}
  
  //read configuration from FS json
  if(DEBUG){ Serial.println("Mounting FS...");}
  if (SPIFFS.begin())                                       // Mount filesystem
  {
    if(DEBUG){ Serial.println("Mounted file system");}
    if (SPIFFS.exists("/config.json")) 
    {
      //file exists, reading and loading
      if(DEBUG){ Serial.println("Reading config file");}
      File configFile = SPIFFS.open("/config.json", "r");   // Open file for reading
      if (configFile) 
      {
        if(DEBUG){ Serial.println("Opened config file");}
        size_t size = configFile.size();                    // Get file size
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);        // Allocate buffer space to read it
        
        configFile.readBytes(buf.get(), size);              // Read it
        
        DynamicJsonDocument doc(1024);                      // Allocate JSON buffers
        DeserializationError error = deserializeJson(doc, buf.get()); // Decode JSON config file
        // Test if parsing succeeds.
        if (error) 
        {
          Serial.print("DeserializeJson() failed: ");       // Flag any errors but keep going in setup
          Serial.println(error.c_str());
        }
        else
        {
          // Fetch values.
          const char* devname = doc["devname"];             // Read ESP device name
          strncpy(myDeviceName, devname, 20);
          WiFi.hostname(myDeviceName);                      // Set WiFi name to match device name 
          if(DEBUG)
          { 
            Serial.print("\ndevice name = ");
            Serial.println(myDeviceName);
          }
          
          JsonArray devices = doc["devices"];               // Get device array
          if(DEBUG)
          { 
            Serial.print("devices.size = ");
            Serial.println(devices.size());
            Serial.println();
          }
          
          for(uint8_t x = 0; x < devices.size(); x++)
          {
            if(x < ESPALEXA_MAXDEVICES)                     // espalexa device limit reached?
            {
              JsonObject devices_x = devices[x];
              const char* devices_x_name = devices_x["name"]; // Get Alexa name
              if(DEBUG){ Serial.print("name = ");}
              if(DEBUG){ Serial.println(devices_x_name);}
              strncpy(myDevices[x].ir_alexaName, devices_x_name, 20); // Store in structure
              
              int devices_x_ir_type = devices_x["decode_type"]; // Get decode_type_t
              if(DEBUG){ Serial.print("decode_type = ");}
              if(DEBUG){ Serial.println(devices_x_ir_type);}
              myDevices[x].ir_type = devices_x_ir_type;     // Store in structure
              
              int devices_x_ir_repeat = devices_x["repeat"]; // IR repeat count
              if(DEBUG){ Serial.print("ir_repeat = ");}
              if(DEBUG){ Serial.println(devices_x_ir_repeat);}
              myDevices[x].ir_repeat = devices_x_ir_repeat; // Store in structure
              
              int devices_x_ir_bits = devices_x["bits"];    // IR code bitcount (max 32 bits)
              if(DEBUG){ Serial.print("ir_bits = ");}
              if(DEBUG){ Serial.println(devices_x_ir_bits);}
              myDevices[x].ir_bits = devices_x_ir_bits;     // Store in structure
              
              const char* devices_x_on = devices_x["on"];   // IR on code (HEX string without the 0x prefix)
              if(DEBUG){ Serial.print("on = ");}
              uint32_t y = strtoul(devices_x_on, NULL, 16); // Convert hex String to number
              if(DEBUG){ Serial.println(y, HEX);}
              myDevices[x].ir_onCode = y;                   // Store in structure
              
              const char* devices_x_off = devices_x["off"]; // IR off code (HEX string without the 0x prefix)
              if(DEBUG){ Serial.print("off = ");}
              y = strtoul(devices_x_off, NULL, 16);         // Convert hex String to number
              if(DEBUG){ Serial.println(y, HEX);}
              myDevices[x].ir_offCode = y;                  // Store in structure
              
              if(DEBUG){ Serial.println();}
              
              // Define devices 
              espalexa.addDevice(myDevices[x].ir_alexaName, deviceChanged, EspalexaDeviceType::onoff); //non-dimmable device
            }
          }
        }
        if(DEBUG){ Serial.println("Close config file");}
        configFile.close();                                 // Close config file
      }
    }
    else
    {
      Serial.println(F("'/config.json' missing"));          // Signal config missing but finish setup
    }
    SPIFFS.end();                                           // Unmount FS
  } 
  else 
  {
    Serial.println("Failed to mount FS");                   // Failed to mount FS?
    return;                                                 // Pointless continuing with setup (will kickup MQTT errors in loop)
  }
  
  irsend.begin();                                           // Init IR
  
  espalexa.begin();                                         // Init Alexa
  
  String scratch = (String)myDeviceName;                    // Get device name (will be empty if config load failed)
  if(scratch == "")
  {
    scratch = ESPALEXA + (String)"/" + (String)"BLANK" + String(ESP.getChipId(), HEX);  // Generate a unique device name for MQTT subscribe (so we can upload a config to a blank FS)
  }
  else
  {
    scratch = ESPALEXA + (String)"/" + (String)myDeviceName;// Create MQTT topic
  }
  scratch.toCharArray(myDeviceName, sizeof(myDeviceName));  // Store modified device name
  Serial.println(myDeviceName);
  
  mqttClient.setServer(MQTTSERVER, MQTTSERVERPORT);         // Setup MQTT stuff
  mqttClient.setCallback(mqttCallback);                     // Register a callback function for received MQTT messages
  Serial.println(F("Booted"));
}

void loop()
{
  if(!mqttClient.connected())                               // Is MQTT connected?
  {
    if(mqttConnect())                                       // Try to connect to MQTT
    {
      if(DEBUG){ Serial.print(F("Publish "));}              // Send MQTT message that we have connected
      if(DEBUG){ Serial.println(myDeviceName);}
      mqttClient.publish(myDeviceName, "Connected");
    }
    else
    {
      Serial.println(F("Reboot Code Here"));                // Reboot if MQTT connect fails?
    }
  }
  mqttClient.loop();                                        // MQTT housekeeping
  espalexa.loop();                                          // Alexa housekeeping
  delay(10);
}

bool mqttConnect()
{
  if(DEBUG){ Serial.print(("Attempting MQTT connection... "));}
  if (mqttClient.connect(MQTTCLIENTID, MQTTUSER, MQTTPASSWORD)) 
  {
    if(DEBUG){ Serial.println(F("Connected"));}
    String scratch = (String)myDeviceName + "/#";           // Substribe to all MQTT topics for this device
    mqttClient.subscribe(scratch.c_str());                  // Subscribe to MQTT input
    if(DEBUG){ Serial.print(F("Subscribe: "));}
    if(DEBUG){ Serial.println(scratch);}
  } 
  else 
  {
    Serial.print(F("Failed, rc="));
    Serial.println(mqttClient.state());
  }
  return mqttClient.connected();
}

// Alexa callback function
void deviceChanged(EspalexaDevice* d) 
{
  if (d == nullptr) return;                                 // This is good practice, but not required
  
  uint8_t devNum = d->getId();                              // Get device number for indexing into myDevices array
  if(myDevices[devNum].ir_bits > 0)                         // Only do this for entries we know about
  {
    sendIR(devNum, d->getValue());                          // Do the IR stuff
  }
}

void sendIR(uint8_t devNo, uint8_t devState)                // Send IR commands and MQTT info
{
  if(DEBUG)
  { 
    Serial.print(myDevices[devNo].ir_alexaName);
    Serial.print(" changed to ");
    Serial.println(devState);
  }
  if(devState)
  {
    // Send on code
    irsend.send((decode_type_t)myDevices[devNo].ir_type, myDevices[devNo].ir_onCode, myDevices[devNo].ir_bits, myDevices[devNo].ir_repeat);
  }
  else
  {
    // Send off code
    irsend.send((decode_type_t)myDevices[devNo].ir_type, myDevices[devNo].ir_offCode, myDevices[devNo].ir_bits, myDevices[devNo].ir_repeat);
  }
  myDevices[devNo].ir_state = devState;                     // Store on/off state in device structure
  String topic = (String)myDeviceName + (String)"/" + (String)myDevices[devNo].ir_alexaName + (String)"/Info";
  String payload = (String)myDevices[devNo].ir_state;
  mqttClient.publish(topic.c_str(), payload.c_str());       // Send MQTT Info of state
  // device[i]->setValue(device[i]->getValue() > 0 ? 0 : 255);	// Value toggle
  // device[i]->doCallback();									                // and set device to value
}

void mqttCallback(char* topic, byte * payload, unsigned int length)// Callback for subscribed MQTT topics
{
  int8_t devNum = -1;                                       // Device number of matching name String
  uint8_t devState = 0;
  
  if (strncmp(topic, myDeviceName, strlen(myDeviceName)) == 0)    // Check if the topic is for me (really needed?)
  {
    payload[length] = '\0';                                 // Terminate the payload (as from PubSub on github)
    String callback_topic = (String)topic;                  // Copy topic
    String callback_payload = String((char*)payload);
    callback_topic.remove(0,strlen(myDeviceName) + 1);      // Remove device name and / from start of string
    
    for(uint8_t x = 0; x < ESPALEXA_MAXDEVICES; x++)        // Loop through devices
    {
      if(myDevices[x].ir_bits > 0)                          // Ignore empty devices
      {
        String device = (String)myDevices[x].ir_alexaName;  // Get device name
        if(callback_topic.startsWith(device))               // Does device name match?
        {
          devNum = x;                                       // Device found
          break;                                            // Drop out of for loop
        }
      }
    }
    if(devNum >= 0)                                         // Did we find a device?
    {
      if(callback_topic.endsWith("/Set"))                   // Set device state
      {
        if(DEBUG){ Serial.println("Set");}
        devState = callback_payload.toInt();                // Get payload state
        sendIR(devNum, devState);                           // Send IR command
        return;
      }
      
      if(callback_topic.endsWith("/Get"))                   // Get device state
      {
        if(DEBUG){ Serial.println("Get");}
        devState = callback_payload.toInt();                // Get payload state
        String topic = (String)myDeviceName + (String)"/" + (String)myDevices[devNum].ir_alexaName + (String)"/Info"; // Construct info topic
        String payload = (String)myDevices[devNum].ir_state;  // Info payload
        mqttClient.publish(topic.c_str(), payload.c_str()); // MQTT publish info
        return;
      }
    }
    
    // None device specific commands
    
    if(callback_topic.equals("Pull"))                       // Read config file and send over MQTT
    {
      if(DEBUG){ Serial.println("Pull");}
      if (SPIFFS.begin())                                   // Open FS
      {
        if (SPIFFS.exists("/config.json"))                  // Check config file exists
        {
          File configFile = SPIFFS.open("/config.json", "r"); // Open config file
          if (configFile) 
          {
            size_t size = configFile.size();                // Get config file size
            char* config_copy = reinterpret_cast<char*>(malloc(size + 1));  // Allocate memory buffer for file + 1 for terminator
            if (config_copy != NULL)                        // Memory allocated
            {
              configFile.readBytes(config_copy, size);      // Read config file into buffer
              config_copy[size] = '\0';                     // Terminate String
              String config_string = String(config_copy);   // Convert to String
              String topic = (String)myDeviceName + (String)"/Pulled";  // Construct MQTT topic
              mqttClient.publish(topic.c_str(), config_string.c_str()); // Publish config file
              free(config_copy);                            // Free the buffer memory
              
            }
            configFile.close();                             // Close config file
          }
        }
        SPIFFS.end();                                       // Close FS
      }
      return;
    }
    
    if(callback_topic.equals("Push"))                       // Set config file to MQTT payload
    {
      if(DEBUG){ Serial.println("Push");}
      
      if(callback_payload.startsWith("{"))                  // Check payload begins with {
      {
        if(callback_payload.endsWith("}"))                  // Check payload ends with }
        {
          if (SPIFFS.begin())                               // Open FS
          {
            File configFile = SPIFFS.open("/config.json", "w"); // Create new config file
            if (configFile) 
            {
              int size = configFile.print(callback_payload);  // Write payload to file
              if(size == callback_payload.length())         // Check file write size matches payload size
              {
                String config_size = (String)size;          // Convert to String
                String topic = (String)myDeviceName + (String)"/Pushed";  // Construct topic
                mqttClient.publish(topic.c_str(), config_size.c_str()); // Publish
              }
              configFile.close();                           // Close config file
            }
            SPIFFS.end();                                   // Close FS
          }
        }
      }
      return;
    }
    
    if(callback_topic.equals("Reset"))                      // ESP reset (maybe require a magic number as the payload?)
    {
      if(DEBUG){ Serial.print("Reset");}
      for(uint8_t x = 5; x != 0; x--)                       // 5 second countdown
      {
        Serial.print(".");
        Serial.print(x);
        delay(1000);
      }
      ESP.reset();                                          // Reset
      return;                                               // Should never get here
    }
    
    if(callback_topic.equals("Mem"))                        // Check memory (for testing for memory leaks)
    {
      if(DEBUG){ Serial.println("Mem");}
      String free = (String)ESP.getFreeHeap();              // Get free heap space
      String topic = (String)myDeviceName + (String)"/Memory";  // Construct topic
      mqttClient.publish(topic.c_str(), free.c_str());      // Publish
      return;
    }
    
    // Only gets here if not a supported topic
    if(DEBUG)
    { 
      Serial.print("Other ");
      Serial.print(callback_topic);
      Serial.print(": ");
      Serial.println(callback_payload);
    }
  }
}

/*
  enum decode_type_t {
  UNKNOWN = -1,
  UNUSED = 0,
  RC5,
  RC6,
  NEC,
  SONY,
  PANASONIC,  // (5)
  JVC,
  SAMSUNG,
  WHYNTER,
  AIWA_RC_T501,
  LG,  // (10)
  SANYO,
  MITSUBISHI,
  DISH,
  SHARP,
  COOLIX,  // (15)
  DAIKIN,
  DENON,
  KELVINATOR,
  SHERWOOD,
  MITSUBISHI_AC,  // (20)
  RCMM,
  SANYO_LC7461,
  RC5X,
  GREE,
  PRONTO,  // Technically not a protocol, but an encoding. (25)
  NEC_LIKE,
  ARGO,
  TROTEC,
  NIKAI,
  RAW,  // Technically not a protocol, but an encoding. (30)
  GLOBALCACHE,  // Technically not a protocol, but an encoding.
  TOSHIBA_AC,
  FUJITSU_AC,
  MIDEA,
  MAGIQUEST,  // (35)
  LASERTAG,
  CARRIER_AC,
  HAIER_AC,
  MITSUBISHI2,
  HITACHI_AC,  // (40)
  HITACHI_AC1,
  HITACHI_AC2,
  GICABLE,
  HAIER_AC_YRW02,
  WHIRLPOOL_AC,  // (45)
  SAMSUNG_AC,
  LUTRON,
  ELECTRA_AC,
  PANASONIC_AC,
  PIONEER,  // (50)
  LG2,
  MWM,
  DAIKIN2,
  VESTEL_AC,
  TECO,  // (55)
  SAMSUNG36,
  TCL112AC,
  LEGOPF,
  MITSUBISHI_HEAVY_88,
  MITSUBISHI_HEAVY_152,  // 60
  DAIKIN216,
  SHARP_AC,
  GOODWEATHER,
  INAX,
  DAIKIN160,  // 65
  NEOCLIMA,
  DAIKIN176,
  DAIKIN128,
  AMCOR,
  DAIKIN152,  // 70
  MITSUBISHI136,
  MITSUBISHI112,
  HITACHI_AC424,
  SONY_38K,
  EPSON,  // 75
  // Add new entries before this one, and update it to point to the last entry.
  kLastDecodeType = EPSON,
  };
  
*/


