# AzureDataSender_Esp32

Sending Sensor Data to Azure Storage Tables using Esp32 Dev board. Works with http- and https- transmission.

For details about the functions of this app and how to use and apply settings have a look on this similar project

https://www.hackster.io/RoSchmi/wio-terminal-app-sending-sensor-data-to-azure-storage-tables-dbb08e

Patches for Esp32 board:

To work with TLS the Stack size of of the Esp32 had to be enlarged
-https://community.platformio.org/t/esp32-stack-configuration-reloaded/20994

