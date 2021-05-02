# AzureDataSender_Esp32

Sending Sensor Data to Azure Storage Tables using Esp32 Dev board. Works with http- and https- transmission.

For details about the functions of this app and how to use and apply settings have a look on this similar project

https://www.hackster.io/RoSchmi/wio-terminal-app-sending-sensor-data-to-azure-storage-tables-dbb08e

Patches for Esp32 board:

To work with TLS the default stack size 8182 of the Esp32 had to be enlarged (e.g. to 16384)

-https://community.platformio.org/t/esp32-stack-configuration-reloaded/20994


On your PC replace the file C:\Users\<user>\.platformio\packages\framework-arduinoespressif32\cores\esp32\main.cpp
with the file 'main.cpp' from folder 'patches' of this repository, then use the following code to configure stack size

    #if !(USING_DEFAULT_ARDUINO_LOOP_STACK_SIZE)
        uint16_t USER_CONFIG_ARDUINO_LOOP_STACK_SIZE = 16384;
    #endif


Use the iPhone- or Android- App 'Charts4Azure' (https://azureiotcharts.home.blog/)  to visualize the uploaded Sensor data
