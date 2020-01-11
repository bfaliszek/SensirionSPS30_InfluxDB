#include <Arduino.h>
#include "src/sps30.h"                                     // https://github.com/paulvha/sps30 // updated - 10.01.2020
#include "src/InfluxDb.h"                                  // https://github.com/tobiasschuerg/ESP8266_Influx_DB // updated - 10.01.2020

int READ_DATA_EVERY_SECONDS = 20; // default 20 sec
int SEND_DATA_EVERY_SECONDS = 60; // default 60 sec

#define INFLUXDB_HOST "XXX"
#define INFLUXDB_DATABASE "XXX"
#define INFLUXDB_USER "XXX"
#define INFLUXDB_PASS "XXX"
#define DEVICE_NAME "XXX"

// WiFi Config
#define WiFi_SSID "SSID"
#define WiFi_Password "PASSWORD"

#ifdef ARDUINO_ARCH_ESP32
#define RX_PIN 17           // D17
#define TX_PIN 16           // D16
#else
#define RX_PIN 4            // D2
#define TX_PIN 5            // D1
#endif

#ifdef ARDUINO_ARCH_ESP32
#include <WiFi.h>
#include <HTTPClient.h>
#include <HardwareSerial.h>
#else
#include <Wire.h>
#include <ESP8266WiFi.h>
#endif

#define SP30_COMMS SERIALPORT1

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

unsigned long previousMillis_READING, previousMillis_SENDING = 0;

float SPS_PM1, SPS_PM2, SPS_PM4, SPS_PM10;

unsigned long READING_INTERVAL = READ_DATA_EVERY_SECONDS * 1000;
unsigned long SENDING_INTERVAL = SEND_DATA_EVERY_SECONDS * 1000;

Influxdb influx(INFLUXDB_HOST);

void setup()
{
  Serial.begin(115000);                                    // Device to serial monitor feedback

  WiFi.begin(WiFi_SSID, WiFi_Password);
  Serial.println();
  Serial.print("Waiting for WiFi... ");
  while (WiFi.status() != WL_CONNECTED) {
#ifdef ARDUINO_ARCH_ESP32
    WiFi.begin(WiFi_SSID, WiFi_Password);
#endif
    Serial.println(".");
    delay(500);
  }
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  influx.setDbAuth(INFLUXDB_DATABASE, INFLUXDB_USER, INFLUXDB_PASS);

#ifdef ARDUINO_ARCH_ESP32
  disableCore0WDT();
  //disableCore1WDT(); // ESP32-solo-1 so only CORE0!
#endif

  Serial.println(F("Trying to connect to SPS30..."));

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
    Serial.println(F("\nMeasurement started"));
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
}

void loop()
{
  yield();

  unsigned long currentMillis_READING = millis();
  if (currentMillis_READING - previousMillis_READING >= READING_INTERVAL) {
    read_pm();
    previousMillis_READING = millis();
  }

  unsigned long currentMillis_SENDING = millis();
  if (currentMillis_SENDING - previousMillis_SENDING >= SENDING_INTERVAL) {
    send_data();
    previousMillis_SENDING = millis();
  }

}

void send_data() {
  Serial.println("\nSending data...");
  InfluxData row(DEVICE_NAME);
  row.addValue("PM1", (float(SPS_PM1)));
  row.addValue("PM2.5", (float(SPS_PM2)));
  row.addValue("PM4", (float(SPS_PM4)));
  row.addValue("PM10", (float(SPS_PM10)));
  influx.write(row);
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

  SPS_PM1 = val.MassPM1;
  SPS_PM2 = val.MassPM2;
  SPS_PM4 = val.MassPM4;
  SPS_PM10 = val.MassPM10;

  Serial.println("PM1: " + String(SPS_PM1) + " μg/m3");
  Serial.println("PM2.5: " + String(SPS_PM2) + " μg/m3");
  Serial.println("PM4: " + String(SPS_PM4) + " μg/m3");
  Serial.println("PM10: " + String(SPS_PM10) + " μg/m3\n");
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
