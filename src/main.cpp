
// ESP_IDF Programming Guide
// Heap Memory Allocation
// https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/mem_alloc.html

// Change Esp32 Stack size which is per default 8192 to 16384
// https://community.platformio.org/t/esp32-stack-configuration-reloaded/20994
// user/platformio/packages/framework-arduinoespressif32/tools/sdk/include/config/sdkconfig.h 
#include <Arduino.h>
#include <time.h>
#include "WiFiWebServer.h"
#include "defines.h"
#include "config.h"
#include "config_secret.h"
#include "DateTime.h"

#include "FreeRTOS.h"
#include "Esp.h"
#include "esp_task_wdt.h"

#include "CloudStorageAccount.h"
#include "TableClient.h"
#include "TableEntityProperty.h"
#include "TableEntity.h"
#include "AnalogTableEntity.h"
#include "OnOffTableEntity.h"

#include "NTPClient_Generic.h"
#include "Timezone_Generic.h"
#include "WiFi.h"
#include "WiFiClientSecure.h"
#include "WiFiUdp.h"
#include "WiFiClient.h"
#include "HTTPClient.h"

#include "DataContainerWio.h"
#include "OnOffDataContainerWio.h"
#include "OnOffSwitcherWio.h"
#include "ImuManagerWio.h"
#include "AnalogSensorMgr.h"

#include "azure/core/az_platform.h"
#include "azure/core/az_http.h"
#include "azure/core/az_http_transport.h"
#include "azure/core/az_result.h"
#include "azure/core/az_config.h"
#include "azure/core/az_context.h"
#include "azure/core/az_span.h"

#include "Rs_TimeNameHelper.h"



// Allocate memory space in memory segment .dram0.bss, ptr to this memory space is later
// passed to TableClient (is used there as the place for some buffers to preserve stack )
uint8_t bufferStore[4000] {0};
uint8_t * bufferStorePtr = &bufferStore[0];

// Used to keep book of used stack
void * StackPtrAtStart;
void * StackPtrEnd;
UBaseType_t watermarkStart;
TaskHandle_t taskHandle_0 =  xTaskGetCurrentTaskHandleForCPU(0);
TaskHandle_t taskHandle_1 =  xTaskGetCurrentTaskHandleForCPU(1);

#define GPIOPin 0
bool buttonPressed = false;

const char analogTableName[45] = ANALOG_TABLENAME;

const char OnOffTableName_1[45] = ON_OFF_TABLENAME_01;
const char OnOffTableName_2[45] = ON_OFF_TABLENAME_02;
const char OnOffTableName_3[45] = ON_OFF_TABLENAME_03;
const char OnOffTableName_4[45] = ON_OFF_TABLENAME_04;

// The PartitionKey for the analog table may have a prefix to be distinguished, here: "Y2_" 
const char * analogTablePartPrefix = (char *)ANALOG_TABLE_PART_PREFIX;

// The PartitionKey for the On/Off-tables may have a prefix to be distinguished, here: "Y3_" 
const char * onOffTablePartPrefix = (char *)ON_OFF_TABLE_PART_PREFIX;

// The PartitinKey can be augmented with a string representing year and month (recommended)
const bool augmentPartitionKey = true;

// The TableName can be augmented with the actual year (recommended)
const bool augmentTableNameWithYear = true;

#define LED_BUILTIN 2

const char *ssid = IOT_CONFIG_WIFI_SSID;
const char *password = IOT_CONFIG_WIFI_PASSWORD;

typedef const char* X509Certificate;

X509Certificate myX509Certificate = baltimore_root_ca;

// Init the Secure client object

#if TRANSPORT_PROTOCOL == 1
    static WiFiClientSecure wifi_client;
  #else
    static WiFiClient wifi_client;
  #endif

  // A UDP instance to let us send and receive packets over UDP
  WiFiUDP ntpUDP;
  static NTPClient timeClient(ntpUDP);
  
  HTTPClient http;
  
  // Ptr to HTTPClient
  static HTTPClient * httpPtr = &http;
  
// Define Datacontainer with SendInterval and InvalidateInterval as defined in config.h
int sendIntervalSeconds = (SENDINTERVAL_MINUTES * 60) < 1 ? 1 : (SENDINTERVAL_MINUTES * 60);

DataContainerWio dataContainer(TimeSpan(sendIntervalSeconds), TimeSpan(0, 0, INVALIDATEINTERVAL_MINUTES % 60, 0), (float)MIN_DATAVALUE, (float)MAX_DATAVALUE, (float)MAGIC_NUMBER_INVALID);

AnalogSensorMgr analogSensorMgr(MAGIC_NUMBER_INVALID); 

OnOffDataContainerWio onOffDataContainer;

OnOffSwitcherWio onOffSwitcherWio;
 
uint64_t loopCounter = 0;
int insertCounterAnalogTable = 0;
uint32_t tryUploadCounter = 0;
uint32_t timeNtpUpdateCounter = 0;
// not used on Esp32
int32_t sysTimeNtpDelta = 0;

  bool ledState = false;
  uint8_t lastResetCause = -1;

  const int timeZoneOffset = (int)TIMEZONEOFFSET;
  const int dstOffset = (int)DSTOFFSET;

  Rs_TimeNameHelper timeNameHelper;

  DateTime dateTimeUTCNow;    // Seconds since 2000-01-01 08:00:00

  Timezone myTimezone; 

  // Set transport protocol as defined in config.h
static bool UseHttps_State = TRANSPORT_PROTOCOL == 0 ? false : true;

CloudStorageAccount myCloudStorageAccount(AZURE_CONFIG_ACCOUNT_NAME, AZURE_CONFIG_ACCOUNT_KEY, UseHttps_State);
CloudStorageAccount * myCloudStorageAccountPtr = &myCloudStorageAccount;

void GPIOPinISR()
{
  buttonPressed = true;
}

// function forward declaration
void scan_WIFI();
boolean connect_Wifi(const char * ssid, const char * password);
String floToStr(float value);
float ReadAnalogSensor(int pSensorIndex);
void createSampleTime(DateTime dateTimeUTCNow, int timeZoneOffsetUTC, char * sampleTime);
az_http_status_code  createTable(CloudStorageAccount * myCloudStorageAccountPtr, X509Certificate pCaCert, const char * tableName);
az_http_status_code insertTableEntity(CloudStorageAccount *myCloudStorageAccountPtr,X509Certificate pCaCert, const char * pTableName, TableEntity pTableEntity, char * outInsertETag);
void makePartitionKey(const char * partitionKeyprefix, bool augmentWithYear, DateTime dateTime, az_span outSpan, size_t *outSpanLength);
void makeRowKey(DateTime actDate, az_span outSpan, size_t *outSpanLength);
int getDayNum(const char * day);
int getMonNum(const char * month);
int getWeekOfMonthNum(const char * weekOfMonth);

void setup() {
  
  // Disable watchdog
  esp_task_wdt_deinit();  
   
  // Get Stackptr at start of setup()
  void* SpStart = NULL;
  StackPtrAtStart = (void *)&SpStart;
  // Get StackHighWatermark at start of setup()
  watermarkStart =  uxTaskGetStackHighWaterMark(NULL);
  // Calculate (not exact) end-address of the stack
  StackPtrEnd = StackPtrAtStart - watermarkStart;

  __unused uint32_t minFreeHeap = esp_get_minimum_free_heap_size();
  uint32_t freeHeapSize = esp_get_free_heap_size();
  
  
  /*
  UBaseType_t watermarkStart_0 = uxTaskGetStackHighWaterMark(taskHandle_0);
  UBaseType_t watermarkStart_1 = uxTaskGetStackHighWaterMark(NULL);
  */
  



  Serial.begin(115200);
  //while (!Serial);

  attachInterrupt(GPIOPin, GPIOPinISR, RISING);
  
  Serial.printf("\r\n\r\nAddress of Stackpointer near start is:  %p \r\n",  (void *)StackPtrAtStart);
  Serial.printf("End of Stack is near:               %p \r\n",  (void *)StackPtrEnd);
  Serial.printf("Free Stack near start is:  %d \r\n",  (uint32_t)StackPtrAtStart - (uint32_t)StackPtrEnd);

  /*
  // Get free stack at actual position (can be used somewhere in program)
  void* SpActual = NULL;
  Serial.printf("\r\nFree Stack at actual position is: %d \r\n", (uint32_t)&SpActual - (uint32_t)StackPtrEnd);
  */

  Serial.print(F("Free Heap: "));
  Serial.println(freeHeapSize);

  delay(2000);
  Serial.println(F("\r\n\r\nPress Boot Button to continue!"));

  // Wait on press/release of boot button
  while (!buttonPressed)
  {
    delay(100);
  }

  pinMode(LED_BUILTIN, OUTPUT);

  Serial.print(F("\nStarting ConnectWPA on "));
  Serial.print(BOARD_NAME);
  Serial.print(F(" with "));
  Serial.println(SHIELD_TYPE); 
  Serial.println(WIFI_WEBSERVER_VERSION);

  // Wait some time (3000 ms)
  uint32_t start = millis();
  while ((millis() - start) < 3000)
  {
    delay(10);
  }
  
  #ifdef WORK_WITH_WATCHDOG == 1
  // Start watchdog with 20 seconds
  esp_task_wdt_init(20, true);
  Serial.println(F("Watchdog enabled with interval of 20 sec"));
  esp_task_wdt_add(NULL);

  //https://www.az-delivery.de/blogs/azdelivery-blog-fur-arduino-und-raspberry-pi/watchdog-und-heartbeat
  
  #endif   

  onOffDataContainer.begin(DateTime(), OnOffTableName_1, OnOffTableName_2, OnOffTableName_3, OnOffTableName_4);

  // Initialize State of 4 On/Off-sensor representations 
  // and of the inverter flags (Application specific)
  for (int i = 0; i < 4; i++)
  {
    onOffDataContainer.PresetOnOffState(i, false, true);
    onOffDataContainer.Set_OutInverter(i, true);
    onOffDataContainer.Set_InputInverter(i, false);
  }

  //Initialize OnOffSwitcher (for tests and simulation)
  onOffSwitcherWio.begin(TimeSpan(15 * 60));   // Toggle every 30 min
  onOffSwitcherWio.SetInactive();
  //onOffSwitcherWio.SetActive();

// Setting Daylightsavingtime. Enter values for your zone in file include/config.h
  // Program aborts in some cases of invalid values
  
  int dstWeekday = getDayNum(DST_START_WEEKDAY);
  int dstMonth = getMonNum(DST_START_MONTH);
  int dstWeekOfMonth = getWeekOfMonthNum(DST_START_WEEK_OF_MONTH);

  TimeChangeRule dstStart {DST_ON_NAME, (uint8_t)dstWeekOfMonth, (uint8_t)dstWeekday, (uint8_t)dstMonth, DST_START_HOUR, TIMEZONEOFFSET + DSTOFFSET};
  
  bool firstTimeZoneDef_is_Valid = (dstWeekday == -1 || dstMonth == - 1 || dstWeekOfMonth == -1 || DST_START_HOUR > 23 ? true : DST_START_HOUR < 0 ? true : false) ? false : true;
  
  dstWeekday = getDayNum(DST_STOP_WEEKDAY);
  dstMonth = getMonNum(DST_STOP_MONTH) + 1;
  dstWeekOfMonth = getWeekOfMonthNum(DST_STOP_WEEK_OF_MONTH);

  TimeChangeRule stdStart {DST_OFF_NAME, (uint8_t)dstWeekOfMonth, (uint8_t)dstWeekday, (uint8_t)dstMonth, (uint8_t)DST_START_HOUR, (int)TIMEZONEOFFSET};

  bool secondTimeZoneDef_is_Valid = (dstWeekday == -1 || dstMonth == - 1 || dstWeekOfMonth == -1 || DST_STOP_HOUR > 23 ? true : DST_STOP_HOUR < 0 ? true : false) ? false : true;
  
  if (firstTimeZoneDef_is_Valid && secondTimeZoneDef_is_Valid)
  {
    myTimezone.setRules(dstStart, stdStart);
  }
  else
  {
    // If timezonesettings are not valid: -> take UTC and wait for ever  
    TimeChangeRule stdStart {DST_OFF_NAME, (uint8_t)dstWeekOfMonth, (uint8_t)dstWeekday, (uint8_t)dstMonth, (uint8_t)DST_START_HOUR, (int)0};
    myTimezone.setRules(stdStart, stdStart);
    while (true)
    {
      Serial.println("Invalid DST Timezonesettings");
      delay(5000);
    }
  }

  //******************************************************

  //Set WiFi to station mode and disconnect from an AP if it was previously connected
  WiFi.mode(WIFI_STA);
  Serial.println(F("First disconnecting, then\r\nConnecting to WiFi-Network"));

  while (WiFi.status() != WL_DISCONNECTED)
  {
    WiFi.disconnect();
    delay(200); 
  }
  WiFi.begin(ssid, password);

if (!WiFi.enableSTA(true))
{
  while (true)
  {
    // Stay in endless loop to reboot through Watchdog
    Serial.println("Connect failed.");
    delay(1000);
    }
}

#if USE_WIFI_STATIC_IP == 1
  if (!WiFi.config(presetIp, presetGateWay, presetSubnet, presetDnsServer1, presetDnsServer2))
  {
    while (true)
    {
      // Stay in endless loop
    lcd_log_line((char *)"WiFi-Config failed");
      delay(3000);
    }
  }
  else
  {
    lcd_log_line((char *)"WiFi-Config successful");
    delay(1000);
  }
  #endif

#if WORK_WITH_WATCHDOG == 1
      esp_task_wdt_reset();
#endif

uint32_t tryConnectCtr = 0;
while (WiFi.status() != WL_CONNECTED)
  {  
    delay(100);
    Serial.print((tryConnectCtr++ % 20 == 0) ? "\r\n" : "." );  
  }

  Serial.print(F("\r\nGot Ip-Address: "));
  Serial.println(WiFi.localIP());
  
  /*
  // Alternative way to connect to network, didn't work reliabel
  if(WiFi.status() != WL_CONNECTED){
      scan_WIFI();
      delay(1000);
      // connect to WIFI
      
      while(WiFi.status() != WL_CONNECTED){
        delay(1000);
        
        connect_Wifi(ssid, password);        
      }
}
*/

timeClient.begin();
  timeClient.setUpdateInterval((NTP_UPDATE_INTERVAL_MINUTES < 1 ? 1 : NTP_UPDATE_INTERVAL_MINUTES) * 60 * 1000);
  // 'setRetryInterval' should not be too short, may be that short intervals lead to malfunction 
  timeClient.setRetryInterval(20000);  // Try to read from NTP Server not more often than every 20 seconds
  Serial.println("Using NTP Server " + timeClient.getPoolServerName());
  
  timeClient.update();
  uint32_t counter = 0;
  uint32_t maxCounter = 10;
  
  while(!timeClient.updated() &&  counter++ <= maxCounter)
  {
    Serial.println(F("NTP FAILED: Trying again"));
    delay(1000);
    #if WORK_WITH_WATCHDOG == 1
      esp_task_wdt_reset();
    #endif
    timeClient.update();
  }

  if (counter >= maxCounter)
  {
    while(true)
    {
      delay(500); //Wait for ever, could not get NTP time, eventually reboot by Watchdog
    }
  }

  Serial.println("\r\n********UPDATED********");
    
  Serial.println("UTC : " + timeClient.getFormattedUTCTime());
  Serial.println("UTC : " + timeClient.getFormattedUTCDateTime());
  Serial.println("LOC : " + timeClient.getFormattedTime());
  Serial.println("LOC : " + timeClient.getFormattedDateTime());
  Serial.println("UTC EPOCH : " + String(timeClient.getUTCEpochTime()));
  Serial.println("LOC EPOCH : " + String(timeClient.getEpochTime()));

  unsigned long utcTime = timeClient.getUTCEpochTime();  // Seconds since 1. Jan. 1970
  
  dateTimeUTCNow =  utcTime;

  Serial.printf("%s %i %i %i %i %i", (char *)"UTC-Time is  :", dateTimeUTCNow.year(), 
                                        dateTimeUTCNow.month() , dateTimeUTCNow.day(),
                                        dateTimeUTCNow.hour() , dateTimeUTCNow.minute());
  Serial.println("");
  
  DateTime localTime = myTimezone.toLocal(dateTimeUTCNow.unixtime());
  
  Serial.printf("%s %i %i %i %i %i", (char *)"Local-Time is:", localTime.year(), 
                                        localTime.month() , localTime.day(),
                                        localTime.hour() , localTime.minute());
  Serial.println("");

}

void loop() 
{
  // put your main code here, to run repeatedly:
  if (++loopCounter % 100000 == 0)   // Make decisions to send data every 100000 th round and toggle Led to signal that App is running
  {
    
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState);    // toggle LED to signal that App is running

    #if WORK_WITH_WATCHDOG == 1
      esp_task_wdt_reset();
    #endif
    
      // Update RTC from Ntp when ntpUpdateInterval has expired, retry when RetryInterval has expired       
      if (timeClient.update())
      {                                                                      
        dateTimeUTCNow = timeClient.getUTCEpochTime();
        
        timeNtpUpdateCounter++;

        #if SERIAL_PRINT == 1
          // Indicate that NTP time was updated         
          char buffer[] = "NTP-Utc: YYYY-MM-DD hh:mm:ss";           
          dateTimeUTCNow.toString(buffer);
          Serial.println(buffer);
        #endif
      }  // End NTP stuff
          
      dateTimeUTCNow = timeClient.getUTCEpochTime();
      
      // Get offset in minutes between UTC and local time with consideration of DST
      int timeZoneOffsetUTC = myTimezone.utcIsDST(dateTimeUTCNow.unixtime()) ? TIMEZONEOFFSET + DSTOFFSET : TIMEZONEOFFSET;
      
      DateTime localTime = myTimezone.toLocal(dateTimeUTCNow.unixtime());

      // In the last 15 sec of each day we set a pulse to Off-State when we had On-State before
      bool isLast15SecondsOfDay = (localTime.hour() == 23 && localTime.minute() == 59 &&  localTime.second() > 45) ? true : false;
      
      // Get readings from 4 differend analog sensors and store the values in a container
      dataContainer.SetNewValue(0, dateTimeUTCNow, ReadAnalogSensor(0));
      dataContainer.SetNewValue(1, dateTimeUTCNow, ReadAnalogSensor(1));
      dataContainer.SetNewValue(2, dateTimeUTCNow, ReadAnalogSensor(2));
      dataContainer.SetNewValue(3, dateTimeUTCNow, ReadAnalogSensor(3));

      // Check if automatic OnOfSwitcher has toggled (used to simulate on/off changes)
      // and accordingly change the state of one representation (here index 0 and 1) in onOffDataContainer
      if (onOffSwitcherWio.hasToggled(dateTimeUTCNow))
      {
        bool state = onOffSwitcherWio.GetState();
        onOffDataContainer.SetNewOnOffValue(0, state, dateTimeUTCNow, timeZoneOffsetUTC);
        onOffDataContainer.SetNewOnOffValue(1, !state, dateTimeUTCNow, timeZoneOffsetUTC);
      }

      // Check if something is to do: send analog data ? send On/Off-Data ? Handle EndOfDay stuff ?
      if (dataContainer.hasToBeSent() || onOffDataContainer.One_hasToBeBeSent(localTime) || isLast15SecondsOfDay)
      {    
        //Create some buffer
        char sampleTime[25] {0};    // Buffer to hold sampletime        
        char strData[100] {0};          // Buffer to hold display message
        
        char EtagBuffer[50] {0};    // Buffer to hold returned Etag

        // Create az_span to hold partitionkey
        char partKeySpan[25] {0};
        size_t partitionKeyLength = 0;
        az_span partitionKey = AZ_SPAN_FROM_BUFFER(partKeySpan);
        
        // Create az_span to hold rowkey
        char rowKeySpan[25] {0};
        size_t rowKeyLength = 0;
        az_span rowKey = AZ_SPAN_FROM_BUFFER(rowKeySpan);

        if (dataContainer.hasToBeSent())       // have to send analog values ?
        {    
          // Retrieve edited sample values from container
          SampleValueSet sampleValueSet = dataContainer.getCheckedSampleValues(dateTimeUTCNow);
                  
          createSampleTime(sampleValueSet.LastUpdateTime, timeZoneOffsetUTC, (char *)sampleTime);

          // Define name of the table (arbitrary name + actual year, like: AnalogTestValues2020)
          String augmentedAnalogTableName = analogTableName; 
          if (augmentTableNameWithYear)
          {
            augmentedAnalogTableName += (dateTimeUTCNow.year());     
          }
          
          // Create Azure Storage Table if table doesn't exist
          if (localTime.year() != dataContainer.Year)    // if new year
          {  
            az_http_status_code theResult = createTable(myCloudStorageAccountPtr, myX509Certificate, (char *)augmentedAnalogTableName.c_str());
                     
            if ((theResult == AZ_HTTP_STATUS_CODE_CONFLICT) || (theResult == AZ_HTTP_STATUS_CODE_CREATED))
            {
              dataContainer.Set_Year(localTime.year());                   
            }
            else
            {
              // Reset board if not successful
             
             //SCB_AIRCR = 0x05FA0004;             
            }                     
          }
          

          // Create an Array of (here) 5 Properties
          // Each Property consists of the Name, the Value and the Type (here only Edm.String is supported)

          // Besides PartitionKey and RowKey we have 5 properties to be stored in a table row
          // (SampleTime and 4 samplevalues)
          size_t analogPropertyCount = 5;
          EntityProperty AnalogPropertiesArray[5];
          AnalogPropertiesArray[0] = (EntityProperty)TableEntityProperty((char *)"SampleTime", (char *) sampleTime, (char *)"Edm.String");
          AnalogPropertiesArray[1] = (EntityProperty)TableEntityProperty((char *)"T_1", (char *)floToStr(sampleValueSet.SampleValues[0].Value).c_str(), (char *)"Edm.String");
          AnalogPropertiesArray[2] = (EntityProperty)TableEntityProperty((char *)"T_2", (char *)floToStr(sampleValueSet.SampleValues[1].Value).c_str(), (char *)"Edm.String");
          AnalogPropertiesArray[3] = (EntityProperty)TableEntityProperty((char *)"T_3", (char *)floToStr(sampleValueSet.SampleValues[2].Value).c_str(), (char *)"Edm.String");
          AnalogPropertiesArray[4] = (EntityProperty)TableEntityProperty((char *)"T_4", (char *)floToStr(sampleValueSet.SampleValues[3].Value).c_str(), (char *)"Edm.String");
  
          // Create the PartitionKey (special format)
          makePartitionKey(analogTablePartPrefix, augmentPartitionKey, localTime, partitionKey, &partitionKeyLength);
          partitionKey = az_span_slice(partitionKey, 0, partitionKeyLength);

          // Create the RowKey (special format)        
          makeRowKey(localTime, rowKey, &rowKeyLength);
          
          rowKey = az_span_slice(rowKey, 0, rowKeyLength);
  
          // Create TableEntity consisting of PartitionKey, RowKey and the properties named 'SampleTime', 'T_1', 'T_2', 'T_3' and 'T_4'
          AnalogTableEntity analogTableEntity(partitionKey, rowKey, az_span_create_from_str((char *)sampleTime),  AnalogPropertiesArray, analogPropertyCount);
          
          #if SERIAL_PRINT == 1
          sprintf(strData, "   Trying to insert %u", insertCounterAnalogTable);
          Serial.println(strData);
          #endif  
             
          // Keep track of tries to insert and check for memory leak
          insertCounterAnalogTable++;

          // RoSchmi, Todo: event. include code to check for memory leaks here

          // Store Entity to Azure Cloud   
          __unused az_http_status_code insertResult =  insertTableEntity(myCloudStorageAccountPtr, myX509Certificate, (char *)augmentedAnalogTableName.c_str(), analogTableEntity, (char *)EtagBuffer);
                 
        }
        else     // Task to do was not send analog table, so it is Send On/Off values or End of day stuff?
        {
        
          OnOffSampleValueSet onOffValueSet = onOffDataContainer.GetOnOffValueSet();

          for (int i = 0; i < 4; i++)    // Do for 4 OnOff-Tables  
          {
            DateTime lastSwitchTimeDate = DateTime(onOffValueSet.OnOffSampleValues[i].LastSwitchTime.year(), 
                                                onOffValueSet.OnOffSampleValues[i].LastSwitchTime.month(), 
                                                onOffValueSet.OnOffSampleValues[i].LastSwitchTime.day());

            DateTime actTimeDate = DateTime(localTime.year(), localTime.month(), localTime.day());

            if (onOffValueSet.OnOffSampleValues[i].hasToBeSent || ((onOffValueSet.OnOffSampleValues[i].actState == true) &&  (lastSwitchTimeDate.operator!=(actTimeDate))))
            {
              onOffDataContainer.Reset_hasToBeSent(i);     
              EntityProperty OnOffPropertiesArray[5];

               // RoSchmi
               TimeSpan  onTime = onOffValueSet.OnOffSampleValues[i].OnTimeDay;
               if (lastSwitchTimeDate.operator!=(actTimeDate))
               {
                  onTime = TimeSpan(0);                 
                  onOffDataContainer.Set_OnTimeDay(i, onTime);

                  if (onOffValueSet.OnOffSampleValues[i].actState == true)
                  {
                    onOffDataContainer.Set_LastSwitchTime(i, actTimeDate);
                  }
               }
                          
              char OnTimeDay[15] = {0};
              sprintf(OnTimeDay, "%03i-%02i:%02i:%02i", onTime.days(), onTime.hours(), onTime.minutes(), onTime.seconds());
              createSampleTime(dateTimeUTCNow, timeZoneOffsetUTC, (char *)sampleTime);

              // Tablenames come from the onOffValueSet, here usually the tablename is augmented with the actual year
              String augmentedOnOffTableName = onOffValueSet.OnOffSampleValues[i].tableName;
              if (augmentTableNameWithYear)
              {               
                augmentedOnOffTableName += (localTime.year()); 
              }

              // Create table if table doesn't exist
              if (localTime.year() != onOffValueSet.OnOffSampleValues[i].Year)
              {
                 az_http_status_code theResult = createTable(myCloudStorageAccountPtr, myX509Certificate, (char *)augmentedOnOffTableName.c_str());
                 
                 if ((theResult == AZ_HTTP_STATUS_CODE_CONFLICT) || (theResult == AZ_HTTP_STATUS_CODE_CREATED))
                 {
                    onOffDataContainer.Set_Year(i, localTime.year());
                 }
                 else
                 {
                    delay(3000);
                     //Reset Teensy 4.1
                    //SCB_AIRCR = 0x05FA0004;      
                 }
              }
              
              TimeSpan TimeFromLast = onOffValueSet.OnOffSampleValues[i].TimeFromLast;

              char timefromLast[15] = {0};
              sprintf(timefromLast, "%03i-%02i:%02i:%02i", TimeFromLast.days(), TimeFromLast.hours(), TimeFromLast.minutes(), TimeFromLast.seconds());
                         
              size_t onOffPropertyCount = 5;
              OnOffPropertiesArray[0] = (EntityProperty)TableEntityProperty((char *)"ActStatus", onOffValueSet.OnOffSampleValues[i].outInverter ? (char *)(onOffValueSet.OnOffSampleValues[i].actState ? "On" : "Off") : (char *)(onOffValueSet.OnOffSampleValues[i].actState ? "Off" : "On"), (char *)"Edm.String");
              OnOffPropertiesArray[1] = (EntityProperty)TableEntityProperty((char *)"LastStatus", onOffValueSet.OnOffSampleValues[i].outInverter ? (char *)(onOffValueSet.OnOffSampleValues[i].lastState ? "On" : "Off") : (char *)(onOffValueSet.OnOffSampleValues[i].lastState ? "Off" : "On"), (char *)"Edm.String");
              OnOffPropertiesArray[2] = (EntityProperty)TableEntityProperty((char *)"OnTimeDay", (char *) OnTimeDay, (char *)"Edm.String");
              OnOffPropertiesArray[3] = (EntityProperty)TableEntityProperty((char *)"SampleTime", (char *) sampleTime, (char *)"Edm.String");
              OnOffPropertiesArray[4] = (EntityProperty)TableEntityProperty((char *)"TimeFromLast", (char *) timefromLast, (char *)"Edm.String");
          
              // Create the PartitionKey (special format)
              makePartitionKey(onOffTablePartPrefix, augmentPartitionKey, localTime, partitionKey, &partitionKeyLength);
              partitionKey = az_span_slice(partitionKey, 0, partitionKeyLength);
              
              // Create the RowKey (special format)            
              makeRowKey(localTime, rowKey, &rowKeyLength);
              
              rowKey = az_span_slice(rowKey, 0, rowKeyLength);
  
              // Create TableEntity consisting of PartitionKey, RowKey and the properties named 'SampleTime', 'T_1', 'T_2', 'T_3' and 'T_4'
              OnOffTableEntity onOffTableEntity(partitionKey, rowKey, az_span_create_from_str((char *)sampleTime),  OnOffPropertiesArray, onOffPropertyCount);
          
              onOffValueSet.OnOffSampleValues[i].insertCounter++;
              
              // Store Entity to Azure Cloud   
             __unused az_http_status_code insertResult =  insertTableEntity(myCloudStorageAccountPtr, myX509Certificate, (char *)augmentedOnOffTableName.c_str(), onOffTableEntity, (char *)EtagBuffer);
              
              delay(1000);     // wait at least 1 sec so that two uploads cannot have the same RowKey

              break;          // Send only one in each round of loop 
            }
            else
            {
              if (isLast15SecondsOfDay && !onOffValueSet.OnOffSampleValues[i].dayIsLocked)
              {
                if (onOffValueSet.OnOffSampleValues[i].actState == true)              
                {               
                   onOffDataContainer.Set_ResetToOnIsNeededFlag(i, true);                 
                   onOffDataContainer.SetNewOnOffValue(i, onOffValueSet.OnOffSampleValues[i].inputInverter ? true : false, dateTimeUTCNow, timeZoneOffsetUTC);
                   delay(1000);   // because we don't want to send twice in the same second 
                  break;
                }
                else
                {              
                  if (onOffValueSet.OnOffSampleValues[i].resetToOnIsNeeded)
                  {                  
                    onOffDataContainer.Set_DayIsLockedFlag(i, true);
                    onOffDataContainer.Set_ResetToOnIsNeededFlag(i, false);
                    onOffDataContainer.SetNewOnOffValue(i, onOffValueSet.OnOffSampleValues[i].inputInverter ? false : true, dateTimeUTCNow, timeZoneOffsetUTC);
                    break;
                  }                 
                }              
              }
            }              
          }
          
        } 
      }    
     
  }

}

// To manage daylightsavingstime stuff convert input ("Last", "First", "Second", "Third", "Fourth") to int equivalent
int getWeekOfMonthNum(const char * weekOfMonth)
{
  for (int i = 0; i < 5; i++)
  {  
    if (strcmp((char *)timeNameHelper.weekOfMonth[i], weekOfMonth) == 0)
    {
      return i;
    }   
  }
  return -1;
}

int getMonNum(const char * month)
{
  for (int i = 0; i < 12; i++)
  {  
    if (strcmp((char *)timeNameHelper.monthsOfTheYear[i], month) == 0)
    {
      return i + 1;
    }   
  }
  return -1;
}

int getDayNum(const char * day)
{
  for (int i = 0; i < 7; i++)
  {  
    if (strcmp((char *)timeNameHelper.daysOfTheWeek[i], day) == 0)
    {
      return i + 1;
    }   
  }
  return -1;
}





// Scan for available Wifi networks
// print result als simple list
void scan_WIFI() 
{
      Serial.println("WiFi scan ...");
      // WiFi.scanNetworks returns the number of networks found
      int n = WiFi.scanNetworks();
      if (n == 0) {
          Serial.println("[ERR] no networks found");
      } else {
          
          Serial.printf("[OK] %i networks found:\n", n);       
          for (int i = 0; i < n; ++i) {
              // Print SSID for each network found
              Serial.printf("  %i: ",i+1);
              Serial.println(WiFi.SSID(i));
              delay(10);
          }
      }
}

// establish the connection to an Wifi Access point
//boolean connect_Wifi(const char * ssid, const char * password)
boolean connect_Wifi(const char *ssid, const char * password)
{
  // Establish connection to the specified network until success.
  // Important to disconnect in case that there is a valid connection
  WiFi.disconnect();
  Serial.println("Connecting to ");
  Serial.println(ssid);
  delay(500);
  //Start connecting (done by the ESP in the background)
  
  #if USE_WIFI_STATIC_IP == 1
  IPAddress presetIp(192, 168, 1, 83);
  IPAddress presetGateWay(192, 168, 1, 1);
  IPAddress presetSubnet(255, 255, 255, 0);
  IPAddress presetDnsServer1(8,8,8,8);
  IPAddress presetDnsServer2(8,8,4,4);

  WiFi.config(presetIp, presetGateWay, presetDnsServer1, presetDnsServer2);
  #endif

  WiFi.begin(ssid, password, 6);
  // read wifi Status
  wl_status_t wifi_Status = WiFi.status();  
  int n_trials = 0;
  // loop while waiting for Wifi connection
  // run only for 5 trials.
  while (wifi_Status != WL_CONNECTED && n_trials < 5) {
    // Check periodicaly the connection status using WiFi.status()
    // Keep checking until ESP has successfuly connected
    // or maximum number of trials is reached
    wifi_Status = WiFi.status();
    n_trials++;
    switch(wifi_Status){
      case WL_NO_SSID_AVAIL:
          Serial.println("[ERR] SSID not available");
          break;
      case WL_CONNECT_FAILED:
          Serial.println("[ERR] Connection failed");
          break;
      case WL_CONNECTION_LOST:
          Serial.println("[ERR] Connection lost");
          break;
      case WL_DISCONNECTED:
          Serial.println("[ERR] WiFi disconnected");
          break;
      case WL_IDLE_STATUS:
          Serial.println("[ERR] WiFi idle status");
          break;
      case WL_SCAN_COMPLETED:
          Serial.println("[OK] WiFi scan completed");
          break;
      case WL_CONNECTED:
          Serial.println("[OK] WiFi connected");
          break;
      default:
          Serial.println("[ERR] unknown Status");
          break;
    }
    delay(500);
  }
  if(wifi_Status == WL_CONNECTED){
    // connected
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    return true;
  } else {
    // not connected
    Serial.println("");
    Serial.println("[ERR] unable to connect Wifi");
    return false;
  }
}

String floToStr(float value)
{
  char buf[10];
  sprintf(buf, "%.1f", (roundf(value * 10.0))/10.0);
  return String(buf);
}

float ReadAnalogSensor(int pSensorIndex)
{
#ifndef USE_SIMULATED_SENSORVALUES
            // Use values read from an analog source
            // Change the function for each sensor to your needs

            double theRead = MAGIC_NUMBER_INVALID;

            if (analogSensorMgr.HasToBeRead(pSensorIndex, dateTimeUTCNow))
            {                     
              switch (pSensorIndex)
              {
                case 0:
                    {
                        float temp_hum_val[2] = {0};
                        if (true)
                        //if (!dht.readTempAndHumidity(temp_hum_val))
                        {
                            analogSensorMgr.SetReadTimeAndValues(pSensorIndex, dateTimeUTCNow, temp_hum_val[1], temp_hum_val[0], MAGIC_NUMBER_INVALID);
                            
                            theRead = temp_hum_val[1];
                            // Take theRead (nearly) 0.0 as invalid
                            // (if no sensor is connected the function returns 0)                        
                            if (!(theRead > - 0.00001 && theRead < 0.00001))
                            {      
                                theRead += SENSOR_1_OFFSET;                                                       
                            }
                            else
                            {
                              theRead = MAGIC_NUMBER_INVALID;
                            }                            
                        }                                                           
                    }
                    break;

                case 1:
                    {
                      // Here we look if the temperature sensor was updated in this loop
                      // If yes, we can get the measured humidity value from the index 0 sensor
                      AnalogSensor tempSensor = analogSensorMgr.GetSensorDates(0);
                      if (tempSensor.LastReadTime.operator==(dateTimeUTCNow))
                      {
                          analogSensorMgr.SetReadTimeAndValues(pSensorIndex, dateTimeUTCNow, tempSensor.Value_1, tempSensor.Value_2, MAGIC_NUMBER_INVALID);
                          theRead = tempSensor.Value_2;
                            // Take theRead (nearly) 0.0 as invalid
                            // (if no sensor is connected the function returns 0)                        
                            if (!(theRead > - 0.00001 && theRead < 0.00001))
                            {      
                                theRead += SENSOR_2_OFFSET;                                                       
                            }
                            else
                            {
                              theRead = MAGIC_NUMBER_INVALID;
                            }                          
                      }                
                    }
                    break;
                case 2:
                    {
                        // Here we do not send a sensor value but the state of the upload counter
                        // Upload counter, limited to max. value of 1399
                        //theRead = (insertCounterAnalogTable % 1399) / 10.0 ;

                        // Alternative                  
                        
                        // Read the light sensor (not used here, collumn is used as upload counter)
                        //theRead = analogRead(WIO_LIGHT);
                        theRead = 20.0; // dummy
                        theRead = map(theRead, 0, 1023, 0, 100);
                        theRead = theRead < 0 ? 0 : theRead > 100 ? 100 : theRead;
                                                                    
                    }
                    break;
                case 3:
                    /*                
                    {
                        // Here we do not send a sensor value but the last reset cause
                        // Read the last reset cause for dignostic purpose 
                        theRead = lastResetCause;                        
                    }
                    */

                    // Read the accelerometer (not used here)
                    // First experiments, don't work well
                    /*
                    {
                        ImuSampleValues sampleValues;
                        sampleValues.X_Read = lis.getAccelerationX();
                        sampleValues.Y_Read = lis.getAccelerationY();
                        sampleValues.Z_Read = lis.getAccelerationZ();
                        imuManagerWio.SetNewImuReadings(sampleValues);

                        theRead = imuManagerWio.GetVibrationValue();                                                                 
                    }
                    */
                    theRead = 10.0; // dummy
                     
                    
                    break;
              }
            }          
            return theRead ;
#endif

#ifdef USE_SIMULATED_SENSORVALUES
      #ifdef USE_TEST_VALUES
            // Here you can select that diagnostic values (for debugging)
            // are sent to your storage table
            double theRead = MAGIC_NUMBER_INVALID;
            switch (pSensorIndex)
            {
                case 0:
                    {
                        theRead = timeNtpUpdateCounter;
                        theRead = theRead / 10; 
                    }
                    break;

                case 1:
                    {                       
                        theRead = sysTimeNtpDelta > 140 ? 140 : sysTimeNtpDelta < - 40 ? -40 : (double)sysTimeNtpDelta;                      
                    }
                    break;
                case 2:
                    {
                        theRead = insertCounterAnalogTable;
                        theRead = theRead / 10;                      
                    }
                    break;
                case 3:
                    {
                        theRead = lastResetCause;                       
                    }
                    break;
            }

            return theRead ;

  

        #endif
            
            onOffSwitcherWio.SetActive();
            // Only as an example we here return values which draw a sinus curve            
            int frequDeterminer = 4;
            int y_offset = 1;
            // different frequency and y_offset for aIn_0 to aIn_3
            if (pSensorIndex == 0)
            { frequDeterminer = 4; y_offset = 1; }
            if (pSensorIndex == 1)
            { frequDeterminer = 8; y_offset = 10; }
            if (pSensorIndex == 2)
            { frequDeterminer = 12; y_offset = 20; }
            if (pSensorIndex == 3)
            { frequDeterminer = 16; y_offset = 30; }
             
            int secondsOnDayElapsed = dateTimeUTCNow.second() + dateTimeUTCNow.minute() * 60 + dateTimeUTCNow.hour() *60 *60;

            // RoSchmi
            switch (pSensorIndex)
            {
              case 3:
              {

                return (double)9.9;   // just something for now
                //return lastResetCause;
              }
              break;
            
              case 2:
              { 
                uint32_t showInsertCounter = insertCounterAnalogTable % 50;               
                double theRead = ((double)showInsertCounter) / 10;
                return theRead;
              }
              break;
              case 0:
              case 1:
              {
                return roundf((float)25.0 * (float)sin(PI / 2.0 + (secondsOnDayElapsed * ((frequDeterminer * PI) / (float)86400)))) / 10  + y_offset;          
              }
              break;
              default:
              {
                return 0;
              }
            }
  #endif
}
void createSampleTime(DateTime dateTimeUTCNow, int timeZoneOffsetUTC, char * sampleTime)
{
  int hoursOffset = timeZoneOffsetUTC / 60;
  int minutesOffset = timeZoneOffsetUTC % 60;
  char sign = timeZoneOffsetUTC < 0 ? '-' : '+';
  char TimeOffsetUTCString[10];
  sprintf(TimeOffsetUTCString, " %c%03i", sign, timeZoneOffsetUTC);
  TimeSpan timespanOffsetToUTC = TimeSpan(0, hoursOffset, minutesOffset, 0);
  DateTime newDateTime = dateTimeUTCNow + timespanOffsetToUTC;
  sprintf(sampleTime, "%02i/%02i/%04i %02i:%02i:%02i%s",newDateTime.month(), newDateTime.day(), newDateTime.year(), newDateTime.hour(), newDateTime.minute(), newDateTime.second(), TimeOffsetUTCString);
}
 
void makeRowKey(DateTime actDate,  az_span outSpan, size_t *outSpanLength)
{
  // formatting the RowKey (= reverseDate) this way to have the tables sorted with last added row upmost
  char rowKeyBuf[20] {0};

  sprintf(rowKeyBuf, "%4i%02i%02i%02i%02i%02i", (10000 - actDate.year()), (12 - actDate.month()), (31 - actDate.day()), (23 - actDate.hour()), (59 - actDate.minute()), (59 - actDate.second()));
  az_span retValue = az_span_create_from_str((char *)rowKeyBuf);
  az_span_copy(outSpan, retValue);
  *outSpanLength = retValue._internal.size;         
}

void makePartitionKey(const char * partitionKeyprefix, bool augmentWithYear, DateTime dateTime, az_span outSpan, size_t *outSpanLength)
{
  // if wanted, augment with year and month (12 - month for right order)                    
  char dateBuf[20] {0};
  sprintf(dateBuf, "%s%d-%02d", partitionKeyprefix, (dateTime.year()), (12 - dateTime.month()));                  
  az_span ret_1 = az_span_create_from_str((char *)dateBuf);
  az_span ret_2 = az_span_create_from_str((char *)partitionKeyprefix);                       
  if (augmentWithYear == true)
  {
    az_span_copy(outSpan, ret_1);            
    *outSpanLength = ret_1._internal.size; 
  }
    else
  {
    az_span_copy(outSpan, ret_2);
    *outSpanLength = ret_2._internal.size;
  }    
}

az_http_status_code createTable(CloudStorageAccount *pAccountPtr, X509Certificate pCaCert, const char * pTableName)
{ 

  #if TRANSPORT_PROTOCOL == 1
    static WiFiClientSecure wifi_client;
  #else
    static WiFiClient wifi_client;
  #endif

    #if TRANSPORT_PROTOCOL == 1
    wifi_client.setCACert(myX509Certificate);
    //wifi_client.setCACert(baltimore_corrupt_root_ca);
  #endif

  #if WORK_WITH_WATCHDOG == 1
      esp_task_wdt_reset();
  #endif

  UBaseType_t  watermarkEntityInsert_1 = uxTaskGetStackHighWaterMark(NULL);
  Serial.print(F("Watermark for core_1 before creating table (create) is: "));
  Serial.println(watermarkEntityInsert_1);
  
  // RoSchmi
  TableClient table(pAccountPtr, pCaCert,  httpPtr, &wifi_client, bufferStorePtr);

  watermarkEntityInsert_1 = uxTaskGetStackHighWaterMark(NULL);
  Serial.print(F("Watermark for core_1 after creating table (create) is: "));
  Serial.println(watermarkEntityInsert_1);

  // Create Table
  az_http_status_code statusCode = table.CreateTable(pTableName, dateTimeUTCNow, ContType::contApplicationIatomIxml, AcceptType::acceptApplicationIjson, returnContent, false);
  
   // RoSchmi for tests: to simulate failed upload
   //az_http_status_code   statusCode = AZ_HTTP_STATUS_CODE_UNAUTHORIZED;

  char codeString[35] {0};
  if ((statusCode == AZ_HTTP_STATUS_CODE_CONFLICT) || (statusCode == AZ_HTTP_STATUS_CODE_CREATED))
  {
    #if WORK_WITH_WATCHDOG == 1
      esp_task_wdt_reset();
    #endif
   
      sprintf(codeString, "%s %i", "Table available: ", az_http_status_code(statusCode));  
      Serial.println((char *)codeString);
  
  }
  else
  {
    
    
      sprintf(codeString, "%s %i", "Table Creation failed: ", az_http_status_code(statusCode));   
      Serial.println((char *)codeString);
 
    delay(1000);
    //NVIC_SystemReset();     // Makes Code 64  
  }
  UBaseType_t watermarkTableCreate_1 = uxTaskGetStackHighWaterMark(NULL);
  Serial.print(F("Watermark for core_1 after first Table Create is: "));
  Serial.println(watermarkTableCreate_1);

return statusCode;
}

az_http_status_code insertTableEntity(CloudStorageAccount *pAccountPtr,  X509Certificate pCaCert, const char * pTableName, TableEntity pTableEntity, char * outInsertETag)
{ 
  #if TRANSPORT_PROTOCOL == 1
    static WiFiClientSecure wifi_client;
  #else
    static WiFiClient wifi_client;
  #endif
  
  #if TRANSPORT_PROTOCOL == 1
    wifi_client.setCACert(myX509Certificate); 
  #endif
  
  /*
  // For tests: Try second upload with corrupted certificate to provoke failure
  #if TRANSPORT_PROTOCOL == 1
    wifi_client.setCACert(myX509Certificate);
    if (insertCounterAnalogTable == 2)
    {
      wifi_client.setCACert(baltimore_corrupt_root_ca);
    }
  #endif
  */

  // RoSchmi
  TableClient table(pAccountPtr, pCaCert,  httpPtr, &wifi_client, bufferStorePtr);

  #if WORK_WITH_WATCHDOG == 1
      esp_task_wdt_reset();
  #endif
  
  DateTime responseHeaderDateTime = DateTime();   // Will be filled with DateTime value of the resonse from Azure Service

  // Insert Entity
  az_http_status_code statusCode = table.InsertTableEntity(pTableName, dateTimeUTCNow, pTableEntity, (char *)outInsertETag, &responseHeaderDateTime, ContType::contApplicationIatomIxml, AcceptType::acceptApplicationIjson, ResponseType::returnContent, false);
  
  #if WORK_WITH_WATCHDOG == 1
      esp_task_wdt_reset();
  #endif

  lastResetCause = 0;
  tryUploadCounter++;

   // RoSchmi for tests: to simulate failed upload
  //az_http_status_code   statusCode = AZ_HTTP_STATUS_CODE_UNAUTHORIZED;
  
  if ((statusCode == AZ_HTTP_STATUS_CODE_NO_CONTENT) || (statusCode == AZ_HTTP_STATUS_CODE_CREATED))
  {
    //sendResultState = true;
     
      char codeString[35] {0};
      sprintf(codeString, "%s %i", "Entity inserted: ", az_http_status_code(statusCode));
      Serial.println((char *)codeString);
    
    
    
    #if UPDATE_TIME_FROM_AZURE_RESPONSE == 1    // System time shall be updated from the DateTime value of the response ?
    
    dateTimeUTCNow = responseHeaderDateTime;
    
    char buffer[] = "Azure-Utc: YYYY-MM-DD hh:mm:ss";
    dateTimeUTCNow.toString(buffer);

    
    Serial.println((char *)buffer);
    
    #endif   
  }
  else            // request failed
  {               // note: internal error codes from -1 to -11 were converted for tests to error codes 401 to 411 since
                  // negative values cannot be returned as 'az_http_status_code' 

    //failedUploadCounter++;
    //sendResultState = false;
    lastResetCause = 100;      // Set lastResetCause to arbitrary value of 100 to signal that post request failed
    
    
      char codeString[35] {0};
      sprintf(codeString, "%s %i", "Insertion failed: ", az_http_status_code(statusCode));
      Serial.println((char *)codeString);
  
    
    #if REBOOT_AFTER_FAILED_UPLOAD == 1   // When selected in config.h -> Reboot through SystemReset after failed uoload

        #if TRANSPORT_PROTOCOL == 1
          
          // The outcommended code resets the WiFi module (did not solve problem)
          //pinMode(RTL8720D_CHIP_PU, OUTPUT); 
          //digitalWrite(RTL8720D_CHIP_PU, LOW); 
          //delay(500); 
          //digitalWrite(RTL8720D_CHIP_PU, HIGH);  
          //delay(500);

          NVIC_SystemReset();     // Makes Code 64
        #endif
        #if TRANSPORT_PROTOCOL == 0     // for http requests reboot after the second, not the first, failed request
          if(failedUploadCounter > 1)
          {
            NVIC_SystemReset();     // Makes Code 64
          }
    #endif

    #endif

    #if WORK_WITH_WATCHDOG == 1
      esp_task_wdt_reset();  
    #endif
    delay(1000);
  }
  
  return statusCode;
}

