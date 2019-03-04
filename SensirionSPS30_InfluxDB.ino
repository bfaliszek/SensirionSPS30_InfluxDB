/*
    =========================  SPS30 connection =================================

    Using serial port1, setting the RX-pin(13/D1) and TX-pin(15/D2)
    Different setup can be configured in the sketch.

    SPS30 pin     ESP32
    1 VCC -------- VUSB
    2 RX  -------- TX  pin 13
    3 TX  -------- RX  pin 25
    4 Select      (NOT CONNECTED)
    5 GND -------- GND

    NO level shifter is needed as the SPS30 is TTL 5V and LVTTL 3.3V compatible

*/
#ifdef ARDUINO_ARCH_ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <Wire.h>
#include "src/sps30.h" // https://github.com/paulvha/sps30 // 4.03.2019 - 1.3.2 // Copyright (c) - Paul van Haastrecht
#include "src/ESPinfluxdb.h" // https://github.com/hwwong/ESP_influxdb // 3.02.2019
ESP8266WiFiMulti WiFiMulti;
#elif defined ARDUINO_ARCH_ESP32
#include <WiFi.h>
#include "src/sps30.h" // https://github.com/paulvha/sps30 // 4.03.2019 - 1.3.2 // Copyright (c) - Paul van Haastrecht
#include "src/ESPinfluxdb.h" // https://github.com/hwwong/ESP_influxdb // 3.02.2019
#endif

/////////////////////////////////////////////////////////////

#define SP30_COMMS SERIALPORT1

/////////////////////////////////////////////////////////////
/* define RX and TX pin for softserial and Serial1 on ESP32
   can be set to zero if not applicable / needed           */
/////////////////////////////////////////////////////////////


#ifdef ARDUINO_ARCH_ESP8266
#define TX_PIN D1
#define RX_PIN D2
#elif defined ARDUINO_ARCH_ESP32
#define TX_PIN 13
#define RX_PIN 15
#endif

///////////////////////////////////////////////////////////////
/* define new AUTO Clean interval
   Will be remembered after power off

   default is 604800 seconds ~ 1 week
   0 = disable Auto Clean
   -1 = do not change current setting */
//////////////////////////////////////////////////////////////
#define AUTOCLEANINTERVAL -1

///////////////////////////////////////////////////////////////
/* Perform a clean NOW ?
    1 = yes
    0 = NO */
//////////////////////////////////////////////////////////////
#define PERFORMCLEANNOW 0

/////////////////////////////////////////////////////////////
/* define driver debug
   0 : no messages
   1 : request sending and receiving
   2 : request sending and receiving + show protocol errors */
//////////////////////////////////////////////////////////////
#define DEBUG 0

// create constructor
SPS30 sps30;

// WiFi – Config
const char* WiFi_SSID = "SSID";
const char* WiFi_Password = "PASSWORD";

// InfluxDB – Config
#define INFLUXDB_HOST "IP_INFLUXDB"
#define INFLUXDB_PORT 8086

#define DATABASE  "mydb"
#define DB_USER "USER"
#define DB_PASSWORD "PASSWORD"
Influxdb influxdb(INFLUXDB_HOST, INFLUXDB_PORT);

// Sleep time – send data every XX seconds
const int sleepTime = 30;

// Your device name!
#define DEVICE_NAME "SensirionSPS30"

float PM1, PM2, PM4, PM10;

void setup() {

  Serial.begin(115200);

  Serial.println(F("Trying to connect"));
  
    // set driver debug level
    sps30.EnableDebugging(DEBUG);

    // set pins to use for softserial and Serial1 on ESP32
    if (TX_PIN != 0 && RX_PIN != 0) sps30.SetSerialPin(RX_PIN, TX_PIN);

    // Begin communication channel;
    if (sps30.begin(SP30_COMMS) == false) {
      Errorloop("could not initialize communication channel.", 0);
    }

    // check for SPS30 connection
    if (sps30.probe() == false) {
      Errorloop("could not probe / connect with SPS30.", 0);
    }
    else
      Serial.println(F("Detected SPS30."));

    // reset SPS30 connection
    if (sps30.reset() == false) {
      Errorloop("could not reset.", 0);
    }

    // read device info
    GetDeviceInfo();

    // do Auto Clean interval
    SetAutoClean();

    // start measurement
    if (sps30.start() == true)
      Serial.println(F("Measurement started"));
    else
      Errorloop("Could NOT start measurement", 0);

    // clean now requested
    if (PERFORMCLEANNOW) {
      // clean now
      if (sps30.clean() == true)
        Serial.println(F("fan-cleaning manually started"));
      else
        Serial.println(F("Could NOT manually start fan-cleaning"));
    }

    if (SP30_COMMS == I2C_COMMS) {
      if (sps30.I2C_expect() == 4)
        Serial.println(F(" !!! Due to I2C buffersize only the SPS30 MASS concentration is available !!! \n"));
    }
  
#ifdef ARDUINO_ARCH_ESP8266
  WiFi.mode(WIFI_STA);
  WiFiMulti.addAP(WiFi_SSID, WiFi_Password);
  Serial.println();
  Serial.print("Waiting for WiFi... ");

  while (WiFiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
#elif defined ARDUINO_ARCH_ESP32
  WiFi.mode(WIFI_MODE_STA);
  WiFi.begin(WiFi_SSID, WiFi_Password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print("Attempting to connect to WPA SSID: ");
    Serial.println(WiFi_SSID);
    // wait 5 seconds for connection:
    Serial.print("Status = ");
    Serial.println(WiFi.status());
    Serial.println(" ");
    delay(500);
  }
#endif

  if (influxdb.opendb(DATABASE, DB_USER, DB_PASSWORD) != DB_SUCCESS) {
    Serial.println("Connecting to database failed");
  }

}

void loop() {

  read_pm();

  dbMeasurement row(DEVICE_NAME);
  row.addField("pm1", (PM1));
  row.addField("pm2.5", (PM2));
  row.addField("pm4", (PM4));
  row.addField("pm10", (PM10));
  if (influxdb.write(row) == DB_SUCCESS) {
    Serial.println("Data send to InfluxDB\n\n");
  }
  row.empty();

  delay(sleepTime * 1000);
}

/**
   @brief : read and display device info
*/
void GetDeviceInfo()
{
  char buf[32];
  uint8_t ret;

  //try to read serial number
  ret = sps30.GetSerialNumber(buf, 32);
  if (ret == ERR_OK) {
    Serial.print(F("Serial number : "));
    if (strlen(buf) > 0)  Serial.println(buf);
    else Serial.println(F("not available"));
  }
  else
    ErrtoMess("could not get serial number", ret);

  // try to get product name
  ret = sps30.GetProductName(buf, 32);
  if (ret == ERR_OK)  {
    Serial.print(F("Product name  : "));

    if (strlen(buf) > 0)  Serial.println(buf);
    else Serial.println(F("not available"));
  }
  else
    ErrtoMess("could not get product name.", ret);

  // try to get article code
  ret = sps30.GetArticleCode(buf, 32);
  if (ret == ERR_OK)  {
    Serial.print(F("Article code  : "));

    if (strlen(buf) > 0)  Serial.println(buf);
    else Serial.println(F("not available"));
  }
  else
    ErrtoMess("could not get Article code .", ret);
}

/*
   @brief: Get & Set new Auto Clean Interval
*/
void SetAutoClean()
{
  uint32_t interval;
  uint8_t ret;

  // try to get interval
  ret = sps30.GetAutoCleanInt(&interval);
  if (ret == ERR_OK) {
    Serial.print(F("Current Auto Clean interval: "));
    Serial.print(interval);
    Serial.println(F(" seconds"));
  }
  else
    ErrtoMess("could not get clean interval.", ret);

  // only if requested
  if (AUTOCLEANINTERVAL == -1) {
    Serial.println(F("No Auto Clean interval change requested."));
    return;
  }

  // try to set interval
  interval = AUTOCLEANINTERVAL;
  ret = sps30.SetAutoCleanInt(interval);
  if (ret == ERR_OK) {
    Serial.print(F("Auto Clean interval now set : "));
    Serial.print(interval);
    Serial.println(F(" seconds"));
  }
  else
    ErrtoMess("could not set clean interval.", ret);

  // try to get interval
  ret = sps30.GetAutoCleanInt(&interval);
  if (ret == ERR_OK) {
    Serial.print(F("Current Auto Clean interval: "));
    Serial.print(interval);
    Serial.println(F(" seconds"));
  }
  else
    ErrtoMess("could not get clean interval.", ret);
}

/*
   @brief : read and display all values
*/
bool read_pm()
{
  static bool header = true;
  uint8_t ret, error_cnt = 0;
  struct sps_values val;

  // loop to get data
  do {

    ret = sps30.GetValues(&val);

    // data might not have been ready
    if (ret == ERR_DATALENGTH) {

      if (error_cnt++ > 3) {
        ErrtoMess("Error during reading values: ", ret);
        return (false);
      }
      delay(1000);
    }

    // if other error
    else if (ret != ERR_OK) {
      ErrtoMess("Error during reading values: ", ret);
      return (false);
    }

  } while (ret != ERR_OK);

  PM1 = val.MassPM1;
  PM2 = val.MassPM2;
  PM4 = val.MassPM4;
  PM10 = val.MassPM10;

  Serial.println("PM1: " + String(PM1) + " μg/m3");
  Serial.println("PM2.5: " + String(PM2) + " μg/m3");
  Serial.println("PM4: " + String(PM4) + " μg/m3");
  Serial.println("PM10: " + String(PM10) + " μg/m3\n");
}

/**
  @brief : continued loop after fatal error
  @param mess : message to display
  @param r : error code

  if r is zero, it will only display the message
*/
void Errorloop(char *mess, uint8_t r)
{
  if (r) ErrtoMess(mess, r);
  else Serial.println(mess);
  Serial.println(F("Program on hold"));
  for (;;) delay(100000);
}

/**
    @brief : display error message
    @param mess : message to display
    @param r : error code

*/
void ErrtoMess(char *mess, uint8_t r)
{
  char buf[80];

  Serial.print(mess);

  sps30.GetErrDescription(r, buf, 80);
  Serial.println(buf);
}
