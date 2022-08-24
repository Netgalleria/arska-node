/*
(C) Netgalleria Oy, Olli Rinne, 2021-2022

Resource files (see data subfolder):
- arska.js - web UI Javascript routines
- style.css - web UI styles
- admin_template.html - html template file for admin web UI
- channels_template.html - template file for channel configuration
- dashboard_template.html - html template file for dashboard UI
- inputs_template.htm - htmll template for services configuration UI
- js/arska_tmpl-js - main javascript code template //TODO:separate variable(constant) and code
- js/jquery-3.6.0.min.js - jquery library
- data/version.txt - file system version info
- data/template-list.json - list of rule templates
- data/templates.json - rule template definitions


 build options defined in platform.ini
#define ESP32 //if ESP32 platform
#define CHANNEL_COUNT 5 //number of channels, GPIO+virtual
#define CHANNEL_CONDITIONS_MAX 4 //max conditions/rules per channel
#define ONEWIRE_DATA_GPIO 13
#define INFLUX_REPORT_ENABLED
#define SENSOR_DS18B20_ENABLED // DS18B20 funtionality
#define RTC_DS3231_ENABLED //real time clock functionality
#define VARIABLE_SOURCE_ENABLED  // RFU for source/replica mode

branch devel 21.8.2022
*/

#define EEPROM_CHECK_VALUE 100920
#define DEBUG_MODE

// #include <improv.h> // testing improv for wifi settings
#include "version.h"
const char compile_date[] = __DATE__ " " __TIME__;
char version_fs[35];

#include <Arduino.h>
#include <math.h> //round
#include <EEPROM.h>
#include <LittleFS.h>
#include "WebAuthentication.h"

#ifdef ESP32 // tested only  with ESP32
#include <WiFi.h>
#include <AsyncTCP.h>
#include <HTTPClient.h>
#elif defined(ESP8266) // not fully tested with ESP8266
//#pragma message("ESP8266 version")
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESP8266HTTPClient.h>
#endif

#include <ESPAsyncWebServer.h>

#include <time.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

// features enabled
// moved to platformio.ini build parameters
#define MAX_DS18B20_SENSORS 3
#define SENSOR_VALUE_EXPIRE_TIME 1200
#define METER_SHELLY3EM_ENABLED
#define INVERTER_FRONIUS_SOLARAPI_ENABLED // can read Fronius inverter solarapi
#define INVERTER_SMA_MODBUS_ENABLED       // can read SMA inverter Modbus TCP

// TODO: replica mode will be probably removed later
#define VARIABLE_SOURCE_ENABLED //!< this calculates variables (not just replica) only ESP32
#define VARIABLE_MODE_SOURCE 0
#define VARIABLE_MODE_REPLICA 1

#define TARIFF_VARIABLES_FI // add Finnish tarifs (yösähkö,kausisähkö) to variables

#define OTA_UPDATE_ENABLED

#define eepromaddr 0
#define WATT_EPSILON 50

const char *default_http_password PROGMEM = "arska";
const char *required_fs_version PROGMEM = "0.91.0";
const char *price_data_filename PROGMEM = "/data/price-data.json";
const char *variables_filename PROGMEM = "/data/variables.json";
const char *fcst_filename PROGMEM = "/data/fcst.json"; // TODO: we need it only for debugging?, remove?
const char *wifis_filename PROGMEM = "/wifis.json";
const char *template_filename PROGMEM = "/data/templates.json";

const char *ntp_server_1 PROGMEM = "europe.pool.ntp.org";
const char *ntp_server_2 PROGMEM = "time.google.com";
const char *ntp_server_3 PROGMEM = "time.windows.com";

#define FORCED_RESTART_DELAY 600 // If cannot create Wifi connection, goes to AP mode for 600 sec and restarts

#define MSG_TYPE_INFO 0
#define MSG_TYPE_WARN 1
#define MSG_TYPE_ERROR 2
#define MSG_TYPE_FATAL 3

#define ACCEPTED_TIMESTAMP_MINIMUM 1656200000

struct variable_st
{
  byte id;
  char code[20];
  byte type;
  long val_l;
};

time_t now; // this is the epoch
tm tm_struct;

// for timezone https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv

time_t forced_restart_ts = 0; // if wifi in forced ap-mode restart automatically to reconnect/start
bool wifi_in_setup_mode = false;
bool wifi_connection_succeeded = false;
time_t last_wifi_connect_tried = 0;
#define WIFI_RECONNECT_INTERVAL 300
bool clock_set = false; // true if we have get (more or less) correct time from net or rtc

#define ERROR_MSG_LEN 100
#define DEBUG_FILE_ENABLED
#ifdef DEBUG_FILE_ENABLED
const char *debug_filename PROGMEM = "/data/log.txt";
#endif

struct msg_st
{
  byte type; // 0 info, 1-warn, 2 - error, 3-fatal
  time_t ts;
  char msg[ERROR_MSG_LEN];
};

msg_st last_msg;

/**
 * @brief Utility, writes date string generated from a time stamp to memory buffer
 *
 * @param tsp
 * @param out_str
 */
void ts_to_date_str(time_t *tsp, char *out_str)
{
  tm tm_local;
  gmtime_r(tsp, &tm_local);
  sprintf(out_str, "%04d-%02d-%02dT%02d:%02d:00Z", tm_local.tm_year + 1900, tm_local.tm_mon + 1, tm_local.tm_mday, tm_local.tm_hour, tm_local.tm_min);
}

// message structure, currently only one/the last message is stored
void log_msg(byte type, const char *msg, bool write_to_file = false)
{
  memset(last_msg.msg, 0, ERROR_MSG_LEN);
  strncpy(last_msg.msg, msg, (ERROR_MSG_LEN - 1));
  last_msg.type = type;
  time(&last_msg.ts);

#ifdef DEBUG_FILE_ENABLED
  char datebuff[30];
  char text_message[(ERROR_MSG_LEN + 30)];
  if (write_to_file)
  {
    File log_file = LittleFS.open(debug_filename, "a");
    if (!log_file)
    {
      Serial.println(F("Cannot open the log file."));
      return;
    }
    ts_to_date_str(&last_msg.ts, datebuff);
    log_file.printf("%s %d %s\n", datebuff, (int)type, msg);
    // debug debug
    Serial.println("Writing to log file:");
    Serial.printf("%s %d %s\n", datebuff, (int)type, msg);
  }
#endif
}

/**
 * @brief System goes to  AP mode  if it cannot connect to existing wifi, but restart automatically.
 * @details Call to check_forced_restart checks if it is time to restart or resets delay if reset_counter == true)
 * For example if the wifi has been down and we have been disconnected, forced restart will retry connection after a while.
 *
 * @param reset_counter , if true resets counter-> automatic restart will be delayd
 */

void check_forced_restart(bool reset_counter = false)
{
  // TODO:tässä tapauksessa kello ei välttämättä ei kunnossa ellei rtc, käy läpi tapaukset
  if (!wifi_in_setup_mode) // only valid if backup ap mode (no normal wifi)
    return;

  time_t now_in_func;
  time(&now_in_func);
  if (reset_counter)
  {
    forced_restart_ts = now_in_func + FORCED_RESTART_DELAY;
  }
  else if ((forced_restart_ts < now_in_func) && ((now_in_func - forced_restart_ts) < 7200)) // check that both values are same way synched
  {
    Serial.println(F("check_forced_restart Restarting after passive period in config mode."));
    WiFi.disconnect();
    log_msg(MSG_TYPE_FATAL, PSTR("Restarting after passive period in config mode."), true);
    delay(2000);
    ESP.restart();
  }
}

AsyncWebServer server_web(80);
WiFiClient wifi_client;

// Clock functions, supports optional DS3231 RTC
bool rtc_found = false;

/**
 * @brief Set the internal clock from RTC or browser
 *
 * @param epoch  epoch (seconds in GMT)
 * @param microseconds
 */
void setInternalTime(uint64_t epoch = 0, uint32_t us = 0)
{
  struct timeval tv;
  tv.tv_sec = epoch;
  tv.tv_usec = us;
  settimeofday(&tv, NULL);
}

const int force_up_hours[] = {0, 1, 2, 4, 8, 12, 24}; //!< dashboard forced channel duration times
const int price_variable_blocks[] = {9, 24};          //!< price ranks are calculated in 9 and 24 period windows

#define NETTING_PERIOD_MIN 60 //!< Netting time in minutes, (in Finland) 60 -> 15 minutes 2023
#define NETTING_PERIOD_SEC (NETTING_PERIOD_MIN * 60)
#define PRICE_PERIOD_SEC 3600
#define SECONDS_IN_DAY 86400

#define PV_FORECAST_HOURS 24 //!< solar forecast consist of this many hours

#define MAX_PRICE_PERIODS 48              //!< number of price period in the memory array
#define VARIABLE_LONG_UNKNOWN -2147483648 //!< variable with this value is undefined

long prices[MAX_PRICE_PERIODS];
bool prices_initiated = false;
time_t prices_first_period = 0;

// API
const char *host_prices PROGMEM = "transparency.entsoe.eu"; //!< EntsoE reporting server for day-ahead prices
const char *entsoe_ca_filename PROGMEM = "/data/sectigo_ca.pem";

const char *fcst_url_base PROGMEM = "http://www.bcdcenergia.fi/wp-admin/admin-ajax.php?action=getChartData"; //<! base url for Solar forecast from BCDC

String url_base = "/api?documentType=A44&processType=A16";
// API documents: https://transparency.entsoe.eu/content/static_content/Static%20content/web%20api/Guide.html#_areas

tm tm_struct_g;
time_t next_query_price_data = 0;
time_t next_query_fcst_data = 0;

// https://transparency.entsoe.eu/api?securityToken=XXX&documentType=A44&In_Domain=10YFI-1--------U&Out_Domain=10YFI-1--------U&processType=A16&outBiddingZone_Domain=10YCZ-CEPS-----N&periodStart=202104200000&periodEnd=202104200100
const int httpsPort = 443;

// https://werner.rothschopf.net/microcontroller/202112_arduino_esp_ntp_rtc_en.htm
int64_t getTimestamp(int year, int mon, int mday, int hour, int min, int sec)
{
  const uint16_t ytd[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};                /* Anzahl der Tage seit Jahresanfang ohne Tage des aktuellen Monats und ohne Schalttag */
  int leapyears = ((year - 1) - 1968) / 4 - ((year - 1) - 1900) / 100 + ((year - 1) - 1600) / 400; /* Anzahl der Schaltjahre seit 1970 (ohne das evtl. laufende Schaltjahr) */
  int64_t days_since_1970 = (year - 1970) * 365 + leapyears + ytd[mon - 1] + mday - 1;
  if ((mon > 2) && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)))
    days_since_1970 += 1; /* +Schalttag, wenn Jahr Schaltjahr ist */
  return sec + 60 * (min + 60 * (hour + 24 * days_since_1970));
}

#ifdef RTC_DS3231_ENABLED
#if ARDUINO_ESP8266_MAJOR < 3
#pragma message("This sketch requires at least ESP8266 Core Version 3.0.0")
#endif
#include <RTClib.h>
#include <coredecls.h>

RTC_DS3231 rtc;
/*
   ESP8266 has no timegm, so we need to create our own...

   Take a broken-down time and convert it to calendar time (seconds since the Epoch 1970)
   Expects the input value to be Coordinated Universal Time (UTC)

   Parameters and values:
   - year  [1970..2038]
   - month [1..12]  ! - start with 1 for January
   - mday  [1..31]
   - hour  [0..23]
   - min   [0..59]
   - sec   [0..59]
   Code based on https://de.wikipedia.org/wiki/Unixzeit example "unixzeit"
*/
// Utility function to convert datetime elements to epoch time

/**
 * @brief print time of RTC to Serial, debugging function
 *
 */
void printRTC()
{
  DateTime dtrtc = rtc.now(); // get date time from RTC i
  if (!dtrtc.isValid())
  {
    Serial.println(F("E103: RTC not valid"));
  }
  else
  {
    time_t newTime = getTimestamp(dtrtc.year(), dtrtc.month(), dtrtc.day(), dtrtc.hour(), dtrtc.minute(), dtrtc.second());
    Serial.print(F("RTC:"));
    Serial.print(newTime);
    Serial.print(F(", Temperature:"));
    Serial.println(rtc.getTemperature());
  }
}

/**
 * @brief set date/time of external RTC. Used only if RTC is enabled.
 *
 */
void setRTC()
{
  Serial.println(F("setRTC --> from internal time"));
  time_t now_in_func;          // this are the seconds since Epoch (1970) - seconds GMT
  tm tm;                       // the structure tm holds time information in a more convient way
  time(&now_in_func);          // read the current time and store to now_in_func
  gmtime_r(&now_in_func, &tm); // update the structure tm with the current GMT
  rtc.adjust(DateTime(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec));
}

/**
 * @brief Callback function (registered with settimeofday_cb ) called when ntp time update received, sets RTC
 *
 * @param from_sntp true if update is from sntp service
 */
void time_is_set(bool from_sntp)
{
  if (from_sntp) // needs Core 3.0.0 or higher!
  {
    Serial.println(F("The internal time is set from SNTP."));
    setRTC();
    printRTC();
  }
  else
  {
    Serial.println(F("The internal time is set."));
  }
}

/**
 * @brief Reads time from external RTC and set value to internal time
 *
 */
void getRTC()
{
  Serial.println(F("getRTC --> update internal clock"));
  DateTime dtrtc = rtc.now(); // get date time from RTC i
  if (!dtrtc.isValid())
  {
    Serial.print(F("E127: RTC not valid"));
  }
  else
  {
    time_t newTime = getTimestamp(dtrtc.year(), dtrtc.month(), dtrtc.day(), dtrtc.hour(), dtrtc.minute(), dtrtc.second());
    setInternalTime(newTime);
    printRTC();
  }
}

#endif // rtc

/**
 * @brief Check whether file system is up-to-date
 *
 * @return true
 * @return false
 */
bool check_filesystem_version()
{
  bool is_ok;
  String current_version;
  File info_file = LittleFS.open("/data/version.txt", "r");

  if (info_file.available())
  {
    String current_fs_version = info_file.readStringUntil('\n');
    strncpy(version_fs, current_fs_version.c_str(), sizeof(version_fs) - 1);
    is_ok = (current_fs_version.compareTo(required_fs_version) <= 0);
    /*
    #ifdef DEBUG_MODE
        if (is_ok)
          Serial.printf("Current fs version %s is ok.\n", current_fs_version.c_str());
        else
          Serial.printf("Current fs version %s is too old.\n", current_fs_version.c_str());
    #endif
    */
  }
  else
    is_ok = false;
  info_file.close();
  return is_ok;
}

/**
 * @brief Operator handling rules

 *
 */
struct oper_st
{
  byte id;           //!< identifier used in data structures
  char code[4];      //!< code used in UI
  bool gt;           //!< true if variable is greater than the compared value
  bool eq;           //!< true if variable is equal with then compared value
  bool reverse;      //!< negate comparison result
  bool boolean_only; //!< hand variable value as boolean (1=true), eq and reverse possible
};

#define OPER_COUNT 8
/**
 * @brief Statament checking conditions
 * @details
0, "=", eq=true  \n
1, ">", gt=true  \n
2, "<", gt and eq are true, result is reversed so reverse=true \n
3, ">=",gt and eq are true \n
4, "<=", gt=true and result is reversed so reverse=true \n
5, "<>", eq=true and thre result is reversed so reverse=true \n
6, "is", boolean_only=true \n
7, "not",  boolean_only=true and because not reverse=true
 */
// TODO: maybe operator NA - not available
const oper_st opers[OPER_COUNT] = {{0, "=", false, true, false, false}, {1, ">", true, false, false, false}, {2, "<", true, true, true, false}, {3, ">=", true, true, false, false}, {4, "<=", true, false, true, false}, {5, "<>", false, true, true, false}, {6, "is", false, false, false, true}, {7, "not", false, false, true, true}};

/*constant_type, variable_type
long val_l
type = 0 default long
type = 1  10**1 stored to long  , ie. 1.5 -> 15
... 10
*/
#define CONSTANT_TYPE_DEC0 0                //!< integer(long) value
#define CONSTANT_TYPE_DEC1 1                //!< numeric value, 1 decimal
#define CONSTANT_TYPE_CHAR_2 22             //!< 2 characters string to long, e.g. hh
#define CONSTANT_TYPE_CHAR_4 24             //!< 4 characters string to long, e.g. hhmm
#define CONSTANT_TYPE_BOOLEAN_NO_REVERSE 50 //!< boolean , no reverse allowed
#define CONSTANT_TYPE_BOOLEAN_REVERSE_OK 51 //!< boolean , reverse allowed

struct statement_st
{
  int variable_id;
  byte oper_id;
  byte constant_type;
  unsigned int depends;
  long const_val;
};

// do not change variable id:s (will broke statements)
//#define VARIABLE_COUNT 22
#define VARIABLE_COUNT 26

#define VARIABLE_PRICE 0        //!< price of current period, 1 decimal
#define VARIABLE_PRICERANK_9 1  //!< price rank within 9 hours window
#define VARIABLE_PRICERANK_24 2 //!< price rank within 24 hours window
#define VARIABLE_PRICEAVG_9 5
#define VARIABLE_PRICEAVG_24 6
#define VARIABLE_PRICEDIFF_9 9
#define VARIABLE_PRICEDIFF_24 10
#define VARIABLE_PVFORECAST_SUM24 20
#define VARIABLE_PVFORECAST_VALUE24 21
#define VARIABLE_PVFORECAST_AVGPRICE24 22
#define VARIABLE_AVGPRICE24_EXCEEDS_CURRENT 23
#define VARIABLE_EXTRA_PRODUCTION 100
#define VARIABLE_PRODUCTION_POWER 101
#define VARIABLE_SELLING_POWER 102
#define VARIABLE_SELLING_ENERGY 103
#define VARIABLE_SELLING_POWER_NOW 104
#define VARIABLE_MM 110
#define VARIABLE_MMDD 111
#define VARIABLE_WDAY 112
#define VARIABLE_HH 115
#define VARIABLE_HHMM 116
#define VARIABLE_DAYENERGY_FI 130 //!< true if day, (07:00-22:00 Finnish tariffs), logical
#define VARIABLE_WINTERDAY_FI 140 //!< true if winterday, (Finnish tariffs), logical
#define VARIABLE_SENSOR_1 201     //!< sensor1 value, float, 1 decimal
#define VARIABLE_LOCALTIME_TS 1001

// variable dependency bitmask
#define VARIABLE_DEPENDS_UNDEFINED 0
#define VARIABLE_DEPENDS_PRICE 1
#define VARIABLE_DEPENDS_SOLAR_FORECAST 2
#define VARIABLE_DEPENDS_GRID_METER 4
#define VARIABLE_DEPENDS_PRODUCTION_METER 8
#define VARIABLE_DEPENDS_SENSOR 16

// combined
#define VARIABLE_DEPENDS_PRICE_SOLAR 3

/**
 * @brief Class defines variables defined by measurements, calculations and used to define channel statuses
 *
 */

class Variables
{
public:
  Variables()
  {
    for (int variable_idx = 0; variable_idx < VARIABLE_COUNT; variable_idx++)
      variables[variable_idx].val_l = VARIABLE_LONG_UNKNOWN;
  }
  bool is_set(int id);
  void set(int id, long value_l);
  void set(int id, float val_f);
  void set_NA(int id);
  long get_l(int id);
  float get_f(int id);

  bool is_statement_true(statement_st *statement, bool default_value = false);
  int get_variable_by_id(int id, variable_st *variable);
  void get_variable_by_idx(int idx, variable_st *variable);
  long float_to_internal_l(int id, float val_float);
  float const_to_float(int id, long const_in);
  int to_str(int id, char *strbuff, bool use_overwrite_val = false, long overwrite_val = 0, size_t buffer_length = 1);
  int get_variable_count() { return VARIABLE_COUNT; };

private:
  // variable_st variables[VARIABLE_COUNT] = {{VARIABLE_PRICE, "price", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_PRICE}, {VARIABLE_PRICERANK_9, "price rank 9h", 0, VARIABLE_DEPENDS_PRICE}, {VARIABLE_PRICERANK_24, "price rank 24h", 0, VARIABLE_DEPENDS_PRICE}, {VARIABLE_PVFORECAST_SUM24, "pv forecast 24 h", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_SOLAR_FORECAST}, {VARIABLE_PVFORECAST_VALUE24, "pv value 24 h", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_PRICE_SOLAR}, {VARIABLE_PVFORECAST_AVGPRICE24, "pv price avg 24 h", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_PRICE_SOLAR}, {VARIABLE_AVGPRICE24_EXCEEDS_CURRENT, "future pv higher", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_PRICE_SOLAR}, {VARIABLE_EXTRA_PRODUCTION, "extra production", CONSTANT_TYPE_BOOLEAN_REVERSE_OK, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_PRODUCTION_POWER, "production (per) W", 0, VARIABLE_DEPENDS_PRODUCTION_METER}, {VARIABLE_SELLING_POWER, "selling W", 0, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_SELLING_ENERGY, "selling Wh", 0, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_SELLING_POWER_NOW, "selling now W", 0, VARIABLE_DEPENDS_UNDEFINED},  {VARIABLE_MM, "mm, month", CONSTANT_TYPE_CHAR_2, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_MMDD, "mmdd", CONSTANT_TYPE_CHAR_4, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_WDAY, "weekday (1-7)", 0, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_HH, "hh, hour", CONSTANT_TYPE_CHAR_2, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_HHMM, "hhmm", CONSTANT_TYPE_CHAR_4, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_DAYENERGY_FI, "day", CONSTANT_TYPE_BOOLEAN_REVERSE_OK, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_WINTERDAY_FI, "winterday", CONSTANT_TYPE_BOOLEAN_REVERSE_OK, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_SENSOR_1, "sensor 1", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_SENSOR}, {VARIABLE_SENSOR_1 + 1, "sensor 2", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_SENSOR}, {VARIABLE_SENSOR_1 + 2, "sensor 3", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_SENSOR}};
  variable_st variables[VARIABLE_COUNT] = {{VARIABLE_PRICE, "price", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_PRICE}, {VARIABLE_PRICERANK_9, "price rank 9h", 0, VARIABLE_DEPENDS_PRICE}, {VARIABLE_PRICERANK_24, "price rank 24h", 0, VARIABLE_DEPENDS_PRICE}, {VARIABLE_PRICEAVG_9, "price avg 9h", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_PRICE}, {VARIABLE_PRICEAVG_24, "price avg 24h", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_PRICE}, {VARIABLE_PRICEDIFF_9, "p diff to avg 9h", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_PRICE}, {VARIABLE_PRICEDIFF_24, "p diff to avg 24h", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_PRICE}, {VARIABLE_PVFORECAST_SUM24, "pv forecast 24 h", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_SOLAR_FORECAST}, {VARIABLE_PVFORECAST_VALUE24, "pv value 24 h", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_PRICE_SOLAR}, {VARIABLE_PVFORECAST_AVGPRICE24, "pv price avg 24 h", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_PRICE_SOLAR}, {VARIABLE_AVGPRICE24_EXCEEDS_CURRENT, "future pv higher", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_PRICE_SOLAR}, {VARIABLE_EXTRA_PRODUCTION, "extra production", CONSTANT_TYPE_BOOLEAN_REVERSE_OK, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_PRODUCTION_POWER, "production (per) W", 0, VARIABLE_DEPENDS_PRODUCTION_METER}, {VARIABLE_SELLING_POWER, "selling W", 0, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_SELLING_ENERGY, "selling Wh", 0, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_SELLING_POWER_NOW, "selling now W", 0, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_MM, "mm, month", CONSTANT_TYPE_CHAR_2, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_MMDD, "mmdd", CONSTANT_TYPE_CHAR_4, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_WDAY, "weekday (1-7)", 0, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_HH, "hh, hour", CONSTANT_TYPE_CHAR_2, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_HHMM, "hhmm", CONSTANT_TYPE_CHAR_4, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_DAYENERGY_FI, "day", CONSTANT_TYPE_BOOLEAN_REVERSE_OK, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_WINTERDAY_FI, "winterday", CONSTANT_TYPE_BOOLEAN_REVERSE_OK, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_SENSOR_1, "sensor 1", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_SENSOR}, {VARIABLE_SENSOR_1 + 1, "sensor 2", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_SENSOR}, {VARIABLE_SENSOR_1 + 2, "sensor 3", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_SENSOR}};
  int get_variable_index(int id);
};

bool Variables::is_set(int id)
{
  int idx = get_variable_index(id);
  if (idx != -1)
  {
    return (variables[idx].val_l != VARIABLE_LONG_UNKNOWN);
  }
  return false;
}
/**
 * @brief Set unconverted internal variable value
 *
 * @param id variable id
 * @param val_l variable unconverted internal value
 */
void Variables::set(int id, long val_l)
{
  int idx = get_variable_index(id);
  if (idx != -1)
  {
    variables[idx].val_l = val_l;
  }
}
/**
 * @brief Set variable unknown/not available
 *
 * @param id variable id
 */
void Variables::set_NA(int id)
{
  set(id, (long)VARIABLE_LONG_UNKNOWN);
}
/**
 * @brief Set variable value (float to be converted to internal value)
 *
 * @param id variable id
 * @param val_f variable float value
 */
void Variables::set(int id, float val_f)
{
  this->set(id, this->float_to_internal_l(id, val_f));
}

long Variables::get_l(int id)
{
  int idx = get_variable_index(id);
  if (idx != -1)
  {
    return variables[idx].val_l;
  }
  return -1;
}

long Variables::float_to_internal_l(int id, float val_float)
{
  variable_st var;
  int idx = get_variable_by_id(id, &var);
  float add_in_round = val_float < 0 ? -0.5 : 0.5;
  if (idx != -1)
  {
    if (var.type < 10)
    {
      return (long)int(val_float * pow(10, var.type) + add_in_round);
    }
    else if ((var.type == CONSTANT_TYPE_CHAR_2) || (var.type == CONSTANT_TYPE_CHAR_4))
    {
      return (long)int(val_float + add_in_round);
    }
  }
  return -1;
}
// convert given value to float based on variables definition
float Variables::const_to_float(int id, long const_in)
{
  variable_st var;
  int idx = get_variable_by_id(id, &var);
  if (var.type < 10)
  {
    return const_in / pow(10, var.type);
  }
  else if ((var.type == CONSTANT_TYPE_CHAR_2) || (var.type == CONSTANT_TYPE_CHAR_4))
  {
    return (float)const_in;
  }
  return -1;
}

int Variables::to_str(int id, char *strbuff, bool use_overwrite_val, long overwrite_val, size_t buffer_length)
{
  variable_st var;
  int idx = get_variable_by_id(id, &var);
  long val_l;

  if (idx != -1)
  {
    if (use_overwrite_val)
      val_l = overwrite_val;
    else
      val_l = var.val_l;
    if (val_l == VARIABLE_LONG_UNKNOWN)
    {
      strncpy(strbuff, "null", buffer_length);
      return -1;
    }
    if (var.type < 10)
    {
      float val_f = val_l / pow(10, var.type);
      dtostrf(val_f, 1, var.type, strbuff);
      return strlen(strbuff);
    }
    else if (var.type == CONSTANT_TYPE_CHAR_4)
    {                                 // 4 char number, 0 padding, e.g. hhmm
                                      //   sprintf(strbuff, "%04ld", val_l);
      sprintf(strbuff, "%ld", val_l); // kokeiltu ilman paddingiä
      return strlen(strbuff);
    }
    else if (var.type == CONSTANT_TYPE_CHAR_2)
    { // 2 char number, 0 padding, e.g. hh
      // sprintf(strbuff, "\"%02ld\"", val_l);
      // sprintf(strbuff, "'%02ld'", val_l);
      sprintf(strbuff, "%ld", val_l); // kokeiltu ilman paddingiä
      return strlen(strbuff);
    }
    else if (var.type == CONSTANT_TYPE_BOOLEAN_NO_REVERSE || var.type == CONSTANT_TYPE_BOOLEAN_REVERSE_OK)
    {
      sprintf(strbuff, "%s", (var.val_l == 1) ? "true" : "false");
      //  Serial.printf("Logical variable, internal value = %ld -> %s\n", var.val_l, strbuff);
      return strlen(strbuff);
    }
  }
  strncpy(strbuff, "null", buffer_length);
  return -1;
}

float Variables::get_f(int id)
{
  variable_st var;
  int idx = get_variable_by_id(id, &var);
  if (idx != -1)
  {
    if (var.type < 10)
    {
      return (float)(var.val_l / pow(10, var.type));
    }
  }
  Serial.printf("get_f var with id %d not found.\n", id);
  return -1;
}

int Variables::get_variable_index(int id)
{
  int var_count = (int)(sizeof(variables) / sizeof(variable_st));
  // Serial.printf("get_variable_index var_count: %d, ( %d /  %d ) \n",var_count,sizeof(variables),sizeof(variable_st));
  for (int i = 0; i < var_count; i++)
  {
    if (id == variables[i].id)
      return i;
  }
  return -1;
}
int Variables::get_variable_by_id(int id, variable_st *variable)
{
  int idx = get_variable_index(id);
  if (idx != -1)
  {
    memcpy(variable, &variables[idx], sizeof(variable_st));
    return idx;
  }
  else
    return -1;
}
/**
 * @brief Copies variable (given by idx/location index) content to given  memory address
 *
 * @param idx variable idx
 * @param variable memory pointer
 */
void Variables::get_variable_by_idx(int idx, variable_st *variable)
{
  memcpy(variable, &variables[idx], sizeof(variable_st));
}
/**
 * @brief Check if statement is true
 * @details  Checks current variable value with statement operatoe and constant value
 *
 * @param statement
 * @param default_value
 * @return true
 * @return false
 */
bool Variables::is_statement_true(statement_st *statement, bool default_value)
{
  // kelaa operaattorit läpi, jos löytyy match niin etene sen kanssa, jos ei niin palauta default
  variable_st var;
  if (statement->variable_id == -1)
  {
    return default_value;
  }

  int variable_idx = get_variable_by_id(statement->variable_id, &var);

  if ((variable_idx == -1) || (var.val_l == VARIABLE_LONG_UNKNOWN))
    return default_value;

  oper_st oper;
  for (int i = 0; i < OPER_COUNT; i++)
  {
    if (opers[i].id == statement->oper_id)
    {
      oper = opers[i];
    }
  }
  bool result = false;

  if (oper.boolean_only)
    result = (var.val_l == 1);
  else
  {
    if (oper.eq && var.val_l == statement->const_val)
      result = true;

    if (oper.gt && var.val_l > statement->const_val)
      result = true;
  }

  // finally optional reverse
  if (oper.reverse)
    result = !result;

  //  Serial.printf("Statement: %ld  %s  %ld  results %s\n", var.val_l, oper.code, statement->const_val, result ? "true" : "false");

  return result;
}

Variables vars;

#ifdef SENSOR_DS18B20_ENABLED
bool sensor_ds18b20_enabled = true;
// see: https://randomnerdtutorials.com/esp8266-ds18b20-temperature-sensor-web-server-with-arduino-ide/
#include <OneWire.h>
#include <DallasTemperature.h> // tätä ei ehkä välttämättä tarvita, jos käyttäisi onewire.h:n rutineeja

time_t temperature_updated = 0;

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(ONEWIRE_DATA_GPIO);

// Pass our oneWire reference to Dallas Temperature sensor
DallasTemperature sensors(&oneWire);
float ds18B20_temp_c;
int sensor_count = 0;
#else
bool sensor_ds18b20_enabled = false;
#endif

#ifdef INFLUX_REPORT_ENABLED
#include <InfluxDbClient.h>
// TODO: should we use macid?
const char *influx_device_id_prefix PROGMEM = "arska-";
String wifi_mac_short;

typedef struct
{
  bool state;
  time_t this_state_started;
  int on_time;
  int off_time;
} channel_log_struct;

// Point state_stats("state_stats");
Point point_sensor_values("sensors");
Point point_period_avg("period_avg"); //!< Influx buffer

/**
 * @brief Handling channel utilization (uptime/downtime) statistics
 *
 */
class ChannelCounters
{
public:
  ChannelCounters()
  {
    init();
  }
  void init();
  void new_log_period(time_t ts_report);
  void set_state(int channel_idx, bool new_state);

private:
  channel_log_struct channel_logs[CHANNEL_COUNT];
};
void ChannelCounters::init()
{
  time_t now_l;
  time(&now_l);
  for (int i = 0; i < CHANNEL_COUNT; i++)
  {
    channel_logs[i].off_time = 0;
    channel_logs[i].on_time = 0;
    channel_logs[i].state = false;
    channel_logs[i].this_state_started = now_l;
  }
}
void ChannelCounters::new_log_period(time_t ts_report)
{
  time_t now_l;
  float utilization;
  time(&now_l);

  // influx buffer
  if (!point_period_avg.hasTime())
    point_period_avg.setTime(ts_report);
  char field_name[10];
  for (int i = 0; i < CHANNEL_COUNT; i++)
  {
    set_state(i, channel_logs[i].state); // this will update counters without changing state
    if ((channel_logs[i].off_time + channel_logs[i].on_time) > 0)
      utilization = (float)channel_logs[i].on_time / (float)(channel_logs[i].off_time + channel_logs[i].on_time);
    else
      utilization = 0;
    snprintf(field_name, sizeof(field_name), "ch%d", i + 1); // 1-indexed channel numbers in UI
    point_period_avg.addField(field_name, utilization);
  }
  // then reset
  for (int i = 0; i < CHANNEL_COUNT; i++)
  {
    channel_logs[i].off_time = 0;
    channel_logs[i].on_time = 0;
    channel_logs[i].this_state_started = now_l;
  }
  // write buffer
}

void ChannelCounters::set_state(int channel_idx, bool new_state)
{
  time_t now_l;
  time(&now_l);
  int previous_state_duration = (now_l - channel_logs[channel_idx].this_state_started);
  if (channel_logs[channel_idx].state)
    channel_logs[channel_idx].on_time += previous_state_duration;
  else
    channel_logs[channel_idx].off_time += previous_state_duration;

  float utilization; // FOR DEBUG
  if ((channel_logs[channel_idx].off_time + channel_logs[channel_idx].on_time) > 0)
  {
    utilization = (float)channel_logs[channel_idx].on_time / (float)(channel_logs[channel_idx].off_time + channel_logs[channel_idx].on_time);
  }
  else
  {
    utilization = 0;
  }
  Serial.printf("set_state, channel: %d, on: %d , off: %d, utilization: %f\n", channel_idx, channel_logs[channel_idx].on_time, channel_logs[channel_idx].off_time, utilization);

  channel_logs[channel_idx].state = new_state;
  channel_logs[channel_idx].this_state_started = now_l;
}

ChannelCounters ch_counters;

typedef struct
{
  char url[70];
  char token[100];
  char org[30];
  char bucket[20];
} influx_settings_struct;

influx_settings_struct s_influx;

/**
 * @brief Add time-series point values to buffer for later database insert
 *
 * @param ts timestamp written to buffer point
 */
// TODO: split to two part, sensors and debug values in th ebeginning and accumulated in the end, or something else
void add_period_variables_to_influx_buffer(time_t ts_report)
{
  point_period_avg.setTime(ts_report);

  // prices are batch updated in function update_prices_to_influx

  if (vars.is_set(VARIABLE_PRODUCTION_POWER))
    point_period_avg.addField("productionW", vars.get_f(VARIABLE_PRODUCTION_POWER));

  if (vars.is_set(VARIABLE_SELLING_POWER))
    point_period_avg.addField("sellingW", vars.get_f(VARIABLE_SELLING_POWER));

  if (vars.is_set(VARIABLE_SELLING_ENERGY))
    point_period_avg.addField("sellingWh", vars.get_f(VARIABLE_SELLING_ENERGY));
}

bool write_point_buffer_influx(InfluxDBClient *ifclient, Point *point_buffer)
{
  Serial.println(F("Starting write_point_buffer_influx"));
  bool write_ok = true;
  if (!point_buffer->hasTime())
  {
    time_t now_in_func;
    time(&now_in_func);
    point_buffer->setTime(now_in_func);
  }

  if (point_buffer->hasFields())
  {
    if (!point_buffer->hasTags())
      point_buffer->addTag("device", String(influx_device_id_prefix) + wifi_mac_short);

    Serial.print("Writing: ");
    Serial.println(ifclient->pointToLineProtocol(*point_buffer));

    // Write point
    write_ok = ifclient->writePoint(*point_buffer);
    if (!write_ok)
    {
      Serial.print(F("InfluxDB write failed: "));
      Serial.println(ifclient->getLastErrorMessage());
    }
    // else {
    //     Serial.println("Write ok");
    // }
    point_buffer->clearFields();
    //  Serial.println("clearFields ok");
  }
  return write_ok;
}

/**
 * @brief Writes Influx buffer to specified server
 *
 * @return true if successful
 * @return false if not successful
 */

bool write_buffer_to_influx()
{

  // probably invalid parameters
  if (((strstr(s_influx.url, "http") - s_influx.url) != 0) || strlen(s_influx.org) < 5 || strlen(s_influx.token) < 5 || strlen(s_influx.bucket) < 1)
  {
    Serial.println(F("write_buffer_to_influx: invalid or missing parameters."));
    return false;
  }

  InfluxDBClient ifclient(s_influx.url, s_influx.org, s_influx.bucket, s_influx.token);
  ifclient.setInsecure(true); // TODO: cert handling

  // ifclient.setWriteOptions(WriteOptions().writePrecision(WritePrecision::S)); // set time precision to seconds
  ifclient.setWriteOptions(WriteOptions().writePrecision(WritePrecision::S).batchSize(2).bufferSize(2));

  ifclient.setHTTPOptions(HTTPOptions().connectionReuse(true));

  bool influx_write_ok = write_point_buffer_influx(&ifclient, &point_period_avg);
  if (!influx_write_ok)
    return false;

/*
  if (point_period_avg.hasFields())
  {
    if (!point_period_avg.hasTags())
      point_period_avg.addTag("device", String(influx_device_id_prefix)+wifi_mac_short);

    ifclient.setInsecure(true); // TODO: cert handling

    Serial.print("Writing: ");
    Serial.println(ifclient.pointToLineProtocol(point_period_avg));

    // Write point
    bool write_ok = ifclient.writePoint(point_period_avg);
    if (!write_ok)
    {
      Serial.print("InfluxDB write failed: ");
      Serial.println(ifclient.getLastErrorMessage());
    }
    point_period_avg.clearFields();
  }

  if (state_stats.hasFields()) // TODO:combine the two
  {
    if (!state_stats.hasTags())
      state_stats.addTag("device", String(influx_device_id_prefix)+wifi_mac_short);
    ifclient.setInsecure(true); // TODO: cert handling

    Serial.print("Writing: ");
    Serial.println(ifclient.pointToLineProtocol(state_stats));

    // Write point
    bool write_ok = ifclient.writePoint(state_stats);
    if (!write_ok)
    {
      Serial.print("InfluxDB write failed: ");
      Serial.println(ifclient.getLastErrorMessage());
    }
    state_stats.clearFields();
  }
*/

// add current debug and sensor values to the buffer and write later them
#ifdef DEBUG_MODE
  point_sensor_values.addField("uptime", (long)(millis() / 1000));
#endif

// add sensor values to influx update
#ifdef SENSOR_DS18B20_ENABLED
  char field_name[10];
  for (int j = 0; j < sensor_count; j++)
  {
    if (vars.is_set(VARIABLE_SENSOR_1 + j))
    {
      snprintf(field_name, sizeof(field_name), "sensor%i", j + 1);
      point_sensor_values.addField(field_name, vars.get_f(VARIABLE_SENSOR_1 + j));
    }
  }
#endif
  influx_write_ok = write_point_buffer_influx(&ifclient, &point_sensor_values);

  ifclient.flushBuffer();
  // Serial.println(F("Ending write_buffer_to_influx"));
  return influx_write_ok; //
}

// under contruction
/*TODO before production
- check what we have in the db, write only new prices, max price, see https://github.com/tobiasschuerg/InfluxDB-Client-for-Arduino/blob/master/examples/QueryAggregated/QueryAggregated.ino
    - done...
- batch processing could be faster, write only in the end, https://github.com/tobiasschuerg/InfluxDB-Client-for-Arduino/blob/master/examples/SecureBatchWrite/SecureBatchWrite.ino
- start as "low priority" task in the loop
*/
bool update_prices_to_influx()
{
  time_t record_start = 0;
  time_t current_period_start_ts;
  long current_price;
  int resolution_secs;
  bool write_ok;
  time_t last_price_in_file_ts;
  String last_price_in_db;
  bool db_price_found;
  char datebuff[30];
  tm tm2;

  // Missing or invalid parameters
  if (((strstr(s_influx.url, "http") - s_influx.url) != 0) || strlen(s_influx.org) < 5 || strlen(s_influx.token) < 5 || strlen(s_influx.bucket) < 1)
  {
    Serial.println(F("write_buffer_to_influx: invalid or missing parameters."));
    return false;
  }

  String query = "from(bucket: \"" + String(s_influx.bucket) + "\") |> range(start: -1d, stop: 2d) |> filter(fn: (r) => r._measurement == \"period_price\" )|> filter(fn: (r) => r[\"_field\"] == \"price\") ";
  query += "  |> keep(columns: [\"_time\"]) |> last(column: \"_time\")";

  InfluxDBClient ifclient(s_influx.url, s_influx.org, s_influx.bucket, s_influx.token);
  ifclient.setInsecure(true); // TODO: cert handling

  Serial.println(query);
  // Send query to the server and get result
  FluxQueryResult result = ifclient.query(query);
  if (result.next())
  {
    FluxDateTime max_price_time = result.getValueByName("_time").getDateTime();
    Serial.print("max_price_time:");
    Serial.println(max_price_time.getRawValue());
    last_price_in_db = max_price_time.getRawValue();
    db_price_found = true;
  }
  else
  {
    Serial.println("no result.next()");
    db_price_found = false;
  }

  File price_file = LittleFS.open(price_data_filename, "r");
  if (!price_file)
  {
    Serial.println(F("Failed to open price file. "));
    return false;
  }

  // see also update_price_rank_variables
  StaticJsonDocument<2024> doc;
  DeserializationError error = deserializeJson(doc, price_file);

  if (error)
  {
    Serial.print(F("update_prices_to_influx deserializeJson() failed: "));
    Serial.println(error.c_str());
    return false;
  }

  record_start = doc["record_start"];
  resolution_secs = ((int)doc["resolution_m"]) * 60;
  JsonArray prices_array = doc["prices"];

  last_price_in_file_ts = record_start + (resolution_secs * (prices_array.size() - 1));
  ts_to_date_str(&last_price_in_file_ts, datebuff);

  Serial.print("Last ts in the file");
  Serial.println(datebuff);

  if (db_price_found)
  {
    if (last_price_in_db.equals(datebuff))
    {
      Serial.print("Last ts in the file equal one in the influxdb");
      return false;
    }
    if (last_price_in_db > String(datebuff))
    {
      // Serial.print("Newer price in the influxDb - SHOULD NOT END UP HERE");
      return false;
    }
    else
    {
      Serial.print("We have new prices to write to influx db. ");
    }
  }

  // return false; // just testing  the first part

  ifclient.setWriteOptions(WriteOptions().writePrecision(WritePrecision::S).batchSize(12).bufferSize(24));
  ifclient.setHTTPOptions(HTTPOptions().connectionReuse(true));

  if (ifclient.isBufferEmpty())
    Serial.print("isBufferEmpty yes ");
  else
    Serial.print("isBufferEmpty no ");

  Point point_period_price("period_price");
  // point_period_price.addTag("device", String(influx_device_id_prefix) + wifi_mac_short);

  for (unsigned int i = 0; (i < prices_array.size() && i < MAX_PRICE_PERIODS); i++)
  {
    current_period_start_ts = record_start + resolution_secs * i;
    // TODO: half period... record_start + resolution_secs * i + (resolution_secs/2)
    ts_to_date_str(&current_period_start_ts, datebuff);
    if (!(last_price_in_db < String(datebuff))) // already in the influxDb
      continue;

    current_price = (long)prices_array[i];
    Serial.println(current_price);
    point_period_price.addField("price", (float)(current_price / 1000.0));
    point_period_price.setTime(current_period_start_ts);                         // set the time, muuta
    point_period_price.setTime(current_period_start_ts + (resolution_secs / 2)); // middle of the period
    Serial.print("Writing: ");
    Serial.println(ifclient.pointToLineProtocol(point_period_price));
    // Write point

    write_ok = ifclient.writePoint(point_period_price);
    Serial.println(write_ok ? "write_ok" : "write not ok");

    point_period_price.clearFields();
  }
  ifclient.flushBuffer();
  delay(100);

  struct tm timeinfo;
  getLocalTime(&timeinfo); // update from NTP?
  if (!ifclient.flushBuffer())
  {
    Serial.print("InfluxDB flush failed: ");
    Serial.println(ifclient.getLastErrorMessage());
    Serial.print("Full buffer: ");
    Serial.println(ifclient.isBufferFull() ? "Yes" : "No");
    return false;
  }
  return true;
}
#endif // inlflux

// Non-volatile memory https://github.com/CuriousTech/ESP-HVAC/blob/master/Arduino/eeMem.cpp
#ifdef INVERTER_SMA_MODBUS_ENABLED
#include <ModbusIP_ESP8266.h>
// Modbus registry offsets
#define SMA_DAYENERGY_OFFSET 30535
#define SMA_TOTALENERGY_OFFSET 30529
#define SMA_POWER_OFFSET 30775
#endif

#define USE_POWER_TO_ESTIMATE_ENERGY_SECS 120 // use power measurement to estimate

#define PROCESS_INTERVAL_SECS 60 // process interval
// unsigned long last_process_ts = -PROCESS_INTERVAL_SECS * 1000; // start reading as soon as you get to first loop
time_t next_process_ts = 0; // start reading as soon as you get to first loop

time_t recording_period_start = 0; // first period: boot time, later period starts
time_t current_period_start = 0;
time_t previous_period_start = 0;
time_t energym_read_last = 0;
time_t started = 0;
bool period_changed = true;

// task requests to be fullfilled in loop asyncronously
bool todo_in_loop_update_price_rank_variables = false;
bool todo_in_loop_influx_write = false;
bool todo_in_loop_restart = false;

bool todo_in_loop_test_gpio = false; //!< gpio should be tested in loop
int gpio_to_test_in_loop = -1;       //!< if not -1 then gpio should be tested in loop
bool todo_in_loop_scan_wifis = false;
bool todo_in_loop_scan_sensors = false;
bool todo_in_loop_set_relays = false;

#define CHANNEL_TYPE_COUNT 4

#define CH_TYPE_UNDEFINED 0
#define CH_TYPE_GPIO_FIXED 1
#define CH_TYPE_GPIO_USER_DEF 3
#define CH_TYPE_WIFI_SHELLY_1GEN 2 // new, was CH_TYPE_SHELLY_ONOFF
#define CH_TYPE_MODBUS_RTU 20      // RFU
#define CH_TYPE_DISABLED 255       // RFU, we could have disabled, but allocated channels (binary )

// channels type string for admin UI
/* OLD
const char *channel_type_strings[] PROGMEM = {
    "undefined",
    "GPIO",
    "Shelly",
};
*/
// TODOX: replace with channel_types, also in Javascript
/*
const char *channel_type_strings[] PROGMEM = {
   "undefined",
   "GPIO", //fixed gpio
   "GPIO user-defined" // user defined gpio
   "Shelly Gen 1", //define last ip, first 3 bytes from wifi(defined address)
//  "Modbus", // define coil id, server id is global
};
*/

struct channel_type_st
{
  byte id;
  const char *name;
};
//#define CH_TYPE_SHELLY_ONOFF 2  -> 10
//#define CH_TYPE_DISABLED 255 // RFU, we could have disabled, but allocated channels (binary )

channel_type_st channel_types[CHANNEL_TYPE_COUNT] = {{CH_TYPE_UNDEFINED, "undefined"}, {CH_TYPE_GPIO_FIXED, "GPIO"}, {CH_TYPE_GPIO_USER_DEF, "GPIO, user defined"}, {CH_TYPE_WIFI_SHELLY_1GEN, "Shelly Gen 1"}};
// later , {CH_TYPE_MODBUS_RTU, "Modbus RTU"}

#define HW_TEMPLATE_COUNT 3
#define HW_TEMPLATE_GPIO_COUNT 4
struct hw_template_st
{
  int id;
  const char *name;
  byte gpios[HW_TEMPLATE_GPIO_COUNT];
};

hw_template_st hw_templates[HW_TEMPLATE_COUNT] = {{0, "manual", {255, 255, 255, 255}}, {1, "esp32lilygo-4ch", {21, 19, 18, 5}}, {2, "esp32wroom-4ch-a", {32, 33, 25, 26}}};

// #define CHANNEL_CONDITIONS_MAX 3 //platformio.ini
#define CHANNEL_STATES_MAX 10
#define RULE_STATEMENTS_MAX 5
#define MAX_CHANNELS_SWITCHED_AT_TIME 1

#define MAX_CH_ID_STR_LENGTH 10
#define MAX_ID_STR_LENGTH 30
#define MAX_URL_STR_LENGTH 70

// Energy metering types
#define ENERGYM_NONE 0
#define ENERGYM_SHELLY3EM 1
#define ENERGYM_FRONIUS_SOLAR 2
#define ENERGYM_SMA_MODBUS_TCP 3
#define ENERGYM_MAX 3

// Type texts for config ui
const char *energym_strings[] PROGMEM = {"none", "Shelly 3EM", "Fronius Solar API", "SMA Modbus TCP"};

#if defined(INVERTER_FRONIUS_SOLARAPI_ENABLED) || defined(INVERTER_SMA_MODBUS_ENABLED)
// inverter productuction info fields
unsigned long inverter_total_period_init = 0;
bool inverter_total_period_init_ok = false;
unsigned long energy_produced_period = 0;
unsigned long power_produced_period_avg = 0;
#endif

// Target/condition row stucture, elements of target array in channel, stored in non-volatile memory
typedef struct
{
  statement_st statements[RULE_STATEMENTS_MAX];
  float target_val;
  bool on;
  bool condition_active; // for showing if the condition is currently active, for tracing
} condition_struct;

#define CHANNEL_CONFIG_MODE_RULE 0
#define CHANNEL_CONFIG_MODE_TEMPLATE 1
// Channel stucture, elements of channel array in setting, stored in non-volatile memory
typedef struct
{
  condition_struct conditions[CHANNEL_CONDITIONS_MAX];
  char id_str[MAX_CH_ID_STR_LENGTH];
  uint8_t switch_id;
  bool is_up;
  bool wanna_be_up;
  byte type;
  time_t uptime_minimum;
  time_t toggle_last;
  time_t force_up_from;
  time_t force_up_until; // TODO: we could have also force_up_from to enable scheduled start
  byte config_mode;      // CHANNEL_CONFIG_MODE_RULE, CHANNEL_CONFIG_MODE_TEMPLATE
  int template_id;
} channel_struct;

#ifdef SENSOR_DS18B20_ENABLED
typedef struct
{
  DeviceAddress address;
  char id_str[MAX_CH_ID_STR_LENGTH];
} sensor_struct;
#endif

// TODO: add fixed ip, subnet?
// Setting stucture, stored in non-volatile memory
typedef struct
{
  int check_value;
  char wifi_ssid[MAX_ID_STR_LENGTH];
  char wifi_password[MAX_ID_STR_LENGTH];
  char http_username[MAX_ID_STR_LENGTH];
  char http_password[MAX_ID_STR_LENGTH];
  channel_struct ch[CHANNEL_COUNT];
  char variable_server[MAX_ID_STR_LENGTH]; // used in replica mode
  char entsoe_api_key[37];
  char entsoe_area_code[17];
  char custom_ntp_server[35]; // TODO:UI to set up
  char timezone[4];           //!< EET,CET supported
  uint32_t baseload;          // production above baseload is "free" to use/store
  bool next_boot_ota_update;  // RFU
  byte energy_meter_type;
  char energy_meter_host[MAX_URL_STR_LENGTH];
  unsigned int energy_meter_port;
  byte energy_meter_id;
  char forecast_loc[MAX_ID_STR_LENGTH];
  byte variable_mode; // VARIABLE_MODE_SOURCE, VARIABLE_MODE_REPLICA
  char lang[3];       // preferred language
#ifdef SENSOR_DS18B20_ENABLED
  sensor_struct sensors[MAX_DS18B20_SENSORS];
#endif
  IPAddress switch_subnet_wifi; // set in automatically in wifi connection setup, if several nw interfaces (wifi+eth), manual setup possibly needed
  int hw_template_id;
} settings_struct;

// this stores settings also to eeprom
settings_struct s;

#define MAX_SPLIT_ARRAY_SIZE 10 // TODO: check if we do still need fixed array here

/**
 * @brief  Parse char array to uint16_t array (e.g. states, ip address)
 * @details description note: current version alter str_in, so use copy in calls if original still needed
 * @param str_in
 * @param array_out
 * @param separator
 */
void str_to_uint_array(const char *str_in, uint16_t array_out[MAX_SPLIT_ARRAY_SIZE], const char *separator)
{
  char *ptr = strtok((char *)str_in, separator); // breaks string str into a series of tokens using the delimiter delim.
  byte i = 0;
  for (int ch_state_idx = 0; ch_state_idx < MAX_SPLIT_ARRAY_SIZE; ch_state_idx++)
  {
    array_out[ch_state_idx] = 0;
  }
  while (ptr)
  {
    /* Serial.print(atol(ptr));
     Serial.print(","); */
    array_out[i] = atol(ptr);
    ptr = strtok(NULL, separator);
    i++;
    if (i == MAX_SPLIT_ARRAY_SIZE)
    {
      break;
    }
  }
  return;
}

/**
 * @brief Checks if given cache files exists and is not expired
 *
 * @param cache_file_name file name in liitlefs
 * @return true  if valid
 * @return false if not valid
 */
bool is_cache_file_valid(const char *cache_file_name)
{
  time_t now_in_func;
  if (!LittleFS.exists(cache_file_name))
  {
    Serial.println(F("No cache file."));
    return false;
  }
  File cache_file = LittleFS.open(cache_file_name, "r");
  if (!cache_file)
  { // failed to open the file, retrn empty result
    Serial.println(F("Failed to open cache file. "));
    return false;
  }
  StaticJsonDocument<16> filter;
  // filter["ts"] = true; // first get timestamp field, old way
  filter["expires"] = true; // first check expires timestamp field

  StaticJsonDocument<50> doc_ts;

  DeserializationError error = deserializeJson(doc_ts, cache_file, DeserializationOption::Filter(filter));
  cache_file.close();

  if (error)
  {
    Serial.print(F("Arska server deserializeJson() failed: "));
    Serial.println(error.f_str());
    return false;
  }

  unsigned long expires = doc_ts["expires"];
  time(&now_in_func);
  return expires > now_in_func;
}

// Serial command interface
String serial_command;
byte serial_command_state = 0;
int network_count = 0;

/**
 * @brief Scans wireless networks on the area and stores list to a file.
 * @details description Started from loop-function. Do not run interactively (from a http call).
 *
 */
void scan_and_store_wifis(bool print_out)
{
  network_count = WiFi.scanNetworks();

  LittleFS.remove(wifis_filename);                      // Delete existing file, otherwise the configuration is appended to the file
  File wifis_file = LittleFS.open(wifis_filename, "w"); // Open file for writing
  wifis_file.printf("wifis = '[");

  int good_wifi_count = 0;

  if (print_out)
    Serial.println("Available WiFi networks:\n");
  for (int i = 0; i < network_count; ++i)
  {
    if (WiFi.RSSI(i) < -80) // too weak signals not listed, could be actually -75
      continue;
    good_wifi_count++;
    wifis_file.print("{\"id\":\"");
    wifis_file.print(WiFi.SSID(i));
    wifis_file.print("\",\"rssi\":");
    wifis_file.print(WiFi.RSSI(i));
    wifis_file.print("},");
    if (print_out)
      Serial.printf("%d - %s (%ld)\n", i, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
  }
  wifis_file.printf("{}]';");
  wifis_file.close();
  if (print_out)
  {
    Serial.println("-");
    Serial.flush();
  }
}

#define CONFIG_JSON_SIZE_MAX 6144
/**
 * @brief Utility for reading a json config file to memory structures- to be tuned
 *
 * @param doc
 * @param key
 * @param tostr
 * @return true
 * @return false
 */

bool copy_doc_str(StaticJsonDocument<CONFIG_JSON_SIZE_MAX> &doc, char *key, char *tostr, size_t buffer_length)
{
#ifdef DEBUG_MODE
  Serial.print("debug: ");
  Serial.println(key);
#endif
  if (doc.containsKey(key))
  {
    strncpy(tostr, doc[key], buffer_length);
    return true;
  }
  return false;
}
/**
 * @brief Get long value from Arduino json object
 *
 * @param doc
 * @param key
 * @param default_val
 * @return long
 */
long get_doc_long(StaticJsonDocument<CONFIG_JSON_SIZE_MAX> &doc, const char *key, long default_val = VARIABLE_LONG_UNKNOWN)
{
  if (doc.containsKey(key))
  {
    return (long)doc[key];
  }
  return default_val;
}

/**
 * @brief Reads settings from eeprom to s and s_influx data structures
 *
 */
void readFromEEPROM()
{
  EEPROM.get(eepromaddr, s);
  int used_size = sizeof(s);
#ifdef INFLUX_REPORT_ENABLED
  EEPROM.get(eepromaddr + used_size, s_influx);
  // Serial.printf("readFromEEPROM influx_url:%s\n", s_influx.url);
  used_size += sizeof(s_influx);
#endif
  Serial.print(F("readFromEEPROM: Reading settings from eeprom, Size: "));
  Serial.println(used_size);
}

/**
 * @brief Writes settings to eeprom
 *
 */
void writeToEEPROM()
{
  int used_size = sizeof(s);
  EEPROM.put(eepromaddr, s); // write data to array in ram
#ifdef INFLUX_REPORT_ENABLED
  EEPROM.put(eepromaddr + used_size, s_influx);
  used_size += sizeof(s_influx);
  // Serial.printf("writeToEEPROM influx_url:%s\n", s_influx.url);
#endif
  EEPROM.commit();
  EEPROM.end();
  Serial.print(F("writeToEEPROM: Writing settings to eeprom."));
}

void notFound(AsyncWebServerRequest *request)
{
  request->send(404, "text/plain", "Not found");
}

/**
 * @brief Utility function to make http request, stores result to a cache file if defined
 *
 * @param url Url to call
 * @param cache_file_name optional cache file name to store result
 * @return String
 */
String httpGETRequest(const char *url, const char *cache_file_name, int32_t connect_timeout = 30000)
{
  unsigned long call_started = millis();
  WiFiClient wifi_client;

  HTTPClient http;
  http.setReuse(false);
  // http.useHTTP10(true); // for json input

  http.setConnectTimeout(connect_timeout);
  http.begin(wifi_client, url); //  IP address with path or Domain name with URL path

  delay(100);
  int httpResponseCode = http.GET(); //  Send HTTP GET request

  String payload = "{}";

  if (httpResponseCode > 0)
  {
    payload = http.getString();

    if (strlen(cache_file_name) > 0) // write to a cache file
    {
      LittleFS.remove(cache_file_name); // Delete existing file, otherwise the configuration is appended to the file

      File cache_file = LittleFS.open(cache_file_name, "w"); // Open file for writing
      if (!cache_file)
      {
        Serial.println(F("Failed to create a cache file:"));
        Serial.println(cache_file_name);
        http.end();
        return String("");
      }
      int bytesWritten = cache_file.print(http.getString());
      Serial.print(F("Wrote to cache file bytes:"));
      Serial.println(bytesWritten);

      if (bytesWritten > 0)
      {
        cache_file.close();
      }
    }
  }
  else
  {
    Serial.printf(PSTR("Error, httpResponseCode: %d in time %lu\n"), httpResponseCode, (millis() - call_started));
    http.end();
    return String("");
  }
  // Free resources
  http.end();

  return payload;
}

#ifdef SENSOR_DS18B20_ENABLED

void print_onewire_address(DeviceAddress deviceAddress)
{
  for (uint8_t i = 0; i < 8; i++)
  {
    if (deviceAddress[i] < 16)
      Serial.print("0");
    Serial.print(deviceAddress[i], HEX);
  }
}

/**
 * @brief Scan onewire sensors
 *
 * @return true if any sensors found
 * @return false
 */
bool scan_sensors()
{
  DeviceAddress device_address;
  int j;
  int32_t temp_raw;
  bool sensor_already_listed;
  int first_free_slot;
  char msgbuff[50];

  sensors.begin(); // initiate bus
  delay(2000);     // let the sensors settle

  sensor_count = min(sensors.getDeviceCount(), (uint8_t)MAX_DS18B20_SENSORS);
  Serial.printf(PSTR("Scanning sensors, sensor_count:%d\n"), sensor_count);
  if (sensor_count == 0)
  {
    log_msg(MSG_TYPE_WARN, PSTR("No sensors found."), true);
  }
  snprintf(msgbuff, sizeof(msgbuff), "Found %d sensors", sensor_count);
  log_msg(MSG_TYPE_INFO, msgbuff, true);

  sensors.requestTemperatures();
  delay(50);

  // clear first nonexistent sensors
  for (j = 0; j < MAX_DS18B20_SENSORS; j++)
  {
    if (s.sensors[j].address[0] != 0)
    {
      if (sensors.getTemp(s.sensors[j].address) != DEVICE_DISCONNECTED_RAW)
      { // still online?
        Serial.println(sensors.getTemp(s.sensors[j].address));
        print_onewire_address(s.sensors[j].address);
        Serial.printf(PSTR(" still online in slot %d\n"), j);
        continue;
      }
    }
    Serial.printf(PSTR("DEBUG: scan_sensors, slot %d cleared\n"), j);
    memset(s.sensors[j].address, 0, sizeof(DeviceAddress));
    strncpy(s.sensors[j].id_str, "-", sizeof(s.sensors[j].id_str));
  }

  for (j = 0; j < sensor_count; j++)
  {
    if (sensors.getAddress(device_address, j)) // there is a sensor with this idx
    {
      sensor_already_listed = false;
      for (int k = 0; k < MAX_DS18B20_SENSORS; k++)
      {
        // verify that this is working
        if (memcmp(device_address, s.sensors[k].address, sizeof(device_address)) == 0)
        {
          sensor_already_listed = true;
          print_onewire_address(device_address);
          Serial.printf("DEBUG:  %02X == %02X\n", (int)device_address[1], (int)s.sensors[k].address[1]);
          Serial.printf("DEBUG: sensor_already_listed in slot %d\n", k);
          break;
        }
        else
        {
          print_onewire_address(device_address);
          Serial.printf("DEBUG:  %02X <> %042\n", (int)device_address[1], (int)s.sensors[k].address[1]);
        }
      }
      if (!sensor_already_listed)
      {
        first_free_slot = MAX_DS18B20_SENSORS;
        for (int l = 0; l < MAX_DS18B20_SENSORS; l++)
        {
          if (s.sensors[l].address[0] == 0)
          {
            first_free_slot = l;
            break;
          }
        }
        if (first_free_slot < MAX_DS18B20_SENSORS)
        { // now we have a slot for the sensor just found
          Serial.printf("DEBUG: new sensor to slot slot %d\n", first_free_slot);
          memcpy(s.sensors[first_free_slot].address, device_address, sizeof(device_address));
          printf(s.sensors[first_free_slot].id_str, "sensor %d", j + 1);
        }
        else
          break; // no more free slots, no reason to loop
      }
    }
  }
  Serial.println("DEBUG AFTER SCAN:");
  for (int k = 0; k < MAX_DS18B20_SENSORS; k++)
  {
    print_onewire_address(s.sensors[k].address);
    Serial.printf("sensor %d \n", k);
  }
  return true;
}

/**
 * @brief Read DS18B20 sensor values.
 *
 * @return true
 * @return false
 */
bool read_ds18b20_sensors()
{
  // temperature_updated PROCESS_INTERVAL_SECS
  if (sensor_count == 0)
    return false;
  Serial.printf(PSTR("Starting read_ds18b20_sensors, sensor_count: %d\n"), sensor_count);
  DeviceAddress device_address;
  sensors.requestTemperatures();
  delay(50);
  int32_t temp_raw;
  float temp_c;
  time_t now_in_func;

  // Loop through each device, print out temperature data
  // Serial.printf("sensor_count:%d\n", sensor_count);
  int j;
  for (j = 0; j < MAX_DS18B20_SENSORS; j++)
  {
    // Get the  address
    if (s.sensors[j].address != 0)
    {
      memcpy(device_address, s.sensors[j].address, sizeof(device_address));
      temp_raw = sensors.getTemp(s.sensors[j].address);
      if (temp_raw != DEVICE_DISCONNECTED_RAW)
      {
        temp_c = sensors.rawToCelsius(temp_raw);

        Serial.printf("Sensor %d, temp C: %f\n", j, temp_c);
        vars.set(VARIABLE_SENSOR_1 + j, temp_c);
        time(&temperature_updated); // TODO: per sensor?
      }
      else
      { //
        time(&now_in_func);
        if ((now_in_func - temperature_updated) < SENSOR_VALUE_EXPIRE_TIME)
        {
          Serial.printf("DEBUG Sensor %d, DEVICE_DISCONNECTED_RAW\n", j);
          vars.set_NA(VARIABLE_SENSOR_1 + j);
        }
      }
    }
    else
    {
      Serial.printf("DEBUG Sensor %d, no address\n", j);
      vars.set_NA(VARIABLE_SENSOR_1 + j);
    }
  }
  return true;
}

#endif

#define RESTART_AFTER_LAST_OK_METER_READ 1800 //!< If all energy meter readings are failed within this period, restart the device

#ifdef METER_SHELLY3EM_ENABLED
unsigned shelly3em_last_period = 0;
long shelly3em_period_first_read_ts = 0;
long shelly3em_meter_read_ts = 0; //!< Last time meter was read successfully
long shelly3em_read_count = 0;
float shelly3em_e_in_prev = 0;
float shelly3em_e_out_prev = 0;
float shelly3em_e_in = 0;
float shelly3em_e_out = 0;
float shelly3em_power_in = 0;
int shelly3em_period_read_count = 0;

/**
 * @brief Get earlier read energy values
 *
 * @param netEnergyInPeriod
 * @param netPowerInPeriod
 */
void get_values_shelly3m(float &netEnergyInPeriod, float &netPowerInPeriod)
{
  if (shelly3em_read_count < 2)
  {
    netPowerInPeriod = shelly3em_power_in; // short/no history, using momentary value
    netEnergyInPeriod = 0;
    //  Serial.printf("get_values_shelly3m  shelly3em_read_count: %ld, netPowerInPeriod: %f, netEnergyInPeriod: %f\n", shelly3em_read_count, netPowerInPeriod, netEnergyInPeriod);
  }
  else
  {
    netEnergyInPeriod = (shelly3em_e_in - shelly3em_e_out - shelly3em_e_in_prev + shelly3em_e_out_prev);
#ifdef DEBUG_MODE
    Serial.printf("get_values_shelly3m netEnergyInPeriod (%.1f) = (shelly3em_e_in (%.1f) - shelly3em_e_out (%.1f) - shelly3em_e_in_prev (%.1f) + shelly3em_e_out_prev (%.1f))\n", netEnergyInPeriod, shelly3em_e_in, shelly3em_e_out, shelly3em_e_in_prev, shelly3em_e_out_prev);
#endif
    if ((shelly3em_meter_read_ts - shelly3em_period_first_read_ts) != 0)
    {
      netPowerInPeriod = round(netEnergyInPeriod * 3600.0 / ((shelly3em_meter_read_ts - shelly3em_period_first_read_ts)));
#ifdef DEBUG_MODE
      Serial.printf("get_values_shelly3m netPowerInPeriod (%.1f) = round(netEnergyInPeriod (%.1f) * 3600.0 / (( shelly3em_meter_read_ts (%ld) - shelly3em_period_first_read_ts (%ld) )))  --- time %ld\n", netPowerInPeriod, netEnergyInPeriod, shelly3em_meter_read_ts, shelly3em_period_first_read_ts, (shelly3em_meter_read_ts - shelly3em_period_first_read_ts));
#endif
    }
    else // Do we ever get here with counter check
    {
      netPowerInPeriod = 0;
    }
  }
}

/**
 * @brief Read energy export/import values from Shelly3EM energy meter
 *
 * @param netEnergyInPeriod
 * @param netPowerInPeriod
 */
bool read_meter_shelly3em()
{
  time_t now_in_func;
  time(&now_in_func);

  if (strlen(s.energy_meter_host) == 0)
    return false;

  DynamicJsonDocument doc(2048);

  String url = "http://" + String(s.energy_meter_host) + "/status";
  Serial.println(url);
  DeserializationError error = deserializeJson(doc, httpGETRequest(url.c_str(), ""));

  if (error)
  {
    Serial.print(F("Shelly meter deserializeJson() failed: "));
    Serial.println(error.f_str());
    return false;
  }
  shelly3em_read_count++;

  unsigned now_period = int(now_in_func / (NETTING_PERIOD_SEC));
  shelly3em_meter_read_ts = now_in_func;

  float netEnergyInPeriod;
  float netPowerInPeriod;
  // if (shelly3em_last_period != now_period && (shelly3em_last_period > 0) && shelly3em_period_read_count == 1)
  if (shelly3em_last_period != now_period)
  {
    shelly3em_period_read_count = 0;
  }

  if ((shelly3em_last_period > 0) && shelly3em_period_read_count == 1)
  { // new period
    Serial.println(F("****Shelly - new period counter reset"));
    // shelly3em_last_period = now_period;
    // from this call
    shelly3em_e_in_prev = shelly3em_e_in;
    shelly3em_e_out_prev = shelly3em_e_out;
  }

  // read
  float power_tot = 0;
  int idx = 0;
  float power[3];
  shelly3em_e_in = 0;
  shelly3em_e_out = 0;
  for (JsonObject emeter : doc["emeters"].as<JsonArray>())
  {
    power[idx] = (float)emeter["power"];
    power_tot += power[idx];
    // float current = emeter["current"];
    //  is_valid = emeter["is_valid"];
    if (emeter["is_valid"])
    {
      shelly3em_e_in += (float)emeter["total"];
      shelly3em_e_out += (float)emeter["total_returned"];
    }
    idx++;
  }
  shelly3em_power_in = power_tot;
  shelly3em_period_read_count++;
  // read done

  // first query since boot
  if (shelly3em_last_period == 0)
  {
    Serial.println(F("Shelly - first query since startup"));
    shelly3em_last_period = now_period;
    shelly3em_period_first_read_ts = shelly3em_meter_read_ts;
    shelly3em_e_in_prev = shelly3em_e_in;
    shelly3em_e_out_prev = shelly3em_e_out;
  }
  // if ((shelly3em_meter_read_ts - shelly3em_period_first_read_ts) != 0)

  get_values_shelly3m(netEnergyInPeriod, netPowerInPeriod);
  vars.set(VARIABLE_EXTRA_PRODUCTION, (long)(netEnergyInPeriod < 0) ? 1L : 0L);
  vars.set(VARIABLE_SELLING_POWER, (long)round(-netPowerInPeriod));
  vars.set(VARIABLE_SELLING_ENERGY, (long)round(-netEnergyInPeriod));
  vars.set(VARIABLE_SELLING_POWER_NOW, (long)round(-shelly3em_power_in)); // momentary

  if (shelly3em_last_period != now_period)
  {
    shelly3em_period_first_read_ts = shelly3em_meter_read_ts;
    shelly3em_last_period = now_period;
  }
  return true;
}
#endif

#ifdef INVERTER_FRONIUS_SOLARAPI_ENABLED
/**
 * @brief Reads production data from Fronius invertes (http/json Solar API)
 *
 * @param total_energy address to total energy
 * @param current_power  address to current power
 * @return true if successful
 * @return false if failed
 */
bool read_inverter_fronius_data(long int &total_energy, long int &current_power)
{
  //  globals updated: inverter_total_period_init
  if (strlen(s.energy_meter_host) == 0)
    return false;
  time_t now_in_func;
  time(&now_in_func);
  StaticJsonDocument<64> filter;

  JsonObject filter_Body_Data = filter["Body"].createNestedObject("Data");
  filter_Body_Data["DAY_ENERGY"] = true; // instead of TOTAL_ENERGY
  filter_Body_Data["PAC"] = true;

  StaticJsonDocument<256> doc;
  String inverter_url = "http://" + String(s.energy_meter_host) + "/solar_api/v1/GetInverterRealtimeData.cgi?scope=Device&DeviceId=1&DataCollection=CumulationInverterData";
  Serial.println(inverter_url);

  DeserializationError error = deserializeJson(doc, httpGETRequest(inverter_url.c_str(), ""), DeserializationOption::Filter(filter));

  if (error)
  {
    Serial.print(F("Fronius inverter deserializeJson() failed: "));
    Serial.println(error.f_str());
    energy_produced_period = 0;
    power_produced_period_avg = 0;
    return false;
  }

  for (JsonPair Body_Data_item : doc["Body"]["Data"].as<JsonObject>())
  {
    if (Body_Data_item.key() == "PAC")
    {
      // Serial.print(F(", PAC:"));
      // Serial.print((long)Body_Data_item.value()["Value"]);
      current_power = Body_Data_item.value()["Value"]; // update and return new value
    }
    // use DAY_ENERGY (more accurate) instead of TOTAL_ENERGY
    if (Body_Data_item.key() == "DAY_ENERGY")
    {
      // Serial.print(F("DAY_ENERGY:"));
      total_energy = Body_Data_item.value()["Value"]; // update and return new value
    }
  }

  return true;
} // read_inverter_fronius
#endif

#ifdef INVERTER_SMA_MODBUS_ENABLED

ModbusIP mb; //!< ModbusIP object for reading Modbus interfaces over TCP
#define REG_COUNT 2
uint16_t buf[REG_COUNT];
uint16_t trans;

/**
 * @brief callback for ModBus, currently just debugging
 *
 * @param event
 * @param transactionId
 * @param data
 * @return true
 * @return false
 */
bool cb(Modbus::ResultCode event, uint16_t transactionId, void *data)
{ // Callback to monitor errors
  if (event != Modbus::EX_SUCCESS)
  {
    if (event == Modbus::EX_TIMEOUT)
    {
      Serial.println(F("EX_TIMEOUT"));
    }
    else
    {
      Serial.print(F("Request result: 0x"));
      Serial.println(event, HEX);
    }
    //  mb.disconnect( remote);
  }
  else
  {
    Serial.println(F("Modbus read succesful"));
  }

  return true;
}

/**
 * @brief Get the  ModBus Hreg value
 *
 * @param remote
 * @param reg_offset
 * @param reg_num
 * @param modbusip_unit
 * @return long int
 */
long int get_mbus_value(IPAddress remote, const int reg_offset, uint16_t reg_num, uint8_t modbusip_unit)
{
  long int combined;
  uint16_t trans = mb.readHreg(remote, reg_offset, buf, reg_num, cb, modbusip_unit);

  while (mb.isTransaction(trans))
  { // Check if transaction is active
    mb.task();
    delay(10);
  }

  if (reg_num == 1)
  {
    combined = buf[0];
  }
  else if (reg_num == 2)
  {
    combined = buf[0] * (65536) + buf[1];
    if (buf[0] == 32768)
    { // special case
      combined = 0;
    }
  }
  else
  {
    combined = 0;
  }
  return combined;
}
/**
 * @brief Reads production data from SMA inverted (ModBus TCP)
 *
 * @param total_energy
 * @param current_power
 * @return true
 * @return false
 */
bool read_inverter_sma_data(long int &total_energy, long int &current_power)
{
  uint16_t ip_octets[MAX_SPLIT_ARRAY_SIZE];
  char host_ip[16];
  strncpy(host_ip, s.energy_meter_host, sizeof(host_ip)); // must be locally allocated
  str_to_uint_array(host_ip, ip_octets, ".");

  IPAddress remote(ip_octets[0], ip_octets[1], ip_octets[2], ip_octets[3]);

  uint16_t ip_port = s.energy_meter_port;
  uint8_t modbusip_unit = s.energy_meter_id;

  Serial.printf("ModBus host: [%s], ip_port: [%d], unit_id: [%d] \n", remote.toString().c_str(), ip_port, modbusip_unit);

  mb.task();
  if (!mb.isConnected(remote))
  {
    Serial.print(F("Connecting Modbus TCP..."));
    bool cresult = mb.connect(remote, ip_port);
    Serial.println(cresult);
    mb.task();
  }

  if (mb.isConnected(remote))
  { // Check if connection to Modbus slave is established
    mb.task();
    Serial.println(F("Connection ok. Reading values from Modbus registries."));
    total_energy = get_mbus_value(remote, SMA_TOTALENERGY_OFFSET, 2, modbusip_unit);
    mb.task();
    Serial.print(F(" total energy Wh:"));
    Serial.print(total_energy);

    current_power = get_mbus_value(remote, SMA_POWER_OFFSET, 2, modbusip_unit);
    Serial.print(F(", current power W:"));
    Serial.println(current_power);

    mb.disconnect(remote); // disconect in the end
    mb.task();
    return true;
  }
  else
  {
    Serial.println(F("Connection failed."));
    return false;
  }
  mb.task();
} // read_inverter_sma_data
#endif

/**
 * @brief Read production data from inverters, calls inverter specific functions
 *
 * @param period_changed is this first time to read in this period
 * @return true
 * @return false
 */
bool read_inverter(bool period_changed)
{
  // global: recording_period_start
  // 4 globals updated: inverter_total_period_init, inverter_total_period_init_ok, energy_produced_period, power_produced_period_avg
  long int total_energy = 0;
  long int current_power = 0;
  time_t now_in_func;
  time(&now_in_func);

  bool read_ok = false;
  if (s.energy_meter_type == ENERGYM_FRONIUS_SOLAR)
  {
    read_ok = read_inverter_fronius_data(total_energy, current_power);

    if (((long)inverter_total_period_init > total_energy) && read_ok)
    {
      inverter_total_period_init = 0; // day have changed probably, reset counter, we get day totals from Fronius
      inverter_total_period_init_ok = true;
    }
  }
  else if (s.energy_meter_type == ENERGYM_SMA_MODBUS_TCP)
  {
    read_ok = read_inverter_sma_data(total_energy, current_power);
  }

  if (read_ok)
  {
    time(&energym_read_last);
    if (period_changed || !inverter_total_period_init_ok) // new period or earlier reads in this period were unsuccessfull
    {
      Serial.println(F("PERIOD CHANGED"));
      inverter_total_period_init = total_energy;
      inverter_total_period_init_ok = true;
    }
    energy_produced_period = total_energy - inverter_total_period_init;
  }
  else
  { // read was not ok
    Serial.println(F("Cannot read from the inverter."));
    if (period_changed)
    {
      inverter_total_period_init = 0;
      inverter_total_period_init_ok = false;
      energy_produced_period = 0;
    }
  }

  long int time_since_recording_period_start = now_in_func - recording_period_start;
  if (time_since_recording_period_start > USE_POWER_TO_ESTIMATE_ENERGY_SECS) // in the beginning of period use current power to estimate energy generated
    power_produced_period_avg = energy_produced_period * 3600 / time_since_recording_period_start;
  else
    power_produced_period_avg = current_power;

  Serial.printf("energy_produced_period: %ld , time_since_recording_period_start: %ld , power_produced_period_avg: %ld , current_power:  %ld\n", energy_produced_period, time_since_recording_period_start, power_produced_period_avg, current_power);

  return read_ok;
} // read_inverter

/**
 * @brief Updates global variables based on date, time or time based tariffs
 *
 */
void update_time_based_variables()
{
  time_t now_in_func;
  time(&now_in_func);
  localtime_r(&now_in_func, &tm_struct);
  vars.set(VARIABLE_MM, (long)(tm_struct.tm_mon + 1));
  vars.set(VARIABLE_MMDD, (long)(tm_struct.tm_mon + 1) * 100 + tm_struct.tm_mday);
  vars.set(VARIABLE_WDAY, (long)(tm_struct.tm_wday + 6) % 7 + 1);

  vars.set(VARIABLE_HH, (long)(tm_struct.tm_hour));
  vars.set(VARIABLE_HHMM, (long)(tm_struct.tm_hour) * 100 + tm_struct.tm_min);

#ifdef TARIFF_VARIABLES_FI
  // päiväsähkö/yösähkö (Finnish day/night tariff)
  bool is_day = (6 < tm_struct.tm_hour && tm_struct.tm_hour < 22);
  bool is_winterday = ((6 < tm_struct.tm_hour && tm_struct.tm_hour < 22) && (tm_struct.tm_mon > 9 || tm_struct.tm_mon < 3) && tm_struct.tm_wday != 0);

  vars.set(VARIABLE_DAYENERGY_FI, (long)(is_day ? 1L : 0L));
  vars.set(VARIABLE_WINTERDAY_FI, (long)(is_winterday ? 1L : 0L));
#endif
}
/**
 * @brief Updates global variables based inverter readings.
 *
 */
void update_meter_based_variables()
{
#ifdef METER_SHELLY3EM_ENABLED
  // grid energy meter enabled
  // functionality in read_meter_shelly3em
#endif

#if defined(INVERTER_FRONIUS_SOLARAPI_ENABLED) || defined(INVERTER_SMA_MODBUS_ENABLED)
  // TODO: tsekkaa miksi joskus nousee ylös lyhyeksi aikaa vaikkei pitäisi - johtuu kai siitä että fronius sammuu välillä illalla, laita kuntoon...
  if ((s.energy_meter_type == ENERGYM_FRONIUS_SOLAR) || (s.energy_meter_type == ENERGYM_SMA_MODBUS_TCP))
  {
    vars.set(VARIABLE_EXTRA_PRODUCTION, (long)(power_produced_period_avg > (s.baseload + WATT_EPSILON)) ? 1L : 0L);
    vars.set(VARIABLE_PRODUCTION_POWER, (long)(power_produced_period_avg));
  }
#endif
}

/**
 * @brief Get the price for given time
 *
 * @param ts
 * @return long current price (long), VARIABLE_LONG_UNKNOWN if unavailable
 */

long get_price_for_time(time_t ts)
{
  // use global prices, prices_first_period
  int price_idx = (int)(ts - prices_first_period) / (PRICE_PERIOD_SEC);
  if (price_idx < 0 || price_idx >= MAX_PRICE_PERIODS)
  {
    return VARIABLE_LONG_UNKNOWN;
  }
  else
  {
    return prices[price_idx];
  }
}
void update_variable_from_json(JsonObject variable_list, String doc_key, int variable_id)
{
  if (variable_list.containsKey(doc_key))
    vars.set(variable_id, (long)variable_list[doc_key]);
  else
    vars.set_NA(variable_id);
}
/**
 * @brief Update current variable values from cache file
 *
 * @param current_period_start
 */
void update_price_variables(time_t current_period_start)
{
  Serial.print(F(" update_price_variables "));
  Serial.print(F("  current_period_start: "));
  Serial.println(current_period_start);

  StaticJsonDocument<16> filter;
  char start_str[11];
  itoa(current_period_start, start_str, 10);
  filter[(const char *)start_str] = true;

  StaticJsonDocument<600> doc;
  DeserializationError error;

  // TODO: what happens if cache is expired and no connection to the server
  if (is_cache_file_valid(variables_filename)) // /variables.json
  {
    //  Using cached price data
    File cache_file = LittleFS.open(variables_filename, "r"); // /variables.json
    error = deserializeJson(doc, cache_file, DeserializationOption::Filter(filter));
    cache_file.close();
  }
  else
  {
    Serial.println(F("No valid variable file."));
    return;
  }
  if (error)
  {
    Serial.print(F("DeserializeJson() state query failed: "));
    Serial.println(error.f_str());
    Serial.println(F("Returning..."));
    return;
  }

  JsonObject variable_list = doc[start_str];

  if (variable_list.containsKey("p"))
  {
    float price = (float)variable_list["p"];
    vars.set(VARIABLE_PRICE, (long)(price + 0.5));
  }
  else
  {
    vars.set_NA(VARIABLE_PRICE);
    log_msg(MSG_TYPE_ERROR, PSTR("Cannot get price info for current period."));
  }

  // set current price and forecasted solar avg price difference
  if (vars.is_set(VARIABLE_PVFORECAST_AVGPRICE24) && vars.is_set(VARIABLE_PRICE))
    vars.set(VARIABLE_AVGPRICE24_EXCEEDS_CURRENT, (long)vars.get_l(VARIABLE_PVFORECAST_AVGPRICE24) - (vars.get_l(VARIABLE_PRICE)));
  else
    vars.set_NA(VARIABLE_AVGPRICE24_EXCEEDS_CURRENT);

  /*
    if (variable_list.containsKey("pr9"))
      vars.set(VARIABLE_PRICERANK_9, (long)variable_list["pr9"]); // calculated in calculate_price_ranks
    else
      vars.set_NA(VARIABLE_PRICERANK_9);

    if (variable_list.containsKey("pr24"))
      vars.set(VARIABLE_PRICERANK_24, (long)variable_list["pr24"]);
    else
      vars.set_NA(VARIABLE_PRICERANK_24);*/
  update_variable_from_json(variable_list, "pr9", VARIABLE_PRICERANK_9);
  update_variable_from_json(variable_list, "pr24", VARIABLE_PRICERANK_24);

  update_variable_from_json(variable_list, "pa9", VARIABLE_PRICEAVG_9);
  update_variable_from_json(variable_list, "pd9", VARIABLE_PRICEDIFF_9);
  update_variable_from_json(variable_list, "pa24", VARIABLE_PRICEAVG_24);
  update_variable_from_json(variable_list, "pd24", VARIABLE_PRICEDIFF_24);
}
/**
 * @brief Get the Element Value from piece of xml
 *
 * @param outerXML
 * @return String
 */
String getElementValue(String outerXML)
{
  int s1 = outerXML.indexOf(">", 0);
  int s2 = outerXML.substring(s1 + 1).indexOf("<");
  return outerXML.substring(s1 + 1, s1 + s2 + 1);
}
/**
 * @brief Convert date time string to UTC time stamp
 *
 * @param elem
 * @return time_t
 */
time_t ElementToUTCts(String elem)
{
  String str_val = getElementValue(elem);
  return getTimestamp(str_val.substring(0, 4).toInt(), str_val.substring(5, 7).toInt(), str_val.substring(8, 10).toInt(), str_val.substring(11, 13).toInt(), str_val.substring(14, 16).toInt(), 0);
}

/**
 * @brief Get solar forecast for next 24 hours from BCDC energia web service
 * @details Uses setting  s.forecast_loc for location
 *
 * @return true - success
 * @return false - failed
 */
bool get_solar_forecast()
{
  DynamicJsonDocument doc(3072);
  char fcst_url[100];

  if (strlen(s.forecast_loc) < 2)
  {
    Serial.println(F("Forecast location undefined. Quitting"));
    return false;
  }

  String query_data_raw = String("action=getChartData&loc=") + String(s.forecast_loc);
  WiFiClient wifi_client;

  HTTPClient client_http;
  client_http.setReuse(false);
  client_http.useHTTP10(true); // for json input
  // Your Domain name with URL path or IP address with path

  // reset variables
  vars.set(VARIABLE_PVFORECAST_SUM24, (long)VARIABLE_LONG_UNKNOWN);
  vars.set(VARIABLE_PVFORECAST_VALUE24, (long)VARIABLE_LONG_UNKNOWN);
  vars.set(VARIABLE_PVFORECAST_AVGPRICE24, (long)VARIABLE_LONG_UNKNOWN);

  // snprintf(fcst_url, sizeof(fcst_url), "%s&loc=%s", fcst_url_base, s.forecast_loc);
  strncpy(fcst_url, fcst_url_base, sizeof(fcst_url));
  client_http.begin(wifi_client, fcst_url);
  Serial.printf("fcst_url: %s\n", fcst_url);

  if (WiFi.status() == WL_CONNECTED)
    Serial.println("WiFi is connected.");

  // Specify content-type header
  client_http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  client_http.setUserAgent("ArskaESP");

  // Send HTTP POST request
  int httpResponseCode = client_http.POST(query_data_raw);

  DeserializationError error = deserializeJson(doc, client_http.getStream());
  if (error)
  {
    Serial.print(F("get_solar_forecast deserializeJson() failed: "));
    Serial.println(error.c_str());
    log_msg(MSG_TYPE_ERROR, PSTR("Failed to read energy forecast data"));
    return false;
  }

  time_t first_ts = 0;
  int j = 0;
  float pv_fcst[PV_FORECAST_HOURS];
  float sum_pv_fcst = 0;
  float pv_value_hour;
  float pv_fcst_hour;
  float pv_value = 0;
  float sum_pv_fcst_with_price = 0;
  long price;
  long long pvenergy_item_time;
  time_t pvenergy_time;
  bool got_future_prices = false;
  time_t now_in_func;

  for (JsonObject pvenergy_item : doc["pvenergy"].as<JsonArray>())
  {
    // TODO:FIX DST
    // bcdc antaa timestampin eet:ssä, ei utc:ssä; TODO: localize DST
    pvenergy_item_time = pvenergy_item["time"];
    pvenergy_time = (pvenergy_item_time / 1000) - (3 * 3600);

    Serial.print(pvenergy_time);

    if (first_ts == 0)
      first_ts = pvenergy_time;

    if (j < PV_FORECAST_HOURS)
      pv_fcst[j] = pvenergy_item["value"];

    Serial.print(" value:");
    Serial.println((float)pvenergy_item["value"]);
    pv_fcst_hour = (float)pvenergy_item["value"];

    sum_pv_fcst += pv_fcst_hour;
    price = get_price_for_time(pvenergy_time);

    if (price != VARIABLE_LONG_UNKNOWN)
    {
      sum_pv_fcst_with_price += (float)pv_fcst_hour;
      pv_value_hour = price * pv_fcst_hour / 1000;
      pv_value += pv_value_hour;
      got_future_prices = true; // we got some price data
      //  Serial.printf("j: %d, price: %ld,  sum_pv_fcst_with_price: %f , pv_value_hour: %f, pv_value: %f\n", j, price, sum_pv_fcst_with_price, pv_value_hour, pv_value);
    }
    j++;
  }
  Serial.printf("avg solar price: %f = %f / %f \n", pv_value / sum_pv_fcst_with_price, pv_value, sum_pv_fcst_with_price);

  vars.set(VARIABLE_PVFORECAST_SUM24, (float)sum_pv_fcst);
  if (got_future_prices)
  {
    vars.set(VARIABLE_PVFORECAST_VALUE24, (float)(pv_value));
    vars.set(VARIABLE_PVFORECAST_AVGPRICE24, (float)(pv_value / sum_pv_fcst_with_price));
  }
  else
  {
    vars.set_NA(VARIABLE_PVFORECAST_VALUE24);
    vars.set_NA(VARIABLE_PVFORECAST_AVGPRICE24);
  }
  doc.clear();

  JsonArray pv_fcst_a = doc.createNestedArray("pv_fcst");

  for (int i = 0; i < PV_FORECAST_HOURS; i++)
  {
    pv_fcst_a.add(pv_fcst[i]);
  }

  time(&now_in_func);
  doc["first_period"] = first_ts;
  doc["resolution_m"] = 3600;
  doc["ts"] = now_in_func;
  doc["expires"] = now_in_func + 3600; // time-to-live of the result, under construction, TODO: set to parameters

  // huom ajat 3 h väärin, eli annettu eet vaikka pitäisi olla utc-aika
  // ajat klo xx:30 eli yksittäinen arvo ei matchaa mutta summa jyllä
  // tästä voisi puristaa jonkun aikasarjan
  vars.set(VARIABLE_PVFORECAST_SUM24, sum_pv_fcst);

  File fcst_file = LittleFS.open(fcst_filename, "w"); // Open file for writing
  serializeJson(doc, fcst_file);
  fcst_file.close();

  Serial.print("HTTP Response code: ");
  Serial.println(httpResponseCode);

  // Free resources
  client_http.end();
  return true;
}

/**
 * @brief Get price rank (1 is best price etc.) of given period within given period window
 * @details  Get entries from now to requested duration in the future. \n
If not enough future periods exist, include periods from history to get full window size.
 * @param time
 * @param window_duration_hours
 * @param time_price_idx
 * @param prices
 * @return int
 */
// int get_period_price_rank_in_window(time_t time, int window_duration_hours, int time_price_idx, long prices[]) //[MAX_PRICE_PERIODS]
int get_period_price_rank_in_window(time_t time, int window_duration_hours, int time_price_idx, long prices[], long *window_price_avg, long *price_differs_avg) //[MAX_PRICE_PERIODS]
{
  int window_end_excl_idx = min(MAX_PRICE_PERIODS, time_price_idx + window_duration_hours);
  int window_start_incl_idx = window_end_excl_idx - window_duration_hours;
  long windows_price_sum = 0;

  int rank = 1;
  for (int price_idx = window_start_incl_idx; price_idx < window_end_excl_idx; price_idx++)
  {
    windows_price_sum += prices[price_idx];
    if (prices[price_idx] < prices[time_price_idx])
    {
      rank++;
      // Serial.printf("(%d:%ld) ", price_idx, prices[price_idx]);
    }
  }
  *window_price_avg = windows_price_sum / window_duration_hours;
  *price_differs_avg = prices[time_price_idx] - *window_price_avg;

  return rank;
}

/**
 * @brief Get price ranks to current and future periods for defined windows/blocks
//  * @details Price ranks tells how good is the price compared to other prices within window of periods \n
if rank is 1 then the price is best within the windows (e.g. from current period to next 9 hours) \n
windows/blocks are defined in variable price_variable_blocks, e.g. next 9 hours and 24 hours.
 *
 * @param record_start
 * @param record_end_excl
 * @param time_idx_now
 * @param prices
 * @param doc
 */
void calculate_price_ranks(time_t record_start, time_t record_end_excl, int time_idx_now, long prices[MAX_PRICE_PERIODS], JsonDocument &doc)
{
  Serial.printf("calculate_price_ranks start: %ld, end: %ld, time_idx_now: %d\n", record_start, record_end_excl, time_idx_now);

  int time_idx = time_idx_now;
  char var_code[25];
  long window_price_avg;
  long price_differs_avg;
  int rank;

  for (time_t time = record_start + time_idx_now * PRICE_PERIOD_SEC; time < record_end_excl; time += PRICE_PERIOD_SEC)
  {
    delay(5);

    snprintf(var_code, sizeof(var_code), "%ld", time);
    JsonObject json_obj = doc.createNestedObject(var_code);

    float energyPriceSpot = prices[time_idx] / 100;
    json_obj["p"] = (prices[time_idx] + 50) / 100;

    localtime_r(&time, &tm_struct_g);

    Serial.printf("time: %ld, time_idx: %d , %04d%02d%02dT%02d00, ", time, time_idx, tm_struct_g.tm_year + 1900, tm_struct_g.tm_mon + 1, tm_struct_g.tm_mday, tm_struct_g.tm_hour);
    Serial.printf("energyPriceSpot: %f \n", energyPriceSpot);

    int price_block_count = (int)(sizeof(price_variable_blocks) / sizeof(*price_variable_blocks));
    for (int block_idx = 0; block_idx < price_block_count; block_idx++)
    {
      // rank = get_period_price_rank_in_window(time, price_variable_blocks[block_idx], time_idx, prices);
      window_price_avg = 0;
      price_differs_avg = 0; // should not needed
      rank = get_period_price_rank_in_window(time, price_variable_blocks[block_idx], time_idx, prices, &window_price_avg, &price_differs_avg);
      if (rank > 0)
      {
        snprintf(var_code, sizeof(var_code), "pr%d", price_variable_blocks[block_idx]);
        json_obj[var_code] = rank;
      }
      snprintf(var_code, sizeof(var_code), "pa%d", price_variable_blocks[block_idx]);
      // json_obj[var_code] = window_price_avg / 100;
      json_obj[var_code] = (window_price_avg + 50) / 100; // round

      snprintf(var_code, sizeof(var_code), "pd%d", price_variable_blocks[block_idx]);
      json_obj[var_code] = (price_differs_avg + 50) / 100; // round
    }
    time_idx++;
  }
  Serial.println("calculate_price_ranks finished");
  return;
}
/**
 * @brief Return true if line is "garbage" from http client, possibly read buffer issue
 *
 * @param line
 * @return true
 * @return false
 */
bool is_garbage_line(String line)
{
  if (line.length() == 4 && line.startsWith("5"))
  { // TODO: what creates this, is 5.. really a http code or some kind of counter
    Serial.printf(PSTR("Garbage removed [%s]\n"), line.c_str());
    return true;
  }
  else
    return false;
}
//
//
// TODO:ssl
/**
 * @brief Gets SPOT-prices from EntroE to a json file  (price_data_file_name)
 * @details If existing price data file is not expired use it and return immediately
 *
 * @return true
 * @return false
 */
bool get_price_data()
{
  if (is_cache_file_valid(price_data_filename) && prices_initiated) // "/price_data.json"
  {
    Serial.println(F("Price cache file %s was not expired, returning"));
    return true;
  }
  if (strlen(s.entsoe_api_key)<36 || strlen(s.entsoe_area_code)< 5) {
    log_msg(MSG_TYPE_WARN, PSTR("Check Entso-E parameters (API key and price area) for price updates."));
    return false;
  }

  prices_initiated = true; // TODO:we could read prices from a non-expired cache file, so requery would not be needed

  time_t period_start = 0, period_end = 0;
  time_t record_start = 0, record_end_excl = 0;
  char date_str_start[13];
  char date_str_end[13];
  WiFiClientSecure client_https;

  bool end_reached = false;
  int price_rows = 0;

  time_t start_ts, end_ts; // this is the epoch
  tm tm_struct;
  time_t now_infunc;

  time(&now_infunc);
  start_ts = now_infunc - (3600 * 18); // no previous day after 18h, assume we have data ready for next day

  end_ts = start_ts + SECONDS_IN_DAY * 2;

  DynamicJsonDocument doc(3072);
  JsonArray price_array = doc.createNestedArray("prices");
  int pos = -1;
  long price = VARIABLE_LONG_UNKNOWN;

  // initiate prices
  for (int price_idx = 0; price_idx < MAX_PRICE_PERIODS; price_idx++)
    prices[price_idx] = VARIABLE_LONG_UNKNOWN;

  localtime_r(&start_ts, &tm_struct);
  Serial.println(start_ts);
  snprintf(date_str_start, sizeof(date_str_start), "%04d%02d%02d0000", tm_struct.tm_year + 1900, tm_struct.tm_mon + 1, tm_struct.tm_mday);
  localtime_r(&end_ts, &tm_struct);
  snprintf(date_str_end, sizeof(date_str_end), "%04d%02d%02d0000", tm_struct.tm_year + 1900, tm_struct.tm_mon + 1, tm_struct.tm_mday);

  Serial.printf("Query period: %s - %s\n", date_str_start, date_str_end);
  if (!LittleFS.exists(entsoe_ca_filename))
  {
    log_msg(MSG_TYPE_ERROR, PSTR("Cannot connect to Entso-E server. Certificate file is missing."));
    return false;
  }
  /*
  if (WiFi.macAddress().equals("4C:11:AE:74:68:2C")) {
     log_msg(MSG_TYPE_ERROR, PSTR("Cannot connect to Entso-E server. Simulated error."));
    return false;
  }
  */

  String ca_cert = LittleFS.open(entsoe_ca_filename, "r").readString();
  client_https.setCACert(ca_cert.c_str());

  client_https.setTimeout(15000); // 15 Seconds
  delay(1000);

  Serial.println(F("Connecting with CA check."));

  if (!client_https.connect(host_prices, httpsPort))
  {
    int err;
    char error_buf[70];
    err = client_https.lastError(error_buf, sizeof(error_buf));
    if (err != 0)
      log_msg(MSG_TYPE_ERROR, error_buf);
    else
      log_msg(MSG_TYPE_ERROR, PSTR("Cannot connect to Entso-E server. Quitting price query."));

    return false;
  }

  String url = url_base + String("&securityToken=") + s.entsoe_api_key + String("&In_Domain=") + s.entsoe_area_code + String("&Out_Domain=") + s.entsoe_area_code + String("&periodStart=") + date_str_start + String("&periodEnd=") + date_str_end;
  Serial.print("requesting URL: ");

  Serial.println(url);

  client_https.print(String("GET ") + url + " HTTP/1.1\r\n" +
                     "Host: " + host_prices + "\r\n" +
                     "User-Agent: ArskaNoderESP\r\n" +
                     "Connection: close\r\n\r\n");

  Serial.println("request sent");
  if (client_https.connected())
    Serial.println("client_https connected");
  else
    Serial.println("client_https not connected");

  bool save_on = false;
  bool read_ok = false;
  while (client_https.connected())
  {
    String lineh = client_https.readStringUntil('\n');
    //  Serial.println(lineh);

    if (lineh == "\r")
    {
      Serial.println("headers received");
      break;
    }
  }

  Serial.println(F("Waiting the document"));
  String line;
  String line2;
  bool line_incomplete = false;
  bool contains_zero_prices = false;
  // we must remove extra carbage cr (13) + "5xx" + cr lines
  // .available() is 1 or low when the "garbage" comes, no more/much to read, after about 8k buffer is read
  while (client_https.available())
  {
    if (!line_incomplete)
    {
      line = client_https.readStringUntil('\n'); //  \r tulee vain dokkarin lopussa (tai bufferin saumassa?)

      if (line.charAt(line.length() - 1) == 13)
      {
        if (is_garbage_line(line)) // skip error status "garbage" line
          continue;
        line.trim();            // remove cr and mark line incomplete
        line_incomplete = true; // we do not have whole line yet
      }
    }
    else // line is incomplete, we will get more to add
    {
      line2 = client_https.readStringUntil('\n');
      if (line2.charAt(line2.length() - 1) == 13)
      {
        if (is_garbage_line(line2)) // skip error status "garbage" line
          continue;
      }
      else
        line_incomplete = false; // ended normally

      line2.trim(); // remove cr
      line = line + line2;
      Serial.print("Combined line:");
      Serial.println(line);
    }

    //   Serial.println(line);

    if (line.indexOf("<Publication_MarketDocument") > -1)
      save_on = true;
    if (line.indexOf("</Publication_MarketDocument>") > -1)
    {
      save_on = false;
      read_ok = true;
    }

    if (line.endsWith(F("</period.timeInterval>")))
    { // header dates
      record_end_excl = period_end;
      record_start = record_end_excl - (PRICE_PERIOD_SEC * MAX_PRICE_PERIODS);
      prices_first_period = record_start;
      Serial.printf("period_start: %ld record_start: %ld - period_end: %ld\n", period_start, record_start, period_end);
    }

    if (line.endsWith(F("</start>")))
      period_start = ElementToUTCts(line);

    if (line.endsWith(F("</end>")))
      period_end = ElementToUTCts(line);

    if (line.endsWith(F("</position>")))
    {
      pos = getElementValue(line).toInt();
      //    Serial.println(pos);
    }

    else if (line.endsWith(F("</price.amount>")))
    {
      price = int(getElementValue(line).toFloat() * 100);

      if (abs(price) < 0.001) // suspicious value, could be parsing/data error
        contains_zero_prices = true;

      price_rows++;
      //   Serial.println(line);
    }
    else if (line.endsWith("</Point>"))
    {
      int period_idx = pos - 1 + (period_start - record_start) / PRICE_PERIOD_SEC;
      if (period_idx >= 0 && period_idx < MAX_PRICE_PERIODS)
      {

        prices[period_idx] = price;
        Serial.printf("period_idx %d, price: %f\n", period_idx, (float)price / 100);
        price_array.add(price);
      }
      // else
      //   Serial.printf("Cannot store price, period_idx: %d\n", period_idx);

      pos = -1;
      price = VARIABLE_LONG_UNKNOWN;
    }

    if (line.indexOf(F("</Publication_MarketDocument")) > -1)
    { // this signals the end of the response from XML API
      end_reached = true;
      save_on = false;
      read_ok = true;
      Serial.println(F("end_reached"));
      break;
    }

    if (line.indexOf(F("Service Temporarily Unavailable")) > 0)
    {
      Serial.println(F("Service Temporarily Unavailable"));
      read_ok = false;
      break;
    }
  }

  client_https.stop();

  if (end_reached && (price_rows >= MAX_PRICE_PERIODS))
  {
    time(&now_infunc);
    doc["record_start"] = record_start;
    doc["record_end_excl"] = record_end_excl;
    doc["resolution_m"] = NETTING_PERIOD_MIN;
    doc["ts"] = now_infunc;

    if (contains_zero_prices)
    { // potential problem in latest fetch, give shorter validity time
      Serial.println("Contains zero prices! Retry in 2 hours.");
      doc["expires"] = now_infunc + (2 * 3600);
    }
    else
    {
      Serial.println("No zero prices.");
      doc["expires"] = record_end_excl - (11 * 3600); // prices for next day should come after 12hUTC, so no need to query before that
    }

    File prices_file = LittleFS.open(price_data_filename, "w"); // Open file for writing "/price_data.json"
    serializeJson(doc, prices_file);
    prices_file.close();
    Serial.println(F("Finished succesfully get_price_data."));

    // TEST INFLUX
    Serial.println(F("Testing influx update update_prices_to_influx"));
    update_prices_to_influx();

    return true;
  }
  else
  {
    Serial.printf("Prices are not saved, end_reached %d, price_rows %d \n", end_reached, price_rows);
  }
  Serial.println(read_ok ? F("Price query OK") : F("Price query failed"));

  if (!read_ok)
    log_msg(MSG_TYPE_ERROR, PSTR("Failed to get price data."));

  return read_ok;
}

/**
 * @brief Update calculated variable values from another device, under construction
 *
 * @return true
 * @return false
 */
/*
bool query_external_variables()
{
  StaticJsonDocument<16> filter;
  filter["variables"] = true;
  StaticJsonDocument<800> doc;

  char variable_url[100];

  Serial.println("query_external_variables b");

  if (strlen(s.variable_server) == 0)
  {
    log_msg(MSG_TYPE_ERROR, PSTR("Variable server undefined."));
    return false;
  }

  snprintf(variable_url, sizeof(variable_url), "http://%s/status", s.variable_server);
  Serial.println(variable_url);
  DeserializationError error = deserializeJson(doc, httpGETRequest(variable_url, ""), DeserializationOption::Filter(filter));

  if (error)
  {
    Serial.print(F("query_external_variables deserializeJson() failed: "));
    Serial.println(error.c_str());
    log_msg(MSG_TYPE_ERROR, PSTR("Cannot process variable data."));

    return false;
  }

  JsonObject variables = doc["variables"];
  Serial.println("variabsle- size:");
  Serial.println(doc["variables"].size());

  // using C++11 syntax (preferred):
  for (JsonPair kv : variables)
  {
    Serial.print(kv.key().c_str());
    Serial.print(" = ");
    Serial.println(kv.value().as<const char *>()); //  as<char*>() with as<const char*>() [
  }
  return true;
}
*/

/**
 * @brief Update price rank variables to a cache file
 *
 * @return true
 * @return false
 */
bool update_price_rank_variables()
{
  time_t record_start = 0, record_end_excl = 0;
  time_t start_ts, end_ts; // this is the epoch
  time_t now_infunc;

  time(&start_ts);
  start_ts -= SECONDS_IN_DAY;
  end_ts = start_ts + SECONDS_IN_DAY * 2;

  DynamicJsonDocument doc(6144);

  File prices_file_in = LittleFS.open(price_data_filename, "r"); // "/price_data.json"
  DeserializationError error = deserializeJson(doc, prices_file_in);
  prices_file_in.close();
  if (error)
  {
    Serial.print(F("update_price_rank_variables deserializeJson() failed: "));
    Serial.println(error.f_str());
    return false;
  }

  record_start = doc["first_period"];
  JsonArray prices_array = doc["prices"];
  for (unsigned int i = 0; (i < prices_array.size() && i < MAX_PRICE_PERIODS); i++)
  {
    prices[i] = (long)prices_array[i];
  }

  record_end_excl = (time_t)doc["record_end_excl"];
  record_start = (time_t)doc["record_start"];

  time(&now_infunc);
  int time_idx_now = int((now_infunc - record_start) / PRICE_PERIOD_SEC);
  Serial.printf("time_idx_now: %d, price now: %f\n", time_idx_now, (float)prices[time_idx_now] / 100);
  Serial.printf("record_start: %ld, record_end_excl: %ld\n", record_start, record_end_excl);

  calculate_price_ranks(record_start, record_end_excl, time_idx_now, prices, doc);

  doc["record_start"] = record_start;
  doc["resolution_m"] = NETTING_PERIOD_MIN;
  doc["ts"] = now_infunc;
  doc["expires"] = now_infunc + 3600; // time-to-live of the result, under construction, TODO: set to parameters

  File prices_file_out = LittleFS.open(variables_filename, "w"); // Open file for writing /variables.json
  serializeJson(doc, prices_file_out);
  prices_file_out.close();
  Serial.println(F("Finished succesfully update_price_rank_variables."));

  return true;
}

/**
 * @brief Get  status info for admin / view forms
 *
 * @param out
 */
// TODO: check if deprecated
void get_status_fields(char *out)
{
  char buff[150];
  time_t current_time;
  time(&current_time);

  char time1[9];
  char time2[9];
  char eupdate[20];

#ifdef SENSOR_DS18B20_ENABLED

  // localtime_r(&temperature_updated, &tm_struct);
  // snprintf(buff, 150, "<div class='fld'><div>Temperature: %s (%02d:%02d:%02d)</div>\n</div>\n", String(ds18B20_temp_c, 1).c_str(), tm_struct.tm_hour, tm_struct.tm_min, tm_struct.tm_sec);
  // strcat(out, buff);
  //
#endif
  char rtc_status[15];
#ifdef RTC_DS3231_ENABLED
  if (rtc_found)
    strncpy(rtc_status, "(RTC OK)", sizeof(rtc_status));
  else
    strncpy(rtc_status, "(RTC FAILED)", sizeof(rtc_status));
#else
  strncpy(rtc_status, "", sizeof(rtc_status));
#endif

  localtime_r(&recording_period_start, &tm_struct);
  snprintf(time1, sizeof(time1), "%02d:%02d:%02d", tm_struct.tm_hour, tm_struct.tm_min, tm_struct.tm_sec);
  localtime_r(&energym_read_last, &tm_struct);

  if (energym_read_last == 0)
  {
    strncpy(time2, "", sizeof(time2));
    strncpy(eupdate, ", not updated", sizeof(eupdate));
  }
  else
  {
    snprintf(time2, sizeof(time2), "%02d:%02d:%02d", tm_struct.tm_hour, tm_struct.tm_min, tm_struct.tm_sec);
    strncpy(eupdate, "", sizeof(eupdate));
  }

  return;
}

// new force_up_from
bool is_force_up_valid(int channel_idx)
{
  time_t now_in_func;
  time(&now_in_func);
  // Serial.printf("force_up_from %ld < %ld < %ld , onko", s.ch[channel_idx].force_up_from, now_in_func, s.ch[channel_idx].force_up_until);

  bool is_valid = ((s.ch[channel_idx].force_up_from < now_in_func) && (now_in_func < s.ch[channel_idx].force_up_until));
  return is_valid;
}

int active_condition(int channel_idx)
{
  for (int i = 0; i < CHANNEL_CONDITIONS_MAX; i++)
  {
    if (s.ch[channel_idx].conditions[i].condition_active)
      return i;
  }
  return -1;
}

/**
 * @brief Template processor for the admin form
 *
 * @param var
 * @return String
 */
String admin_form_processor(const String &var)
{
  if (var == "wifi_ssid")
    return s.wifi_ssid;
  if (var == "wifi_ssid_edit")
  {
    return "";
  }
  if (var == "wifi_password")
    return s.wifi_password;
  if (var == "http_username")
    return s.http_username;
  if (var == "http_password")
    return s.http_password;
  if (var == "lang")
    return s.lang;
  if (var == F("timezone"))
    return String(s.timezone);
  if (var == F("hw_template_id"))
    return String(s.hw_template_id);

  return String();
}

/**
 * @brief Template processor for the service (inputs) form
 *
 * @param var
 * @return String
 */
String inputs_form_processor(const String &var)
{
  // Serial.println(var);
  if (var == F("emt"))
    return String(s.energy_meter_type);

  if (var == F("emt_options"))
  {
    char out[200];
    char buff[50];
    for (int energym_idx = 0; energym_idx <= ENERGYM_MAX; energym_idx++)
    {
      snprintf(buff, sizeof(buff), "<option value='%d' %s>%s</>", energym_idx, (s.energy_meter_type == energym_idx) ? "selected" : "", energym_strings[energym_idx]);
      strcat(out, buff);
    }
    return String(out);
  }
  if (var == F("VARIABLE_SOURCE_ENABLED"))
#ifdef VARIABLE_SOURCE_ENABLED
    return String(1);
#else
    return String(0);
#endif

  if (var == F("emh"))
    return String(s.energy_meter_host);
  if (var == F("emh"))
    return String(s.energy_meter_host);
  if (var == F("emp"))
    return String(s.energy_meter_port);
  if (var == F("emid"))
    return String(s.energy_meter_id);

  if (var == F("baseload"))
    return String(s.baseload);

  if (var == F("variable_mode"))
    return String((VARIABLE_MODE_SOURCE)); // removed selection

  if (var == F("entsoe_api_key"))
    return String(s.entsoe_api_key);

  if (var == F("entsoe_area_code"))
    return String(s.entsoe_area_code);

  if (var == F("variable_server"))
    return String(s.variable_server);
  if (var == F("forecast_loc"))
    return String(s.forecast_loc);

  // influx
  if (var == F("INFLUX_REPORT_ENABLED"))
#ifdef INFLUX_REPORT_ENABLED
    return String(1);
#else
    return String(0);
#endif
#ifdef INFLUX_REPORT_ENABLED
  if (var == F("influx_url"))
    return String(s_influx.url);
  if (var == F("influx_token"))
  {
    return String(s_influx.token);
  }
  if (var == F("influx_org"))
    return String(s_influx.org);
  if (var == F("influx_bucket"))
    return String(s_influx.bucket);
#endif

  return String();
}

/**
 * @brief Template processor for the javascript code.
 *
 * @param var
 * @return String
 */
String jscode_form_processor(const String &var)
{
  // Serial.printf("jscode_form_processor starting processing %s\n", var.c_str());
  char out[1000]; // depends on VARIABLE_COUNT
  char buff[50];
  if (var == F("compile_date"))
    return String(compile_date);
  if (var == F("HWID"))
    return String(HWID);

  if (var == F("VERSION"))
    return String(VERSION);
  if (var == F("VERSION_SHORT"))
    return String(VERSION_SHORT);

  if (var == F("version_fs"))
    return String(version_fs);

  if (var == F("switch_subnet_wifi"))
    return s.switch_subnet_wifi.toString();

  if (var == F("RULE_STATEMENTS_MAX"))
    return String(RULE_STATEMENTS_MAX);
  if (var == "CHANNEL_COUNT")
    return String(CHANNEL_COUNT);
  if (var == F("CHANNEL_CONDITIONS_MAX"))
    return String(CHANNEL_CONDITIONS_MAX);
  if (var == F("OPERS"))
  {
    strcpy(out, "[");
    for (int i = 0; i < OPER_COUNT; i++)
    {
      snprintf(buff, 40, "[%d, \"%s\", %s, %s, %s, %s]", opers[i].id, opers[i].code, opers[i].gt ? "true" : "false", opers[i].eq ? "true" : "false", opers[i].reverse ? "true" : "false", opers[i].boolean_only ? "true" : "false");
      // TODO: memory safe strncat
      strcat(out, buff);
      if (i < OPER_COUNT - 1)
        strcat(out, ", "); // TODO: memory safe strncat
    }
    strcat(out, "]"); // TODO: memory safe strncat
    return out;
  }
  if (var == F("channels"))
  { // used by Javascript
    strcpy(out, "[");
    channel_struct *chp;
    for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
    {
      chp = &s.ch[channel_idx];
      snprintf(buff, 50, "{\"cm\": %d ,\"tid\":%d}", (int)chp->config_mode, (int)chp->template_id);
      // TODO: memory safe strncat
      strcat(out, buff);
      if (channel_idx < CHANNEL_COUNT - 1)
        strcat(out, ", ");
    }
    strcat(out, "]");
    return out;
  }

  if (var == F("channel_types"))
  { // used by Javascript
    strcpy(out, "[");
    for (int channel_type_idx = 0; channel_type_idx < CHANNEL_TYPE_COUNT; channel_type_idx++)
    {
      snprintf(buff, 50, "{\"id\": %d ,\"name\":\"%s\"}", (int)channel_types[channel_type_idx].id, channel_types[channel_type_idx].name);
      // TODO: memory safe strncat
      strcat(out, buff);
      if (channel_type_idx < CHANNEL_TYPE_COUNT - 1)
        strcat(out, ", ");
    }
    strcat(out, "]");
    return out;
  }

  // TODO: currently unused when coded in html template
  if (var == F("hw_templates"))
  { // used by Javascript
    strcpy(out, "[");
    for (int hw_template_idx = 0; hw_template_idx < HW_TEMPLATE_COUNT; hw_template_idx++)
    {
      snprintf(buff, 50, "{\"id\": %d ,\"name\":\"%s\"}", (int)hw_templates[hw_template_idx].id, hw_templates[hw_template_idx].name);
      // TODO: memory safe strncat
      strcat(out, buff);
      if (hw_template_idx < HW_TEMPLATE_COUNT - 1)
        strcat(out, ", ");
    }
    strcat(out, "]");
    return out;
  }

  if (var == F("VARIABLES")) // used by Javascript
  {
    strcpy(out, "[");
    int variable_count = vars.get_variable_count();
    variable_st variable;
    for (int variable_idx = 0; variable_idx < variable_count; variable_idx++)
    {
      // Serial.println(variable.code);
      //  YYY
      vars.get_variable_by_idx(variable_idx, &variable);
      snprintf(buff, 40, "[%d, \"%s\", %d]", variable.id, variable.code, variable.type);
      strcat(out, buff); // TODO: memory safe strncat
      if (variable_idx < variable_count - 1)
        strcat(out, ", ");
    }
    strcat(out, "]");
    return out;
  };
  if (var == "lang")
    return s.lang;

  if (var == F("using_default_password"))
    return (strcmp(s.http_password, default_http_password) == 0) ? "true" : "false";
  if (var == F("DEBUG_MODE"))
#ifdef DEBUG_MODE
    return "true";
#endif
  if (var == "wifi_in_setup_mode")
    return String(wifi_in_setup_mode ? "true" : "false");

  return String();
}

// variables for the admin form
/**
 * @brief Template processor for the admin form
 *
 * @param var
 * @return String
 */
String setup_form_processor(const String &var)
{
  // Serial.printf("Debug setup_form_processor: %s\n", var.c_str());
  // Javascript replacements
  if (var == "CHANNEL_CONDITIONS_MAX")
    return String(CHANNEL_CONDITIONS_MAX);
  if (var == "wifi_in_setup_mode")
    return String(wifi_in_setup_mode ? 1 : 0);
  return String("");
}

/**
 * @brief Read grid or production info from energy meter/inverter
 *
 */
void read_energy_meter()
{
  bool read_ok;
  time_t now_in_func;
  // SHELLY
  if (s.energy_meter_type == ENERGYM_SHELLY3EM)
  {
#ifdef METER_SHELLY3EM_ENABLED
    read_ok = read_meter_shelly3em();
#endif
  }
  //INVERTER
  else if (s.energy_meter_type == ENERGYM_FRONIUS_SOLAR or (s.energy_meter_type == ENERGYM_SMA_MODBUS_TCP))
  {
#if defined(INVERTER_FRONIUS_SOLARAPI_ENABLED) || defined(INVERTER_SMA_MODBUS_ENABLED)
    read_ok = read_inverter(period_changed);
#endif
  }
  // NO ENERGY METER DEFINED, function should not be called
  else {
    return;
  }

  time(&now_in_func);
  if (read_ok)
    energym_read_last = now_in_func;
  else if ((energym_read_last + RESTART_AFTER_LAST_OK_METER_READ < now_in_func) && (energym_read_last > 0)) // restart after too many errors
  {
    Serial.println(("Restarting after failed energy meter reads."));

    WiFi.disconnect();
    log_msg(MSG_TYPE_FATAL, PSTR("Restarting after failed energy meter reads."), true);
    delay(2000);
    ESP.restart();
  }
  else
    log_msg(MSG_TYPE_ERROR, PSTR("Failed to read energy meter. Check Wifi"));
}
//

/**
 * @brief Get a channel to switch next
 * @details There can be multiple channels which could be switched but not all are switched at the same round
 *
 * @param is_rise
 * @param switch_count
 * @return int
 */
int get_channel_to_switch(bool is_rise, int switch_count)
{
  int nth_channel = random(0, switch_count) + 1;
  int match_count = 0;
  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {
    if (is_rise && !s.ch[channel_idx].is_up && s.ch[channel_idx].wanna_be_up)
    { // we should rise this up
      match_count++;
      if (match_count == nth_channel)
        return channel_idx;
    }
    if (!is_rise && s.ch[channel_idx].is_up && !s.ch[channel_idx].wanna_be_up)
    { // we should drop this channel
      match_count++;
      if (match_count == nth_channel)
        return channel_idx;
    }
  }
  return -1; // we should not end up here
}

//
/**
 * @brief Switch a channel up/down
 *
 * @param channel_idx
 * @param up
 * @return true
 * @return false
 */
bool set_channel_switch(int channel_idx, bool up)
{
  char error_msg[ERROR_MSG_LEN];
  if (s.ch[channel_idx].type == CH_TYPE_UNDEFINED)
    return false;

  ch_counters.set_state(channel_idx, up); // counters
  if ((s.ch[channel_idx].type == CH_TYPE_GPIO_FIXED) || (s.ch[channel_idx].type == CH_TYPE_GPIO_USER_DEF))
  {
    digitalWrite(s.ch[channel_idx].switch_id, (up ? HIGH : LOW));
    return true;
  }
  else if (s.ch[channel_idx].type == CH_TYPE_WIFI_SHELLY_1GEN)
  {
    IPAddress switch_ip = s.switch_subnet_wifi;
    switch_ip[3] = s.ch[channel_idx].switch_id; // 24 (or longer) bit subnet mask assumed, last octet from switch_id
    String url_to_call = "http://" + switch_ip.toString() + "/relay/0?turn=" + (up ? String("on") : String("off"));

    Serial.printf("url_to_call:%s\n", url_to_call.c_str());

    // DynamicJsonDocument doc(256);
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, httpGETRequest(url_to_call.c_str(), "", 5000)); // shorter connect timeout for a local switch
    if (error)
    {
      Serial.print(F("Shelly relay call deserializeJson() failed: "));
      Serial.println(error.f_str());

      snprintf(error_msg, ERROR_MSG_LEN, PSTR("Cannot connect channel %d switch at %s "), channel_idx + 1, switch_ip.toString().c_str());
      log_msg(MSG_TYPE_WARN, error_msg, false);
      return false;
    }
    else
    {
      Serial.println(F("Shelly relay switched."));

      if (doc.containsKey("ison"))
      {
        if (doc["ison"].is<bool>() && doc["ison"] == up)
        {
          Serial.println("Switch set properly.");
        }
        else
        {
          Serial.println("Switch not set properly.");
          snprintf(error_msg, ERROR_MSG_LEN, PSTR("Switch for channel  %d switch at %s not timely set."), channel_idx + 1, switch_ip.toString().c_str());
          log_msg(MSG_TYPE_WARN, error_msg, false);
        }
      }
      else
      {
        Serial.println("Shelly, invalid response");
        snprintf(error_msg, ERROR_MSG_LEN, PSTR("Switch for channel  %d switch at %s, invalid response."), channel_idx + 1, switch_ip.toString().c_str());
        log_msg(MSG_TYPE_WARN, error_msg, false);
      }

      return true;
    }
  }

  /* TODOX REWRITE
    else if (s.ch[channel_idx].type == CH_TYPE_SHELLY_ONOFF && s.energy_meter_type == ENERGYM_SHELLY3EM)
    {
      String url_to_call = "http://" + String(s.energy_meter_host) + "/relay/0?turn=";
      if (up)
        url_to_call = url_to_call + String("on");
      else
        url_to_call = url_to_call + String("off");
      Serial.println(url_to_call);

      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, httpGETRequest(url_to_call.c_str(), "")); // muutettu käyttämään omaa funtiota
      if (error)
      {
        Serial.print(F("Shelly relay call deserializeJson() failed: "));
        Serial.println(error.f_str());
        return false;
      }
      else
      {
        Serial.println(F("Shelly relay switched."));
        return true;
      }
    }
    */
  // else
  //   Serial.print(F("Cannot switch this channel"));

  return false;
}

/**
 * @brief Check which channels can be raised /dropped
 *
 */
void update_relay_states()
{
  time_t now_in_func;
  bool forced_up;
  time(&now_in_func);

  // loop channels and check whether channel should be up
  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {
    if (s.ch[channel_idx].type == CH_TYPE_UNDEFINED)
    {
      s.ch[channel_idx].wanna_be_up = false;
      continue;
    }

    // reset condition_active variable
    bool wait_minimum_uptime = ((now_in_func - s.ch[channel_idx].toggle_last) < s.ch[channel_idx].uptime_minimum); // channel must stay up minimum time

    if (s.ch[channel_idx].force_up_until == -1)
    { // force down
      s.ch[channel_idx].force_up_until = 0;
      wait_minimum_uptime = false;
    }
    // forced_up = (s.ch[channel_idx].force_up_until > now_in_func); // signal to keep it up
    forced_up = (is_force_up_valid(channel_idx));

    if (s.ch[channel_idx].is_up && (wait_minimum_uptime || forced_up))
    {
      Serial.printf("Not yet time to drop channel %d . Since last toggle %d, force_up_until: %ld .\n", channel_idx, (int)(now_in_func - s.ch[channel_idx].toggle_last), s.ch[channel_idx].force_up_until);
      s.ch[channel_idx].wanna_be_up = true;
      continue;
    }

    for (int condition_idx = 0; condition_idx < CHANNEL_CONDITIONS_MAX; condition_idx++)
    {
      s.ch[channel_idx].conditions[condition_idx].condition_active = false;
    }

    if (!s.ch[channel_idx].is_up && forced_up)
    { // the channel is now down but should be forced up
      s.ch[channel_idx].wanna_be_up = true;
      Serial.println("forcing up");
      continue;
    }

    // now checking normal state based conditions
    s.ch[channel_idx].wanna_be_up = false;
    // loop channel targets until there is match (or no more targets)
    bool statement_true;
    // if no statetements -> false (or default)
    int nof_valid_statements;
    bool one_or_more_failed;

    for (int condition_idx = 0; condition_idx < CHANNEL_CONDITIONS_MAX; condition_idx++)
    {
      nof_valid_statements = 0;
      one_or_more_failed = false;
      // now loop the statement until end or false statement
      for (int statement_idx = 0; statement_idx < RULE_STATEMENTS_MAX; statement_idx++)
      {
        statement_st *statement = &s.ch[channel_idx].conditions[condition_idx].statements[statement_idx];
        if (statement->variable_id != -1)
        {
          nof_valid_statements++;

          //   Serial.printf("update_relay_states statement.variable_id: %d\n", statement->variable_id);
          statement_true = vars.is_statement_true(statement);
          if (!statement_true)
          {
            one_or_more_failed = true;
            break;
          }
        }
      }

      if (!(nof_valid_statements == 0) && !one_or_more_failed)
      {
        if (!s.ch[channel_idx].conditions[condition_idx].condition_active)
        {
          // report debug change
          Serial.printf("channel_idx %d, condition_idx %d matches, channel wanna_be_up: %s\n", channel_idx, condition_idx, s.ch[channel_idx].wanna_be_up ? "true" : "false");
        }
        s.ch[channel_idx].wanna_be_up = s.ch[channel_idx].conditions[condition_idx].on;
        s.ch[channel_idx].conditions[condition_idx].condition_active = true;
        break;
      }

    } // conditions loop
  }   // channel loop
}

/**
 * @brief Set relays up and down
 * @details  MAX_CHANNELS_SWITCHED_AT_TIME defines how many channel can be switched at time \n
 *
 *
 */
void set_relays()
{
  //
  // random
  int rise_count = 0;
  int drop_count = 0;
  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {
    if (!s.ch[channel_idx].is_up && s.ch[channel_idx].wanna_be_up)
      rise_count++;
    if (s.ch[channel_idx].is_up && !s.ch[channel_idx].wanna_be_up)
      drop_count++;
  }
  if (rise_count > 0 || drop_count > 0)
    Serial.printf("set_relays rise_count: %d, drop_count: %d\n", rise_count, drop_count);

  int switchings_to_todo;
  bool is_rise;
  int oper_count;

  for (int drop_rise = 0; drop_rise < 2; drop_rise++)
  { // first round drops, second rises
    is_rise = (drop_rise == 1);
    oper_count = is_rise ? rise_count : drop_count;
    switchings_to_todo = min(oper_count, MAX_CHANNELS_SWITCHED_AT_TIME);
    for (int i = 0; i < switchings_to_todo; i++)
    {
      int ch_to_switch = get_channel_to_switch(is_rise, oper_count--);
      Serial.printf("Switching ch %d  (%d) from %d .-> %d\n", ch_to_switch, s.ch[ch_to_switch].switch_id, s.ch[ch_to_switch].is_up, is_rise);
      s.ch[ch_to_switch].is_up = is_rise;
      s.ch[ch_to_switch].toggle_last = now;

      // digitalWrite(s.ch[ch_to_switch].switch_id, (s.ch[ch_to_switch].is_up ? HIGH : LOW));
      set_channel_switch(ch_to_switch, s.ch[ch_to_switch].is_up);
    }
  }
}
/*
#define WRONG_PW_CHECK_DELAY 10
void wrong_pw_delay() {
  // globals: last_wrong_pw_ts, wrong_pw_count
  time_t current_time;
  time(&current_time);
  if (current_time-last_wrong_pw_ts<WRONG_PW_CHECK_DELAY) {
    wrong_pw_count++;
    last_wrong_pw_ts = current_time;
    delay(wrong_pw_count*1000);
  }
  else {
    wrong_pw_count = 0;
  }
}
*/
/*
TO BE REMOVED
void sendForm(AsyncWebServerRequest *request, const char *template_name)
{
  if (!request->authenticate(s.http_username, s.http_password)) {
    wrong_pw_delay();
    return request->requestAuthentication();
    }
  check_forced_restart(true); // if in forced ap-mode, reset counter to delay automatic restart
  request->send(LittleFS, template_name, "text/html", false, setup_form_processor);
}
*/

/**
 * @brief Authenticate and send given template processed by given template processor
 *
 * @param request
 * @param template_name
 * @param processor
 */
void sendForm(AsyncWebServerRequest *request, const char *template_name, AwsTemplateProcessor processor)
{
  Serial.printf("sendForm2: %s\n", template_name);
  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();
  check_forced_restart(true); // if in forced ap-mode, reset counter to delay automatic restart
  request->send(LittleFS, template_name, "text/html", false, processor);
}

#ifdef OTA_UPDATE_ENABLED

// The other templates come from littlefs filesystem, but on update we do not want to be dependant on that
const char update_page_html[] PROGMEM = "<html><head></head>\
<!-- https://codewithmark.com/easily-create-file-upload-progress-bar-using-only-javascript -->\
<body style='background-color: #1a1e15;margin: 1.8em; font-size: 20px;font-family:  Helvetica, Arial, sans-serif;color: #f7f7e6;'>\
<script type = 'text/javascript'>\
 function _(el){return document.getElementById(el);}\
    function upload() {\
        var file = _('firmware').files[0];\
        var formdata = new FormData();\
        formdata.append('firmware', file);\
        var ajax = new XMLHttpRequest();\
        ajax.upload.addEventListener('progress', progressHandler, false);\
        ajax.addEventListener('load', completeHandler, false);\
        ajax.addEventListener('error', errorHandler, false);\
        ajax.addEventListener('abort', abortHandler, false);\
        ajax.open('POST', 'doUpdate');\
        ajax.send(formdata);\
    }\
    function progressHandler(event) { _('loadedtotal').innerHTML = 'Uploaded ' + event.loaded + ' bytes of ' + event.total;  var percent = (event.loaded / event.total) * 100;  _('progressBar').value = Math.round(percent);_('status').innerHTML = Math.round(percent) + '&percnt; uploaded... please wait'; }\
    function reloadAdmin() { window.location.href = '/admin';}\
    function completeHandler(event) {_('status').innerHTML = event.target.responseText; _('progressBar').value = 0;setTimeout(reloadAdmin, 20000);}\
    function errorHandler(event) { _('status').innerHTML = 'Upload Failed';}\
    function abortHandler(event) {   _('status').innerHTML = 'Upload Aborted';}\
    </script>\
    <h1>Firmware and filesystem update</h1>\
    <p>Update firmware first and filesystem (littlefs.bin) after that (if required).</p>\
        <form method='post' enctype='multipart/form-data'>\
        <input type='file' name ='firmware' id='firmware' onchange='upload()'><br>\
        <progress id='progressBar' value='0' max='100' style='width:250px;'></progress>\
        <h2 id='status'></h2>\
        <p id='loadedtotal'></p>\
        </form>\
        <br>Current versions: Program %VERSION% (%HWID%), Filesystem %version_fs%  </i>\
        </ body></ html> \
        ";

// https://github.com/lbernstone/asyncUpdate/blob/master/AsyncUpdate.ino

#include <Update.h>
size_t content_len;
#define U_PART U_SPIFFS

/**
 * @brief Returns update form from memory variable.
 *
 * @param request
 */
void onWebUpdateGet(AsyncWebServerRequest *request)
{
  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();
  Serial.println("update-form");
  request->send_P(200, "text/html", update_page_html, jscode_form_processor);
}

/**
 * @brief Process update chunks
 *
 * @param request
 * @param filename
 * @param index
 * @param data
 * @param len
 */
void handleDoUpdate(AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final)
{
  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();
  if (!index)
  {
    Serial.println("Update");
    content_len = request->contentLength();
    int cmd = (filename.indexOf("littlefs") > -1) ? U_PART : U_FLASH;
#ifdef ESP8266
    Update.runAsync(true);
    if (!Update.begin(content_len, cmd))
    {
#else
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd))
    {
#endif
      Update.printError(Serial);
    }
  }

  if (Update.write(data, len) != len)
  {
    Update.printError(Serial);
#ifdef ESP8266
  }
  else
  {
    Serial.printf("Progress: %d%%\n", (Update.progress() * 100) / Update.size());
#endif
  }

  if (final)
  {
    AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", "Please wait while the device reboots");
    // response->addHeader("Refresh", "20");
    // response->addHeader("Location", "/");

    response->addHeader("REFRESH", "15;URL=/admin");
    request->send(response);
    if (!Update.end(true))
    {
      Update.printError(Serial);
    }
    else
    {
      Serial.println("Update complete");
      Serial.flush();
      WiFi.disconnect();
      log_msg(MSG_TYPE_FATAL, PSTR("Restarting after firmware update."), true);
      delay(2000);
      ESP.restart();
    }
  }
}

void printProgress(size_t prg, size_t sz)
{
  Serial.printf("Progress: %d%%\n", (prg * 100) / content_len);
}

#endif

/**
 * @brief Reset config variables to defaults
 *
 * @param full_reset Is admin password also resetted
 */
void reset_config(bool full_reset)
{
  Serial.println(F("Starting reset_config"));

  // TODO: handle influx somehow
  // reset memory
  char current_password[MAX_ID_STR_LENGTH];

  char current_wifi_ssid[MAX_ID_STR_LENGTH];
  char current_wifi_password[MAX_ID_STR_LENGTH];

  if (full_reset)
  {
    strncpy(current_wifi_ssid, "", 1);
    strncpy(current_wifi_password, "", 1);
  }
  else
  {
    strncpy(current_wifi_ssid, s.wifi_ssid, sizeof(current_wifi_ssid));
    strncpy(current_wifi_password, s.wifi_password, sizeof(current_wifi_password));
    strncpy(current_password, s.http_password, sizeof(current_password));
  }

  memset(&s, 0, sizeof(s));
  memset(&s_influx, 0, sizeof(s_influx));
  s.check_value = EEPROM_CHECK_VALUE;

  strncpy(s.http_username, "admin", sizeof(s.http_username));
  if (full_reset)
    strncpy(s.http_password, default_http_password, sizeof(s.http_password));
  else
    strncpy(s.http_password, current_password, sizeof(s.http_password));

  // use previous wifi settings by default
  strncpy(s.wifi_ssid, current_wifi_ssid, sizeof(s.wifi_ssid));
  strncpy(s.wifi_password, current_wifi_password, sizeof(s.wifi_password));

  strncpy(s.http_password, default_http_password, sizeof(s.http_password));
  s.variable_mode = VARIABLE_MODE_SOURCE;

  strncpy(s.custom_ntp_server, "", sizeof(s.custom_ntp_server));

  s.baseload = 0;
  s.energy_meter_type = ENERGYM_NONE;
  strcpy(s.forecast_loc, "#");

  strcpy(s.lang, "EN");
  strcpy(s.timezone, "EET");

  s.hw_template_id = 0; // undefined my default

  bool gpios_defined = false; // if CH_GPIOS array hardcoded (in platformio.ini)
  uint16_t channel_gpios[CHANNEL_COUNT];
  char ch_gpios_local[35];
#ifdef CH_GPIOS
  gpios_defined = true;
  strncpy(ch_gpios_local, CH_GPIOS, sizeof(ch_gpios_local)); //  split comma separated gpio string to an array
  str_to_uint_array(ch_gpios_local, channel_gpios, ",");     // ESP32: first param must be locally allocated to avoid memory protection crash
#endif

  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {
    if (gpios_defined)
      s.ch[channel_idx].switch_id = channel_gpios[channel_idx]; // TODO: check first type, other types available
    else
      s.ch[channel_idx].switch_id = 255;

    s.ch[channel_idx].type = (s.ch[channel_idx].switch_id < 255) ? CH_TYPE_GPIO_FIXED : CH_TYPE_UNDEFINED;
    s.ch[channel_idx].uptime_minimum = 60;
    s.ch[channel_idx].force_up_from = 0;
    s.ch[channel_idx].force_up_until = 0;
    s.ch[channel_idx].config_mode = CHANNEL_CONFIG_MODE_RULE;
    s.ch[channel_idx].template_id = -1;

    snprintf(s.ch[channel_idx].id_str, sizeof(s.ch[channel_idx].id_str), "channel %d", channel_idx + 1);
    for (int condition_idx = 0; condition_idx < CHANNEL_CONDITIONS_MAX; condition_idx++)
    {
      s.ch[channel_idx].conditions[condition_idx].on = false;
      for (int stmt_idx = 0; stmt_idx < RULE_STATEMENTS_MAX; stmt_idx++)
      {
        s.ch[channel_idx].conditions[condition_idx].statements[stmt_idx].variable_id = -1;
        s.ch[channel_idx].conditions[condition_idx].statements[stmt_idx].oper_id = 255;
        s.ch[channel_idx].conditions[condition_idx].statements[stmt_idx].const_val = 0;
      }
    }
  }
  Serial.println(F("Finishing reset_config"));
}
/**
 * @brief Export running configuration as json to web response
 *
 * @param request
 */
void export_config(AsyncWebServerRequest *request)
{
  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();

  DynamicJsonDocument doc(CONFIG_JSON_SIZE_MAX);

  String output;
  char export_time[20];
  char floatbuff[20];
  char stmt_buff[50];
  time_t current_time;
  time(&current_time);
  int active_condition_idx;

  localtime_r(&current_time, &tm_struct);
  snprintf(export_time, 20, "%04d-%02d-%02dT%02d:%02d:%02d", tm_struct.tm_year + 1900, tm_struct.tm_mon + 1, tm_struct.tm_mday, tm_struct.tm_hour, tm_struct.tm_min, tm_struct.tm_sec);
  doc["export_time"] = export_time;

  doc["check_value"] = s.check_value;
  doc["wifi_ssid"] = s.wifi_ssid;
  doc["wifi_password"] = s.wifi_password; // TODO: maybe not here
  doc["http_username"] = s.http_username;
  // doc["http_password"] = s.http_password; // TODO: maybe not here
  doc["variable_mode"] = VARIABLE_MODE_SOURCE; // no selection
                                               // if (s.variable_mode == VARIABLE_MODE_SOURCE)
                                               // {
  doc["entsoe_api_key"] = s.entsoe_api_key;
  doc["entsoe_area_code"] = s.entsoe_area_code;
  // }
  //  if (s.variable_mode == VARIABLE_MODE_REPLICA)
  //   doc["variable_server"] = s.variable_server;
  doc["custom_ntp_server"] = s.custom_ntp_server;
  doc["timezone"] = s.timezone;
  doc["baseload"] = s.baseload;
  doc["energy_meter_type"] = s.energy_meter_type;
  if (s.energy_meter_type != ENERGYM_NONE)
    doc["energy_meter_host"] = s.energy_meter_host;
  if (s.energy_meter_type == ENERGYM_SMA_MODBUS_TCP)
  {
    doc["energy_meter_port"] = s.energy_meter_port;
    doc["energy_meter_id"] = s.energy_meter_id;
  }

  doc["forecast_loc"] = s.forecast_loc;
  doc["lang"] = s.lang;

#ifdef INFLUX_REPORT_ENABLED
  doc["influx_url"] = s_influx.url;
  doc["influx_token"] = s_influx.token;
  doc["influx_org"] = s_influx.org;
  doc["influx_bucket"] = s_influx.bucket;
#endif

  int rule_idx_output;
  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {
    //  Serial.printf(PSTR("Exporting channel %d\n"), channel_idx);

    doc["ch"][channel_idx]["id_str"] = s.ch[channel_idx].id_str;
    doc["ch"][channel_idx]["type"] = s.ch[channel_idx].type;
    doc["ch"][channel_idx]["config_mode"] = s.ch[channel_idx].config_mode;
    doc["ch"][channel_idx]["template_id"] = s.ch[channel_idx].template_id;
    doc["ch"][channel_idx]["uptime_minimum"] = s.ch[channel_idx].uptime_minimum;
    doc["ch"][channel_idx]["toggle_last"] = s.ch[channel_idx].toggle_last;
    doc["ch"][channel_idx]["force_up_from"] = s.ch[channel_idx].force_up_from;
    doc["ch"][channel_idx]["force_up_until"] = s.ch[channel_idx].force_up_until;
    doc["ch"][channel_idx]["is_up"] = s.ch[channel_idx].is_up;
    doc["ch"][channel_idx]["wanna_be_up"] = s.ch[channel_idx].wanna_be_up;
    doc["ch"][channel_idx]["switch_id"] = s.ch[channel_idx].switch_id;

    // conditions[condition_idx].condition_active
    active_condition_idx = -1;

    rule_idx_output = 0; //***

    int active_rule_count = 0;
    for (int rule_idx = 0; rule_idx < CHANNEL_CONDITIONS_MAX; rule_idx++)
    {
      if (s.ch[channel_idx].conditions[rule_idx].condition_active)
        active_condition_idx = rule_idx_output;

      int stmt_count = 0;
      for (int stmt_idx = 0; stmt_idx < RULE_STATEMENTS_MAX; stmt_idx++)
      {
        statement_st *stmt = &s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx];
        // is there any statements for the rule
        if (stmt->variable_id != -1 && stmt->oper_id != 255)
        {
          // Serial.printf("Active statement %d %d \n", (int)stmt->variable_id, (int)stmt->oper_id);
          vars.to_str(stmt->variable_id, floatbuff, true, stmt->const_val, sizeof(floatbuff));

          // Serial.printf("floatbuff:%s\n", floatbuff);

          doc["ch"][channel_idx]["rules"][rule_idx_output]["stmts"][stmt_count][0] = stmt->variable_id;
          doc["ch"][channel_idx]["rules"][rule_idx_output]["stmts"][stmt_count][1] = stmt->oper_id;
          doc["ch"][channel_idx]["rules"][rule_idx_output]["stmts"][stmt_count][2] = stmt->const_val;

          doc["ch"][channel_idx]["rules"][rule_idx_output]["stmts"][stmt_count][3] = floatbuff;
          // vars.const_to_float(stmt->variable_id, stmt->const_val);
          stmt_count++;
        }
      }
      if (stmt_count > 0)
      {
        doc["ch"][channel_idx]["rules"][rule_idx_output]["on"] = s.ch[channel_idx].conditions[rule_idx].on;
        rule_idx_output++;
        active_rule_count++;
      }
    }
    // Serial.printf("Channel: %d,active_rule_count %d \n", channel_idx, active_rule_count);
    if (active_rule_count > 0)
    {
      doc["ch"][channel_idx]["active_condition_idx"] = active_condition_idx;
    }
  }

  serializeJson(doc, output);

  // TODO: format parameter, file or ajax response
  // byte format = 1;
  // TODO: format option
  byte format = 0;
  if (request->hasParam("format"))
  {
    if (request->getParam("format")->value() == "file")
      format = 1;
  }

  if (format == 0)
    request->send(200, "application/json", output);
  else
  {
    char Content_Disposition[70];
    snprintf(Content_Disposition, 70, "attachment; name=arska-config-%s.json", export_time);
    AsyncWebServerResponse *response = request->beginResponse(200, "application/json", output);
    response->addHeader("Content-Disposition", Content_Disposition);
    request->send(response);
  }

  // Serial.println(Content_Disposition);
  // request->send(404, "text/plain", "Not found");
}

/**
 * @brief Read config variables from config.json file
 *
 * @param config_file_name
 * @return true
 * @return false
 */
bool read_config_file(const char *config_file_name)
{
  Serial.println(F("Reading config file"));
  if (!LittleFS.exists(config_file_name))
  {
    Serial.println(F("No config file. "));
    return false;
  }

  File file = LittleFS.open(config_file_name, "r");
  if (!file)
  {
    Serial.println(F("Failed to open config file. "));
    return false;
  }

  Serial.println(F("Got config file"));
  StaticJsonDocument<CONFIG_JSON_SIZE_MAX> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error)
  {
    Serial.println(F("deserializeJson() the config file failed: "));
    Serial.println(error.c_str());
    return false;
  }
  Serial.println(F("deserializeJson() config file OK."));

  copy_doc_str(doc, (char *)"wifi_ssid", s.wifi_ssid, sizeof(s.wifi_ssid));
  copy_doc_str(doc, (char *)"wifi_password", s.wifi_password, sizeof(s.wifi_password));
  // copy_doc_str(doc, (char *)"http_password", s.http_password,sizeof());
  s.variable_mode = VARIABLE_MODE_SOURCE; // get_doc_long(doc, "variable_mode", VARIABLE_MODE_SOURCE);
  copy_doc_str(doc, (char *)"entsoe_api_key", s.entsoe_api_key, sizeof(s.entsoe_api_key));
  copy_doc_str(doc, (char *)"entsoe_area_code", s.entsoe_area_code, sizeof(s.entsoe_area_code));
  copy_doc_str(doc, (char *)"variable_server", s.variable_server, sizeof(s.variable_server));
  copy_doc_str(doc, (char *)"custom_ntp_server", s.custom_ntp_server, sizeof(s.custom_ntp_server));
  copy_doc_str(doc, (char *)"timezone", s.timezone, sizeof(s.timezone));
  copy_doc_str(doc, (char *)"forecast_loc", s.forecast_loc, sizeof(s.forecast_loc));
  copy_doc_str(doc, (char *)"lang", s.lang, sizeof(s.lang));
  s.hw_template_id = get_doc_long(doc, "hw_template_id", s.hw_template_id);
  s.baseload = get_doc_long(doc, "baseload", s.baseload);

  s.energy_meter_type = get_doc_long(doc, "energy_meter_type", s.energy_meter_type);
  copy_doc_str(doc, (char *)"energy_meter_host", s.energy_meter_host, sizeof(s.energy_meter_host));
  s.energy_meter_port = get_doc_long(doc, "energy_meter_port", s.energy_meter_port);
  s.energy_meter_id = get_doc_long(doc, "energy_meter_id", s.energy_meter_id);

#ifdef INFLUX_REPORT_ENABLED
  copy_doc_str(doc, (char *)"influx_url", s_influx.url, sizeof(s_influx.url));
  copy_doc_str(doc, (char *)"influx_token", s_influx.token, sizeof(s_influx.token));
  copy_doc_str(doc, (char *)"influx_org", s_influx.org, sizeof(s_influx.org));
  copy_doc_str(doc, (char *)"influx_bucket", s_influx.bucket, sizeof(s_influx.bucket));
#endif

  int channel_idx = 0;
  int rule_idx = 0;
  int stmt_idx = 0;

  for (JsonObject ch_item : doc["ch"].as<JsonArray>())
  {
    strncpy(s.ch[channel_idx].id_str, ch_item["id_str"], 9);
    s.ch[channel_idx].type = ch_item["type"];
    s.ch[channel_idx].config_mode = ch_item["config_mode"];
    s.ch[channel_idx].template_id = ch_item["template_id"];
    s.ch[channel_idx].uptime_minimum = ch_item["uptime_minimum"];
    rule_idx = 0;
    for (JsonObject ch_rule : ch_item["rules"].as<JsonArray>())
    {
      s.ch[channel_idx].conditions[rule_idx].on = ch_rule["on"];
      stmt_idx = 0;
      for (JsonArray ch_rule_stmt : ch_rule["stmts"].as<JsonArray>())
      {

        s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].variable_id = ch_rule_stmt[0];
        s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].oper_id = ch_rule_stmt[1];
        s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].const_val = ch_rule_stmt[2];
        Serial.printf("Tulos: [%d, %d, %ld]", s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].variable_id, (int)s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].oper_id, s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].const_val);

        stmt_idx++;
      }
      rule_idx++;
    }

    channel_idx++;
  }

  writeToEEPROM();

  return true;
}

//
/**
 * @brief Handle config upload
 *
 * @param request
 * @param filename
 * @param index
 * @param data
 * @param len
 */
void onWebUploadConfig(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();

  String logmessage = "Client:" + request->client()->remoteIP().toString() + " " + request->url();
  Serial.println(logmessage);
  const char *filename_internal = "/data/config_in.json";

  if (!index)
  {
    logmessage = "Upload Start: " + String(filename);
    // open the file on first call and store the file handle in the request object
    request->_tempFile = LittleFS.open(filename_internal, "w");
    Serial.println(logmessage);
  }

  if (len)
  {
    // stream the incoming chunk to the opened file
    request->_tempFile.write(data, len);
    logmessage = "Writing file: " + String(filename) + " index=" + String(index) + " len=" + String(len);
    Serial.println(logmessage);
  }

  if (final)
  {
    logmessage = "Upload Complete: " + String(filename) + ",size: " + String(index + len);
    // close the file handle as the upload is now done
    request->_tempFile.close();
    Serial.println(logmessage);

    reset_config(false);
    read_config_file(filename_internal);

    request->redirect("/");
  }
}
/**
 * @brief Return dashboard form
 *
 * @param request
 */
void onWebDashboardGet(AsyncWebServerRequest *request)
{

  if ((strcmp(s.http_password, default_http_password) == 0) || wifi_in_setup_mode)
  {
    Serial.println("DEBUG: onWebDashboardGet redirect /admin");
    request->redirect("/admin");
    return;
  }
  sendForm(request, "/dashboard_template.html", setup_form_processor);
  //  sendForm(request, "/dashboard_template.html");
}
/**
 * @brief Returns services (inputs) form
 *
 * @param request
 */
void onWebInputsGet(AsyncWebServerRequest *request)
{
  sendForm(request, "/inputs_template.html", inputs_form_processor);
}

/**
 * @brief Returns channel config form
 *
 * @param request
 */
/*
void onWebChannelsGet_old(AsyncWebServerRequest *request)
{
  sendForm(request, "/channels_template_old.html", setup_form_processor);
}
// devel
*/
void onWebChannelsGet(AsyncWebServerRequest *request)
{
  sendForm(request, "/channels_template.html", setup_form_processor);
}

/**
 * @brief Returns admin form
 *
 * @param request
 */
void onWebAdminGet(AsyncWebServerRequest *request)
{
  sendForm(request, "/admin_template.html", admin_form_processor);
}

/**
 * @brief Get individual rule template by id
 *
 * @param request
 */
void onWebTemplateGet(AsyncWebServerRequest *request)
{
  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();

  if (!request->hasParam("id"))
    request->send(404, "text/plain", "Not found");

  StaticJsonDocument<16> filter;
  AsyncWebParameter *id = request->getParam("id");
  // Serial.printf(PSTR("Template id: %s\n"), p->value().c_str());
  filter[id->value().c_str()] = true;
  StaticJsonDocument<4096> doc;

  File template_file = LittleFS.open(template_filename, "r");
  DeserializationError error = deserializeJson(doc, template_file, DeserializationOption::Filter(filter));
  String output;
  if (error)
  {
    Serial.print(F("Template deserializeJson() failed: "));
    Serial.println(error.c_str());
    return;
  }
  template_file.close();
  JsonObject root = doc[id->value().c_str()];
  serializeJson(root, output);
  request->send(200, "application/json", output);
}

/**
 * @brief Process dashboard form, forcing channels up
 *
 * @param request
 */
void onWebDashboardPost(AsyncWebServerRequest *request)
{
  time(&now);
  int params = request->params();
  int channel_idx;
  bool force_up_changes = false;
  bool channel_already_forced;
  long force_up_minutes;
  time_t force_up_from = 0;
  time_t force_up_until;
  char force_up_from_fld[15];
  char duration_fld[15];

  for (channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {
    snprintf(force_up_from_fld, sizeof(force_up_from_fld), "fupfrom_%d", channel_idx);
    snprintf(duration_fld, sizeof(duration_fld), "fups_%d", channel_idx);

    if (request->hasParam(duration_fld, true))
    {
      if (request->getParam(duration_fld, true)->value().toInt() == -1)
        continue; // no selection
      channel_already_forced = is_force_up_valid(channel_idx);
      force_up_minutes = request->getParam(duration_fld, true)->value().toInt();

      if (request->hasParam(force_up_from_fld, true))
      {
        //  Serial.printf("%s: %d\n", force_up_from_fld, (int)request->getParam(force_up_from_fld, true)->value().toInt());
        if (request->getParam(force_up_from_fld, true)->value().toInt() == 0)
          force_up_from = now;
        else
          force_up_from = max(now, request->getParam(force_up_from_fld, true)->value().toInt()); // absolute unix ts is waited
      }

      Serial.printf("channel_idx: %d, force_up_minutes: %ld , force_up_from %ld\n", channel_idx, force_up_minutes, force_up_from);

      // if ((force_up_minutes != -1) && (channel_already_forced || force_up_minutes > 0))

      if (force_up_minutes > 0)
      {
        force_up_until = force_up_from + force_up_minutes * 60; //-1;
        s.ch[channel_idx].force_up_from = force_up_from;
        s.ch[channel_idx].force_up_until = force_up_until;
        if (is_force_up_valid(channel_idx))
          s.ch[channel_idx].wanna_be_up = true;
      }
      else
      {
        s.ch[channel_idx].force_up_from = -1;  // forced down
        s.ch[channel_idx].force_up_until = -1; // forced down
        s.ch[channel_idx].wanna_be_up = false;
      }
      force_up_changes = true;
    }
  }

  if (force_up_changes)
  {
    todo_in_loop_set_relays = true;
    writeToEEPROM();
  }

  request->redirect("/");
}

/**
 * @brief Process service (input) form
 *
 * @param request
 */

void onWebInputsPost(AsyncWebServerRequest *request)
{
  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();

  bool todo_in_loop_restart_local = false; // set global variable in the end when data is set
  time_t now_infunc;
  time(&now_infunc);
  bool entsoe_params_changed = false;

  // INPUTS
  if (s.energy_meter_type != request->getParam("emt", true)->value().toInt())
  {
    todo_in_loop_restart_local = true;
    s.energy_meter_type = request->getParam("emt", true)->value().toInt();
  }
 
  strncpy(s.energy_meter_host, request->getParam("emh", true)->value().c_str(), sizeof(s.energy_meter_host));
  s.energy_meter_port = request->getParam("emp", true)->value().toInt();
  s.energy_meter_id = request->getParam("emid", true)->value().toInt();

  s.baseload = request->getParam("baseload", true)->value().toInt();

  s.variable_mode = VARIABLE_MODE_SOURCE; // (byte)request->getParam("variable_mode", true)->value().toInt();
  if (s.variable_mode == 0) //TODO: remove other than this mode
  {
    if ((strcmp(s.entsoe_api_key, request->getParam("entsoe_api_key", true)->value().c_str())!=0))
      entsoe_params_changed = true;

    strncpy(s.entsoe_api_key, request->getParam("entsoe_api_key", true)->value().c_str(), sizeof(s.entsoe_api_key));
    if (strcmp(s.entsoe_area_code, request->getParam("entsoe_area_code", true)->value().c_str()) != 0)
    {
      strncpy(s.entsoe_area_code, request->getParam("entsoe_area_code", true)->value().c_str(), sizeof(s.entsoe_area_code));
      entsoe_params_changed = true;
    }
    if (entsoe_params_changed) {
      // api key or price area changes, clear cache and requery
      LittleFS.remove(price_data_filename); // "/price_data.json"
      next_query_price_data = now + 10; // query with new parameters soon
    }
    // Solar forecast supported currently only in Finland
    if (strcmp(s.entsoe_area_code, "10YFI-1--------U") == 0)
      strncpy(s.forecast_loc, request->getParam("forecast_loc", true)->value().c_str(), sizeof(s.forecast_loc));
    else
      strcpy(s.forecast_loc, "#");
  }
  /* if (s.variable_mode == 1)
   {
     strncpy(s.variable_server, request->getParam("variable_server", true)->value().c_str(),sizeof(variable_server));
   }*/
#ifdef INFLUX_REPORT_ENABLED
  strncpy(s_influx.url, request->getParam("influx_url", true)->value().c_str(), sizeof(s_influx.url));
  // Serial.printf("influx_url:%s\n", s_influx.url);
  strncpy(s_influx.token, request->getParam("influx_token", true)->value().c_str(), sizeof(s_influx.token));
  strncpy(s_influx.org, request->getParam("influx_org", true)->value().c_str(), sizeof(s_influx.org));
  strncpy(s_influx.bucket, request->getParam("influx_bucket", true)->value().c_str(), sizeof(s_influx.bucket));
#endif

  // END OF INPUTS
  writeToEEPROM();

  todo_in_loop_restart = todo_in_loop_restart_local;

  if (todo_in_loop_restart)
    request->send(200, "text/html", "<html><head><meta http-equiv='refresh' content='10; url=/inputs' /></head><body>restarting...wait...</body></html>");
  else
    request->redirect("/inputs");
}

/**
 * @brief Process channel config  edits
 *
 * @param request
 */
void onWebChannelsPost(AsyncWebServerRequest *request)
{
  char ch_fld[20];
  char state_fld[20];
  char stmts_fld[20];
  char target_fld[20];
  char ctrb_fld[20];

  StaticJsonDocument<300> stmts_json;

  // bool stmts_emptied = false;
  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {

    snprintf(ch_fld, 20, "id_ch_%i", channel_idx); // channel id
    if (request->hasParam(ch_fld, true))
      strncpy(s.ch[channel_idx].id_str, request->getParam(ch_fld, true)->value().c_str(), sizeof(s.ch[channel_idx].id_str));

    snprintf(ch_fld, 20, "chty_%i", channel_idx); // channel type
    if (request->hasParam(ch_fld, true))
      s.ch[channel_idx].type = request->getParam(ch_fld, true)->value().toInt();

    snprintf(ch_fld, 20, "ch_uptimem_%i", channel_idx);
    if (request->hasParam(ch_fld, true))
    {
      s.ch[channel_idx].uptime_minimum = request->getParam(ch_fld, true)->value().toInt();
    }

    snprintf(ch_fld, 20, "ch_swid_%i", channel_idx);
    if (request->hasParam(ch_fld, true))
    {
      s.ch[channel_idx].switch_id = request->getParam(ch_fld, true)->value().toInt();
    }

    snprintf(ch_fld, 20, "mo_%i", channel_idx); // channel rule mode
    if (request->hasParam(ch_fld, true))
      s.ch[channel_idx].config_mode = request->getParam(ch_fld, true)->value().toInt();

    snprintf(ch_fld, 20, "rts_%i", channel_idx); // channe rule template
    if (request->hasParam(ch_fld, true))
    {

      s.ch[channel_idx].template_id = request->getParam(ch_fld, true)->value().toInt();
    }

    for (int condition_idx = 0; condition_idx < CHANNEL_CONDITIONS_MAX; condition_idx++)
    {
      // statements
      snprintf(stmts_fld, 20, "stmts_%i_%i", channel_idx, condition_idx);

      // clean always
      for (int stmt_idx = 0; stmt_idx < RULE_STATEMENTS_MAX; stmt_idx++)
      {
        s.ch[channel_idx].conditions[condition_idx].statements[stmt_idx].variable_id = -1;
        s.ch[channel_idx].conditions[condition_idx].statements[stmt_idx].oper_id = 255;
        s.ch[channel_idx].conditions[condition_idx].statements[stmt_idx].const_val = 0;
      }

      if (request->hasParam(stmts_fld, true) && !request->getParam(stmts_fld, true)->value().isEmpty())
      {

        /*  for (int stmt_idx = 0; stmt_idx < RULE_STATEMENTS_MAX; stmt_idx++)
          {
            s.ch[channel_idx].conditions[condition_idx].statements[stmt_idx].variable_id = -1;
            s.ch[channel_idx].conditions[condition_idx].statements[stmt_idx].oper_id = 255;
            s.ch[channel_idx].conditions[condition_idx].statements[stmt_idx].const_val = 0;
          } */

        DeserializationError error = deserializeJson(stmts_json, request->getParam(stmts_fld, true)->value());
        if (error)
        {
          Serial.print(F("onWebChannelsPost deserializeJson() failed: "));
          Serial.println(error.f_str());
        }
        else
        {
          if (stmts_json.size() > 0)
          {
            variable_st var_this;
            int var_index;
            for (int stmt_idx = 0; stmt_idx < min((int)stmts_json.size(), RULE_STATEMENTS_MAX); stmt_idx++)
            {
              Serial.printf("Saving %d %d %d : %d, %d \n", channel_idx, condition_idx, stmt_idx, (int)stmts_json[stmt_idx][0], (int)stmts_json[stmt_idx][1]);
              var_index = vars.get_variable_by_id((int)stmts_json[stmt_idx][0], &var_this);
              if (var_index != -1)
              {
                int variable_id = (int)stmts_json[stmt_idx][0];
                s.ch[channel_idx].conditions[condition_idx].statements[stmt_idx].variable_id = variable_id;
                s.ch[channel_idx].conditions[condition_idx].statements[stmt_idx].oper_id = (byte)stmts_json[stmt_idx][1];

                float val_f = stmts_json[stmt_idx][2];
                long long_val = vars.float_to_internal_l(variable_id, val_f);

                s.ch[channel_idx].conditions[condition_idx].statements[stmt_idx].const_val = long_val;
              }
              else
              {
                Serial.printf(PSTR("Error, cannot find variable with index %d\n"), (int)stmts_json[stmt_idx][0]);
              }
            }
          }
        }
      }

      snprintf(ctrb_fld, 20, "ctrb_%i_%i", channel_idx, condition_idx);

      if (request->hasParam(ctrb_fld, true))
      {
        s.ch[channel_idx].conditions[condition_idx].on = (request->getParam(ctrb_fld, true)->value().toInt() == (int)1);
        //   Serial.print(request->getParam(ctrb_fld, true)->value().toInt());
      }
      else
        Serial.printf("Field %s not found in the form.\n", ctrb_fld);
    }
  }

  writeToEEPROM();
  request->redirect("/channels");
}

/**
 * @brief Process admin form edits
 *
 * @param request
 */
void onWebAdminPost(AsyncWebServerRequest *request)
{
  if (!request->authenticate(s.http_username, s.http_password))
  {
    return request->requestAuthentication();
  }
  bool todo_in_loop_restart_local = false; // set global variable in the end when data is set

  String message;

  if (!(request->getParam("wifi_ssid", true)->value().equals("NA")))
  {
    strncpy(s.wifi_ssid, request->getParam("wifi_ssid", true)->value().c_str(), sizeof(s.wifi_ssid));
    strncpy(s.wifi_password, request->getParam("wifi_password", true)->value().c_str(), sizeof(s.wifi_password));
    todo_in_loop_restart_local = true;
  }

  if (request->hasParam("http_password", true) && request->hasParam("http_password2", true))
  {
    Serial.println(F("Password ...."));
    if (request->getParam("http_password", true)->value().equals(request->getParam("http_password2", true)->value()) && request->getParam("http_password", true)->value().length() >= 5)
    {
      strncpy(s.http_password, request->getParam("http_password", true)->value().c_str(), sizeof(s.http_password));
    }
  }

  strncpy(s.timezone, request->getParam("timezone", true)->value().c_str(), 4);
  strncpy(s.lang, request->getParam("lang", true)->value().c_str(), sizeof(s.lang));

  if (request->hasParam("hw_template_id", true))
  { // older firmware may not have it in the form
    if ((request->getParam("hw_template_id", true)->value().toInt() != s.hw_template_id))
    {
      s.hw_template_id = request->getParam("hw_template_id", true)->value().toInt();
      // TODO: change gpios etc
      for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
      {
        if (channel_idx < HW_TEMPLATE_GPIO_COUNT)
        { // touch only channel which could have gpio definitions
          if (hw_templates[s.hw_template_id].gpios[channel_idx] < 255)
          {
            s.ch[channel_idx].type = CH_TYPE_GPIO_FIXED;
            s.ch[channel_idx].switch_id = hw_templates[s.hw_template_id].gpios[channel_idx];
          }
          else if (s.ch[channel_idx].type == CH_TYPE_GPIO_FIXED)
          { // fixed gpio -> user defined
            s.ch[channel_idx].type = CH_TYPE_GPIO_USER_DEF;
          }
        }
      }
    }
  }

  // admin actions
  Serial.println(request->getParam("action", true)->value().c_str());
  if (request->getParam("action", true)->value().equals("ts"))
  {
    time_t ts = request->getParam("ts", true)->value().toInt();
    setInternalTime(ts);
#ifdef RTC_DS3231_ENABLED
    if (rtc_found)
      setRTC();
#endif
  }

  if (request->getParam("action", true)->value().equals("reboot"))
  {
    todo_in_loop_restart_local = true;
  }

  if (request->getParam("action", true)->value().equals("scan_wifis"))
  {
    todo_in_loop_scan_wifis = true;
    request->send(200, "text/html", "<html><head><meta http-equiv='refresh' content='5; url=/admin' /></head><body>Scanning, wait a while...</body></html>");
    return;
  }

  if (request->getParam("action", true)->value().equals("scan_sensors"))
  {
    todo_in_loop_scan_sensors = true;
    request->send(200, "text/html", "<html><head><meta http-equiv='refresh' content='5; url=/admin' /></head><body>Scanning, wait a while...</body></html>");
    return;
  }

  if (request->getParam("action", true)->value().equals("op_test_gpio"))
  {
    gpio_to_test_in_loop = request->getParam("test_gpio", true)->value().toInt();
    todo_in_loop_test_gpio = true;
    request->send(200, "text/html", "<html><head><meta http-equiv='refresh' content='1; url=/admin' /></head><body>GPIO testing</body></html>");
    return;
  }

  if (request->getParam("action", true)->value().equals("reset"))
  {
    Serial.println(F("Starting reset..."));
    reset_config(false);
    todo_in_loop_restart_local = true;
  }
  writeToEEPROM();

  todo_in_loop_restart = todo_in_loop_restart_local;

  if (todo_in_loop_restart)
    request->send(200, "text/html", "<html><head><meta http-equiv='refresh' content='10; url=./' /></head><body>restarting...wait...</body></html>");
  else
    request->redirect("/admin");
}

/**
 * @brief Returns status in json
 *
 * @param request
 */
void onWebStatusGet(AsyncWebServerRequest *request)
{
  if (!request->authenticate(s.http_username, s.http_password))
  {
    return request->requestAuthentication();
  }
  StaticJsonDocument<2048> doc; //
  String output;

  JsonObject var_obj = doc.createNestedObject("variables");

  /*
  #ifdef METER_SHELLY3EM_ENABLED
    float netEnergyInPeriod;
    float netPowerInPeriod;
    get_values_shelly3m(netEnergyInPeriod, netPowerInPeriod);
    variables["netEnergyInPeriod"] = netEnergyInPeriod;
    variables["netPowerInPeriod"] = netPowerInPeriod;
  #endif

  #ifdef INVERTER_FRONIUS_SOLARAPI_ENABLED
    variables["energyProducedPeriod"] = energy_produced_period;
    variables["powerProducedPeriodAvg"] = power_produced_period_avg;
  #endif
  */
  // var_obj["updated"] = shelly3em_meter_read_ts;
  // var_obj["freeHeap"] = ESP.getFreeHeap();
  // var_obj["uptime"] = (unsigned long)(millis() / 1000);

  char id_str[6];
  char buff_value[20];
  variable_st variable;
  for (int variable_idx = 0; variable_idx < vars.get_variable_count(); variable_idx++)
  {
    vars.get_variable_by_idx(variable_idx, &variable);
    // if (variable.val_l != VARIABLE_LONG_UNKNOWN)
    snprintf(id_str, 6, "%d", variable.id);
    vars.to_str(variable.id, buff_value, false, 0, sizeof(buff_value));

    var_obj[id_str] = buff_value;
  }
  /*
    JsonArray channel_array = doc.createNestedArray("channels");
    for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
    {
      channel_array.add(s.ch[channel_idx].is_up);
    } */

  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {
    doc["ch"][channel_idx]["is_up"] = s.ch[channel_idx].is_up;
    doc["ch"][channel_idx]["active_condition"] = active_condition(channel_idx);
    doc["ch"][channel_idx]["force_up"] = is_force_up_valid(channel_idx);
    doc["ch"][channel_idx]["force_up_from"] = s.ch[channel_idx].force_up_from;
    doc["ch"][channel_idx]["force_up_until"] = s.ch[channel_idx].force_up_until;
  }

  time_t current_time;
  time(&current_time);
  localtime_r(&current_time, &tm_struct);
  snprintf(buff_value, 20, "%04d-%02d-%02d %02d:%02d:%02d", tm_struct.tm_year + 1900, tm_struct.tm_mon + 1, tm_struct.tm_mday, tm_struct.tm_hour, tm_struct.tm_min, tm_struct.tm_sec);
  doc["localtime"] = buff_value;

  doc["last_msg_msg"] = last_msg.msg;
  doc["last_msg_ts"] = last_msg.ts;
  doc["last_msg_type"] = last_msg.type;
  doc["energym_read_last"] = energym_read_last;

  serializeJson(doc, output);
  request->send(200, "application/json", output);
}

// TODO: check how it works with RTC
/**
 * @brief Set the timezone info etc after wifi connected
 *
 */
void set_time_settings()
{
  time_t now_infunc;
  if (clock_set)
    return;
  // Set timezone info
  char timezone_info[35];
  if (strcmp("EET", s.timezone) == 0)
    strcpy(timezone_info, "EET-2EEST,M3.5.0/3,M10.5.0/4");
  else // CET default
    strcpy(timezone_info, "CET-1CEST,M3.5.0/02,M10.5.0/03");
  // assume working wifi
  configTime(0, 0, ntp_server_1, ntp_server_2, ntp_server_3);

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 10000) && (now < ACCEPTED_TIMESTAMP_MINIMUM))
  {
    log_msg(MSG_TYPE_ERROR, PSTR("Failed to obtain time"));
    time(&now_infunc);
    Serial.printf(PSTR("Setup: %ld"), now_infunc);
  }
  else
  {
    setenv("TZ", timezone_info, 1);
    Serial.printf(PSTR("timezone_info: %s, %s"), timezone_info, s.timezone);
    tzset();
    clock_set = true;
  }
  clock_set = (time(nullptr) > ACCEPTED_TIMESTAMP_MINIMUM);
}

/**
 * @brief Reports (acts on) changing wifi states
 *
 * @param event
 */
void wifi_event_handler(WiFiEvent_t event)
{
  //  Serial.printf("[WiFi-event] event: %d\n", event);
  switch (event)
  {
  case SYSTEM_EVENT_STA_CONNECTED:
    wifi_connection_succeeded = true;
    Serial.println(F("Connected to WiFi Network"));
    set_time_settings();
    break;
  case SYSTEM_EVENT_STA_DISCONNECTED:
    wifi_connection_succeeded = false;
    // Serial.println(F("Disconnected from WiFi Network"));
    break;
  case SYSTEM_EVENT_AP_START:
    Serial.println(F("ESP soft AP started"));
    break;
  case SYSTEM_EVENT_AP_STACONNECTED:
    Serial.println(F("Station connected to ESP soft AP"));
    break;
  case SYSTEM_EVENT_AP_STADISCONNECTED:
    Serial.println(F("Station disconnected from ESP soft AP"));
    break;
  default:
    break;
  }
}

/**
 * @brief Arduino framwork function.  Everything starts from here while starting the controller.
 *
 */

void setup()
{
  time_t now_infunc;
  bool create_wifi_ap = false;
  s.variable_mode = VARIABLE_MODE_SOURCE;
  Serial.begin(115200);
  delay(2000); // wait for console settle - only needed when debugging

  randomSeed(analogRead(0)); // initiate random generator
  Serial.printf(PSTR("Version: %s\n"), compile_date);

  String mac = WiFi.macAddress();
  Serial.printf(PSTR("Device mac address: %s\n"), WiFi.macAddress().c_str());

  for (int i = 14; i > 0; i -= 3)
  {
    mac.remove(i, 1);
  }
  wifi_mac_short = mac;

#ifdef SENSOR_DS18B20_ENABLED
  sensors.begin();
  delay(1000); // let the sensors settle
  // get a count of devices on the wire
  DeviceAddress tempDeviceAddress;
  sensor_count = min(sensors.getDeviceCount(), (uint8_t)MAX_DS18B20_SENSORS);
  Serial.printf(PSTR("sensor_count:%d\n"), sensor_count);

#endif

  while (!LittleFS.begin())
  {
    Serial.println(F("Failed to initialize LittleFS library, restarting..."));
    delay(5000);
    ESP.restart();
  }
  Serial.println(F("LittleFS initialized"));

  // TODO: notify, ota-update
  check_filesystem_version();
  /*
  if (check_filesystem_version())
    Serial.println(F("Filesystem is up-to-date."));
  else
    Serial.println(F("Filesystem is too old."));
*/
  // initiate EEPROM with correct size
  int eeprom_used_size = sizeof(s);
#ifdef INFLUX_REPORT_ENABLED
  eeprom_used_size += sizeof(s_influx);
#endif
  EEPROM.begin(eeprom_used_size);
  readFromEEPROM();

  if (s.check_value != EEPROM_CHECK_VALUE) // setup not initiated
  {
    Serial.println(F("Resetting settings"));
    reset_config(true);
  }

  /*
    if (1 == 2) //Softap should be created if cannot connect to wifi (like in init), redirect
    { // check also https://github.com/me-no-dev/ESPAsyncWebServer/blob/master/examples/CaptivePortal/CaptivePortal.ino
      if (WiFi.softAP("arska-node", "arska", 1, false, 1) == true)
      {
        Serial.println(F("WiFi AP created!"));
      }
    }*/
  Serial.println("Starting wifi");
  scan_and_store_wifis(true); // testing this in the beginning

  WiFi.onEvent(wifi_event_handler);

  WiFi.mode(WIFI_STA);

  Serial.printf(PSTR("Trying to connect wifi [%s] with password [%s]\n"), s.wifi_ssid, s.wifi_password);
  /*if (strlen(s.wifi_ssid) == 0)
  {
    strcpy(s.wifi_ssid, "NA");
  }*/
  // TODO: WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS))
  WiFi.setHostname("arska");
  WiFi.begin(s.wifi_ssid, s.wifi_password);

  if ((strlen(s.wifi_ssid) == 0) || WiFi.waitForConnectResult(60000L) != WL_CONNECTED)
  {
    Serial.println(F("WiFi Failed!"));

    delay(1000);

    WiFi.disconnect();
    delay(3000);
    wifi_in_setup_mode = true;
    create_wifi_ap = true;
    //  scan_and_store_wifis(true); we had this in the beginnig
    check_forced_restart(true); // schedule restart
  }
  else
  {
    Serial.printf(PSTR("Connected to wifi [%s] with IP Address:"), s.wifi_ssid);
    Serial.println(WiFi.localIP());
    s.switch_subnet_wifi = WiFi.localIP();
    s.switch_subnet_wifi[3] = 0; // assume 24 bit subnet
    wifi_connection_succeeded = true;

    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);

    if (!LittleFS.exists(wifis_filename))
    { // no wifi list found
      Serial.println("No wifi list found - rescanning...");
      scan_and_store_wifis(false);
    }
  }

  // if (wifi_in_setup_mode) // Softap should be created if  cannot connect to wifi

  if (create_wifi_ap)

  { // TODO: check also https://github.com/me-no-dev/ESPAsyncWebServer/blob/master/examples/CaptivePortal/CaptivePortal.ino
    // create ap-mode ssid for config wifi

    Serial.print("Creating AP, wifi_in_setup_mode:");
    Serial.print(wifi_in_setup_mode);

    String APSSID = String("ARSKA-") + wifi_mac_short;
    int wifi_channel = (int)random(1, 14);
    if (WiFi.softAP(APSSID.c_str(), (const char *)__null, wifi_channel, 0, 3) == true)
    {
      Serial.printf(PSTR("\nEnter valid WiFi SSID and password:, two methods:\n 1) Give WiFi number (see the list above) <enter> and give WiFi password <enter>.\n 2) Connect to WiFi %s and go to url http://%s to update your WiFi info.\n"), APSSID.c_str(), WiFi.softAPIP().toString());
      Serial.println();
      Serial.flush();
    }
    else
    {
      Serial.println(F("Cannot create AP, restarting"));
      delay(2000); // cannot create AP, restart
      ESP.restart();
    }
  }

#ifdef RTC_DS3231_ENABLED
  Serial.println(F("Starting RTC!"));
  Wire.begin(I2CSDA_GPIO, I2CSCL_GPIO);
  if (!rtc.begin())
  {
    Serial.println(F("Couldn't find RTC!"));
    Serial.flush();
  }
  else
  {
    rtc_found = true;
    Serial.println(F("RTC found"));
    Serial.flush();
    settimeofday_cb(time_is_set); // register callback if time was sent
    if (time(nullptr) < ACCEPTED_TIMESTAMP_MINIMUM)
      getRTC(); // Fallback to RTC on startup if we are before 2020-09-13
  }
#endif

#ifdef OTA_UPDATE_ENABLED
  server_web.on("/update", HTTP_GET, [](AsyncWebServerRequest *request)
                { onWebUpdateGet(request); });

  server_web.on(
      "/doUpdate", HTTP_POST,
      [](AsyncWebServerRequest *request) {},
      [](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data,
         size_t len, bool final)
      { handleDoUpdate(request, filename, index, data, len, final); });
#endif

  server_web.on("/status", HTTP_GET, onWebStatusGet);
  server_web.on("/export-config", HTTP_GET, export_config);
  // run handleUpload function when any file is uploaded

  server_web.on(
      "/upload-config", HTTP_POST, [](AsyncWebServerRequest *request)
      { request->send(200); },
      onWebUploadConfig);

  server_web.on("/", HTTP_GET, onWebDashboardGet);
  server_web.on("/", HTTP_POST, onWebDashboardPost);

  server_web.on("/inputs", HTTP_GET, onWebInputsGet);
  server_web.on("/inputs", HTTP_POST, onWebInputsPost);

  // server_web.on("/channels", HTTP_GET, onWebChannelsGet_old);
  server_web.on("/channels", HTTP_GET, onWebChannelsGet);
  server_web.on("/channels", HTTP_POST, onWebChannelsPost);

  server_web.on("/admin", HTTP_GET, onWebAdminGet);
  server_web.on("/admin", HTTP_POST, onWebAdminPost);

  // server_web.on("/update", HTTP_GET, bootInUpdateMode); // now we should restart in update mode

  server_web.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request)
                { request->redirect("/"); }); // redirect url, if called from OTA

  server_web.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, "/style.css", "text/css"); });
  /*  server_web.on("/js/arska.js", HTTP_GET, [](AsyncWebServerRequest *request)
                  { request->send(LittleFS, "/js/arska.js", "text/javascript"); });
  */

  server_web.on("/js/arska.js", HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, "/js/arska_tmpl.js", "text/html", false, jscode_form_processor); });

  server_web.on("/js/jquery-3.6.0.min.js", HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, F("/js/jquery-3.6.0.min.js"), F("text/javascript")); });
  // server_web.serveStatic("/js/", LittleFS, "/js/");

  // server_web.on("/data/template-list.json", HTTP_GET, [](AsyncWebServerRequest *request)
  //               { request->send(LittleFS, F("/data/template-list.json"), F("application/json")); });

  server_web.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, F("/data/favicon.ico"), F("image/x-icon")); });
  // TODO: check authentication or relocate potentially sensitive files
  server_web.serveStatic("/data/", LittleFS, "/data/");

  // just for debugging
  /* server_web.on("/data/config_in.json", HTTP_GET, [](AsyncWebServerRequest *request)
                 { request->send(LittleFS, F("/data/config_in.json"), F("application/json")); });
                 */

  // no authenticatipn
  server_web.on("/data/templates", HTTP_GET, onWebTemplateGet);

  // TODO: nämä voisi mennä yhdellä
  server_web.on(variables_filename, HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, variables_filename, F("application/json")); });
  server_web.on(fcst_filename, HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, fcst_filename, F("application/json")); });
  server_web.on(price_data_filename, HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, price_data_filename, F("text/plain")); }); // "/price_data.json"

  // debug
  server_web.on("/wifis.json", HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, "/wifis.json", "text/json"); });

  server_web.onNotFound(notFound);
  server_web.begin();

  if (wifi_in_setup_mode)
    return; // no more setting, just wait for new SSID/password and then restarts

  // configTime ESP32 and ESP8266 libraries differ
  /* #ifdef ESP32
   // if (!wifi_in_setup_mode)

    if (wifi_connection_succeeded)
    {
      // First connect to NTP server, with 0 TZ offset
      // TODO: custom ntp server ui admin

      configTime(0, 0, ntp_server_1, ntp_server_2, ntp_server_3);

      struct tm timeinfo;
      if (!getLocalTime(&timeinfo, 10000) && (now < ACCEPTED_TIMESTAMP_MINIMUM))
      {
        //  Serial.println("Failed to obtain time, retrying");
        log_msg(MSG_TYPE_ERROR, PSTR("Failed to obtain time"));
        for (int k = 0; k < 100; k++)
        {
          delay(5000);
          if (getLocalTime(&timeinfo, 10000))
            break;
          time(&now_infunc);
          Serial.printf(PSTR("Setup: %ld"),now_infunc);
        }
      }
      else
      {
        setenv("TZ", timezone_info, 1);
        Serial.printf(PSTR("timezone_info: %s, %s"), timezone_info, s.timezone);
        tzset();
        clock_set = true;
      }
    }
    clock_set = (time(nullptr) > ACCEPTED_TIMESTAMP_MINIMUM);


  #elif defined(ESP8266)
    // TODO: prepare for no internet connection? -> channel defaults probably, RTC?
    // https://werner.rothschopf.net/202011_arduino_esp8266_ntp_en.htm
    configTime(timezone_info, s.custom_ntp_server);
  #endif
  */
  // init relays
  //  split comma separated gpio string to an array
  // TODO: if set in reset, we do not set it here?

  /*
    uint16_t channel_gpios[CHANNEL_COUNT];
    char ch_gpios_local[35];
    strncpy(ch_gpios_local, CH_GPIOS, sizeof(ch_gpios_local));
    str_to_uint_array(ch_gpios_local, channel_gpios, ","); // ESP32: first param must be locally allocated to avoid memory protection crash
    */
  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {
    // TODOX: this should be in flash already
    // s.ch[channel_idx].switch_id = channel_gpios[channel_idx];

    s.ch[channel_idx].toggle_last = now;
    // reset values from eeprom
    s.ch[channel_idx].wanna_be_up = false;
    s.ch[channel_idx].is_up = false;
    if (s.ch[channel_idx].type == CH_TYPE_GPIO_FIXED)
    { // gpio channel
      pinMode(s.ch[channel_idx].switch_id, OUTPUT);
    }
    set_channel_switch(channel_idx, s.ch[channel_idx].is_up);
  }
  if (!wifi_in_setup_mode)
    Serial.printf("\nArska dashboard url: http://%s/\n", WiFi.localIP().toString().c_str());

  Serial.printf(PSTR("Web admin: [%s], password: [%s]\n\n"), s.http_username, s.http_password);

  Serial.println(F("setup() finished:"));

  update_time_based_variables();

  // wifi_request.status = 0u; // listening //TESTING...

} // end of setup()

// returns start time period (first second of an hour if 60 minutes netting period) of time stamp,
long get_netting_period_start_time(long ts)
{
  return long(ts / (NETTING_PERIOD_SEC)) * (NETTING_PERIOD_SEC);
}

/**
 * @brief Arduino framwork function. This function is executed repeatedly after setup().  Make calls to scheduled functions
 *
 */
void loop()
{
  bool updated_ok;
  bool got_external_data_ok;

  //  handle initial wifi setting from the serial console command line
  if (wifi_in_setup_mode && Serial.available())
  {
    serial_command = Serial.readStringUntil('\n');
    if (serial_command_state == 0)
    {
      if (serial_command.c_str()[0] == 's')
      {
        scan_and_store_wifis(true);
        return;
      }
      if (isdigit(serial_command[0]))
      {
        int wifi_idx = serial_command.toInt();
        if (wifi_idx < network_count)
        {
          strncpy(s.wifi_ssid, WiFi.SSID(wifi_idx).c_str(), 30);
          Serial.printf(PSTR("Enter password for network %s\n"), WiFi.SSID(wifi_idx).c_str());
          Serial.println();
          Serial.flush();
          serial_command_state = 1;
        }
      }
    }
    else if (serial_command_state == 1)
    {
      strncpy(s.wifi_password, serial_command.c_str(), 30);
      for (int j = 0; j < strlen(s.wifi_password); j++)
        if (s.wifi_password[j] < 32) // cleanup, line feed
          s.wifi_password[j] = 0;

      Serial.printf(PSTR("Restarting with the new WiFI settings (SSID: %s, password: %s). Wait...\n\n\n"), s.wifi_ssid, s.wifi_password);
      Serial.println();
      Serial.flush();
      writeToEEPROM();
      delay(1000);
      ESP.restart();
    }
  }

#ifdef DEBUG_MODE
  // test gpio, started from admin UI
  if (todo_in_loop_test_gpio)
  {
    Serial.printf(PSTR("Testing gpio %d\n"), gpio_to_test_in_loop);
    pinMode(gpio_to_test_in_loop, OUTPUT);
    for (int j = 0; j < 3; j++)
    {
      digitalWrite(gpio_to_test_in_loop, LOW);
      delay(1000);
      digitalWrite(gpio_to_test_in_loop, HIGH);
      delay(500);
    }
    todo_in_loop_test_gpio = false;
    Serial.println(F("GPIO Testing ready"));
  }
#endif

  if (todo_in_loop_restart)
  {
    WiFi.disconnect();
    log_msg(MSG_TYPE_FATAL, PSTR("Restarting due to user activity (settings/cmd)."), true);
    delay(2000);
    ESP.restart();
  }
  if (todo_in_loop_scan_wifis)
  {
    todo_in_loop_scan_wifis = false;
    scan_and_store_wifis(false);
  }

  // started from admin UI
  if (todo_in_loop_scan_sensors)
  {
    todo_in_loop_scan_sensors = false;
    if (scan_sensors())
      writeToEEPROM();
  }

  // if in Wifi AP Mode (192.168.4.1), no other operations allowed
  check_forced_restart(); //!< if in config mode restart when time out
  if (wifi_in_setup_mode)
  { //!< do nothing else if in forced ap-mode
    delay(500);
    return;
  }

  // no other operations allowed before the clock is set
  time(&now);

  // initial message
  if (now < ACCEPTED_TIMESTAMP_MINIMUM)
  {
    delay(10000);
    return;
  }
  else if (started == 0) // we have clock set
  {
    started = now;
    log_msg(MSG_TYPE_INFO, PSTR("Started processing"), true);
    set_time_settings(); // set tz info
    ch_counters.init();
  }

  // set relays, if forced from dashboard
  if (todo_in_loop_set_relays)
  {
    todo_in_loop_set_relays = false;
    update_relay_states(); // new
    set_relays();
  }

  // just in case check the wifi and reconnect/restart if neede
  if (WiFi.waitForConnectResult(10000) != WL_CONNECTED)
  {
    for (int wait_loop = 0; wait_loop < 10; wait_loop++)
    {
      delay(1000);
      Serial.print('w');
      if (WiFi.waitForConnectResult(10000) == WL_CONNECTED)
        break;
    }
    if (WiFi.waitForConnectResult(10000) != WL_CONNECTED)
    {
      Serial.println(F("Restarting."));
      log_msg(MSG_TYPE_FATAL, PSTR("Restarting due to wifi error."), true);
      delay(1000);
      ESP.restart(); // boot if cannot recover wifi in time
    }
  }

  // update period info
  time(&now);
  current_period_start = get_netting_period_start_time(now);
  if (get_netting_period_start_time(now) == get_netting_period_start_time(started))
    recording_period_start = started;
  else
    recording_period_start = current_period_start;

  // new period
  if (previous_period_start != current_period_start)
  {
    Serial.printf("\nPeriod changed %ld -> %ld\n", previous_period_start, current_period_start);
    period_changed = true;
    next_process_ts = now; // process now if new period
    update_time_based_variables();
  }

#ifdef INFLUX_REPORT_ENABLED
  if (todo_in_loop_influx_write) // TODO: maybe we could combine this with buffer update
  {
    todo_in_loop_influx_write = false;
    write_buffer_to_influx();
  }
#endif

  if (next_query_price_data < now)
  {
    //   Serial.printf("next_query_price_data now: %ld \n", now);
    got_external_data_ok = get_price_data();
    todo_in_loop_update_price_rank_variables = got_external_data_ok;
    next_query_price_data = now + (got_external_data_ok ? 1200 : 120);
    Serial.printf("next_query_price_data: %ld \n", next_query_price_data);
  }

  if (todo_in_loop_update_price_rank_variables)
  {
    todo_in_loop_update_price_rank_variables = false;
    updated_ok = update_price_rank_variables();
    Serial.println("Returned from update_price_rank_variables");
    return;
  }

  if (next_query_fcst_data < now) // got solar fcsts
  {
    got_external_data_ok = get_solar_forecast();
    next_query_fcst_data = now + (got_external_data_ok ? 1200 : 120);
  }

  // TODO: all sensor /meter reads could be here?, do we need diffrent frequencies?
  if (next_process_ts <= now) // time to process
  {
    localtime_r(&now, &tm_struct);
    // Serial.printf("\nPeriods prev %ld ---- current %ld  (now %ld)\n", previous_period_start, current_period_start,now);
    Serial.printf(PSTR("\n%02d:%02d:%02d (%ld) Reading sensor and meter data \n"), tm_struct.tm_hour, tm_struct.tm_min, tm_struct.tm_sec, now);
    if (s.energy_meter_type != ENERGYM_NONE)
      read_energy_meter();

#ifdef SENSOR_DS18B20_ENABLED
    read_ds18b20_sensors();
#endif
    update_time_based_variables();
    update_meter_based_variables(); // TODO: if period change we could set write influx buffer after this?
    update_price_variables(current_period_start);

    // last_process_ts = millis();

    time(&now);
    next_process_ts = max((time_t)(next_process_ts + PROCESS_INTERVAL_SECS), now + (PROCESS_INTERVAL_SECS / 2)); // max is just in case to allow skipping processing, if processing takes too long
    update_relay_states();
    set_relays();
  }

  if (period_changed)
  {
#ifdef INFLUX_REPORT_ENABLED
    if (previous_period_start != 0)
    {
      // add values from the last period to the influx buffer and schedule writing to the influx db
      add_period_variables_to_influx_buffer(previous_period_start + (NETTING_PERIOD_SEC / 2));
      ch_counters.new_log_period(previous_period_start + (NETTING_PERIOD_SEC / 2));
      todo_in_loop_influx_write = true;
    }
#endif
    previous_period_start = current_period_start;
    period_changed = false;
  }
  period_changed = false;

#ifdef INVERTER_SMA_MODBUS_ENABLED
  mb.task(); // process modbuss event queue
#endif
}
