# ESP-Alexa
Use ESP Alexa lib to control hacked Broadlink RM3 Mini IR Blaster

Configuration of Alexa device names and IR codes is via config.json file stored in SPIFFS and you can also control devices using MQTT


On first run the ESP8266 will create an access point (ESP_AlexaWM) where you enter the WiFi credentials. Ideally it should also prompt for MQTT settings but they are currently hard coded so a blank device can be found in MQTT to upload a config file.

After the WiFi has connected the ESP8266 tries to load a config.json file from SPIFFS that contains the device name (used for MQTT) and also an array of Alexa device names and IR codes.

If no config.json file exists or it fails to load correctly then a MQTT topic that is compounded from ESPALEXA basename + "/BLANK" + (ESP.getChipId(), HEX) is created and connected this allows a channel back to a blank device so a config.json file can be uploaded.

If the config file loads correctly then the MQTT topic will be ESPALEXA + "/" + devname

MQTT control topics are...
ESPALEXA + "/" + devname + "/" + name + "/Set" with a payload of the value to set for the Alexa name device.


ESPALEXA + "/" + devname + "/" + name + "/Get" will return the Alexa name device state.
This will be invalid for the first call and may not reflect the true state of the device if it did not receive the last IR transmission.


ESPALEXA + "/" + devname + "/Pull" will return the config.json file from SPIFFS.


ESPALEXA + "/" + devname + "/Push" with the payload of a config.json file will create/overwrite the config.json file in SPIFFS.


ESPALEXA + "/" + devname + "/Reset" will start a 5 second countdown to the ESP8266 resetting.


ESPALEXA + "/" + devname + "/Mem" will return the free heap space. Used for testing for memory leaks.


