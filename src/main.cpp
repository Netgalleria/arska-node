/*
(C) Netgalleria Oy, Olli Rinne, 2021-2023

Resource files (see data subfolder):
- arska.js - web UI Javascript routines
- style.css - web UI styles
- ui2.html
- js/arska-ui.js - main javascript code template //TODO:separate variable(constant) and code
- js/jquery-3.6.0.min.js - jquery library
- data/version.txt - file system version info
- data/templates.json - rule template definitions
DEVEL BRANCH


 build options defined in platform.ini
#define ESP32 //if ESP32 platform
#define CHANNEL_COUNT 5 //number of channels, GPIO+virtual
#define CHANNEL_CONDITIONS_MAX 4 //max conditions/rules per channel
#define ONEWIRE_DATA_GPIO 13
#define INFLUX_REPORT_ENABLED
#define SENSOR_DS18B20_ENABLED // DS18B20 funtionality
#define RTC_DS3231_ENABLED //real time clock functionality
#define VARIABLE_SOURCE_ENABLED  // RFU for source/replica mode
10.11.2022 removed esp8266 options
*/

#define EEPROM_CHECK_VALUE 10100
#define DEBUG_MODE

#include <Arduino.h>
#include <math.h> //round
#include <EEPROM.h>
#include <LittleFS.h>
#include "WebAuthentication.h"

#include "ArskaGeneric.h"

#include "version.h"
const char compile_date[] = __DATE__ " " __TIME__;
char version_fs[40];
String version_fs_base; //= "";

#include <WiFi.h>
#include <AsyncTCP.h>
#include <HTTPClient.h>

#include <ESPAsyncWebServer.h>

#include <time.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

// features enabled
// moved to platformio.ini build parameters
#define MAX_DS18B20_SENSORS 3             //!< max number of sensors
#define SENSOR_VALUE_EXPIRE_TIME 1200     //!< if new value cannot read in this time (seconds), sensor value is set to 0
#define METER_SHELLY3EM_ENABLED           //!< Shelly 3EM functionality enabled
#define INVERTER_FRONIUS_SOLARAPI_ENABLED // can read Fronius inverter solarapi
#define INVERTER_SMA_MODBUS_ENABLED       // can read SMA inverter Modbus TCP
#define METER_HAN_ENABLED 1

#define MDNS_ENABLED_NOTENABLED // experimental, disabled due to stability concerns
#define PING_ENABLED            // for testing if internet connection etc ok

// TODO: replica mode will be probably removed later
// #define VARIABLE_SOURCE_ENABLED //!< this calculates variables (not just replica) only ESP32
// #define VARIABLE_MODE_SOURCE 0
// #define VARIABLE_MODE_REPLICA 1

#define TARIFF_VARIABLES_FI // add Finnish tariffs (yösähkö,kausisähkö) to variables

#define OTA_UPDATE_ENABLED // OTA general
// #define OTA_DOWNLOAD_ENABLED // OTA download from web site, OTA_UPDATE_ENABLED required, ->define in  platform.ini

#define eepromaddr 0
#define WATT_EPSILON 50

const char *default_http_password PROGMEM = "arska";
const char *price_data_filename PROGMEM = "/data/price-data.json";
const char *variables_filename PROGMEM = "/data/variables.json";
const char *wifis_filename PROGMEM = "/data/wifis.json";
const char *template_filename PROGMEM = "/data/templates.json";
// const char *ui_constants_filename PROGMEM = "/data/ui-constants.json";

const char *ntp_server_1 PROGMEM = "europe.pool.ntp.org";
const char *ntp_server_2 PROGMEM = "time.google.com";
const char *ntp_server_3 PROGMEM = "time.windows.com";

#define FORCED_RESTART_DELAY 600 // If cannot create Wifi connection, goes to AP mode for 600 sec and restarts

#define MSG_TYPE_INFO 0
#define MSG_TYPE_WARN 1
#define MSG_TYPE_ERROR 2
#define MSG_TYPE_FATAL 3

IPAddress IP_UNDEFINED(0, 0, 0, 0);

#define ACCEPTED_TIMESTAMP_MINIMUM 1656200000 // if timetstamp is greater, we assume it is from a real-time clock

#ifdef PING_ENABLED
#include <ESP32Ping.h>
bool ping_enabled = true;
/**
 * @brief test/ping connection to a host
 *
 * @param hostip
 * @param count
 * @return true
 * @return false
 */
bool test_host(IPAddress hostip, uint8_t count = 5)
{
  Serial.printf("Pinging %s\n", hostip.toString().c_str());
  bool success = Ping.ping(hostip, count);
  Serial.println(success ? "success" : "no response");
  return success;
}
#else
bool ping_enabled = false;
bool test_host(IPAddress hostip, uint8_t count = 5) return true; // stub, not in use

#endif

struct variable_st
{
  uint8_t id;
  char code[20];
  uint8_t type;
  long val_l;
};

time_t now; // this is the epoch
tm tm_struct;

// for timezone https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv

time_t forced_restart_ts = 0; // if wifi in forced ap-mode restart automatically to reconnect/start
bool wifi_in_setup_mode = false;
bool wifi_connection_succeeded = false;
time_t last_wifi_connect_tried = 0;
bool clock_set = false;       // true if we have get (more or less) correct time from net or rtc
bool config_resetted = false; // true if configuration cleared when version upgraded

#define ERROR_MSG_LEN 100
#define DEBUG_FILE_ENABLED
#ifdef DEBUG_FILE_ENABLED
const char *debug_filename PROGMEM = "/data/log.txt";
#endif

struct msg_st
{
  uint8_t type; // 0 info, 1-warn, 2 - error, 3-fatal
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

tm tm_struct_g;

/**
 * @brief Store latest log message
 *
 * @param type
 * @param msg
 * @param write_to_file
 */
void log_msg(uint8_t type, const char *msg, bool write_to_file = false)
{
  memset(last_msg.msg, 0, ERROR_MSG_LEN);
  strncpy(last_msg.msg, msg, (ERROR_MSG_LEN - 1));
  last_msg.type = type;
  time(&last_msg.ts);

  localtime_r(&last_msg.ts, &tm_struct_g);

  Serial.printf("%02d:%02d:%02d %s\n", tm_struct_g.tm_hour, tm_struct_g.tm_min, tm_struct_g.tm_sec, msg);

#ifdef DEBUG_FILE_ENABLED
  char datebuff[30];
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

#ifdef MDNS_ENABLED
#include "ESPmDNS.h"
#endif

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
    // Serial.println(F("check_forced_restart Restarting after passive period in config mode."));
    WiFi.disconnect();
    log_msg(MSG_TYPE_FATAL, PSTR("Restarting after passive period in config mode."), true);
    delay(2000);
    ESP.restart();
  }
}

AsyncWebServer server_web(80);

// Clock functions, supports optional DS3231 RTC
bool rtc_found = false;

const int force_up_hours[] = {0, 1, 2, 4, 8, 12, 24}; //!< dashboard forced channel duration times
const int price_variable_blocks[] = {9, 24};          //!< price ranks are calculated in 9 and 24 period windows

#define NETTING_PERIOD_MIN 60 //!< Netting time in minutes, (in Finland) 60 -> 15 minutes 2023
#define NETTING_PERIOD_SEC (NETTING_PERIOD_MIN * 60)
#define PRICE_PERIOD_SEC 3600
#define SECONDS_IN_DAY 86400

#define PV_FORECAST_HOURS 24 //!< solar forecast consist of this many hours

#define MAX_PRICE_PERIODS 48 //!< number of price period in the memory array
#define MAX_HISTORY_PERIODS 24
#define HISTORY_VARIABLE_COUNT 2
#define VARIABLE_LONG_UNKNOWN -2147483648 //!< variable with this value is undefined

long prices[MAX_PRICE_PERIODS];

bool prices_initiated = false;
time_t prices_first_period = 0;

// API
// const char *host_prices PROGMEM = "transparency.entsoe.eu"; //!< EntsoE reporting server for day-ahead prices
// fixed 14.2.2023, see https://transparency.entsoe.eu/news/widget?id=63eb9d10f9b76c35f7d06f2e
const char *host_prices PROGMEM = "web-api.tp.entsoe.eu";

const char *entsoe_ca_filename PROGMEM = "/data/sectigo_ca.pem";
const char *host_releases PROGMEM = "iot.netgalleria.fi";

#define RELEASES_HOST "iot.netgalleria.fi"
#define RELEASES_URL "/arska-install/releases.php?pre_releases=true"

const char *fcst_url_base PROGMEM = "http://www.bcdcenergia.fi/wp-admin/admin-ajax.php?action=getChartData"; //<! base url for Solar forecast from BCDC

// String url_base = "/api?documentType=A44&processType=A16";
const char *url_base PROGMEM = "/api?documentType=A44&processType=A16";
// API documents: https://transparency.entsoe.eu/content/static_content/Static%20content/web%20api/Guide.html#_areas

time_t next_query_price_data = 0;
time_t next_query_fcst_data = 0;

// https://transparency.entsoe.eu/api?securityToken=XXX&documentType=A44&In_Domain=10YFI-1--------U&Out_Domain=10YFI-1--------U&processType=A16&outBiddingZone_Domain=10YCZ-CEPS-----N&periodStart=202104200000&periodEnd=202104200100
const int httpsPort = 443;

// https://werner.rothschopf.net/microcontroller/202112_arduino_esp_ntp_rtc_en.htm
/**
 * @brief Get the Timestamp based of date/time components
 *
 * @param year
 * @param mon
 * @param mday
 * @param hour
 * @param min
 * @param sec
 * @return int64_t
 */
/*
int64_t getTimestamp(int year, int mon, int mday, int hour, int min, int sec)
{
  const uint16_t ytd[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};                // Anzahl der Tage seit Jahresanfang ohne Tage des aktuellen Monats und ohne Schalttag
  int leapyears = ((year - 1) - 1968) / 4 - ((year - 1) - 1900) / 100 + ((year - 1) - 1600) / 400; // Anzahl der Schaltjahre seit 1970 (ohne das evtl. laufende Schaltjahr)
  int64_t days_since_1970 = (year - 1970) * 365 + leapyears + ytd[mon - 1] + mday - 1;
  if ((mon > 2) && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)))
    days_since_1970 += 1; // +Schalttag, wenn Jahr Schaltjahr ist
  return sec + 60 * (min + 60 * (hour + 24 * days_since_1970));
}


*/

/**
 * @brief Set the internal clock from RTC or browser
 *
 * @param epoch  epoch (seconds in GMT)
 * @param microseconds
 */
/*
void setInternalTime(uint64_t epoch = 0, uint32_t us = 0)
{
  struct timeval tv;
  tv.tv_sec = epoch;
  tv.tv_usec = us;
  settimeofday(&tv, NULL);
}
*/

#ifdef RTC_DS3231_ENABLED

#include <RTClib.h>
#include <coredecls.h>

RTC_DS3231 rtc; //!< Real time clock object
/*

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
    String current_fs_version = info_file.readStringUntil('\n'); // e.g. 0.92.0-rc1.1102 - 2022-09-28 12:24:56
    version_fs_base = current_fs_version.substring(0, current_fs_version.lastIndexOf('.'));
    Serial.printf("version_fs_base: %s , VERSION_BASE %s\n", version_fs_base.c_str(), VERSION_BASE);
    strncpy(version_fs, current_fs_version.c_str(), sizeof(version_fs) - 1);
    is_ok = version_fs_base.equals(VERSION_BASE);
    if (is_ok)
      Serial.println("No need for filesystem update.");
    else
      Serial.println("Filesystem update required.");
  }
  else
  {
    is_ok = false;
    Serial.println(F("Cannot open version.txt for reading."));
  }

  info_file.close();
  return is_ok;
}

/**
 * @brief Operator handling rules

 *
 */
struct oper_st
{
  uint8_t id;        //!< identifier used in data structures
  char code[10];     //!< code used in UI
  bool gt;           //!< true if variable is greater than the compared value
  bool eq;           //!< true if variable is equal with then compared value
  bool reverse;      //!< negate comparison result
  bool boolean_only; //!< hand variable value as boolean (1=true), eq and reverse possible
  bool has_value;    //!< true if not value not VARIABLE_LONG_UNKNOWN, reverse possible
};

#define OPER_COUNT 10
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
8, "defined", true if variable has value
9, "undefined", true is variable value is not set
 */
// TODO: maybe operator NA - not available
const oper_st opers[OPER_COUNT] = {{0, "=", false, true, false, false, false}, {1, ">", true, false, false, false, false}, {2, "<", true, true, true, false, false}, {3, ">=", true, true, false, false, false}, {4, "<=", true, false, true, false, false}, {5, "<>", false, true, true, false, false}, {6, "is", false, false, false, true, false}, {7, "not", false, false, true, true, false}, {8, "defined", false, false, false, false, true}, {9, "undefined", false, false, true, false, true}};

/*constant_type, variable_type
long val_l
type = 0 default long
type = 1  10**1 stored to long  , ie. 1.5 -> 15
... 10
*/
#define CONSTANT_TYPE_INT 0                 //!< integer(long) value
#define CONSTANT_TYPE_DEC1 1                //!< numeric value, 1 decimal
#define CONSTANT_TYPE_CHAR_2 22             //!< 2 characters string to long, e.g. hh
#define CONSTANT_TYPE_CHAR_4 24             //!< 4 characters string to long, e.g. hhmm
#define CONSTANT_TYPE_BOOLEAN_NO_REVERSE 50 //!< boolean , no reverse allowed
#define CONSTANT_TYPE_BOOLEAN_REVERSE_OK 51 //!< boolean , reverse allowed
/**
 * @brief Statement structure, rules (conditions) consist of one or more statements
 *
 */
struct statement_st
{
  int variable_id;
  uint8_t oper_id;
  uint8_t constant_type;
  // uint16_t depends; // TODO: check if needed?
  long const_val;
};

// do not change variable id:s (will broke statements)
#define VARIABLE_COUNT 33

#define VARIABLE_PRICE 0                     //!< price of current period, 1 decimal
#define VARIABLE_PRICERANK_9 1               //!< price rank within 9 hours window
#define VARIABLE_PRICERANK_24 2              //!< price rank within 24 hours window
#define VARIABLE_PRICERANK_FIXED_24 3        //!< price rank within 24 hours (day) fixed 00-23
#define VARIABLE_PRICERANK_FIXED_8 4         //!< price rank within 8 hours fixed blocks 1)23-06, 2) 07-14, 3)15-22
#define VARIABLE_PRICEAVG_9 5                //!< average price of 9 hours sliding windows
#define VARIABLE_PRICEAVG_24 6               //!< average price of 24 hours sliding windows
#define VARIABLE_PRICERANK_FIXED_8_BLOCKID 7 //!< block id of 8 hours block, 0-indexed 0-2
#define VARIABLE_PRICEDIFF_9 9               //!< price diffrence of current price and VARIABLE_PRICEAVG_9
#define VARIABLE_PRICEDIFF_24 10             //!< price diffrence of current price and VARIABLE_PRICEAVG_24
#define VARIABLE_PRICERATIO_9 13
#define VARIABLE_PRICERATIO_24 14
#define VARIABLE_PRICERATIO_FIXED_24 15
#define VARIABLE_PVFORECAST_SUM24 20
#define VARIABLE_PVFORECAST_VALUE24 21
#define VARIABLE_PVFORECAST_AVGPRICE24 22
#define VARIABLE_AVGPRICE24_EXCEEDS_CURRENT 23
#define VARIABLE_EXTRA_PRODUCTION 100
#define VARIABLE_PRODUCTION_POWER 101
#define VARIABLE_SELLING_POWER 102
#define VARIABLE_SELLING_ENERGY 103
#define VARIABLE_SELLING_POWER_NOW 104
#define VARIABLE_PRODUCTION_ENERGY 105
#define VARIABLE_MM 110
#define VARIABLE_MMDD 111
#define VARIABLE_WDAY 112
#define VARIABLE_HH 115
#define VARIABLE_HHMM 116
#define VARIABLE_DAYENERGY_FI 130        //!< true if day, (07:00-22:00 Finnish tariffs), logical
#define VARIABLE_WINTERDAY_FI 140        //!< true if winterday, (Finnish tariffs), logical
#define VARIABLE_SENSOR_1 201            //!< sensor1 value, float, 1 decimal
#define VARIABLE_BEEN_UP_AGO_HOURS_0 170 // RFU
#define VARIABLE_LOCALTIME_TS 1001

// variable dependency bitmask
/*
#define VARIABLE_DEPENDS_UNDEFINED 0
#define VARIABLE_DEPENDS_PRICE 1
#define VARIABLE_DEPENDS_SOLAR_FORECAST 2
#define VARIABLE_DEPENDS_GRID_METER 4
#define VARIABLE_DEPENDS_PRODUCTION_METER 8
#define VARIABLE_DEPENDS_SENSOR 16

// combined
#define VARIABLE_DEPENDS_PRICE_SOLAR 3
*/

long variable_history[HISTORY_VARIABLE_COUNT][MAX_HISTORY_PERIODS];
byte channel_history[CHANNEL_COUNT][MAX_HISTORY_PERIODS];
int history_variables[HISTORY_VARIABLE_COUNT] = {VARIABLE_SELLING_ENERGY, VARIABLE_PRODUCTION_ENERGY}; // oli VARIABLE_SELLING_POWER,VARIABLE_PRODUCTION_POWER,
int get_variable_history_idx(int id)
{
  for (int i = 0; i < HISTORY_VARIABLE_COUNT; i++)
    if (history_variables[i] == id)
      return i;
  return -1;
}

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

  // get
  // unsigned long operator [](int i) {return variables[i];}

  bool is_statement_true(statement_st *statement, bool default_value = false);
  int get_variable_by_id(int id, variable_st *variable);
  void get_variable_by_idx(int idx, variable_st *variable);
  long float_to_internal_l(int id, float val_float);
  float const_to_float(int id, long const_in);
  int to_str(int id, char *strbuff, bool use_overwrite_val = false, long overwrite_val = 0, size_t buffer_length = 1);
  int get_variable_count() { return VARIABLE_COUNT; };
  void rotate_period();

private:
  // dependency removed variable_st variables[VARIABLE_COUNT] = {{VARIABLE_PRICE, "price", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_PRICE}, {VARIABLE_PRICERANK_9, "price rank 9h", 0, VARIABLE_DEPENDS_PRICE}, {VARIABLE_PRICERANK_24, "price rank 24h", 0, VARIABLE_DEPENDS_PRICE}, {VARIABLE_PRICERANK_FIXED_24, "price rank fix 24h", 0, VARIABLE_DEPENDS_PRICE}, {VARIABLE_PRICERANK_FIXED_8, "rank in 8 h block", CONSTANT_TYPE_INT, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_PRICERANK_FIXED_8_BLOCKID, "8 h block id"}, {VARIABLE_PRICEAVG_9, "price avg 9h", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_PRICE}, {VARIABLE_PRICEAVG_24, "price avg 24h", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_PRICE}, {VARIABLE_PRICERATIO_9, "p ratio to avg 9h", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_PRICE}, {VARIABLE_PRICEDIFF_9, "p diff to avg 9h", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_PRICE}, {VARIABLE_PRICEDIFF_24, "p diff to avg 24h", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_PRICE}, {VARIABLE_PRICERATIO_24, "p ratio to avg 24h", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_PRICE}, {VARIABLE_PRICERATIO_FIXED_24, "p ratio fixed 24h", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_PRICE}, {VARIABLE_PVFORECAST_SUM24, "pv forecast 24 h", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_SOLAR_FORECAST}, {VARIABLE_PVFORECAST_VALUE24, "pv value 24 h", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_PRICE_SOLAR}, {VARIABLE_PVFORECAST_AVGPRICE24, "pv price avg 24 h", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_PRICE_SOLAR}, {VARIABLE_AVGPRICE24_EXCEEDS_CURRENT, "future pv higher", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_PRICE_SOLAR}, {VARIABLE_EXTRA_PRODUCTION, "extra production", CONSTANT_TYPE_BOOLEAN_REVERSE_OK, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_PRODUCTION_POWER, "production (per) W", 0, VARIABLE_DEPENDS_PRODUCTION_METER}, {VARIABLE_SELLING_POWER, "selling W", 0, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_SELLING_ENERGY, "selling Wh", 0, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_SELLING_POWER_NOW, "selling now W", 0, VARIABLE_DEPENDS_UNDEFINED},  {VARIABLE_PRODUCTION_ENERGY, "production Wh", 0, VARIABLE_DEPENDS_UNDEFINED},  {VARIABLE_MM, "mm, month", CONSTANT_TYPE_CHAR_2, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_MMDD, "mmdd", CONSTANT_TYPE_CHAR_4, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_WDAY, "weekday (1-7)", 0, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_HH, "hh, hour", CONSTANT_TYPE_CHAR_2, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_HHMM, "hhmm", CONSTANT_TYPE_CHAR_4, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_DAYENERGY_FI, "day", CONSTANT_TYPE_BOOLEAN_REVERSE_OK, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_WINTERDAY_FI, "winterday", CONSTANT_TYPE_BOOLEAN_REVERSE_OK, VARIABLE_DEPENDS_UNDEFINED}, {VARIABLE_SENSOR_1, "sensor 1", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_SENSOR}, {VARIABLE_SENSOR_1 + 1, "sensor 2", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_SENSOR}, {VARIABLE_SENSOR_1 + 2, "sensor 3", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_SENSOR}};

  variable_st variables[VARIABLE_COUNT] = {{VARIABLE_PRICE, "price", CONSTANT_TYPE_DEC1}, {VARIABLE_PRICERANK_9, "price rank 9h", 0}, {VARIABLE_PRICERANK_24, "price rank 24h", 0}, {VARIABLE_PRICERANK_FIXED_24, "price rank fix 24h", 0}, {VARIABLE_PRICERANK_FIXED_8, "rank in 8 h block", CONSTANT_TYPE_INT}, {VARIABLE_PRICERANK_FIXED_8_BLOCKID, "8 h block id"}, {VARIABLE_PRICEAVG_9, "price avg 9h", CONSTANT_TYPE_DEC1}, {VARIABLE_PRICEAVG_24, "price avg 24h", CONSTANT_TYPE_DEC1}, {VARIABLE_PRICERATIO_9, "p ratio to avg 9h", CONSTANT_TYPE_DEC1}, {VARIABLE_PRICEDIFF_9, "p diff to avg 9h", CONSTANT_TYPE_DEC1}, {VARIABLE_PRICEDIFF_24, "p diff to avg 24h", CONSTANT_TYPE_DEC1}, {VARIABLE_PRICERATIO_24, "p ratio to avg 24h", CONSTANT_TYPE_DEC1}, {VARIABLE_PRICERATIO_FIXED_24, "p ratio fixed 24h", CONSTANT_TYPE_DEC1}, {VARIABLE_PVFORECAST_SUM24, "pv forecast 24 h", CONSTANT_TYPE_DEC1}, {VARIABLE_PVFORECAST_VALUE24, "pv value 24 h", CONSTANT_TYPE_DEC1}, {VARIABLE_PVFORECAST_AVGPRICE24, "pv price avg 24 h", CONSTANT_TYPE_DEC1}, {VARIABLE_AVGPRICE24_EXCEEDS_CURRENT, "future pv higher", CONSTANT_TYPE_DEC1}, {VARIABLE_EXTRA_PRODUCTION, "extra production", CONSTANT_TYPE_BOOLEAN_REVERSE_OK}, {VARIABLE_PRODUCTION_POWER, "production (per) W", 0}, {VARIABLE_SELLING_POWER, "selling W", 0}, {VARIABLE_SELLING_ENERGY, "selling Wh", 0}, {VARIABLE_SELLING_POWER_NOW, "selling now W", 0}, {VARIABLE_PRODUCTION_ENERGY, "production Wh", 0}, {VARIABLE_MM, "mm, month", CONSTANT_TYPE_CHAR_2}, {VARIABLE_MMDD, "mmdd", CONSTANT_TYPE_CHAR_4}, {VARIABLE_WDAY, "weekday (1-7)", 0}, {VARIABLE_HH, "hh, hour", CONSTANT_TYPE_CHAR_2}, {VARIABLE_HHMM, "hhmm", CONSTANT_TYPE_CHAR_4}, {VARIABLE_DAYENERGY_FI, "day", CONSTANT_TYPE_BOOLEAN_REVERSE_OK}, {VARIABLE_WINTERDAY_FI, "winterday", CONSTANT_TYPE_BOOLEAN_REVERSE_OK}, {VARIABLE_SENSOR_1, "sensor 1", CONSTANT_TYPE_DEC1}, {VARIABLE_SENSOR_1 + 1, "sensor 2", CONSTANT_TYPE_DEC1}, {VARIABLE_SENSOR_1 + 2, "sensor 3", CONSTANT_TYPE_DEC1}};
  // experimental , not in use, { VARIABLE_BEEN_UP_AGO_HOURS_0, "ch 1, up x h ago", CONSTANT_TYPE_DEC1, VARIABLE_DEPENDS_UNDEFINED }};
  // {VARIABLE_PRICERANK_FIXED_8,"rank in 8 h block", CONSTANT_TYPE_INT,VARIABLE_DEPENDS_UNDEFINED} ,{ VARIABLE_PRICERANK_FIXED_8_BLOCKID, "8 h block id"}
  int get_variable_index(int id);
};

void Variables::rotate_period()
{
  // rotate to variable history
  for (int v_idx = 0; v_idx < HISTORY_VARIABLE_COUNT; v_idx++)
  {
    variable_history[v_idx][MAX_HISTORY_PERIODS - 1] = this->get_l(history_variables[v_idx]);
    for (int h_idx = 0; (h_idx + 1) < MAX_HISTORY_PERIODS; h_idx++)
      variable_history[v_idx][h_idx] = variable_history[v_idx][h_idx + 1];
    variable_history[v_idx][MAX_HISTORY_PERIODS - 1] = 0; // current peeriod
  }
}

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
    // update history
    int v_h_idx = get_variable_history_idx(id);
    if (v_h_idx != -1)
      variable_history[v_h_idx][MAX_HISTORY_PERIODS - 1] = val_l;
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

      sprintf(strbuff, "%ld", val_l); // kokeiltu ilman paddingiä
      return strlen(strbuff);
    }
    else if (var.type == CONSTANT_TYPE_BOOLEAN_NO_REVERSE || var.type == CONSTANT_TYPE_BOOLEAN_REVERSE_OK)
    {
      sprintf(strbuff, "%s", (var.val_l == 1) ? "true" : "false");
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
  if ((variable_idx == -1))
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

  if (oper.has_value)
  {
    if (oper.reverse)
      return (var.val_l == VARIABLE_LONG_UNKNOWN);
    else
      return (var.val_l != VARIABLE_LONG_UNKNOWN);
  }

  if ((var.val_l == VARIABLE_LONG_UNKNOWN))
    return default_value;

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
time_t next_process_ts = 0;      // start reading as soon as you get to first loop

// experimental 0.93
int grid_protection_delay_max = 0;  // TODO: still disabled (=0),later set to admin parameters
int grid_protection_delay_interval; // random, init in setup()

time_t recording_period_start = 0; // first period: boot time, later period starts
time_t current_period_start = 0;
time_t previous_period_start = 0;
time_t energym_read_last = 0;
time_t productionm_read_last = 0;
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
bool todo_in_loop_discover_devices = false;
bool todo_in_loop_get_releases = false;
bool todo_in_loop_write_to_eeprom = false;
bool todo_in_loop_update_firmware_partition = false;

bool todo_in_loop_reapply_relay_states = false;
bool relay_state_reapply_required[CHANNEL_COUNT]; // if true channel parameters have been changed and r

#define CH_TYPE_UNDEFINED 0
#define CH_TYPE_GPIO_FIXED 1
#define CH_TYPE_GPIO_USER_DEF 3
#define CH_TYPE_SHELLY_1GEN 2        // new, was CH_TYPE_SHELLY_ONOFF
#define CH_TYPE_SHELLY_2GEN 4        //
#define CH_TYPE_TASMOTA 5            //
#define CH_TYPE_GPIO_USR_INVERSED 10 // RFU
#define CH_TYPE_MODBUS_RTU 20        // RFU
#define CH_TYPE_DISABLED 255         // RFU, we could have disabled, but allocated channels (binary )

struct channel_type_st
{
  uint8_t id;
  const char *name;
};
// #define CH_TYPE_SHELLY_ONOFF 2  -> 10
// #define CH_TYPE_DISABLED 255 // RFU, we could have disabled, but allocated channels (binary )

#define CHANNEL_TYPE_COUNT 7

//channel_type_st channel_types[CHANNEL_TYPE_COUNT] = {{CH_TYPE_UNDEFINED, "undefined"}, {CH_TYPE_GPIO_FIXED, "GPIO fixed"}, {CH_TYPE_GPIO_USER_DEF, "GPIO"}, {CH_TYPE_SHELLY_1GEN, "Shelly Gen 1"}, {CH_TYPE_SHELLY_2GEN, "Shelly Gen 2"}, {CH_TYPE_TASMOTA, "Tasmota"}, {CH_TYPE_GPIO_USR_INVERSED, "GPIO, inversed"}};

channel_type_st channel_types[CHANNEL_TYPE_COUNT] = {{CH_TYPE_UNDEFINED, "undefined"},  {CH_TYPE_GPIO_USER_DEF, "GPIO"}, {CH_TYPE_SHELLY_1GEN, "Shelly Gen 1"}, {CH_TYPE_SHELLY_2GEN, "Shelly Gen 2"}, {CH_TYPE_TASMOTA, "Tasmota"}, {CH_TYPE_GPIO_USR_INVERSED, "GPIO, inversed"}};

// later , {CH_TYPE_MODBUS_RTU, "Modbus RTU"}

struct device_db_struct
{
  const char *app;
  uint8_t switch_type;
  uint8_t outputs;
};

device_db_struct device_db[] PROGMEM = {{"shelly1l", CH_TYPE_SHELLY_1GEN, 1}, {"shellyswitch", CH_TYPE_SHELLY_1GEN, 2}, {"shellyswitch25", CH_TYPE_SHELLY_1GEN, 2}, {"shelly4pro", CH_TYPE_SHELLY_1GEN, 4}, {"shellyplug", CH_TYPE_SHELLY_1GEN, 1}, {"shellyplug-s", CH_TYPE_SHELLY_1GEN, 1}, {"shellyem", CH_TYPE_SHELLY_1GEN, 1}, {"shellyem3", CH_TYPE_SHELLY_1GEN, 1}, {"shellypro2", CH_TYPE_SHELLY_2GEN, 2}};

#define HW_TEMPLATE_COUNT 4
#define HW_TEMPLATE_GPIO_COUNT 4
struct hw_template_st
{
  int id;
  const char *name;
  uint8_t gpios[HW_TEMPLATE_GPIO_COUNT];
};

hw_template_st hw_templates[HW_TEMPLATE_COUNT] = {{0, "manual", {255, 255, 255, 255}}, {1, "esp32lilygo-4ch", {21, 19, 18, 5}}, {2, "esp32wroom-4ch-a", {32, 33, 25, 26}}, {3, "devantech-esp32lr42", {33, 25, 26, 27}}};

// #define CHANNEL_CONDITIONS_MAX 3 //platformio.ini
#define CHANNEL_STATES_MAX 10
#define RULE_STATEMENTS_MAX 5
#define MAX_CHANNELS_SWITCHED_AT_TIME 1

#define MAX_CH_ID_STR_LENGTH 20
#define MAX_ID_STR_LENGTH 30
#define MAX_URL_STR_LENGTH 70
#define MAX_PWD_STR_LENGTH 15

// Current phase of OTA update, s.ota_update_phase
#define OTA_PHASE_NONE 0
#define OTA_PHASE_FWUPDATED_CHECKFS 100

// Energy metering types
#define ENERGYM_NONE 0
#define ENERGYM_SHELLY3EM 1
#define ENERGYM_HAN_WIFI 4

// Production metering (inverter) types
#define PRODUCTIONM_NONE 0
#define PRODUCTIONM_FRONIUS_SOLAR 1
#define PRODUCTIONM_SMA_MODBUS_TCP 2

// Type texts for config ui - now hardcoded in html
// const char *energym_strings[] PROGMEM = {"none", "Shelly 3EM", "Fronius Solar API", "SMA Modbus TCP"};

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
  float target_val; // TODO: remove
  bool on;
  bool condition_active; // for showing if the condition is currently active, for tracing
} condition_struct;      // size 88

#define CHANNEL_CONFIG_MODE_RULE 0
#define CHANNEL_CONFIG_MODE_TEMPLATE 1
// Channel stucture, elements of channel array in setting, stored in non-volatile memory
typedef struct
{
  condition_struct conditions[CHANNEL_CONDITIONS_MAX];
  char id_str[MAX_CH_ID_STR_LENGTH];
  uint8_t relay_id;       //!< relay id, eg. number modbus server id
  uint8_t relay_unit_id;  //!<  unit id, eg. port in a relay
  uint8_t relay_iface_id; // RFU, interface, eg eth, wifi
  IPAddress relay_ip;     //!< relay ip address
  bool is_up;             //!< is channel currently up
  bool wanna_be_up;       //!< should channel be switched up (when the time is right)
  uint8_t type;           //!< channel type, for values see constants CH_TYPE_...
  time_t uptime_minimum;  //!< minimum time channel should be up
  time_t up_last;         //!< last last time up time
  time_t force_up_from;   //<! force channel up starting from
  time_t force_up_until;  //<! force channel up until
  uint8_t config_mode;    //<! rule config mode: CHANNEL_CONFIG_MODE_RULE, CHANNEL_CONFIG_MODE_TEMPLATE
  int template_id;        //<! template id if config mode is CHANNEL_CONFIG_MODE_TEMPLATE
  uint32_t channel_color; // UI color in graphs etc
} channel_struct;

#ifdef SENSOR_DS18B20_ENABLED
typedef struct
{
  DeviceAddress address;             //!< 1-wire hardware address of the sensor device
  char id_str[MAX_CH_ID_STR_LENGTH]; //!< sensor id string, RFU
} sensor_struct;
#endif

// TODO: add fixed ip, subnet?
// Setting stucture, stored in non-volatile memory
typedef struct
{
  int check_value;                       //!< version number of memory struct, if equals one in the eeprom, stored data can be directly used
  char wifi_ssid[MAX_ID_STR_LENGTH];     //!< WiFi SSID
  char wifi_password[MAX_ID_STR_LENGTH]; //!< WiFi password
  char http_username[MAX_ID_STR_LENGTH];
  char http_password[MAX_ID_STR_LENGTH];
  channel_struct ch[CHANNEL_COUNT];
  char variable_server[MAX_ID_STR_LENGTH]; //!< projected to be used in replica mode, RFU
  char entsoe_api_key[37];                 //!< EntsoE API key
  char entsoe_area_code[17];               //!< Price area code in day ahead market
  char custom_ntp_server[35];              //!< RFU, TODO:UI to set up
  char timezone[4];                        //!< EET,CET supported
  // uint32_t baseload;                       //!< production above baseload is "free" to use/store, used to estimate own consumption when production is read from inverter and no ebergy meter is connected
  uint8_t ota_update_phase;  //!< Phase of curent OTA update, if updating
  uint8_t energy_meter_type; //!< energy metering type, see constants: ENERGYM_
  // char energy_meter_host[MAX_URL_STR_LENGTH]; //!< enerygy meter address string
  IPAddress energy_meter_ip;  //!< enerygy meter address string
  uint16_t energy_meter_port; //!< energy meter port,  tcp port if energy_meter_type == ENERGYM_SMA_MODBUS_TCP
  uint8_t energy_meter_id;    //!< energy meter id,  uinid if energy_meter_type == ENERGYM_SMA_MODBUS_TCP
  char energy_meter_password[MAX_PWD_STR_LENGTH];
  uint8_t production_meter_type;
  IPAddress production_meter_ip;
  uint16_t production_meter_port;
  uint8_t production_meter_id;
  char forecast_loc[MAX_ID_STR_LENGTH]; //!< Energy forecast location, BCDC-energy location
  // uint8_t variable_mode;                // VARIABLE_MODE_SOURCE (currently only supported), VARIABLE_MODE_REPLICA (not implemented)
  char lang[3]; //<! preferred language
#ifdef SENSOR_DS18B20_ENABLED
  sensor_struct sensors[MAX_DS18B20_SENSORS]; //!< 1-wire temperature sensors
#endif
  int hw_template_id;  //!< hardware template defining channel gpios, see hw_templates
  bool mdns_activated; //!< is mDSN device discovery active, currently deactivatated due to stability concerns
#ifdef INFLUX_REPORT_ENABLED
  char influx_url[70];
  char influx_token[100];
  char influx_org[30];
  char influx_bucket[20];
#endif
} settings_struct;

// this stores settings also to eeprom
settings_struct s;

#ifdef INFLUX_REPORT_ENABLED
#include <InfluxDbClient.h>
const char *influx_device_id_prefix PROGMEM = "arska-";
String wifi_mac_short;

typedef struct
{
  bool state;
  time_t this_state_started_period;
  time_t this_state_started_epoch;
  int on_time;
  int off_time;
  byte utilization_;
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
  time_t get_duration_in_this_state(int channel_idx);
  byte get_utilization(int channel_idx) { return channel_logs[channel_idx].utilization_; }
  void update_utilization(int channel_idx);

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
    channel_logs[i].this_state_started_period = now_l;
    channel_logs[i].this_state_started_epoch = now_l;
  }
}

time_t ChannelCounters::get_duration_in_this_state(int channel_idx)
{
  time_t now_l;
  time(&now_l);
  return (now_l - channel_logs[channel_idx].this_state_started_epoch);
};
void ChannelCounters::update_utilization(int channel_idx)
{
  float utilization = 0;
  set_state(channel_idx, channel_logs[channel_idx].state); // this will update counters without changing state
  if ((channel_logs[channel_idx].off_time + channel_logs[channel_idx].on_time) > 0)
    utilization = (float)channel_logs[channel_idx].on_time / (float)(channel_logs[channel_idx].off_time + channel_logs[channel_idx].on_time);

  channel_logs[channel_idx].utilization_ = (byte)(utilization * 100 + 0.001);
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
    // update current (old) period utilization
    set_state(i, channel_logs[i].state); // this will update counters without changing state
    if ((channel_logs[i].off_time + channel_logs[i].on_time) > 0)
      utilization = (float)channel_logs[i].on_time / (float)(channel_logs[i].off_time + channel_logs[i].on_time);
    else
      utilization = 0;

    channel_logs[i].utilization_ = (byte)(utilization * 100 + 0.001);

    snprintf(field_name, sizeof(field_name), "ch%d", i + 1); // 1-indexed channel numbers in UI
    point_period_avg.addField(field_name, utilization);

    // rotate to channel history
    channel_history[i][MAX_HISTORY_PERIODS - 1] = (byte)(utilization * 100 + 0.001);
    for (int h_idx = 0; (h_idx + 1) < MAX_HISTORY_PERIODS; h_idx++)
      channel_history[i][h_idx] = channel_history[i][h_idx + 1];
    channel_history[i][MAX_HISTORY_PERIODS - 1] = 0;
  }

  // then reset
  for (int i = 0; i < CHANNEL_COUNT; i++)
  {
    channel_logs[i].off_time = 0;
    channel_logs[i].on_time = 0;
    channel_logs[i].this_state_started_period = now_l;
  }
  // write buffer
}

void ChannelCounters::set_state(int channel_idx, bool new_state)
{
  time_t now_l;
  time(&now_l);
  int previous_state_duration = (now_l - channel_logs[channel_idx].this_state_started_period);
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
  channel_logs[channel_idx].utilization_ = utilization;

  Serial.printf("%d, (on: %d / off: %d ) = %f\n", channel_idx, channel_logs[channel_idx].on_time, channel_logs[channel_idx].off_time, utilization);

  bool old_state = channel_logs[channel_idx].state;
  channel_logs[channel_idx].state = new_state;
  channel_logs[channel_idx].this_state_started_period = now_l;
  if (old_state != new_state)
    channel_logs[channel_idx].this_state_started_epoch = now_l;
}

ChannelCounters ch_counters;

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

  // if (vars.is_set(VARIABLE_PRODUCTION_ENERGY))
  //   point_period_avg.addField("productionWh", vars.get_f(VARIABLE_PRODUCTION_ENERGY));

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
  if (((strstr(s.influx_url, "http") - s.influx_url) != 0) || strlen(s.influx_org) < 5 || strlen(s.influx_token) < 5 || strlen(s.influx_bucket) < 1)
  {
    Serial.println(F("Skipping influx write: invalid or missing parameters."));
    return false;
  }

  InfluxDBClient ifclient(s.influx_url, s.influx_org, s.influx_bucket, s.influx_token);
  ifclient.setInsecure(true); // TODO: cert handling

  // ifclient.setWriteOptions(WriteOptions().writePrecision(WritePrecision::S)); // set time precision to seconds
  ifclient.setWriteOptions(WriteOptions().writePrecision(WritePrecision::S).batchSize(2).bufferSize(2));

  ifclient.setHTTPOptions(HTTPOptions().connectionReuse(true));

  bool influx_write_ok = write_point_buffer_influx(&ifclient, &point_period_avg);
  if (!influx_write_ok)
    return false;

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

  // Missing or invalid parameters
  if (((strstr(s.influx_url, "http") - s.influx_url) != 0) || strlen(s.influx_org) < 5 || strlen(s.influx_token) < 5 || strlen(s.influx_bucket) < 1)
  {
    Serial.println(F("write_buffer_to_influx: invalid or missing parameters."));
    return false;
  }

  String query = "from(bucket: \"" + String(s.influx_bucket) + "\") |> range(start: -1d, stop: 2d) |> filter(fn: (r) => r._measurement == \"period_price\" )|> filter(fn: (r) => r[\"_field\"] == \"price\") ";
  query += "  |> keep(columns: [\"_time\"]) |> last(column: \"_time\")";

  InfluxDBClient ifclient(s.influx_url, s.influx_org, s.influx_bucket, s.influx_token);
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

  for (uint16_t i = 0; (i < prices_array.size() && i < MAX_PRICE_PERIODS); i++)
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

  // removed from 0.93
  // struct tm timeinfo;
  // getLocalTime(&timeinfo); // update from NTP?
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
  uint8_t i = 0;
  for (int ch_state_idx = 0; ch_state_idx < MAX_SPLIT_ARRAY_SIZE; ch_state_idx++)
  {
    array_out[ch_state_idx] = 0;
  }
  while (ptr)
  {
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
uint8_t serial_command_state = 0;
int network_count = 0;

/**
 * @brief Scans wireless networks on the area and stores list to a file.
 * @details description Started from loop-function. Do not run interactively (from a http call).
 *
 */
void scan_and_store_wifis(bool print_out)
{
  network_count = WiFi.scanNetworks();
  int good_wifi_count = 0;
  StaticJsonDocument<1248> doc;

  if (print_out)
    Serial.println("Available WiFi networks:\n");

  for (int i = 0; i < network_count; ++i)
  {
    if (WiFi.RSSI(i) < -80) // too weak signals not listed, could be actually -75
      continue;
    good_wifi_count++;
    JsonObject json_wifi = doc.createNestedObject();
    json_wifi["id"] = WiFi.SSID(i);
    json_wifi["rssi"] = WiFi.RSSI(i);

    if (print_out)
      Serial.printf("%d - %s (%ld)\n", i, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
  }

  if (print_out)
  {
    Serial.println("-");
    Serial.flush();
  }

  File wifi_file = LittleFS.open(wifis_filename, "w"); // Open file for writing
  serializeJson(doc, wifi_file);
  wifi_file.close();
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
  int eeprom_used_size = sizeof(s);
  EEPROM.begin(eeprom_used_size); // TODO:
  EEPROM.get(eepromaddr, s);
  Serial.printf(PSTR("readFromEEPROM: Reading settings from eeprom, Size: %d\n"), eeprom_used_size);
  EEPROM.end();
}

time_t last_eeprom_write = 0;
bool eeprom_noncritical_cache_dirty = false;
#define EEPROM_CACHE_TIME_CONDITIONAL (15 * 60) // max interval of non-critical writes to eeprom

/**
 * @brief Writes settings to eeprom
 *
 */
void writeToEEPROM()
{
  // is directly called for critical update
  time_t now_infunc;
  time(&now_infunc);
  last_eeprom_write = now_infunc;
  eeprom_noncritical_cache_dirty = false;

  int eeprom_used_size = sizeof(s);
  EEPROM.begin(eeprom_used_size);
  EEPROM.put(eepromaddr, s); // write data to array in ram
  bool commit_ok = EEPROM.commit();
  Serial.printf(PSTR("writeToEEPROM: Writing %d bytes to eeprom. Result %s\n"), eeprom_used_size, commit_ok ? "OK" : "FAILED");
  EEPROM.end();
}
/**
 * @brief Delayed write to eeprom
 *
 */
void flush_noncritical_eeprom_cache()
{
  time_t now_infunc;
  time(&now_infunc);
  if (((last_eeprom_write + EEPROM_CACHE_TIME_CONDITIONAL) < now_infunc) && eeprom_noncritical_cache_dirty)
    writeToEEPROM();
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
  // http.useHTTP10(true); // for json input, maybe also for disable chunked response

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
  else
  {
    snprintf(msgbuff, sizeof(msgbuff), "Found %d sensors", sensor_count);
    log_msg(MSG_TYPE_INFO, msgbuff, true);
  }

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

#define RESTART_AFTER_LAST_OK_METER_READ 18000 //!< If all energy meter readings are failed within this period, restart the device

#ifdef METER_HAN_ENABLED

#endif

unsigned energym_last_period = 0;
long energym_period_first_read_ts = 0;
long energym_meter_read_ts = 0; //!< Last time meter was read successfully
long energym_read_count = 0;
double energym_e_in_prev = 0;
double energym_e_out_prev = 0;
double energym_e_in = 0;
double energym_e_out = 0;
float energym_power_in = 0;
int energym_period_read_count = 0;

/**
 * @brief Get earlier read energy values
 *
 * @param netEnergyInPeriod
 * @param netPowerInPeriod
 */
void get_values_energym(float &netEnergyInPeriod, float &netPowerInPeriod)
{
  if (energym_read_count < 2)
  {
    netPowerInPeriod = energym_power_in; // short/no history, using momentary value
    netEnergyInPeriod = 0;
    //  Serial.printf("get_values_energym  energym_read_count: %ld, netPowerInPeriod: %f, netEnergyInPeriod: %f\n", energym_read_count, netPowerInPeriod, netEnergyInPeriod);
  }
  else
  {
    netEnergyInPeriod = (energym_e_in - energym_e_out - energym_e_in_prev + energym_e_out_prev);
#ifdef DEBUG_MODE
    Serial.printf("get_values_energym netEnergyInPeriod (%.1f) = (energym_e_in (%.1f) - energym_e_out (%.1f) - energym_e_in_prev (%.1f) + energym_e_out_prev (%.1f))\n", netEnergyInPeriod, energym_e_in, energym_e_out, energym_e_in_prev, energym_e_out_prev);
#endif
    if ((energym_meter_read_ts - energym_period_first_read_ts) != 0)
    {
      netPowerInPeriod = round(netEnergyInPeriod * 3600.0 / ((energym_meter_read_ts - energym_period_first_read_ts)));
#ifdef DEBUG_MODE
      Serial.printf("get_values_energym netPowerInPeriod (%.1f) = round(netEnergyInPeriod (%.1f) * 3600.0 / (( energym_meter_read_ts (%ld) - energym_period_first_read_ts (%ld) )))  --- time %ld\n", netPowerInPeriod, netEnergyInPeriod, energym_meter_read_ts, energym_period_first_read_ts, (energym_meter_read_ts - energym_period_first_read_ts));
#endif
    }
    else // Do we ever get here with counter check
    {
      netPowerInPeriod = 0;
    }
  }
}

#ifdef METER_HAN_ENABLED
/**
 *
 */
bool read_meter_han()
{
  char url[90];
  snprintf(url, sizeof(url), "http://%s/api/v1/telegram", s.energy_meter_ip.toString().c_str());
  Serial.println(url);
  String telegram = httpGETRequest(url, "");

  time_t now_in_func;
  time(&now_in_func);

  int s_idx = 0, e_idx;
  int vs_idx, ve_idx, va_idx;
  int len = telegram.length();
  Serial.printf("Length of telegram: %i\n", len);
  int i = 0;
  energym_read_count++; // global
  unsigned now_period = int(now_in_func / (NETTING_PERIOD_SEC));
  energym_meter_read_ts = now_in_func;

  float netEnergyInPeriod;
  float netPowerInPeriod;
  if (energym_last_period != now_period)
  {
    energym_period_read_count = 0;
  }

  if ((energym_last_period > 0) && energym_period_read_count == 1)
  { // new period
    ESP_LOGI(TAG, "****HAN - new period counter reset");
    // from this call
    energym_e_in_prev = energym_e_in;
    energym_e_out_prev = energym_e_out;
  }

  // read
  double power_tot = 0;
  energym_e_in = 0;
  energym_e_out = 0;

  double power_in = 0;
  double power_out = 0;

  while (s_idx <= len)
  {
    e_idx = telegram.indexOf('\n', s_idx);
    if (e_idx == -1)
    {
      Serial.printf("Cannot find newline, searching from %d\n", s_idx);
      break;
    }

    vs_idx = telegram.indexOf('(', s_idx);
    if ((vs_idx == -1) || (vs_idx > e_idx))
    { // no '(' on the line
      s_idx = e_idx + 1;
      continue;
    }
    ve_idx = telegram.indexOf(')', vs_idx);
    if ((ve_idx == -1) || (ve_idx > e_idx))
    { // no ')' on the line
      s_idx = e_idx + 1;
      continue;
    }

    va_idx = telegram.indexOf('*', vs_idx);
    if ((va_idx == -1) || (va_idx > e_idx))
    { // no '*' on the line, no unit
      s_idx = e_idx + 1;
      continue;
    }

    Serial.print(telegram.substring(s_idx, vs_idx)); // OBIS code
    Serial.print("|value:");
    Serial.print(telegram.substring(vs_idx + 1, va_idx)); // Value
    Serial.print("|unit:");
    Serial.print(telegram.substring(va_idx + 1, ve_idx)); // unit
    Serial.println("|");

    Serial.printf("s_idx: %i, vs_idx: %i, [%s]\n", s_idx, vs_idx, telegram.substring(s_idx, vs_idx).c_str());
    if (telegram.substring(s_idx, vs_idx).startsWith("1-0:1.7.0"))
    {
      power_in = telegram.substring(vs_idx + 1, va_idx).toDouble();
      if (telegram.substring(va_idx + 1, ve_idx).startsWith("kW"))
        power_in = power_in * 1000;
    }

    if (telegram.substring(s_idx, vs_idx).startsWith("1-0:2.7.0"))
    {
      power_out = telegram.substring(vs_idx + 1, va_idx).toDouble();
      if (telegram.substring(va_idx + 1, ve_idx).startsWith("kW"))
        power_out = power_out * 1000;
    }
    power_tot = power_in - power_out;
    if (telegram.substring(s_idx, vs_idx).startsWith("1-0:1.8.0"))
    {
      energym_e_in = telegram.substring(vs_idx + 1, va_idx).toDouble();
      if (telegram.substring(va_idx + 1, ve_idx).startsWith("kWh"))
        energym_e_in = energym_e_in * 1000;
    }
    if (telegram.substring(s_idx, vs_idx).startsWith("1-0:2.8.0"))
    {
      energym_e_out = telegram.substring(vs_idx + 1, va_idx).toDouble();
      if (telegram.substring(va_idx + 1, ve_idx).startsWith("kWh"))
        energym_e_out = energym_e_out * 1000;
    }

    s_idx = e_idx + 1;
    i++;
  }

  Serial.printf("HAN readings: power_in %f, power_out %f, energym_e_in %f, energym_e_out %f", power_in, power_out, energym_e_in, energym_e_out);

  energym_power_in = power_tot;
  energym_period_read_count++;
  // read done

  // first query since boot
  if (energym_last_period == 0)
  {
    ESP_LOGI(TAG, "HAN - first query since startup");
    energym_last_period = now_period;
    energym_period_first_read_ts = energym_meter_read_ts;
    energym_e_in_prev = energym_e_in;
    energym_e_out_prev = energym_e_out;
  }

  get_values_energym(netEnergyInPeriod, netPowerInPeriod);
  vars.set(VARIABLE_EXTRA_PRODUCTION, (long)(netEnergyInPeriod < 0) ? 1L : 0L);
  vars.set(VARIABLE_SELLING_POWER, (long)round(-netPowerInPeriod));
  vars.set(VARIABLE_SELLING_ENERGY, (long)round(-netEnergyInPeriod));
  vars.set(VARIABLE_SELLING_POWER_NOW, (long)round(-energym_power_in)); // momentary

  // history
  // net_imports[MAX_HISTORY_PERIODS - 1] = -vars.get_f(VARIABLE_SELLING_POWER);

  if (energym_last_period != now_period)
  {
    energym_period_first_read_ts = energym_meter_read_ts;
    energym_last_period = now_period;
  }

  return true;
}

#endif

#ifdef METER_SHELLY3EM_ENABLED

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

  if (s.energy_meter_ip == IP_UNDEFINED)
    return false;

  DynamicJsonDocument doc(2048);

  char url[90];
  char auth[35];
  if (strlen(s.energy_meter_password) > 0)
    snprintf(auth, sizeof(auth), "admin:%s@", s.energy_meter_password);
  else
    auth[0] = 0;

  Serial.println(s.energy_meter_ip.toString());

  snprintf(url, sizeof(url), "http://%s%s:%d/status", auth, s.energy_meter_ip.toString().c_str(), s.energy_meter_port);
  Serial.println(url);
  DeserializationError error = deserializeJson(doc, httpGETRequest(url, ""));

  if (error)
  {
    Serial.print(F("Shelly meter deserializeJson() failed: "));
    Serial.println(error.f_str());
    return false;
  }
  energym_read_count++;

  unsigned now_period = int(now_in_func / (NETTING_PERIOD_SEC));
  energym_meter_read_ts = now_in_func;

  float netEnergyInPeriod;
  float netPowerInPeriod;
  // if (energym_last_period != now_period && (energym_last_period > 0) && energym_period_read_count == 1)
  if (energym_last_period != now_period)
  {
    energym_period_read_count = 0;
  }

  if ((energym_last_period > 0) && energym_period_read_count == 1)
  { // new period
    Serial.println(F("****Shelly - new period counter reset"));
    // energym_last_period = now_period;
    // from this call
    energym_e_in_prev = energym_e_in;
    energym_e_out_prev = energym_e_out;
  }

  // read
  float power_tot = 0;
  int idx = 0;
  float power[3];
  energym_e_in = 0;
  energym_e_out = 0;
  for (JsonObject emeter : doc["emeters"].as<JsonArray>())
  {
    power[idx] = (float)emeter["power"];
    power_tot += power[idx];
    // float current = emeter["current"];
    //  is_valid = emeter["is_valid"];
    if (emeter["is_valid"])
    {
      energym_e_in += (float)emeter["total"];
      energym_e_out += (float)emeter["total_returned"];
    }
    idx++;
  }
  energym_power_in = power_tot;
  energym_period_read_count++;
  // read done

  // first query since boot
  if (energym_last_period == 0)
  {
    Serial.println(F("Shelly - first query since startup"));
    energym_last_period = now_period;
    energym_period_first_read_ts = energym_meter_read_ts;
    energym_e_in_prev = energym_e_in;
    energym_e_out_prev = energym_e_out;
  }
  // if ((energym_meter_read_ts - energym_period_first_read_ts) != 0)

  get_values_energym(netEnergyInPeriod, netPowerInPeriod);
  vars.set(VARIABLE_EXTRA_PRODUCTION, (long)(netEnergyInPeriod < 0) ? 1L : 0L);
  vars.set(VARIABLE_SELLING_POWER, (long)round(-netPowerInPeriod));
  vars.set(VARIABLE_SELLING_ENERGY, (long)round(-netEnergyInPeriod));
  vars.set(VARIABLE_SELLING_POWER_NOW, (long)round(-energym_power_in)); // momentary

  if (energym_last_period != now_period)
  {
    energym_period_first_read_ts = energym_meter_read_ts;
    energym_last_period = now_period;
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
  Serial.println("read_inverter_fronius_data");
  if (s.production_meter_ip == IP_UNDEFINED)
    return false;

  time_t now_in_func;
  time(&now_in_func);
  StaticJsonDocument<64> filter;

  JsonObject filter_Body_Data = filter["Body"].createNestedObject("Data");
  filter_Body_Data["DAY_ENERGY"] = true; // instead of TOTAL_ENERGY
  filter_Body_Data["PAC"] = true;

  StaticJsonDocument<256> doc;
  char inverter_url[190];
  snprintf(inverter_url, sizeof(inverter_url), "http://%s/solar_api/v1/GetInverterRealtimeData.cgi?scope=Device&DeviceId=1&DataCollection=CumulationInverterData", s.production_meter_ip.toString().c_str());
  Serial.println(inverter_url);

  DeserializationError error = deserializeJson(doc, httpGETRequest(inverter_url, ""), DeserializationOption::Filter(filter));

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
  // uint16_t ip_octets[MAX_SPLIT_ARRAY_SIZE];
  // char host_ip[16];
  // strncpy(host_ip, s.energy_meter_host, sizeof(host_ip)); // must be locally allocated
  // str_to_uint_array(host_ip, ip_octets, ".");

  // IPAddress remote(ip_octets[0], ip_octets[1], ip_octets[2], ip_octets[3]);

  uint16_t ip_port = s.production_meter_port;
  uint8_t modbusip_unit = s.production_meter_id;

  Serial.printf("ModBus host: [%s], ip_port: [%d], unit_id: [%d] \n", s.production_meter_ip.toString().c_str(), ip_port, modbusip_unit);

  mb.task();
  if (!mb.isConnected(s.production_meter_ip))
  {
    Serial.print(F("Connecting Modbus TCP..."));
    bool cresult = mb.connect(s.production_meter_ip, ip_port);
    Serial.println(cresult);
    mb.task();
  }

  if (mb.isConnected(s.production_meter_ip))
  { // Check if connection to Modbus slave is established
    mb.task();
    Serial.println(F("Connection ok. Reading values from Modbus registries."));
    total_energy = get_mbus_value(s.production_meter_ip, SMA_TOTALENERGY_OFFSET, 2, modbusip_unit);
    mb.task();
    Serial.print(F(" total energy Wh:"));
    Serial.print(total_energy);

    current_power = get_mbus_value(s.production_meter_ip, SMA_POWER_OFFSET, 2, modbusip_unit);
    Serial.print(F(", current power W:"));
    Serial.println(current_power);

    mb.disconnect(s.production_meter_ip); // disconect in the end
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
  if ((s.production_meter_type == PRODUCTIONM_FRONIUS_SOLAR))
  {
    read_ok = read_inverter_fronius_data(total_energy, current_power);

    if (((long)inverter_total_period_init > total_energy) && read_ok)
    {
      inverter_total_period_init = 0; // day have changed probably, reset counter, we get day totals from Fronius
      inverter_total_period_init_ok = true;
    }
  }
  else if (s.production_meter_type == PRODUCTIONM_SMA_MODBUS_TCP)
  {
    read_ok = read_inverter_sma_data(total_energy, current_power);
  }

  if (read_ok)
  {
    time(&productionm_read_last);
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
  if ((s.production_meter_type == PRODUCTIONM_FRONIUS_SOLAR) || (s.production_meter_type == PRODUCTIONM_SMA_MODBUS_TCP))
  {
    // if (s.energy_meter_type == ENERGYM_NONE) // estimate using baseload
    //   vars.set(VARIABLE_EXTRA_PRODUCTION, (long)(power_produced_period_avg > (s.baseload + WATT_EPSILON)) ? 1L : 0L);

    vars.set(VARIABLE_PRODUCTION_POWER, (long)(power_produced_period_avg));
    vars.set(VARIABLE_PRODUCTION_ENERGY, (long)(energy_produced_period));
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
  localtime_r(&current_period_start, &tm_struct_g);
  Serial.printf(PSTR("update_price_variables, current period %02d:%02d \n"), tm_struct_g.tm_hour, tm_struct_g.tm_min);
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

  yield();
  update_variable_from_json(variable_list, "pr9", VARIABLE_PRICERANK_9);
  update_variable_from_json(variable_list, "pr24", VARIABLE_PRICERANK_24);

  update_variable_from_json(variable_list, "prf24", VARIABLE_PRICERANK_FIXED_24);
  update_variable_from_json(variable_list, "prrf24", VARIABLE_PRICERATIO_FIXED_24);
  yield();

  update_variable_from_json(variable_list, "prf8", VARIABLE_PRICERANK_FIXED_8);
  update_variable_from_json(variable_list, "prf8bid", VARIABLE_PRICERANK_FIXED_8_BLOCKID);

  update_variable_from_json(variable_list, "pa9", VARIABLE_PRICEAVG_9);
  update_variable_from_json(variable_list, "pd9", VARIABLE_PRICEDIFF_9);
  update_variable_from_json(variable_list, "prr9", VARIABLE_PRICERATIO_9);
  yield();

  update_variable_from_json(variable_list, "pa24", VARIABLE_PRICEAVG_24);
  update_variable_from_json(variable_list, "pd24", VARIABLE_PRICEDIFF_24);
  update_variable_from_json(variable_list, "prr24", VARIABLE_PRICERATIO_24);
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

  char query_data_cha[70];
  snprintf(query_data_cha, sizeof(query_data_cha), "action=getChartData&loc=%s", s.forecast_loc);

  WiFiClient wifi_client;

  HTTPClient client_http;
  client_http.setReuse(false);
  client_http.useHTTP10(true); // for json input, not chunked
  // Your Domain name with URL path or IP address with path

  // reset variables
  vars.set(VARIABLE_PVFORECAST_SUM24, (long)VARIABLE_LONG_UNKNOWN);
  vars.set(VARIABLE_PVFORECAST_VALUE24, (long)VARIABLE_LONG_UNKNOWN);
  vars.set(VARIABLE_PVFORECAST_AVGPRICE24, (long)VARIABLE_LONG_UNKNOWN);

  strncpy(fcst_url, fcst_url_base, sizeof(fcst_url));
  client_http.begin(wifi_client, fcst_url);
  Serial.printf("Solar forecast url: %s\n", fcst_url);

  // Specify content-type header
  client_http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  client_http.setUserAgent("ArskaESP");

  // Send HTTP POST request
  client_http.POST(query_data_cha);

  DeserializationError error = deserializeJson(doc, client_http.getStream());
  if (error)
  {
    log_msg(MSG_TYPE_WARN, PSTR("Failed to read energy forecast data"));
    Serial.println(error.c_str());
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
  yield();
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
  yield();

  JsonArray pv_fcst_a = doc.createNestedArray("pv_fcst");

  for (int i = 0; i < PV_FORECAST_HOURS; i++)
  {
    pv_fcst_a.add(pv_fcst[i]);
  }
  vars.set(VARIABLE_PVFORECAST_SUM24, sum_pv_fcst);

  // Free resources
  client_http.end();
  return true;
}

/**
 * @brief Get price rank (1 is best price etc.) of given period within given period window
 * @details  Get entries from now to requested duration in the future. \n
If not enough future periods exist, include periods from history to get full window size.
 * @param window_start_incl_idx
 * @param window_duration_hours
 * @param time_price_idx
 * @param prices
 * @return int
 */
int get_period_price_rank_in_window(int window_start_incl_suggested_idx, int window_duration_hours, int time_price_idx, long prices[], long *window_price_avg, long *price_differs_avg, long *price_ratio_avg) //[MAX_PRICE_PERIODS]
{
  int window_start_incl_idx = min(MAX_PRICE_PERIODS, (window_start_incl_suggested_idx + window_duration_hours)) - window_duration_hours;
  int window_end_excl_idx = window_start_incl_idx + window_duration_hours;

  long window_price_sum = 0;

  int rank = 1;
  for (int price_idx = window_start_incl_idx; price_idx < window_end_excl_idx; price_idx++)
  {
    window_price_sum += prices[price_idx];
    if (prices[price_idx] < prices[time_price_idx])
    {
      rank++;
    }
  }
  *window_price_avg = window_price_sum / window_duration_hours;
  *price_differs_avg = prices[time_price_idx] - *window_price_avg;
  if (abs(window_price_sum) > 0)
  {
    *price_ratio_avg = window_duration_hours * (prices[time_price_idx] * 1000) / window_price_sum;
  }
  else
    *price_ratio_avg = VARIABLE_LONG_UNKNOWN;

  // Serial.printf("price %ld, price rank: %d in [%d - %d[  --> rank: %d, price ratio %ld, window_price_avg %ld, price_differs_avg %ld \n", prices[time_price_idx], time_price_idx, window_start_incl_idx, window_end_excl_idx, rank, *price_ratio_avg, *window_price_avg, *price_differs_avg);
  return rank;
}

long round_divide(long lval, long divider)
{
  long add_in_round = lval < 0 ? -divider / 2 : divider / 2;
  return (lval + add_in_round) / divider;
}

long get_price_for_segment(int start_idx_incl, int end_idx_incl)
{
  int price_count = 0;
  long price_sum = 0;

  for (int cur_idx = start_idx_incl; cur_idx <= end_idx_incl; cur_idx++)
  {
    price_sum += prices[cur_idx];
    price_count++;
  }
  long segment_price_avg = (price_sum / price_count); // / 1000;
  return segment_price_avg;
}

bool is_in_cheapest_segment(int start_idx_incl, int end_idx_incl, int time_idx, int segment_size)
{
  if ((start_idx_incl < 0) || end_idx_incl >= MAX_PRICE_PERIODS)
    return false;
  // long segment_price_this = get_price_for_segment(time_idx, time_idx+segment_size - 1);
  long segment_price;
  long segment_price_cheapest = LONG_MAX;
  int cheapest_idx = 0;
  for (int price_idx = start_idx_incl; price_idx <= (end_idx_incl + 1 - segment_size); price_idx++)
  {
    segment_price = get_price_for_segment(price_idx, price_idx + segment_size - 1);
    if (segment_price < segment_price_cheapest)
    {
      segment_price_cheapest = segment_price;
      cheapest_idx = price_idx;
    }
  }
  // Serial.printf("segment_price_cheapest: %ld, cheapest_idx: %d\n", segment_price_cheapest, cheapest_idx);
  //  is time_idx within segment (of segment size) starting from cheapest_idx
  if ((time_idx >= cheapest_idx) && (time_idx < cheapest_idx + segment_size))
  {
    Serial.printf("Segment starting with time_idx %d is within %d h segment in the block\n", segment_size, time_idx);
    return true;
  }

  return false;
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
  long price_ratio_avg;
  int rank;
  int window_start_incl_idx;

  // experimental
  // long price_2hseg_min[MAX_PRICE_PERIODS];
  // for (int i = 0; i < MAX_PRICE_PERIODS; i++)
  //  price_2hseg_min[i] = LONG_MAX; // init defaults

  for (time_t time = record_start + time_idx_now * PRICE_PERIOD_SEC; time < record_end_excl; time += PRICE_PERIOD_SEC)
  {
    delay(5);

    snprintf(var_code, sizeof(var_code), "%ld", time);
    JsonObject json_obj = doc.createNestedObject(var_code);

   // float energyPriceSpot = prices[time_idx] / 100;
    json_obj["p"] = (prices[time_idx] + 50) / 100;

    localtime_r(&time, &tm_struct_g);

    // Serial.printf("time: %ld, time_idx: %d , %04d-%02d-%02d %02d:00, ", time, time_idx, tm_struct_g.tm_year + 1900, tm_struct_g.tm_mon + 1, tm_struct_g.tm_mday, tm_struct_g.tm_hour);
    // Serial.printf("price: %f \n", energyPriceSpot);

    int price_block_count = (int)(sizeof(price_variable_blocks) / sizeof(*price_variable_blocks));
    for (int block_idx = 0; block_idx < price_block_count; block_idx++)
    {
      window_price_avg = 0;

      price_differs_avg = 0; // should not needed
      rank = get_period_price_rank_in_window(time_idx, price_variable_blocks[block_idx], time_idx, prices, &window_price_avg, &price_differs_avg, &price_ratio_avg);
      if (rank > 0)
      {
        snprintf(var_code, sizeof(var_code), "pr%d", price_variable_blocks[block_idx]);
        json_obj[var_code] = rank;
      }
      snprintf(var_code, sizeof(var_code), "pa%d", price_variable_blocks[block_idx]);
      json_obj[var_code] = round_divide(window_price_avg, 100);

      snprintf(var_code, sizeof(var_code), "pd%d", price_variable_blocks[block_idx]);
      json_obj[var_code] = round_divide(price_differs_avg, 100);

      // price ratio
      snprintf(var_code, sizeof(var_code), "prr%d", price_variable_blocks[block_idx]);
      json_obj[var_code] = price_ratio_avg;
    }
    // fixed 24H, prf24 VARIABLE_PRICERANK_FIXED_24
    window_start_incl_idx = time_idx - tm_struct_g.tm_hour; // first hour of the day/nychthemeron
    // Serial.printf("%d %d %d \n",time_idx-tm_struct_g.tm_hour,time_idx,tm_struct_g.tm_hour);

    rank = get_period_price_rank_in_window(window_start_incl_idx, 24, time_idx, prices, &window_price_avg, &price_differs_avg, &price_ratio_avg);
    if (rank > 0)
    {
      json_obj["prf24"] = rank;
    }
    json_obj["prrf24"] = price_ratio_avg;

    // experimental rank within fixed 8 h block, e.g. 23-07,07-15, 15-23
    int first_block_start_hour = 23;
    int block_size = 8;
    int nbr_of_blocks = 24 / block_size;

    int block_idx = (int)((24 + tm_struct_g.tm_hour - first_block_start_hour) / block_size) % nbr_of_blocks;
    int block_start_before_this_idx = (24 + tm_struct_g.tm_hour - first_block_start_hour) % block_size;
    window_start_incl_idx = time_idx - block_start_before_this_idx;
    rank = get_period_price_rank_in_window(window_start_incl_idx, block_size, time_idx, prices, &window_price_avg, &price_differs_avg, &price_ratio_avg);
    if (rank > 0)
    {
      json_obj["prf8"] = rank;
      json_obj["prf8bid"] = block_idx + 1; // for users 1-indexed
    }
    Serial.printf("%d h fixed , block_start_before_this_idx: %d, first block starting %d, block_idx: %d , rank %d\n", block_size, block_start_before_this_idx, first_block_start_hour, block_idx, rank);

    // check if this is full block, that we have all hours of the the block
    if (is_in_cheapest_segment(window_start_incl_idx, window_start_incl_idx + block_size - 1, time_idx, 2))
      json_obj["ps2f8"] = true;
    if (is_in_cheapest_segment(window_start_incl_idx, window_start_incl_idx + block_size - 1, time_idx, 3))
      json_obj["ps3f8"] = true;
    if (is_in_cheapest_segment(window_start_incl_idx, window_start_incl_idx + block_size - 1, time_idx, 4))
      json_obj["ps4f8"] = true;

    /*
    if (tm_struct_g.tm_hour < 6)
      window_start_incl_idx = time_idx - tm_struct_g.tm_hour;
    else
      window_start_incl_idx = time_idx - tm_struct_g.tm_hour + ((int)(tm_struct_g.tm_hour/6))*6;
    rank = get_period_price_rank_in_window(window_start_incl_idx, 6, time_idx, prices, &window_price_avg, &price_differs_avg, &price_ratio_avg);
    Serial.printf("6 h fixed rank: %d\n",rank);
    if (rank > 0)
    {
      json_obj["prf6"] = rank;
    }
    */
    //
    time_idx++;
  }
  // Serial.println("calculate_price_ranks finished");

  return;
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
    Serial.println(F("Price cache file was not expired, returning"));
    return true;
  }
  if (strlen(s.entsoe_api_key) < 36 || strlen(s.entsoe_area_code) < 5)
  {
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

  // Simulated start times for testing
  // #pragma message("Simulated start times for testing")
  /*if (WiFi.macAddress().equals("4C:11:AE:74:68:2C")) {
  start_ts = 1663239600 - (3600 * 18)+SECONDS_IN_DAY*-15 ;
  end_ts = start_ts + SECONDS_IN_DAY * 3;
  //log_msg(MSG_TYPE_WARN, PSTR("Simulated price interval"));
  }
*/

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
  if (WiFi.macAddress().equals("4C:11:AE:74:68:2C")) { //Test
     log_msg(MSG_TYPE_ERROR, PSTR("Cannot connect to Entso-E server. Simulated error."));
    return false;
  }
  */

  String ca_cert = LittleFS.open(entsoe_ca_filename, "r").readString();
  client_https.setCACert(ca_cert.c_str());

  client_https.setTimeout(15); // 15 Seconds
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
  char url[220];
  snprintf(url, sizeof(url), "%s&securityToken=%s&In_Domain=%s&Out_Domain=%s&periodStart=%s&periodEnd=%s", url_base, s.entsoe_api_key, s.entsoe_area_code, s.entsoe_area_code, date_str_start, date_str_end);
  Serial.print("requesting URL: ");

  Serial.println(url);

  //
  // #pragma message("EXPERIMENTAL http 1.0 , was 1.1")
  client_https.print(String("GET ") + url + " HTTP/1.1\r\n" +
                     "Host: " + host_prices + "\r\n" +
                     "User-Agent: ArskaNodeESP\r\n" +
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
    Serial.println(lineh);
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
        if (is_chunksize_line(line)) // skip error status "garbage" line, probably chuck size to read
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
        if (is_chunksize_line(line2)) // skip error status "garbage" line
          continue;
      }
      else
        line_incomplete = false; // ended normally

      line2.trim(); // remove cr
      line = line + line2;
      Serial.print("Combined line:");
      Serial.println(line);
    }

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
      time_t doc_expires = record_end_excl - (11 * 3600); // prices for next day should come after 12hUTC, so no need to query before that
      // time_t doc_expires = min((record_end_excl - (11 * 3600)), (now_infunc + (18 * 3600))); // expire in 18 hours or 11 hour before price data end, which comes first
      doc["expires"] = doc_expires;
      Serial.printf("No zero prices. Document expires at %ld\n", doc_expires);
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
    Serial.printf("ENTSO-E price data missing future prices, end_reached %d, price_rows %d \n", end_reached, price_rows);
    log_msg(MSG_TYPE_WARN, PSTR("ENTSO-E price data missing future prices."));
  }

  Serial.println(read_ok ? F("Price query OK") : F("Price query failed"));

  if (!read_ok)
    log_msg(MSG_TYPE_ERROR, PSTR("Failed to get price data from ENTSO-E."));

  return read_ok;
}

// TODO: no cache, check for eval versions
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
  for (uint16_t i = 0; (i < prices_array.size() && i < MAX_PRICE_PERIODS); i++)
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

// new force_up_from
bool is_force_up_valid(int channel_idx)
{
  time_t now_in_func;
  time(&now_in_func);
  bool is_valid = ((s.ch[channel_idx].force_up_from <= now_in_func) && (now_in_func < s.ch[channel_idx].force_up_until));
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
 * @brief Generate constant file from setting values for the html UI
 *
 * @param force_create
 * @return true
 * @return false
 */

void onWebApplicationGet(AsyncWebServerRequest *request)
{
  if (!request->authenticate(s.http_username, s.http_password))
  {
    return request->requestAuthentication();
  }

  // No up-to-date json cache file, let's create one
  DynamicJsonDocument doc(8192);
  doc["compile_date"] = compile_date;
  doc["HWID"] = HWID;

  doc["VERSION"] = VERSION;
  doc["VERSION_SHORT"] = VERSION_SHORT;
  doc["version_fs"] = version_fs;
  doc["RULE_STATEMENTS_MAX"] = RULE_STATEMENTS_MAX;
  doc["CHANNEL_COUNT"] = CHANNEL_COUNT;
  doc["CHANNEL_CONDITIONS_MAX"] = CHANNEL_CONDITIONS_MAX;

#ifdef INFLUX_REPORT_ENABLED
  doc["INFLUX_REPORT_ENABLED"] = true;
#else
  doc["INFLUX_REPORT_ENABLED"] = false;
#endif

#ifdef DEBUG_MODE
  doc["DEBUG_MODE"] = true;
#else
  doc["DEBUG_MODE"] = false;
#endif

  JsonArray json_opers = doc.createNestedArray("opers");
  for (int i = 0; i < OPER_COUNT; i++)
  {
    JsonArray json_oper = json_opers.createNestedArray();
    json_oper.add(opers[i].id);

    json_oper.add(opers[i].code);
    json_oper.add(opers[i].gt);
    json_oper.add(opers[i].eq);
    json_oper.add(opers[i].reverse);
    json_oper.add(opers[i].boolean_only);
    json_oper.add(opers[i].has_value);
  }

  int variable_count = vars.get_variable_count();
  variable_st variable;
  String output;
  JsonArray json_variables = doc.createNestedArray("variables");
  for (int variable_idx = 0; variable_idx < variable_count; variable_idx++)
  {
    JsonArray json_variable = json_variables.createNestedArray();
    vars.get_variable_by_idx(variable_idx, &variable);
    json_variable.add(variable.id);
    json_variable.add(variable.code);
    json_variable.add(variable.type);
    vars.get_variable_by_idx(variable_idx, &variable);
  }

  JsonArray json_channel_types = doc.createNestedArray("channel_types");
  for (int channel_type_idx = 0; channel_type_idx < CHANNEL_TYPE_COUNT; channel_type_idx++)
  {
    JsonObject json_channel_type = json_channel_types.createNestedObject();
    json_channel_type["id"] = (int)channel_types[channel_type_idx].id;
    json_channel_type["name"] = channel_types[channel_type_idx].name;
  }

  JsonArray json_hs_templates = doc.createNestedArray("hw_templates");
  for (int hw_template_idx = 0; hw_template_idx < HW_TEMPLATE_COUNT; hw_template_idx++)
  {
    JsonObject json_hs_template = json_hs_templates.createNestedObject();
    json_hs_template["id"] = (int)hw_templates[hw_template_idx].id;
    json_hs_template["name"] = hw_templates[hw_template_idx].name;
  }
  serializeJson(doc, output);

  request->send(200, "application/json", output);
  request->send(200, "application/json;charset=UTF-8", output);
}

/**
 * @brief Read grid or production info from energy meter/inverter
 *
 */
void read_energy_meter()
{
  bool read_ok;
  Serial.println("read_energy_meter");
  time_t now_in_func;
  // SHELLY
  if (s.energy_meter_type == ENERGYM_SHELLY3EM)
  {
#ifdef METER_SHELLY3EM_ENABLED
    read_ok = read_meter_shelly3em();
#endif
  }
  else if (s.energy_meter_type == ENERGYM_HAN_WIFI)
  {
#ifdef METER_HAN_ENABLED
    read_ok = read_meter_han();
#endif
  }
  // NO ENERGY METER DEFINED, function should not be called
  else
  {
    return;
  }
  time(&now_in_func);
  bool internet_connection_ok = false;
  if (read_ok)
    energym_read_last = now_in_func;
  else
  {
    if (ping_enabled)
    {
      internet_connection_ok = test_host(IPAddress(8, 8, 8, 8)); // Google DNS, TODO: set address to parameters
    }
    if (internet_connection_ok)
      log_msg(MSG_TYPE_FATAL, PSTR("Internet connection ok, but cannot read energy meter. Check the meter."));
    else if ((energym_read_last + RESTART_AFTER_LAST_OK_METER_READ < now_in_func) && (energym_read_last > 0))
    { // connected earlier, but now many unsuccesfull reads
      WiFi.disconnect();
      log_msg(MSG_TYPE_FATAL, PSTR("Restarting after failed energy meter connections."), true);
      delay(2000);
      ESP.restart();
    }
    else
      log_msg(MSG_TYPE_ERROR, PSTR("Failed to read energy meter. Check Wifi, internet connection and the meter."));
  }
}
//

/**
 * @brief Read  production info /inverter
 *
 */
void read_production_meter()
{
  bool read_ok;
  Serial.println("read_production_meter");
  time_t now_in_func;
  if (s.production_meter_type == PRODUCTIONM_FRONIUS_SOLAR or (s.production_meter_type == PRODUCTIONM_SMA_MODBUS_TCP))
  {
#if defined(INVERTER_FRONIUS_SOLARAPI_ENABLED) || defined(INVERTER_SMA_MODBUS_ENABLED)
    read_ok = read_inverter(period_changed);
#endif
  }
  else
  {
    return; // NO ENERGY METER DEFINED, function should not be called
  }
  time(&now_in_func);
  bool internet_connection_ok = false;
  if (read_ok)
    productionm_read_last = now_in_func;
  else
  {
    if (ping_enabled) // TODO: ping local gw
    {
      internet_connection_ok = test_host(IPAddress(8, 8, 8, 8)); // Google DNS, TODO: set address to parameters
    }
    if (internet_connection_ok)
      log_msg(MSG_TYPE_FATAL, PSTR("Internet connection ok, but cannot production meter/inverter. Check the meter."));
    else
      log_msg(MSG_TYPE_ERROR, PSTR("Failed to production energy meter. Check Wifi, internet connection and the meter."));
  }
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
/*
//WiP
void set_and_write_gpio(uint8_t gpio, uint8_t new_pin_value) {
   digitalWrite(gpio, new_pin_value);
   pinMode(gpio, OUTPUT);
}
*/

/**
 * @brief Test gpio and optionally set pin mode for gpio switches
 *
 * @param channel_idx
 * @param set_pinmode
 * @return true
 * @return false
 */

bool test_set_gpio_pinmode(int channel_idx, bool set_pinmode = true)
{
  if (s.ch[channel_idx].type == CH_TYPE_GPIO_FIXED || s.ch[channel_idx].type == CH_TYPE_GPIO_USER_DEF || s.ch[channel_idx].type == CH_TYPE_GPIO_USR_INVERSED)
  {
    uint8_t gpio = s.ch[channel_idx].relay_id;
    if ((gpio == 20) || (gpio == 24) || (gpio >= 28 && gpio <= 31) || (gpio > 39))
    {
      Serial.printf("Channel %d, invalid output gpio %d\n", channel_idx, (int)gpio);
      return false;
    }
    if (set_pinmode)
      pinMode(gpio, OUTPUT);
    return true;
  }
  return false;
}

/**
 * @brief Switch http get relays
 *
 * @param channel_idx
 * @param up
 * @return true
 * @return false
 */
bool switch_http_relay(int channel_idx, bool up)
{
  char error_msg[ERROR_MSG_LEN];
  char url_to_call[50];

  char response_key[20];
  bool switch_set_ok = false;
  IPAddress undefined_ip = IPAddress(0, 0, 0, 0);
  if (s.ch[channel_idx].relay_ip == undefined_ip)
  {
    snprintf(error_msg, ERROR_MSG_LEN, PSTR("Channel %d has undefined relay ip."), channel_idx + 1);
    log_msg(MSG_TYPE_WARN, error_msg, false);
    return false;
  }
  if (s.ch[channel_idx].type == CH_TYPE_SHELLY_1GEN)
  {
    snprintf(url_to_call, sizeof(url_to_call), "http://%s/relay/%d?turn=%s", s.ch[channel_idx].relay_ip.toString().c_str(), (int)s.ch[channel_idx].relay_unit_id, up ? "on" : "off");
    strcpy(response_key, "ison");
  }
  if (s.ch[channel_idx].type == CH_TYPE_SHELLY_2GEN)
  {
    snprintf(url_to_call, sizeof(url_to_call), "http://%s/rpc/Switch.Set?id=%d&on=%s", s.ch[channel_idx].relay_ip.toString().c_str(), (int)s.ch[channel_idx].relay_unit_id, up ? "true" : "false");
    strcpy(response_key, "was_on");
  }
  else if (s.ch[channel_idx].type == CH_TYPE_TASMOTA)
  {
    snprintf(url_to_call, sizeof(url_to_call), "http://%s/cm?cmnd=Power%d%%20%s", s.ch[channel_idx].relay_ip.toString().c_str(), (int)s.ch[channel_idx].relay_unit_id, up ? "On" : "Off");
    sprintf(response_key, "POWER%d", s.ch[channel_idx].relay_unit_id);
  }

  Serial.printf("url_to_call    :%s\n", url_to_call);

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, httpGETRequest(url_to_call, "", 5000)); // shorter connect timeout for a local switch
  if (error)
  {
    snprintf(error_msg, ERROR_MSG_LEN, PSTR("Cannot connect channel %d switch at %s "), channel_idx + 1, s.ch[channel_idx].relay_ip.toString().c_str());
    log_msg(MSG_TYPE_WARN, error_msg, false);
    Serial.println(error.f_str());
    return false;
  }
  else
  {
    Serial.println(F("Http relay switched."));
    if (doc.containsKey(response_key))
    {
      if (s.ch[channel_idx].type == CH_TYPE_SHELLY_1GEN && doc[response_key].is<bool>() && doc[response_key] == up)
        switch_set_ok = true;
      if (s.ch[channel_idx].type == CH_TYPE_SHELLY_2GEN && doc[response_key].is<bool>()) // we do not get new switch state, just check that response is ok
        switch_set_ok = true;
      else if (s.ch[channel_idx].type == CH_TYPE_TASMOTA && doc[response_key] == (up ? "ON" : "OFF"))
        switch_set_ok = true;
      if (switch_set_ok)
      {
        Serial.println("Switch set properly.");
      }
      else
      {
        snprintf(error_msg, ERROR_MSG_LEN, PSTR("Switch for channel %d switch at %s not timely set."), channel_idx + 1, s.ch[channel_idx].relay_ip.toString().c_str());
        log_msg(MSG_TYPE_WARN, error_msg, false);
      }
    }
    else
    {
      snprintf(error_msg, ERROR_MSG_LEN, PSTR("Switch for channel  %d switch at %s, invalid response."), channel_idx + 1, s.ch[channel_idx].relay_ip.toString().c_str());
      log_msg(MSG_TYPE_WARN, error_msg, false);
    }
    return true;
  }
}
//
/**
 * @brief Sets a channel relay up/down
 *
 * @param channel_idx
 * @param up
 * @return true
 * @return false
 */
bool apply_relay_state(int channel_idx, bool init_relay)
{
  time_t now_in_func;
  time(&now_in_func);

  relay_state_reapply_required[channel_idx] = false;

  if (s.ch[channel_idx].type == CH_TYPE_UNDEFINED)
    return false;

  bool up = s.ch[channel_idx].is_up;

  if (!init_relay && !up)
  { // channel goes normally down, record last time seen up and queue for delayd eeprom write
    s.ch[channel_idx].up_last = now_in_func;
    eeprom_noncritical_cache_dirty = true;
    Serial.printf("Channel %d seen up now at %ld \n", channel_idx, (long)s.ch[channel_idx].up_last);
  }
  Serial.printf("ch%d ->%s", channel_idx, up ? "HIGH  " : "LOW  ");

  ch_counters.set_state(channel_idx, up); // counters
  if ((s.ch[channel_idx].type == CH_TYPE_GPIO_FIXED) || (s.ch[channel_idx].type == CH_TYPE_GPIO_USER_DEF) || (s.ch[channel_idx].type == CH_TYPE_GPIO_USR_INVERSED))
  {
    if (test_set_gpio_pinmode(channel_idx, init_relay))
    {
      uint8_t pin_val;
      if ((s.ch[channel_idx].type == CH_TYPE_GPIO_USR_INVERSED))
        pin_val = (up ? LOW : HIGH);
      else
        pin_val = (up ? HIGH : LOW);
      digitalWrite(s.ch[channel_idx].relay_id, pin_val);
      return true;
    }
    else
      return false; // invalid gpio
  }
  // do not try to connect if there is no wifi stack initiated
  else if (wifi_connection_succeeded && (s.ch[channel_idx].type == CH_TYPE_SHELLY_1GEN || s.ch[channel_idx].type == CH_TYPE_SHELLY_2GEN || s.ch[channel_idx].type == CH_TYPE_TASMOTA))
    switch_http_relay(channel_idx, up);

  return false;
}

/**
 * @brief Check which channels can be raised /dropped
 *
 */
void update_channel_states()
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
    bool wait_minimum_uptime = (ch_counters.get_duration_in_this_state(channel_idx) < s.ch[channel_idx].uptime_minimum); // channel must stay up minimum time

    // debug / development:
    // Serial.printf("DEBUG: Channel %d has been %s %d secs, waiting minimum uptime %d - %s\n", channel_idx, s.ch[channel_idx].is_up ? "UP" : "DOWN", (int)ch_counters.get_duration_in_this_state(channel_idx), s.ch[channel_idx].uptime_minimum, wait_minimum_uptime ? "YES" : "NO");

    if (s.ch[channel_idx].force_up_until == -1)
    { // force down
      s.ch[channel_idx].force_up_until = 0;
      wait_minimum_uptime = false;
    }

    forced_up = (is_force_up_valid(channel_idx));

    if (s.ch[channel_idx].is_up && (wait_minimum_uptime || forced_up))
    {
      //  Not yet time to drop channel
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
      continue; // forced, not checking channel rules
    }

    // Now checking normal state based conditions
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
        if (statement->variable_id != -1) // statement defined
        {
          nof_valid_statements++;
          //   Serial.printf("update_channel_states statement.variable_id: %d\n", statement->variable_id);
          statement_true = vars.is_statement_true(statement);
          if (!statement_true)
          {
            one_or_more_failed = true;
            break;
          }
        }
      } // statement loop

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

  /* experimental
  // first test, move to channel loop when ready
  time_t since_last_up_secs = now_in_func - s.ch[0].up_last;
  if (since_last_up_secs<0 || since_last_up_secs> (SECONDS_IN_DAY*30)) {
    Serial.printf("Not valid since_last_up_secs %ld\nn",(long)since_last_up_secs);
    vars.set_NA(VARIABLE_BEEN_UP_AGO_HOURS_0);
  }
  else {
     vars.set(VARIABLE_BEEN_UP_AGO_HOURS_0, (long)(round(since_last_up_secs / 360))); //1 decimal
  }
   */
}

/**
 * @brief Set relays up and down
 * @details  MAX_CHANNELS_SWITCHED_AT_TIME defines how many channel can be switched at time \n
 *
 *
 */
void set_relays(bool grid_protection_delay_used)
{
  // check if random delay is used (optional way:we could also limit rise_count?)
  time_t now_infunc;
  time(&now_infunc);
  if (grid_protection_delay_used && (now_infunc < (current_period_start + grid_protection_delay_interval)))
  {
    Serial.printf(PSTR("Grid protection delay %ld of %d secs, %ld left\n"), grid_protection_delay_interval, (current_period_start + grid_protection_delay_interval - now));
    return;
  }

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
  { // first round drops, second round rises
    is_rise = (drop_rise == 1);
    oper_count = is_rise ? rise_count : drop_count;
    switchings_to_todo = min(oper_count, MAX_CHANNELS_SWITCHED_AT_TIME);
    for (int i = 0; i < switchings_to_todo; i++)
    {
      int ch_to_switch = get_channel_to_switch(is_rise, oper_count--); // return in random order if many
      Serial.printf("Switching ch %d  (%d) from %d .-> %d\n", ch_to_switch, s.ch[ch_to_switch].relay_id, s.ch[ch_to_switch].is_up, is_rise);
      s.ch[ch_to_switch].is_up = is_rise;
      //   s.ch[ch_to_switch].toggle_last = now;

      apply_relay_state(ch_to_switch, false);
    }
  }
}

#ifdef OTA_DOWNLOAD_ENABLED
#include <HTTPUpdate.h> //

// We keep the CA certificate in program code to avoid potential littlefs-hack
// Let’s Encrypt R3 (RSA 2048, O = Let's Encrypt, CN = R3) Signed by ISRG Root X1:  pem
const char *letsencrypt_ca_certificate =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIFFjCCAv6gAwIBAgIRAJErCErPDBinU/bWLiWnX1owDQYJKoZIhvcNAQELBQAw\n"
    "TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh\n"
    "cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMjAwOTA0MDAwMDAw\n"
    "WhcNMjUwOTE1MTYwMDAwWjAyMQswCQYDVQQGEwJVUzEWMBQGA1UEChMNTGV0J3Mg\n"
    "RW5jcnlwdDELMAkGA1UEAxMCUjMwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEK\n"
    "AoIBAQC7AhUozPaglNMPEuyNVZLD+ILxmaZ6QoinXSaqtSu5xUyxr45r+XXIo9cP\n"
    "R5QUVTVXjJ6oojkZ9YI8QqlObvU7wy7bjcCwXPNZOOftz2nwWgsbvsCUJCWH+jdx\n"
    "sxPnHKzhm+/b5DtFUkWWqcFTzjTIUu61ru2P3mBw4qVUq7ZtDpelQDRrK9O8Zutm\n"
    "NHz6a4uPVymZ+DAXXbpyb/uBxa3Shlg9F8fnCbvxK/eG3MHacV3URuPMrSXBiLxg\n"
    "Z3Vms/EY96Jc5lP/Ooi2R6X/ExjqmAl3P51T+c8B5fWmcBcUr2Ok/5mzk53cU6cG\n"
    "/kiFHaFpriV1uxPMUgP17VGhi9sVAgMBAAGjggEIMIIBBDAOBgNVHQ8BAf8EBAMC\n"
    "AYYwHQYDVR0lBBYwFAYIKwYBBQUHAwIGCCsGAQUFBwMBMBIGA1UdEwEB/wQIMAYB\n"
    "Af8CAQAwHQYDVR0OBBYEFBQusxe3WFbLrlAJQOYfr52LFMLGMB8GA1UdIwQYMBaA\n"
    "FHm0WeZ7tuXkAXOACIjIGlj26ZtuMDIGCCsGAQUFBwEBBCYwJDAiBggrBgEFBQcw\n"
    "AoYWaHR0cDovL3gxLmkubGVuY3Iub3JnLzAnBgNVHR8EIDAeMBygGqAYhhZodHRw\n"
    "Oi8veDEuYy5sZW5jci5vcmcvMCIGA1UdIAQbMBkwCAYGZ4EMAQIBMA0GCysGAQQB\n"
    "gt8TAQEBMA0GCSqGSIb3DQEBCwUAA4ICAQCFyk5HPqP3hUSFvNVneLKYY611TR6W\n"
    "PTNlclQtgaDqw+34IL9fzLdwALduO/ZelN7kIJ+m74uyA+eitRY8kc607TkC53wl\n"
    "ikfmZW4/RvTZ8M6UK+5UzhK8jCdLuMGYL6KvzXGRSgi3yLgjewQtCPkIVz6D2QQz\n"
    "CkcheAmCJ8MqyJu5zlzyZMjAvnnAT45tRAxekrsu94sQ4egdRCnbWSDtY7kh+BIm\n"
    "lJNXoB1lBMEKIq4QDUOXoRgffuDghje1WrG9ML+Hbisq/yFOGwXD9RiX8F6sw6W4\n"
    "avAuvDszue5L3sz85K+EC4Y/wFVDNvZo4TYXao6Z0f+lQKc0t8DQYzk1OXVu8rp2\n"
    "yJMC6alLbBfODALZvYH7n7do1AZls4I9d1P4jnkDrQoxB3UqQ9hVl3LEKQ73xF1O\n"
    "yK5GhDDX8oVfGKF5u+decIsH4YaTw7mP3GFxJSqv3+0lUFJoi5Lc5da149p90Ids\n"
    "hCExroL1+7mryIkXPeFM5TgO9r0rvZaBFOvV2z0gp35Z0+L4WPlbuEjN/lxPFin+\n"
    "HlUjr8gRsI3qfJOQFy/9rKIJR0Y/8Omwt/8oTWgy1mdeHmmjk7j1nYsvC9JSQ6Zv\n"
    "MldlTTKB3zhThV1+XWYp6rjd5JW1zbVWEkLNxE7GJThEUG3szgBVGP7pSWTUTsqX\n"
    "nLRbwHOoq7hHwg==\n"
    "-----END CERTIFICATE-----\n";

String update_releases = "{}"; // software releases for updates, cached in RAM
String update_release_selected = "";
/**
 * @brief Get firmware releases from download web server
 *
 * @return true
 * @return false
 */
bool get_releases()
{
  WiFiClientSecure client_https;
  client_https.setCACert(letsencrypt_ca_certificate);
  if (!client_https.connect(RELEASES_HOST, 443))
  {
    Serial.println(F("Cannot get release info from the firmware site."));
    return false;
  }

  client_https.print("GET " RELEASES_URL " HTTP/1.1\r\n"
                     "Host: " RELEASES_HOST "\r\n"
                     "User-Agent: ArskaNoderESP\r\n"
                     "Connection: close\r\n\r\n");

  while (client_https.connected())
  {
    String lineh = client_https.readStringUntil('\n');
    if (lineh == "\r")
    {
      break;
    }
  }

  if (client_https.connected())
  {
    update_releases = client_https.readString();
    Serial.println(update_releases);
  }

  client_https.stop();
  Serial.println("last");
  return true;
}
/**
 * @brief // Callback called after succesful flash(program) update
 *
 */
void flash_update_ended()
{
  Serial.println("CALLBACK:  HTTP update process finished");
  // set phase to enable fs version check after next boot
  s.ota_update_phase = OTA_PHASE_FWUPDATED_CHECKFS;
  writeToEEPROM();
  delay(1000);
  todo_in_loop_restart = true;
}

/**
 * @brief Download and update flash(program), restarts the device if successful
 *
 * @return t_httpUpdate_return
 */

t_httpUpdate_return update_program()
{
  Serial.printf(PSTR("Updating firmware to version %s\n"), update_release_selected.c_str());
  if (String(VERSION_BASE).equals(update_release_selected))
  {
    Serial.println(F("No need for firmware update."));
    return HTTP_UPDATE_NO_UPDATES;
  }
  WiFiClientSecure client_https;
  Serial.println("update_program");
  client_https.setCACert(letsencrypt_ca_certificate);
  client_https.setTimeout(15); // timeout for SSL fetch
  String file_to_download = "/arska-install/files/" + String(HWID) + "/" + update_release_selected + "/firmware.bin";
  Serial.println(file_to_download);

  httpUpdate.onEnd(flash_update_ended); // change update phase after succesful update but before restart
  t_httpUpdate_return result = httpUpdate.update(client_https, host_releases, 443, file_to_download);

  return result;
}
/**
 * @brief Callback called after succesfull filesystem update
 *
 */
void fs_update_ended()
{
  Serial.println("CALLBACK:  FS HTTP update process finished");
  // set phase to none/finished
  s.ota_update_phase = OTA_PHASE_NONE;
  writeToEEPROM();
}

/**
 * @brief Download and update littlefs filesystem
 *
 * @return t_httpUpdate_return
 */
t_httpUpdate_return update_fs()
{
  Serial.printf(PSTR("Updating filesystem to version %s\n"), VERSION_BASE);
  if (String(VERSION_BASE).equals(version_fs_base))
  {
    Serial.println(F("No need for filesystem update."));
    return HTTP_UPDATE_NO_UPDATES;
  }
  WiFiClient wifi_client;
  LittleFS.end();

  // TODO: update to new version without String when you can test it
  String file_to_download = "http://" + String(host_releases) + "/arska-install/files/" + String(HWID) + "/" + String(VERSION_BASE) + "/littlefs.bin";
  // char const *file_to_download = "http://" RELEASES_HOST "/arska-install/files/" HWID "/" VERSION_BASE "/littlefs.bin";
  Serial.println(file_to_download);
  // Serial.println(file_to_download_new);

  httpUpdate.onEnd(fs_update_ended); // change update phase after succesful update but before restart
  t_httpUpdate_return update_ok = httpUpdate.updateSpiffs(wifi_client, file_to_download.c_str(), "");
  if (update_ok == HTTP_UPDATE_FAILED)
  {
    Serial.println(F("LittleFS update failed!"));
    return update_ok;
  }
  if (update_ok == HTTP_UPDATE_OK)
  {
    Serial.println(F("Restarting after filesystem update."));
    log_msg(MSG_TYPE_FATAL, PSTR("Restarting after filesystem update."), true);

    ESP.restart(); // Restart to recreate cache files etc
  }
  return update_ok;
}
/**
 * @brief Update flash(program) or filesystem
 *
 * @param cmd
 */
void update_firmware_partition(bool cmd = U_FLASH)
{
  Serial.println("update_firmware_partition");
  t_httpUpdate_return update_result;
  if (cmd == U_FLASH)
  {
    update_result = update_program();
  }
  else
  {
    if (s.ota_update_phase == OTA_PHASE_FWUPDATED_CHECKFS)
    {
      update_result = update_fs();
    }
    else
    {
      Serial.println(F("Filesystem update requested but phase does not match."));
      Serial.println(s.ota_update_phase);
      return; // not correct update phase, should not normalyy end up here
    }
  }
  switch (update_result)
  {
  case HTTP_UPDATE_FAILED:
    Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
    break;

  case HTTP_UPDATE_NO_UPDATES:
    Serial.println("HTTP_UPDATE_NO_UPDATES");
    break;

  case HTTP_UPDATE_OK:
    Serial.println("HTTP_UPDATE_OK");
    break;
  }
}

// The other html pages come from littlefs filesystem, but on update we do not want to be dependant on that

// minimized from/data/update.html with https://www.textfixer.com/html/compress-html-compression.php
//  no double quotes, no onload etc with strinbg params, no double slash // comments
//const char update_page_html[] PROGMEM = "<html><head> <!-- Copyright Netgalleria Oy 2022, Olli Rinne, Unminimized version: /data/update.html --> <title>Arska update</title> <script src='https://cdnjs.cloudflare.com/ajax/libs/jquery/3.6.0/jquery.min.js'></script> <style> body { background-color: #fff; margin: 1.8em; font-size: 20px; font-family: lato, sans-serif; color: #485156; } .indent { margin-left: 2em; clear: left; } a { cursor: pointer; border-bottom: 3px dotted #485156; color: black; text-decoration: none; } </style></head><body> <script> window.addEventListener('load', (event) => {init_document();}); let hw = ''; let load_count = 0; let VERSION_SHORT = ''; function init_document() { if (window.jQuery) { document.getElementById('frm2').addEventListener('submit', (event) => {return confirm('Update software. This can take 5-10 minutes. Patience is a Virtue');}); $.ajax({ url: '/application', dataType: 'json', async: false, success: function (data, textStatus, jqXHR) { VERSION_SHORT = data.VERSION_SHORT; console.log('got ui-constants.json', VERSION_SHORT); $('#ver_sw').text(data.VERSION); $('#ver_fs').text(data.version_fs); }, error: function (jqXHR, textStatus, errorThrown) { console.log('Cannot get ui-constants.json', textStatus, jqXHR.status); } }); load_releases(); } else { document.getElementById('div_upd2').style.display = 'none'; console.log('Cannot load jQuery library'); } } function load_releases() { $.ajax({ url: '/releases', dataType: 'json', async: false, success: function (data, textStatus, jqXHR) { load_count++; console.log('got releases'); hw = data.hw; if (!(data.hasOwnProperty('releases'))) { /* retry to get releases*/ if (load_count < 5) setTimeout(function () { load_releases(); }, 5000); else document.getElementById('div_upd2').style.display = 'none'; } else { $.each(data.releases, function (i, release) { d = new Date(release[1] * 1000); $('#sel_releases').append($('<option>', { value: release[0], text: release[0] + ' ' + d.toLocaleDateString() })); }); $('#btn_update').prop('disabled', (!(data.hasOwnProperty('releases')))); $('#sel_releases').prop('disabled', (!(data.hasOwnProperty('releases')))); if (VERSION_SHORT) { version_base = VERSION_SHORT.substring(0, VERSION_SHORT.lastIndexOf('.')); console.log('version_base', version_base); $('#sel_releases option:contains(' + version_base + ')').append(' ***'); } $('#div_upd2').css('opacity', '1'); } }, error: function (jqXHR, textStatus, errorThrown) { console.log('Cannot get releases', textStatus, jqXHR.status); document.getElementById('div_upd2').style.display = 'none'; } }); }; function _(el) { return document.getElementById(el); } function upload() { var file = _('firmware').files[0]; var formdata = new FormData(); formdata.append('firmware', file); var ajax = new XMLHttpRequest(); ajax.upload.addEventListener('progress', progressHandler, false); ajax.addEventListener('load', completeHandler, false); ajax.addEventListener('error', errorHandler, false); ajax.addEventListener('abort', abortHandler, false); ajax.open('POST', 'doUpdate'); ajax.send(formdata); } function progressHandler(event) { _('loadedtotal').innerHTML = 'Uploaded ' + event.loaded + ' bytes of ' + event.total; var percent = (event.loaded / event.total) * 100; _('progressBar').value = Math.round(percent); _('status').innerHTML = Math.round(percent) + '&percnt; uploaded... please wait'; } function reloadAdmin() { window.location.href = '/#admin'; } function completeHandler(event) { _('status').innerHTML = event.target.responseText; _('progressBar').value = 0; setTimeout(reloadAdmin, 20000); } function errorHandler(event) { _('status').innerHTML = 'Upload Failed'; } function abortHandler(event) { _('status').innerHTML = 'Upload Aborted'; } </script> <h1>Firmware and filesystem update</h1> <div class='indent'> <p><a href='/settings?format=file'>Backup configuration</a> before starting upgrade.</p><br> </div> <div id='div_upd1'> <h2>Upload firmware files</h2> <div class='indent'> <p>Download files from <a href='https://iot.netgalleria.fi/arska-install/'>the installation page</a> or build from <a href='https://github.com/Netgalleria/arska-node'>the source code</a>. Update software (firmware.bin) first and filesystem (littlefs.bin) after that. After update check version data from the bottom of the page - update could be succeeded even if you get an error message. </p> <form id='frm1' method='post' enctype='multipart/form-data'> <input type='file' name='firmware' id='firmware' onchange='upload()'><br> <progress id='progressBar' value='0' max='100' style='width:250px;'></progress> <h2 id='status'></h2> <p id='loadedtotal'></p> </form> </div></div> <div id='div_upd2' style='opacity: 0.5;'> <h2>Automatic update</h2> <div class='indent'> <form method='post' id='frm2' action='/update'> <select name='release' id='sel_releases'> <option value=''>select release</option> </select> <input type='submit' id='btn_update' value='Update'> </form> </div></div> <br>Software: <span id='ver_sw'>*</span>, Filesystem: <span id='ver_fs'>*</span> - <a href='/#admin'>Return to Admin</a></body></html>";
//only manual update, automatic is integrated
const char update_page_html[] PROGMEM = "<html><head> <!-- Copyright Netgalleria Oy 2023, Olli Rinne, Unminimized version: /data/update.html --> <title>Arska update</title> <script src='https://cdnjs.cloudflare.com/ajax/libs/jquery/3.6.0/jquery.min.js'></script> <style> body { background-color: #fff; margin: 1.8em; font-size: 20px; font-family: lato, sans-serif; color: #485156; } .indent { margin-left: 2em; clear: left; } a { cursor: pointer; border-bottom: 3px dotted #485156; color: black; text-decoration: none; } </style></head><body> <script> window.addEventListener('load', (event) => { init_document(); }); let hw = ''; let load_count = 0; let VERSION_SHORT = ''; function init_document() { if (window.jQuery) { /* document.getElementById('frm2').addEventListener('submit', (event) => { return confirm('Update software, this can take several minutes.'); });*/ $.ajax({ url: '/application', dataType: 'json', async: false, success: function (data, textStatus, jqXHR) { VERSION_SHORT = data.VERSION_SHORT; $('#ver_sw').text(data.VERSION); $('#ver_fs').text(data.version_fs); }, error: function (jqXHR, textStatus, errorThrown) { console.log('Cannot get /application', textStatus, jqXHR.status); } }); } else { console.log('Cannot load jQuery library'); } } function _(el) { return document.getElementById(el); } function upload() { var file = _('firmware').files[0]; var formdata = new FormData(); formdata.append('firmware', file); var ajax = new XMLHttpRequest(); ajax.upload.addEventListener('progress', progressHandler, false); ajax.addEventListener('load', completeHandler, false); ajax.addEventListener('error', errorHandler, false); ajax.addEventListener('abort', abortHandler, false); ajax.open('POST', 'doUpdate'); ajax.send(formdata); } function progressHandler(event) { _('loadedtotal').innerHTML = 'Uploaded ' + event.loaded + ' bytes of ' + event.total; var percent = (event.loaded / event.total) * 100; _('progressBar').value = Math.round(percent); _('status').innerHTML = Math.round(percent) + '&percnt; uploaded... please wait'; } function reloadAdmin() { window.location.href = '/#admin'; } function completeHandler(event) { _('status').innerHTML = event.target.responseText; _('progressBar').value = 0; setTimeout(reloadAdmin, 20000); } function errorHandler(event) { _('status').innerHTML = 'Upload Failed'; } function abortHandler(event) { _('status').innerHTML = 'Upload Aborted'; } </script> <h1>Arska firmware and filesystem update</h1> <div class='indent'> <p><a href='/setting?format=file'>Backup configuration</a> before starting upgrade.</p><br> </div> <div id='div_upd1'> <h3>Upload firmware files</h3> <div class='indent'> <p>Download files from <a href='https://iot.netgalleria.fi/arska-install/'>the installation page</a> or build from <a href='https://github.com/Netgalleria/arska-node'>the source code</a>. Update software (firmware.bin) first and filesystem (littlefs.bin) after that. After update check version data from the bottom of the page - update could be succeeded even if you get an error message. </p> <form id='frm1' method='post' enctype='multipart/form-data'> <input type='file' name='firmware' id='firmware' onchange='upload()'><br> <progress id='progressBar' value='0' max='100' style='width:250px;'></progress> <h2 id='status'></h2> <p id='loadedtotal'></p> </form> </div> </div> Current versions:<br> <table><tr><td>Firmware:</td><td><span id='ver_sw'>*</span></td></tr><tr><td>Filesystem:</td><td><span id='ver_fs'>*</span></td></tr></table> <br><a href='/'>Return to Arska</a></body></html>";
#include <Update.h>
#define U_PART U_SPIFFS
          /**
 * @brief Sends update form (url: /update)
 *
 * @param request
 */
void onWebUpdatePost(AsyncWebServerRequest *request)
{
  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();

  update_release_selected = request->getParam("release", true)->value();
  if (!(update_release_selected.equals(String(VERSION_BASE))))
  {
    todo_in_loop_update_firmware_partition = true;
    Serial.println(update_release_selected);
    AsyncWebServerResponse *response = request->beginResponse(302, "text/html", PSTR("<html><body>Please wait while the device updates. This can take several minutes.</body></html>"));
    request->send(response);
  }
  else
  {
    request->redirect("/update");
  }
}

#endif // OTA_DOWNLOAD_ENABLED
#ifdef OTA_UPDATE_ENABLED

/**
 * @brief Returns update form from memory variable. (no littlefs required)
 *
 * @param request
 */
void onWebUpdateGet(AsyncWebServerRequest *request)
{
  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();
  todo_in_loop_get_releases = true;
  request->send_P(200, "text/html", update_page_html);
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
  size_t content_len;

  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();
  if (!index)
  {
    Serial.println("Update");
    content_len = request->contentLength();
    int cmd = (filename.indexOf("littlefs") > -1) ? U_PART : U_FLASH;
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd))
    {
      Update.printError(Serial);
    }
  }

  if (Update.write(data, len) != len)
  {
    Update.printError(Serial);
  }

  if (final)
  {
    AsyncWebServerResponse *response = request->beginResponse(302, "text/plain", PSTR("Please wait while the device reboots"));
    response->addHeader("REFRESH", "15;URL=/#admin");
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

#endif

/**
 * @brief Reset config variables to defaults
 *
 * @param full_reset Is admin password also resetted
 */
#define DEFAULT_COLOR_COUNT 9
void reset_config(bool full_reset)
{
  Serial.println(F("Starting reset_config"));

  uint32_t default_colors[DEFAULT_COLOR_COUNT] = {0x005d85, 0x1fb9d8, 0x1d8fc0, 0x00b595, 0xf4a80c, 0xe26a00, 0xff547a, 0xea1e5f, 0x884ac1};

  // TODO: handle influx somehow
  // reset memory
  char current_password[MAX_ID_STR_LENGTH];

  char current_wifi_ssid[MAX_ID_STR_LENGTH];
  char current_wifi_password[MAX_ID_STR_LENGTH];

  bool reset_wifi_settings = false;

  if ((strlen(s.wifi_ssid) > sizeof(s.wifi_ssid) - 1) || (strlen(s.wifi_password) > sizeof(s.wifi_password) - 1))
    reset_wifi_settings = true;

  if (s.wifi_ssid[0] == 255 || s.wifi_password[0] == 255) // indication that flash is erased?
    reset_wifi_settings = true;

  if (reset_wifi_settings) // reset disabled, lets try to use old wifi settings anyways
  {
    strncpy(current_wifi_ssid, "", 1);
    strncpy(current_wifi_password, "", 1);
  }
  else
  {
    strncpy(current_wifi_ssid, s.wifi_ssid, sizeof(current_wifi_ssid));
    strncpy(current_wifi_password, s.wifi_password, sizeof(current_wifi_password));
  }

  if (!full_reset)
  {
    strncpy(current_password, s.http_password, sizeof(current_password));
  }

  memset(&s, 0, sizeof(s));
  // memset(&s_influx, 0, sizeof(s_influx));
  s.check_value = EEPROM_CHECK_VALUE;

  strncpy(s.http_username, "admin", sizeof(s.http_username)); // admin id is fixed
  if (full_reset)
  {
    strncpy(s.http_password, default_http_password, sizeof(s.http_password));
  }
  else
  {
    strncpy(s.http_password, current_password, sizeof(s.http_password));
  }

  // use previous wifi settings by default
  strncpy(s.wifi_ssid, current_wifi_ssid, sizeof(s.wifi_ssid));
  strncpy(s.wifi_password, current_wifi_password, sizeof(s.wifi_password));

  // s.variable_mode = VARIABLE_MODE_SOURCE; // this mode only supported now

  strncpy(s.custom_ntp_server, "", sizeof(s.custom_ntp_server));

  // s.baseload = 0;
  s.ota_update_phase = OTA_PHASE_NONE;
  s.energy_meter_type = ENERGYM_NONE;
  s.energy_meter_port = 80;
  s.production_meter_type = PRODUCTIONM_NONE;
  s.production_meter_port = 80;
  s.production_meter_id = 3;

  strcpy(s.forecast_loc, "#");

  strcpy(s.lang, "EN");
  strcpy(s.timezone, "EET");

  s.hw_template_id = 0; // undefined my default

  bool gpios_defined = false; // if CH_GPIOS array hardcoded (in platformio.ini)
  uint16_t channel_gpios[CHANNEL_COUNT];
  /*
  char ch_gpios_local[35];
#ifdef CH_GPIOS
  gpios_defined = true;
  strncpy(ch_gpios_local, CH_GPIOS, sizeof(ch_gpios_local)); //  split comma separated gpio string to an array
  str_to_uint_array(ch_gpios_local, channel_gpios, ",");     // ESP32: first param must be locally allocated to avoid memory protection crash
#endif
*/
  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {
    if (gpios_defined)
      s.ch[channel_idx].relay_id = channel_gpios[channel_idx]; // TODO: check first type, other types available
    else
      s.ch[channel_idx].relay_id = 255;

    s.ch[channel_idx].type = (s.ch[channel_idx].relay_id < 255) ? CH_TYPE_GPIO_USER_DEF : CH_TYPE_UNDEFINED;

    s.ch[channel_idx].uptime_minimum = 60;
    s.ch[channel_idx].force_up_from = 0;
    s.ch[channel_idx].force_up_until = 0;
    s.ch[channel_idx].up_last = 0;
    s.ch[channel_idx].config_mode = CHANNEL_CONFIG_MODE_RULE;
    s.ch[channel_idx].template_id = -1;

    s.ch[channel_idx].channel_color = default_colors[channel_idx % DEFAULT_COLOR_COUNT];

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

void clean_str(char *str)
{
  for (int j = 0; j < strlen(str); j++)
  {
    Serial.printf("%x ", str[j]);
    if ((str[j] < 32) && (str[j] > 0))
    {
      str[j] = '_';
    }
  }
}

/**
 * @brief Export running configuration as json to web response
 *
 * @param request
 */
void onWebSettingsGet(AsyncWebServerRequest *request)
{
  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();

  DynamicJsonDocument doc(CONFIG_JSON_SIZE_MAX);

  String output;
  char export_time[20];
  char char_buffer[20];
  time_t current_time;
  time(&current_time);
  int active_condition_idx;

  localtime_r(&current_time, &tm_struct);
  snprintf(export_time, 20, "%04d-%02d-%02dT%02d:%02d:%02d", tm_struct.tm_year + 1900, tm_struct.tm_mon + 1, tm_struct.tm_mday, tm_struct.tm_hour, tm_struct.tm_min, tm_struct.tm_sec);
  doc["export_time"] = export_time;

  doc["check_value"] = s.check_value;
  doc["wifi_ssid"] = s.wifi_ssid;
  doc["wifi_password"] = s.wifi_password;
  doc["http_username"] = s.http_username;

  // current status, do not import
  doc["wifi_in_setup_mode"] = wifi_in_setup_mode;
  doc["using_default_password"] = String(s.http_password).equals(default_http_password);
  //  (strcmp(s.http_password, default_http_password) == 0) ? true : false;

  // doc["variable_mode"] = VARIABLE_MODE_SOURCE; // no selection

  doc["entsoe_api_key"] = s.entsoe_api_key;
  doc["entsoe_area_code"] = s.entsoe_area_code;
  //  if (s.variable_mode == VARIABLE_MODE_REPLICA)
  //   doc["variable_server"] = s.variable_server;
  doc["custom_ntp_server"] = s.custom_ntp_server;
  doc["timezone"] = s.timezone;
  // doc["baseload"] = s.baseload;
  doc["energy_meter_type"] = s.energy_meter_type;
  if (s.energy_meter_type != ENERGYM_NONE)
  {
    doc["energy_meter_ip"] = s.energy_meter_ip.toString();
    doc["energy_meter_port"] = s.energy_meter_port;
    doc["energy_meter_password"] = s.energy_meter_password;
  }

  doc["production_meter_type"] = s.production_meter_type;
  if (s.production_meter_type != PRODUCTIONM_NONE)
  {
    doc["production_meter_ip"] = s.production_meter_ip.toString();
    doc["production_meter_port"] = s.production_meter_port;
  }
  if (s.production_meter_type == PRODUCTIONM_SMA_MODBUS_TCP)
  {
    doc["production_meter_id"] = s.production_meter_id;
  }

  doc["forecast_loc"] = s.forecast_loc;
  doc["lang"] = s.lang;
  doc["hw_template_id"] = s.hw_template_id;

#ifdef INFLUX_REPORT_ENABLED
  doc["influx_url"] = s.influx_url;
  doc["influx_token"] = s.influx_token;
  doc["influx_org"] = s.influx_org;
  doc["influx_bucket"] = s.influx_bucket;
#endif

  int rule_idx_output;
  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {
    //  Serial.printf(PSTR("Exporting channel %d\n"), channel_idx);

    doc["ch"][channel_idx]["id_str"] = s.ch[channel_idx].id_str;
    // Debug
    /*
    Serial.println(s.ch[channel_idx].id_str);
    clean_str(s.ch[channel_idx].id_str);
    Serial.println(s.ch[channel_idx].id_str);
    for (int j = 0; j < strlen(s.ch[channel_idx].id_str); j++)
    {
      Serial.printf("%c (%x) ", s.ch[channel_idx].id_str[j], s.ch[channel_idx].id_str[j]);
    }
    Serial.println();
    */

    doc["ch"][channel_idx]["type"] = s.ch[channel_idx].type;
    doc["ch"][channel_idx]["config_mode"] = s.ch[channel_idx].config_mode;
    doc["ch"][channel_idx]["template_id"] = s.ch[channel_idx].template_id;
    doc["ch"][channel_idx]["uptime_minimum"] = int(s.ch[channel_idx].uptime_minimum);
    snprintf(char_buffer, 8, "#%06x", s.ch[channel_idx].channel_color);
    doc["ch"][channel_idx]["channel_color"] = char_buffer;
    doc["ch"][channel_idx]["up_last"] = s.ch[channel_idx].up_last;
    // doc["ch"][channel_idx]["force_up"] = is_force_up_valid(channel_idx);
    doc["ch"][channel_idx]["force_up_from"] = s.ch[channel_idx].force_up_from;
    doc["ch"][channel_idx]["force_up_until"] = s.ch[channel_idx].force_up_until;
    doc["ch"][channel_idx]["is_up"] = s.ch[channel_idx].is_up;
    doc["ch"][channel_idx]["wanna_be_up"] = s.ch[channel_idx].wanna_be_up;
    doc["ch"][channel_idx]["r_id"] = s.ch[channel_idx].relay_id;
    doc["ch"][channel_idx]["r_ip"] = s.ch[channel_idx].relay_ip.toString();
    doc["ch"][channel_idx]["r_uid"] = s.ch[channel_idx].relay_unit_id;
    doc["ch"][channel_idx]["r_ifid"] = s.ch[channel_idx].relay_iface_id;

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
          vars.to_str(stmt->variable_id, char_buffer, true, stmt->const_val, sizeof(char_buffer));

          doc["ch"][channel_idx]["rules"][rule_idx_output]["stmts"][stmt_count][0] = stmt->variable_id;
          doc["ch"][channel_idx]["rules"][rule_idx_output]["stmts"][stmt_count][1] = stmt->oper_id;
          doc["ch"][channel_idx]["rules"][rule_idx_output]["stmts"][stmt_count][2] = stmt->const_val;

          doc["ch"][channel_idx]["rules"][rule_idx_output]["stmts"][stmt_count][3] = char_buffer;
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

  uint8_t format = 0;
  if (request->hasParam("format"))
  {
    if (request->getParam("format")->value() == "file")
      format = 1;
  }

  if (format == 0)
  {
    request->send(200, "application/json;charset=UTF-8", output);
  }
  else
  {
    char Content_Disposition[70];
    snprintf(Content_Disposition, 70, "attachment; filename=\"arska-config-%s.json\"", export_time);
    // AsyncWebServerResponse *response = request->beginResponse(200, "application/json", output); //text
    AsyncWebServerResponse *response = request->beginResponse(200, "application/octet-stream", output); // file
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
/*
bool import_config(const char *config_file_name)
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
  s.variable_mode = VARIABLE_MODE_SOURCE; // get_doc_long(doc, "variable_mode", VARIABLE_MODE_SOURCE);
  copy_doc_str(doc, (char *)"entsoe_api_key", s.entsoe_api_key, sizeof(s.entsoe_api_key));
  copy_doc_str(doc, (char *)"entsoe_area_code", s.entsoe_area_code, sizeof(s.entsoe_area_code));
  // copy_doc_str(doc, (char *)"variable_server", s.variable_server, sizeof(s.variable_server));
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
  copy_doc_str(doc, (char *)"influx_url", s.influx_url, sizeof(s.influx_url));
  copy_doc_str(doc, (char *)"influx_token", s.influx_token, sizeof(s.influx_token));
  copy_doc_str(doc, (char *)"influx_org", s.influx_org, sizeof(s.influx_org));
  copy_doc_str(doc, (char *)"influx_bucket", s.influx_bucket, sizeof(s.influx_bucket));
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

    // relay params
    char ip_buff[16];
    if (ch_item.containsKey("r_id"))
      s.ch[channel_idx].relay_id = ch_item["r_id"];
    if (ch_item.containsKey("r_ip"))
    {
      strncpy(ip_buff, ch_item["r_ip"], 16);
      s.ch[channel_idx].relay_ip.fromString(ip_buff);
    }
    if (ch_item.containsKey("r_uid"))
      s.ch[channel_idx].relay_unit_id = ch_item["r_uid"];
    if (ch_item.containsKey("r_ifid"))
      s.ch[channel_idx].relay_iface_id = ch_item["r_ifid"];

    // channel rules
    for (JsonObject ch_rule : ch_item["rules"].as<JsonArray>())
    {
      s.ch[channel_idx].conditions[rule_idx].on = ch_rule["on"];
      stmt_idx = 0;
      Serial.printf("rule on %s", s.ch[channel_idx].conditions[rule_idx].on ? "true" : "false");

      for (JsonArray ch_rule_stmt : ch_rule["stmts"].as<JsonArray>())
      {
        s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].variable_id = ch_rule_stmt[0];
        s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].oper_id = ch_rule_stmt[1];
        s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].const_val = ch_rule_stmt[2];
        Serial.printf("rules/stmts: [%d, %d, %ld]", s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].variable_id, (int)s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].oper_id, s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].const_val);

        stmt_idx++;
      }
      rule_idx++;
    }
    channel_idx++;
  }

  writeToEEPROM();

  return true;
}


*/

bool ajson_str_to_mem(JsonVariant parent_node, char *doc_key, char *tostr, size_t buffer_length)
{
  JsonVariant element = parent_node[doc_key];
  if (!element.isNull())
  {
    Serial.println(element.as<const char *>());
    strncpy(tostr, element.as<const char *>(), buffer_length);
    return true;
  }
  Serial.printf("Element %s isNull.\n", doc_key);
  return false;
}

// ajson_int_get
int32_t ajson_int_get(JsonVariant parent_node, char *doc_key, int32_t default_val = INT32_MIN)
{
  JsonVariant element = parent_node[doc_key];
  if (!element.isNull())
  {
    if (element.is<int32_t>())
      return element.as<int32_t>();
    else
      return (int32_t)atoi(element.as<const char *>());
  }
  return default_val;
}

IPAddress ajson_ip_get(JsonVariant parent_node, char *doc_key, IPAddress default_val = (0, 0, 0, 0))
{
  JsonVariant element = parent_node[doc_key];
  if (!element.isNull())
  {
    IPAddress res_ip;
    res_ip.fromString(element.as<const char *>());
    return res_ip;
  }
  return default_val;
}

double ajson_double_get(JsonVariant parent_node, char *doc_key, double default_val = 0)
{
  JsonVariant element = parent_node[doc_key];
  if (!element.isNull())
  {
    if (element.is<double>())
      return element.as<double>();
    // else
    //   return atof(cJSON_GetObjectItem(parent_node, doc_key)->valuestring);
  }
  return default_val;
}

bool ajson_bool_get(JsonVariant parent_node, char *doc_key, bool default_val)
{
  JsonVariant element = parent_node[doc_key];
  if (!element.isNull())
  {
    return element.as<bool>();
    // return cJSON_IsTrue(cJSON_GetObjectItem(parent_node, doc_key));
  }
  return default_val;
}

#define CONFIG_JSON_SIZE_MAX 6144
bool store_settings_json(StaticJsonDocument<CONFIG_JSON_SIZE_MAX> doc)
{

  char http_password[MAX_ID_STR_LENGTH];
  char http_password2[MAX_ID_STR_LENGTH];

  ajson_str_to_mem(doc, (char *)"wifi_ssid", s.wifi_ssid, sizeof(s.wifi_ssid));
  ajson_str_to_mem(doc, (char *)"wifi_password", s.wifi_password, sizeof(s.wifi_password));
  //  s.variable_mode = VARIABLE_MODE_SOURCE; // get_doc_long(doc, "variable_mode", VARIABLE_MODE_SOURCE);
  ajson_str_to_mem(doc, (char *)"entsoe_api_key", s.entsoe_api_key, sizeof(s.entsoe_api_key));
  ajson_str_to_mem(doc, (char *)"entsoe_area_code", s.entsoe_area_code, sizeof(s.entsoe_area_code));

  // copy_doc_str(doc, (char *)"variable_server", s.variable_server, sizeof(s.variable_server));
  ajson_str_to_mem(doc, (char *)"custom_ntp_server", s.custom_ntp_server, sizeof(s.custom_ntp_server));
  ajson_str_to_mem(doc, (char *)"timezone", s.timezone, sizeof(s.timezone));
  ajson_str_to_mem(doc, (char *)"forecast_loc", s.forecast_loc, sizeof(s.forecast_loc));
  ajson_str_to_mem(doc, (char *)"lang", s.lang, sizeof(s.lang));

  if (ajson_str_to_mem(doc, (char *)"http_password", http_password, sizeof(http_password)))
  {
    if (ajson_str_to_mem(doc, (char *)"http_password2", http_password2, sizeof(http_password2)))
    {
      if ((strcmp(http_password, http_password2) == 0) && strlen(http_password) > 0 && strlen(http_password2) > 0)
      { // equal
        strncpy(s.http_password, http_password, sizeof(s.http_password));
        Serial.println("Password changed");
      }
    }
  }

  s.hw_template_id = ajson_int_get(doc, (char *)"hw_template_id", s.hw_template_id);
  // s.baseload = ajson_int_get(doc, (char *)"baseload", s.baseload);

  s.energy_meter_type = (uint8_t)ajson_int_get(doc, (char *)"energy_meter_type", s.energy_meter_type);
  Serial.printf("s.energy_meter_type %d\n", (int)s.energy_meter_type);
  // ajson_str_to_mem(doc, (char *)"energy_meter_host", s.energy_meter_host, sizeof(s.energy_meter_host));
  s.energy_meter_ip = ajson_ip_get(doc, (char *)"energy_meter_ip", s.energy_meter_ip);
  ajson_str_to_mem(doc, (char *)"energy_meter_password", s.energy_meter_password, sizeof(s.energy_meter_password));

  // doc["energy_meter_password"] = s.energy_meter_password;
  s.energy_meter_port = ajson_int_get(doc, (char *)"energy_meter_port", s.energy_meter_port);
  s.energy_meter_id = ajson_int_get(doc, (char *)"energy_meter_id", s.energy_meter_id);

  s.production_meter_type = ajson_int_get(doc, (char *)"production_meter_type", s.production_meter_type);
  s.production_meter_ip = ajson_ip_get(doc, (char *)"production_meter_ip", s.production_meter_ip);
  s.production_meter_port = ajson_int_get(doc, (char *)"production_meter_port", s.production_meter_port);
  s.production_meter_id = ajson_int_get(doc, (char *)"production_meter_id", s.production_meter_id);

#ifdef INFLUX_REPORT_ENABLED
  ajson_str_to_mem(doc, (char *)"influx_url", s.influx_url, sizeof(s.influx_url));
  ajson_str_to_mem(doc, (char *)"influx_token", s.influx_token, sizeof(s.influx_token));
  ajson_str_to_mem(doc, (char *)"influx_org", s.influx_org, sizeof(s.influx_org));
  ajson_str_to_mem(doc, (char *)"influx_bucket", s.influx_bucket, sizeof(s.influx_bucket));
#endif

  int channel_idx;
  int rule_idx = 0;
  int stmt_idx = 0;
  char hex_buffer[8];

  int32_t hw_template_id;

  if (!doc["hw_template_id"].isNull())
  {
    hw_template_id = ajson_int_get(doc, (char *)"hw_template_id", s.hw_template_id);
    s.hw_template_id = hw_template_id;
    if ((hw_template_id != s.hw_template_id) && hw_template_id > 0)
    {
      for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
      {
        if (channel_idx < HW_TEMPLATE_GPIO_COUNT)
        { // touch only channel which could have gpio definitions
          if (hw_templates[s.hw_template_id].gpios[channel_idx] < 255)
          {
            s.ch[channel_idx].type = CH_TYPE_GPIO_USER_DEF; // was CH_TYPE_GPIO_FIXED;
            s.ch[channel_idx].relay_id = hw_templates[s.hw_template_id].gpios[channel_idx];
          }
          else if (s.ch[channel_idx].type == CH_TYPE_GPIO_FIXED) // deprecate CH_TYPE_GPIO_FIXED
          { // fixed gpio -> user defined, new way
            s.ch[channel_idx].type = CH_TYPE_GPIO_USER_DEF;
          }
        }
      }
    }
  }

  for (JsonObject ch : doc["ch"].as<JsonArray>())
  {
    // channel_idx = ajson_int_get(ch, (char *)"idx", channel_idx_loop);
    ajson_str_to_mem(ch, (char *)"id_str", s.ch[channel_idx].id_str, sizeof(s.ch[channel_idx].id_str));
    Serial.printf("s.ch[channel_idx].id_str %s\n", s.ch[channel_idx].id_str);

    s.ch[channel_idx].type = ajson_int_get(ch, (char *)"type", s.ch[channel_idx].type);
    s.ch[channel_idx].config_mode = ajson_int_get(ch, (char *)"config_mode", s.ch[channel_idx].config_mode);
    s.ch[channel_idx].template_id = ajson_int_get(ch, (char *)"template_id", s.ch[channel_idx].template_id);
    s.ch[channel_idx].uptime_minimum = ajson_int_get(ch, (char *)"uptime_minimum", s.ch[channel_idx].uptime_minimum);

    if (ch.containsKey("channel_color"))
    {
      ajson_str_to_mem(ch, (char *)"channel_color", hex_buffer, sizeof(hex_buffer));
      s.ch[channel_idx].channel_color = (uint32_t)strtol(hex_buffer + 1, NULL, 16);
    }

    s.ch[channel_idx].relay_id = ajson_int_get(ch, (char *)"r_id", s.ch[channel_idx].relay_id);
    s.ch[channel_idx].relay_ip = ajson_ip_get(ch, (char *)"r_ip", s.ch[channel_idx].relay_ip);
    s.ch[channel_idx].relay_unit_id = ajson_int_get(ch, (char *)"r_uid", s.ch[channel_idx].relay_unit_id);

    // clear  statements
    // TODO: add to new version
    for (rule_idx = 0; rule_idx < CHANNEL_CONDITIONS_MAX; rule_idx++)
    {
      for (stmt_idx = 0; stmt_idx < RULE_STATEMENTS_MAX; stmt_idx++)
      {
        s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].variable_id = -1;
        s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].oper_id = -1;
        s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].const_val = VARIABLE_LONG_UNKNOWN;
      }
    }

    // channel rules
    rule_idx = 0;

    // channel rules
    for (JsonObject ch_rule : ch["rules"].as<JsonArray>())
    {
      s.ch[channel_idx].conditions[rule_idx].on = ch_rule["on"];
      stmt_idx = 0;
      Serial.printf("rule on %s", s.ch[channel_idx].conditions[rule_idx].on ? "true" : "false");

      for (JsonArray ch_rule_stmt : ch_rule["stmts"].as<JsonArray>())
      {
        s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].variable_id = ch_rule_stmt[0];
        s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].oper_id = ch_rule_stmt[1];
        s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].const_val = ch_rule_stmt[2];
        s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].const_val = vars.float_to_internal_l(ch_rule_stmt[0], ch_rule_stmt[3]);
        Serial.printf("rules/stmts: [%d, %d, %ld]", s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].variable_id, (int)s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].oper_id, s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].const_val);
        stmt_idx++;
      }
      rule_idx++;
    }
    channel_idx++;
  }
  writeToEEPROM();
  return true;
}

/*
//

 * @brief Handle config file upload
 *
 * @param request
 * @param filename
 * @param index
 * @param data
 * @param len
 */
// String * 8

// void onWebUploadConfig(AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final)
void onWebUploadConfig(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{

  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();

  bool final = ((len + index) == total);
  Serial.println("onWebUploadConfig   len,  index,  total final ");
  Serial.println(len);
  Serial.println(index);
  Serial.println(total);
  Serial.println(final);
  Serial.println();

  String logmessage = "Client:" + request->client()->remoteIP().toString() + " " + request->url();
  Serial.println(logmessage);
  const char *filename_internal = "/data/config_in.json";

  if (!index) // first
  {
    Serial.println("onWebUploadConfig start");
    // logmessage = "Upload Start: " + String(filename);
    //  open the file on first call and store the file handle in the request object
    request->_tempFile = LittleFS.open(filename_internal, "w");
  }

  if (len) // contains data
  {
    Serial.println("onWebUploadConfig data");
    // stream the incoming chunk to the opened file
    request->_tempFile.write(data, len);

    // logmessage = "Writing file: " + String(filename) + " index=" + String(index) + " len=" + String(len);
    // Serial.println(logmessage);
    //  request->send(501, "application/json", "{\"status\":\"failed\"}");
  }

  if (final) // last call
  {
    Serial.println("onWebUploadConfig final");
    // logmessage = "Upload Complete: " + String(filename) + ",size: " + String(index + len);
    // close the file handle as the upload is now done
    request->_tempFile.close();

    File config_file = LittleFS.open(filename_internal, "r");
    StaticJsonDocument<CONFIG_JSON_SIZE_MAX> doc;
    DeserializationError error = deserializeJson(doc, config_file);
    if (error)
    {
      Serial.println(error.f_str());
      request->send(500, "application/json", "{\"status\":\"failed\"}");
    }
    else
    {
      reset_config(false);
      store_settings_json(doc);

      request->send(200, "application/json", "{\"status\":\"ok\"}");
    }

    // import_config(filename_internal);
    // request->redirect("/");
  }
}

void onWebUIGet(AsyncWebServerRequest *request)
{
  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();
  check_forced_restart(true); // if in forced ap-mode, reset counter to delay automatic restart
  request->send(LittleFS, "/ui3.html", "text/html");
}

/**
 * @brief Get individual rule template by id
 *
 * @param request
 */
/*
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

*/
/**
 * @brief Process dashboard form, forcing channels up, JSON update, work in progress
 *
 * @param request
 */
void onScheduleUpdate(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  time(&now);
  int channel_idx;
  bool force_up_changes = false;
  bool channel_already_forced;
  long force_up_minutes;
  time_t force_up_from = 0;
  time_t force_up_until;

  StaticJsonDocument<2048> doc; //
  // Serial.println((const char *)data);
  DeserializationError error = deserializeJson(doc, (const char *)data);
  if (error)
  {
    Serial.print(F("onWebChannelsPost deserializeJson() failed: "));
    Serial.println(error.f_str());
    request->send(200, "application/json", "{\"status\":\"error\"}");
  }

  for (JsonObject schedule : doc["schedules"].as<JsonArray>())
  {
    channel_idx = schedule["ch_idx"];
    int duration = schedule["duration"];
    time_t from = schedule["from"];
    Serial.printf("%d %d %d\n", channel_idx, duration, from);

    if (duration == -1)
      continue; // no selection

    channel_already_forced = is_force_up_valid(channel_idx);
    force_up_minutes = duration;

    if (from == 0)
      force_up_from = now;
    else
      force_up_from = max(now, from); // absolute unix ts is waited

    Serial.printf("channel_idx: %d, force_up_minutes: %ld , force_up_from %ld\n", channel_idx, force_up_minutes, force_up_from);

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
  if (force_up_changes)
  {
    todo_in_loop_set_relays = true;
    writeToEEPROM();
  }
  request->send(200, "application/json", "{\"status\":\"ok\"}");
}

void onWebActionsPost(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{

  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();

  Serial.println("onWebActionsPost");

  StaticJsonDocument<500> doc; //
  DeserializationError error = deserializeJson(doc, (const char *)data);
  if (error)
  {
    Serial.print(F("onWebActionsPost deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }
  String action = doc["action"];
  Serial.println(action);
  Serial.println((const char *)data);

  bool todo_in_loop_restart_local = false; // set global variable in the end when data is set

  /*

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
  */

  if (action == "update")
  {
    update_release_selected = String((const char*)doc["version"]);
   
    // copy_doc_str()

    //strncpy(tostr, doc[key], buffer_length);

    todo_in_loop_update_firmware_partition = true;
    Serial.println(update_release_selected);
    // AsyncWebServerResponse *response = request->beginResponse(302, "text/html", PSTR("<html><body>Please wait while the device updates. This can take several minutes.</body></html>"));
    // request->send(response);
    request->send(200, "application/json", "{\"status\":\"ok\", \"refresh\" : 300}");
  }

  if (doc["action"] == "restart")
  {
    todo_in_loop_restart_local = true;
  }
  if (doc["action"] == "scan_sensors")
  {
    todo_in_loop_scan_sensors = true;
    request->send(200, "application/json", "{\"status\":\"ok\", \"refresh\" : 0}");
    return;
  }

  if (doc["action"] == "reset")
  {
    reset_config(false);
    todo_in_loop_restart_local = true;
    writeToEEPROM();
  }
  if (doc["action"] == "scan_wifis")
  {
    todo_in_loop_scan_wifis = true;
  }

  /*


    if (request->getParam("action", true)->value().equals("op_test_gpio"))
    {
      gpio_to_test_in_loop = request->getParam("test_gpio", true)->value().toInt();
      todo_in_loop_test_gpio = true;
      request->send(200, "text/html", "<html><head><meta http-equiv='refresh' content='1; url=/#admin' /></head><body>GPIO testing</body></html>");
      return;
    }


  */
  todo_in_loop_restart = todo_in_loop_restart_local;

  if (todo_in_loop_restart)
  {
    request->send(200, "application/json", "{\"status\":\"ok\", \"refresh\" : 30}");
  }
  else
    request->send(200, "application/json", "{\"status\":\"ok\", \"refresh\" : 0}");
}

void onWebSettingsPost(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{

  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();
  Serial.println("onWebSettingsPost authenticated len index total");
  Serial.println(len);
  Serial.println(index);
  Serial.println(total);

  Serial.println((const char *)data);

  StaticJsonDocument<CONFIG_JSON_SIZE_MAX> doc; //
  DeserializationError error = deserializeJson(doc, (const char *)data);
  if (error)
  {
    Serial.print(F("onWebSettingsPost deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }
  else
  {
    Serial.println(doc["forecast_loc"].as<const char *>());
  }

  store_settings_json(doc);

  // TODO: mieti vielä paluuarvo
  ////****
  /*

  ajson_str_to_mem(doc, (char *)"wifi_ssid", s.wifi_ssid, sizeof(s.wifi_ssid));
  ajson_str_to_mem(doc, (char *)"wifi_password", s.wifi_password, sizeof(s.wifi_password));
  //  s.variable_mode = VARIABLE_MODE_SOURCE; // get_doc_long(doc, "variable_mode", VARIABLE_MODE_SOURCE);
  ajson_str_to_mem(doc, (char *)"entsoe_api_key", s.entsoe_api_key, sizeof(s.entsoe_api_key));
  ajson_str_to_mem(doc, (char *)"entsoe_area_code", s.entsoe_area_code, sizeof(s.entsoe_area_code));

  // copy_doc_str(doc, (char *)"variable_server", s.variable_server, sizeof(s.variable_server));
  ajson_str_to_mem(doc, (char *)"custom_ntp_server", s.custom_ntp_server, sizeof(s.custom_ntp_server));
  ajson_str_to_mem(doc, (char *)"timezone", s.timezone, sizeof(s.timezone));
  ajson_str_to_mem(doc, (char *)"forecast_loc", s.forecast_loc, sizeof(s.forecast_loc));
  ajson_str_to_mem(doc, (char *)"lang", s.lang, sizeof(s.lang));

if (ajson_str_to_mem(doc, (char *)"http_password", http_password, sizeof(http_password))) {
  if (ajson_str_to_mem(doc, (char *)"http_password2", http_password2, sizeof(http_password2))) {
    if ((strcmp( http_password,http_password2)==0) && strlen(http_password)>0 && strlen(http_password2)>0) { //equal
      strncpy(s.http_password, http_password, sizeof(s.http_password));
      Serial.println("Passwrod changed");
    }
  }
}

  s.hw_template_id = ajson_int_get(doc, (char *)"hw_template_id", s.hw_template_id);
  s.baseload = ajson_int_get(doc, (char *)"baseload", s.baseload);

  s.energy_meter_type = (uint8_t)ajson_int_get(doc, (char *)"energy_meter_type", s.energy_meter_type);
  Serial.printf("s.energy_meter_type %d\n", (int)s.energy_meter_type);
  // ajson_str_to_mem(doc, (char *)"energy_meter_host", s.energy_meter_host, sizeof(s.energy_meter_host));
  s.energy_meter_ip = ajson_ip_get(doc, (char *)"energy_meter_ip", s.energy_meter_ip);
  ajson_str_to_mem(doc, (char *)"energy_meter_password", s.energy_meter_password, sizeof(s.energy_meter_password));

  // doc["energy_meter_password"] = s.energy_meter_password;
  s.energy_meter_port = ajson_int_get(doc, (char *)"energy_meter_port", s.energy_meter_port);
  s.energy_meter_id = ajson_int_get(doc, (char *)"energy_meter_id", s.energy_meter_id);

  s.production_meter_type = ajson_int_get(doc, (char *)"production_meter_type", s.production_meter_type);
  s.production_meter_ip = ajson_ip_get(doc, (char *)"production_meter_ip", s.production_meter_ip);
  s.production_meter_port = ajson_int_get(doc, (char *)"production_meter_port", s.production_meter_port);
  s.production_meter_id = ajson_int_get(doc, (char *)"production_meter_id", s.production_meter_id);

#ifdef INFLUX_REPORT_ENABLED
  ajson_str_to_mem(doc, (char *)"influx_url", s.influx_url, sizeof(s.influx_url));
  ajson_str_to_mem(doc, (char *)"influx_token", s.influx_token, sizeof(s.influx_token));
  ajson_str_to_mem(doc, (char *)"influx_org", s.influx_org, sizeof(s.influx_org));
  ajson_str_to_mem(doc, (char *)"influx_bucket", s.influx_bucket, sizeof(s.influx_bucket));
#endif

  int channel_idx_loop = 0;
  int channel_idx;
  int rule_idx = 0;
  int stmt_idx = 0;
  char hex_buffer[8];

  for (JsonObject ch : doc["ch"].as<JsonArray>())
  {
    channel_idx = ajson_int_get(ch, (char *)"idx", channel_idx_loop);
    ajson_str_to_mem(ch, (char *)"id_str", s.ch[channel_idx].id_str, sizeof(s.ch[channel_idx].id_str));
    Serial.printf("s.ch[channel_idx].id_str %s\n", s.ch[channel_idx].id_str);

    s.ch[channel_idx].type = ajson_int_get(ch, (char *)"type", s.ch[channel_idx].type);
    s.ch[channel_idx].config_mode = ajson_int_get(ch, (char *)"config_mode", s.ch[channel_idx].config_mode);
    s.ch[channel_idx].template_id = ajson_int_get(ch, (char *)"template_id", s.ch[channel_idx].template_id);
    s.ch[channel_idx].uptime_minimum = ajson_int_get(ch, (char *)"uptime_minimum", s.ch[channel_idx].uptime_minimum);

    if (ch.containsKey("channel_color"))
    {
      ajson_str_to_mem(ch, (char *)"channel_color", hex_buffer, sizeof(hex_buffer));
      s.ch[channel_idx].channel_color = (uint32_t)strtol(hex_buffer + 1, NULL, 16);
    }

    s.ch[channel_idx].relay_id = ajson_int_get(ch, (char *)"r_id", s.ch[channel_idx].relay_id);
    s.ch[channel_idx].relay_ip = ajson_ip_get(ch, (char *)"r_ip", s.ch[channel_idx].relay_ip);
    s.ch[channel_idx].relay_unit_id = ajson_int_get(ch, (char *)"r_uid", s.ch[channel_idx].relay_unit_id);

    // clear  statements
    // TODO: add to new version
    for (rule_idx = 0; rule_idx < CHANNEL_CONDITIONS_MAX; rule_idx++)
    {
      for (stmt_idx = 0; stmt_idx < RULE_STATEMENTS_MAX; stmt_idx++)
      {
        s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].variable_id = -1;
        s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].oper_id = -1;
        s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].const_val = VARIABLE_LONG_UNKNOWN;
      }
    }

    // channel rules
    rule_idx = 0;

    // channel rules
    for (JsonObject ch_rule : ch["rules"].as<JsonArray>())
    {
      s.ch[channel_idx].conditions[rule_idx].on = ch_rule["on"];
      stmt_idx = 0;
      Serial.printf("rule on %s", s.ch[channel_idx].conditions[rule_idx].on ? "true" : "false");

      for (JsonArray ch_rule_stmt : ch_rule["stmts"].as<JsonArray>())
      {
        s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].variable_id = ch_rule_stmt[0];
        s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].oper_id = ch_rule_stmt[1];
        s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].const_val = ch_rule_stmt[2];
        s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].const_val = vars.float_to_internal_l(ch_rule_stmt[0], ch_rule_stmt[3]);
        Serial.printf("rules/stmts: [%d, %d, %ld]", s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].variable_id, (int)s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].oper_id, s.ch[channel_idx].conditions[rule_idx].statements[stmt_idx].const_val);
        stmt_idx++;
      }
      rule_idx++;
    }
  }

  writeToEEPROM();
  */
  ////****

  StaticJsonDocument<256> out_doc; // global doc to discoveries
  out_doc["rc"] = 0;
  out_doc["msg"] = "ok";
  String output;
  serializeJson(out_doc, output);
  request->send(200, "application/json", output);
  return;
}

/**
 * @brief Process service (input) form
 *
 * @param request
 */
/*
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

  //s.baseload = request->getParam("baseload", true)->value().toInt();

  s.variable_mode = VARIABLE_MODE_SOURCE; // (uint8_t)request->getParam("variable_mode", true)->value().toInt();
  if (s.variable_mode == 0)               // TODO: remove other than this mode
  {
    if ((strcmp(s.entsoe_api_key, request->getParam("entsoe_api_key", true)->value().c_str()) != 0) || (strlen(s.entsoe_api_key) != strlen(request->getParam("entsoe_api_key", true)->value().c_str())))
      entsoe_params_changed = true;

    strncpy(s.entsoe_api_key, request->getParam("entsoe_api_key", true)->value().c_str(), sizeof(s.entsoe_api_key));
    if ((strcmp(s.entsoe_area_code, request->getParam("entsoe_area_code", true)->value().c_str()) != 0) || (strlen(s.entsoe_area_code) != strlen(request->getParam("entsoe_area_code", true)->value().c_str())))
    {
      strncpy(s.entsoe_area_code, request->getParam("entsoe_area_code", true)->value().c_str(), sizeof(s.entsoe_area_code));
      entsoe_params_changed = true;
    }
    if (entsoe_params_changed)
    {
      // api key or price area changes, clear cache and requery
      LittleFS.remove(price_data_filename); // "/price_data.json"
      next_query_price_data = now + 10;     // query with new parameters soon
    }
    // Solar forecast supported currently only in Finland
    if (strcmp(s.entsoe_area_code, "10YFI-1--------U") == 0)
      strncpy(s.forecast_loc, request->getParam("forecast_loc", true)->value().c_str(), sizeof(s.forecast_loc));
    else
      strcpy(s.forecast_loc, "#");
  }

#ifdef INFLUX_REPORT_ENABLED
  strncpy(s.influx_url, request->getParam("influx_url", true)->value().c_str(), sizeof(s.influx_url));
  // Serial.printf("influx_url:%s\n", s.influx_url);
  strncpy(s.influx_token, request->getParam("influx_token", true)->value().c_str(), sizeof(s.influx_token));
  strncpy(s.influx_org, request->getParam("influx_org", true)->value().c_str(), sizeof(s.influx_org));
  strncpy(s.influx_bucket, request->getParam("influx_bucket", true)->value().c_str(), sizeof(s.influx_bucket));
#endif

  // END OF INPUTS
  writeToEEPROM();

  todo_in_loop_restart = todo_in_loop_restart_local;

  if (todo_in_loop_restart)
  {
    request->send(200, "text/html", "<html><head><meta http-equiv='refresh' content='10; url=/#services' /></head><body>restarting...wait...</body></html>");
  }
  else
  {
    request->redirect("/#services");
  }
}
*/
// TODO: refaktoroi, myös intille oma
bool update_channel_field_str(AsyncWebServerRequest *request, char *target_str, int channel_idx, const char *field_format, size_t max_length)
{
  char ch_fld[20];
  snprintf(ch_fld, 20, field_format, channel_idx);
  if (request->hasParam(ch_fld, true))
  {
    strncpy(target_str, request->getParam(ch_fld, true)->value().c_str(), max_length);
    return true;
  }
  Serial.println(ch_fld);
  return false;
}


#ifdef MDNS_ENABLED

#define MAX_DISCOVERY_COUNT 10
StaticJsonDocument<2048> discover_doc; // global doc to discoveries
/**
 * @brief Get the switch type by MSDN info (app), recornizes Shelly devices
 *
 * @return uint8_t
 */
uint8_t get_device_swith_type(int mdns_device_idx, device_db_struct *device)
{
  int device_db_count = sizeof(device_db) / sizeof(*device_db);
  Serial.println("device_db_count:");
  Serial.println(device_db_count);
  if (!MDNS.hasTxt(mdns_device_idx, "app") || MDNS.txt(mdns_device_idx, "app").length() == 0)
    return CH_TYPE_UNDEFINED;

  for (int i = 0; i < device_db_count; i++)
  {
    if ((strcmp(device_db[i].app, MDNS.txt(mdns_device_idx, "app").c_str()) == 0) && strlen(device_db[i].app) == strlen(MDNS.txt(mdns_device_idx, "app").c_str()))
    {
      memcpy(device, &device_db[i], sizeof(device_db_struct));
      return CH_TYPE_SHELLY_1GEN;
    }
  }

  return CH_TYPE_UNDEFINED;
}

/**
 * @brief mDNS discovery of devices in the local network, update global json doc
 *
 */
void discover_devices()
{
  unsigned long started = millis();
  device_db_struct device_db_entry;
  uint8_t switch_type;

  if (s.mdns_activated)
  {
    discover_doc.clear();

    int service_count = min(MDNS.queryService("http", "tcp"), MAX_DISCOVERY_COUNT);
    for (int i = 0; i < service_count; i++)
    {
      discover_doc["services"][i]["host"] = MDNS.hostname(i);
      discover_doc["services"][i]["ip"] = MDNS.IP(i);
      discover_doc["services"][i]["port"] = MDNS.port(i);
      switch_type = get_device_swith_type(i, &device_db_entry);
      discover_doc["services"][i]["type"] = switch_type;

      if (switch_type != CH_TYPE_UNDEFINED)
      {
        Serial.printf("switch_type:%d, outputs: %d\n", (int)switch_type, (int)device_db_entry.outputs);
        discover_doc["services"][i]["outputs"] = (int)device_db_entry.outputs;
      }
      if (MDNS.hasTxt(i, "app"))
      {
        discover_doc["services"][i]["app"] = MDNS.txt(i, "app");
      }
    }
  }
  else
  {
    discover_doc["result"] = "deactivated";
  }
  time_t now_l;
  time(&now_l);
  discover_doc["ts"] = now_l;
  discover_doc["process_time"] = millis() - started; // debug
}

/**
 * @brief returns global mDNS discoverydoc
 *
 * @param request
 */
void onWebDiscoverGet(AsyncWebServerRequest *request)
{
  if (!request->authenticate(s.http_username, s.http_password))
  {
    return request->requestAuthentication();
  }
  String output;
  serializeJson(discover_doc, output);
  request->send(200, "application/json", output);
}
#endif

/**
 * @brief Add tasks to task queueu
 *
 * @param request
 */
/*
void onWebDoGet(AsyncWebServerRequest *request)
{
  if (!request->authenticate(s.http_username, s.http_password))
  {
    return request->requestAuthentication();
  }
  if (!request->hasParam("action"))
    request->send(404, "text/plain", "Not found");

  if (request->getParam("action")->value() == "discover_devices")
    todo_in_loop_discover_devices = true;
  else if (request->getParam("action")->value() == "scan_wifis")
    todo_in_loop_scan_wifis = true;
  else if (request->getParam("action")->value() == "scan_sensors")
    todo_in_loop_scan_sensors = true;

  request->send(200, "application/json", "{\"result\": \"queued\"}");
}
*/
/*

void json_get_price_handler(AsyncWebServerRequest *request)
{
    DynamicJsonDocument doc(4096);
  String output;

  JsonObject var_obj = doc.createNestedObject("variables");

    cJSON *root;
    root = cJSON_CreateObject();

    cJSON_AddItemToObject(root, "prices", cJSON_CreateIntArray(prices, MAX_PRICE_PERIODS));

    time_t record_start = record_end_excl - (PRICE_PERIOD_SEC * MAX_PRICE_PERIODS);
    cJSON_AddNumberToObject(root, "record_start", (long)record_start);

    ESP_LOGI(TAG, "record_start record_end_excl %ld %ld", (long)record_start, (long)record_end_excl);

    double apu = (long)record_end_excl;
    // cJSON_AddNumberToObject(root, "record_end_excl", record_end_excl);
    cJSON_AddNumberToObject(root, "record_end_excl", apu);

    char *json_string = cJSON_PrintUnformatted(root); // cJSON_Print

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_string, strlen(json_string));

    if (json_string) // free memory
        cJSON_free(json_string);

    cJSON_Delete(root);

    return ESP_OK;
}
*/

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

  // StaticJsonDocument<2048> doc; //
  DynamicJsonDocument doc(CONFIG_JSON_SIZE_MAX);
  String output;

  JsonObject var_obj = doc.createNestedObject("variables");

  /*
  #ifdef INVERTER_FRONIUS_SOLARAPI_ENABLED
    variables["energyProducedPeriod"] = energy_produced_period;
    variables["powerProducedPeriodAvg"] = power_produced_period_avg;
  #endif
  */
  // var_obj["updated"] = energym_meter_read_ts;
  // var_obj["freeHeap"] = ESP.getFreeHeap();
  // var_obj["uptime"] = (unsigned long)(millis() / 1000);

  char id_str[6];
  char buff_value[20];
  variable_st variable;
  for (int variable_idx = 0; variable_idx < vars.get_variable_count(); variable_idx++)
  {
    vars.get_variable_by_idx(variable_idx, &variable);
    snprintf(id_str, 6, "%d", variable.id);
    vars.to_str(variable.id, buff_value, false, 0, sizeof(buff_value));

    var_obj[id_str] = buff_value;
  }

  // variables with history time series
  for (int v_idx = 0; v_idx < HISTORY_VARIABLE_COUNT; v_idx++)
  {
    snprintf(id_str, 6, "%d", history_variables[v_idx]);
    JsonArray v_history_array_item = doc["variable_history"].createNestedArray(id_str);
    for (int h_idx = 0; h_idx < MAX_HISTORY_PERIODS; h_idx++)
    {
      v_history_array_item.add((VARIABLE_LONG_UNKNOWN == variable_history[v_idx][h_idx]) ? 0 : variable_history[v_idx][h_idx]);
    }
  }

  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {
    doc["ch"][channel_idx]["is_up"] = s.ch[channel_idx].is_up;
    doc["ch"][channel_idx]["active_condition"] = active_condition(channel_idx);
    // doc["ch"][channel_idx]["force_up"] = is_force_up_valid(channel_idx);
    doc["ch"][channel_idx]["force_up_from"] = s.ch[channel_idx].force_up_from;
    doc["ch"][channel_idx]["force_up_until"] = s.ch[channel_idx].force_up_until;
    doc["ch"][channel_idx]["up_last"] = s.ch[channel_idx].up_last;

    for (int h_idx = 0; h_idx < MAX_HISTORY_PERIODS - 1; h_idx++)
    {
      doc["channel_history"][channel_idx][h_idx] = channel_history[channel_idx][h_idx];
    }

    ch_counters.update_utilization(channel_idx);
    doc["channel_history"][channel_idx][MAX_HISTORY_PERIODS - 1] = ch_counters.get_utilization(channel_idx);
  }

  time_t current_time;
  time(&current_time);
  // localtime_r(&current_time, &tm_struct);
  // snprintf(buff_value, 20, "%04d-%02d-%02d %02d:%02d:%02d", tm_struct.tm_year + 1900, tm_struct.tm_mon + 1, tm_struct.tm_mday, tm_struct.tm_hour, tm_struct.tm_min, tm_struct.tm_sec);
  // doc["localtime"] = buff_value;

  doc["ts"] = current_time;
  doc["started"] = started;

  doc["last_msg_msg"] = last_msg.msg;
  doc["last_msg_ts"] = last_msg.ts;
  doc["last_msg_type"] = last_msg.type;
  doc["energym_read_last"] = energym_read_last;
  doc["productionm_read_last"] = productionm_read_last;
  doc["next_process_in"] = max((long)0, (long)next_process_ts - current_time);

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
 * @brief Arduino framework function.  Everything starts from here while starting the controller.
 *
 */

void setup()
{
  bool create_wifi_ap = false;
  // s.variable_mode = VARIABLE_MODE_SOURCE;
  Serial.begin(115200);
  delay(2000); // wait for console settle - only needed when debugging

  // Serial.prinf("settings_struct: %d,  settings_struct2: %d %d\n", (int)sizeof(settings_struct),(int)sizeof(settings_struct2),(int)sizeof(bool));
  randomSeed(analogRead(0)); // initiate random generator
  // Serial.printf("IDF_VER: %s\n",IDF_VER);
  Serial.printf(PSTR("ARSKA VERSION_BASE %s, Version: %s, compile_date: %s\n"), VERSION_BASE, VERSION, compile_date);

  String wifi_mac_short = WiFi.macAddress();
  Serial.printf(PSTR("Device mac address: %s\n"), WiFi.macAddress().c_str());

  // Experimental
  grid_protection_delay_interval = random(0, grid_protection_delay_max / PROCESS_INTERVAL_SECS) * PROCESS_INTERVAL_SECS;
  Serial.printf(PSTR("Grid protection delay after interval change %d seconds.\n"), grid_protection_delay_interval);

  for (int i = 14; i > 0; i -= 3)
  {
    wifi_mac_short.remove(i, 1);
  }

#ifdef SENSOR_DS18B20_ENABLED
  sensors.begin();
  delay(1000); // let the sensors settle
  // get a count of devices on the wire
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

  // Check if filesystem update is needed
  Serial.println(F("Checking filesystem version"));
  todo_in_loop_update_firmware_partition = !(check_filesystem_version());

  readFromEEPROM();
  if (s.check_value != EEPROM_CHECK_VALUE) // setup not initiated
  {
    Serial.println(F("Memory structure changed. Resetting settings"));
    reset_config(true);
    config_resetted = true;
  }

  // Channel init with state DOWN/failsafe
  Serial.println(F("Setting relays default/failsafe."));
  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {
    if (s.ch[channel_idx].type == CH_TYPE_GPIO_FIXED) //deprecate CH_TYPE_GPIO_FIXED type
      s.ch[channel_idx].type = CH_TYPE_GPIO_USER_DEF;

    //  reset values from eeprom
    s.ch[channel_idx].wanna_be_up = false;
    s.ch[channel_idx].is_up = false;
    apply_relay_state(channel_idx, true);
    relay_state_reapply_required[channel_idx] = false;
  }

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
    check_forced_restart(true); // schedule restart
  }
  else
  {
    Serial.printf(PSTR("Connected to wifi [%s] with IP Address: %s, gateway: %s \n"), s.wifi_ssid, WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str());
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
      log_msg(MSG_TYPE_FATAL, PSTR("Cannot create AP, restarting."), true);

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
  // update form
  server_web.on("/update", HTTP_GET, [](AsyncWebServerRequest *request)
                { onWebUpdateGet(request); });

  //
  server_web.on("/update", HTTP_POST, [](AsyncWebServerRequest *request)
                { onWebUpdatePost(request); });
  // experimental
  // if (!LittleFS.exists("/update.html")) -> we could use in /update
  server_web.on("/update2", HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, "/update.html", "text/html"); });

  server_web.on(
      "/releases", HTTP_GET, [](AsyncWebServerRequest *request)
      { request->send(200,"application/json",update_releases.c_str()); 
        todo_in_loop_get_releases= true; });

  server_web.on(
      "/doUpdate", HTTP_POST,
      [](AsyncWebServerRequest *request) {},
      [](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data,
         size_t len, bool final)
      { handleDoUpdate(request, filename, index, data, len, final); });
#endif

#ifdef MDNS_ENABLED
  if (s.mdns_activated)
  {
    if (!MDNS.begin(("arskanode-" + wifi_mac_short).c_str())) // UNIQUE NAME with mac here
    {
      Serial.println("Error starting mDNS");
    }
    else
    {
      Serial.println("Started mDNS service");
      MDNS.addService("http", "tcp", 80);
    }
  }
  else
  {
    Serial.println("mDNS service not activated.");
  }

  server_web.on("/discover", HTTP_GET, onWebDiscoverGet);
#endif

  // server_web.on("/do", HTTP_GET, onWebDoGet); // action queueu

  server_web.on("/status", HTTP_GET, onWebStatusGet);

  server_web.on("/application", HTTP_GET, onWebApplicationGet);

  server_web.on("/settings", HTTP_GET, onWebSettingsGet);

  server_web.on(
      "/actions", HTTP_POST,
      [](AsyncWebServerRequest *request) {},
      [](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data,
         size_t len, bool final) {},
      onWebActionsPost);

  server_web.on(
      "/settings", HTTP_POST,
      [](AsyncWebServerRequest *request) {},
      [](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data,
         size_t len, bool final) {},
      onWebSettingsPost);

  // full json file, multi part upload
  server_web.on(
      "/settings-restore",
      HTTP_POST,
      [](AsyncWebServerRequest *request)
      { Serial.println("#"); },
      [](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data,
         size_t len, bool final)
      { Serial.println("@"); },
      onWebUploadConfig);

  server_web.on("/", HTTP_GET, onWebUIGet);

  server_web.on(
      "/update.schedule", HTTP_POST,
      [](AsyncWebServerRequest *request) {},
      [](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data,
         size_t len, bool final) {},
      onScheduleUpdate);

  // server_web.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request)
  //               { request->send(LittleFS, "/style.css", "text/css"); });

  server_web.serveStatic("/js/", LittleFS, "/js/");
  server_web.serveStatic("/css/", LittleFS, "/css/");

  //  /data/ui-constants.json
  /*server_web.on("/ui-constants.json", HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, ui_constants_filename, F("application/json")); });
*/
  server_web.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, F("/data/favicon.ico"), F("image/x-icon")); });
  server_web.on("/favicon-32x32.png", HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, F("/data/favicon-32x32.png"), F("image/png")); });
  server_web.on("/favicon-16x16.png", HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, F("/data/favicon-16x16.png"), F("image/png")); });

  // refresh test
  server_web.on(wifis_filename, HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, wifis_filename, "text/json"); });

  // TODO: check authentication or relocate potentially sensitive files
  server_web.serveStatic("/data/", LittleFS, "/data/");

  // no authentication
  // server_web.on("/data/templates", HTTP_GET, onWebTemplateGet);

  // debug
  server_web.on("/data/config_in.json", HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, "/data/config_in.json", F("application/json")); });

  // server_web.on("/data/arska-mappings.json", HTTP_GET, [](AsyncWebServerRequest *request)
  //               { request->send(LittleFS, "/data/arska-mappings.json", F("application/json")); });

  // TODO: nämä voisi mennä yhdellä
  server_web.on(variables_filename, HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, variables_filename, F("application/json")); });

  server_web.on(price_data_filename, HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, price_data_filename, F("text/plain")); }); // "/price_data.json", miksi text/plain

  // templates
  server_web.on(template_filename, HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, template_filename, "text/json"); });

  // server_web.on("/ui.html", HTTP_GET, [](AsyncWebServerRequest *request)
  //               { request->send(LittleFS, "/ui.html", "text/html"); });

  server_web.onNotFound(notFound);
  // TODO: remove force create
  // generate_ui_constants(true); // generate ui constant json if needed
  server_web.begin();

  if (wifi_in_setup_mode)
    return; // no more setting, just wait for new SSID/password and then restarts

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
      log_msg(MSG_TYPE_FATAL, PSTR("Restarting with the new WiFI settings."), true);

      delay(2000);
      ESP.restart();
    }
  }
  if (todo_in_loop_write_to_eeprom)
  {
    todo_in_loop_write_to_eeprom = false;
    writeToEEPROM();
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
    delay(1000);
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

#ifdef MDNS_ENABLED
  if (todo_in_loop_discover_devices) // TODO: check that stable enough
  {
    todo_in_loop_discover_devices = false;
    discover_devices();
  }
#endif

#ifdef OTA_DOWNLOAD_ENABLED
  if (todo_in_loop_get_releases)
  {
    todo_in_loop_get_releases = false;
    if (update_releases.length() < 10)
    {
      get_releases();
    }
  }
#endif

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

    if (config_resetted)
      log_msg(MSG_TYPE_WARN, PSTR("Version upgrade caused configuration reset. Started processing."), true);
    else
      log_msg(MSG_TYPE_INFO, PSTR("Started processing."), true);

    set_time_settings(); // set tz info

    // experimental ota update, should be when wifi is up and  time is up-to-date
    if (now < 1664446053)
    { // skip it if the code is accidentally not commented out
      Serial.println("Testing firmware update");
      todo_in_loop_update_firmware_partition = true;
    }

    ch_counters.init();
    next_query_price_data = now;
    next_query_fcst_data = now;
  }

#ifdef OTA_UPDATE_ENABLED
  if (todo_in_loop_update_firmware_partition)
  {
    todo_in_loop_update_firmware_partition = false;
    Serial.printf(PSTR("Partition update VERSION_BASE: %s, version_fs_base: %s, update_release_selected: %s\n"), VERSION_BASE, version_fs_base.c_str(), update_release_selected.c_str());

    // experimental, we should check the phase or have two different todo_in variables
    // TODO: check that there is no upload in process at the same time (especially filesystem)
    if (!update_release_selected.equals(VERSION_BASE) && update_release_selected.length() > 0) // update firmware if requested and needed
    {
      Serial.println(F("Starting firmware update."));
      update_firmware_partition(U_FLASH);
    }
    else
    {
      Serial.println(F("Starting filesystem update."));
      update_firmware_partition(U_PART); // update fs if needed
      check_filesystem_version();        // update variables to see if  version is updated
    }
  }
#endif
  // reapply current relay states (if relay parameters are changed)
  if (todo_in_loop_reapply_relay_states)
  {
    Serial.println("queue task todo_in_loop_reapply_relay_states");
    todo_in_loop_reapply_relay_states = false;
    for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
    {
      if (relay_state_reapply_required[channel_idx])
      {
        Serial.printf("Reapply relay %d\n", channel_idx);
        apply_relay_state(channel_idx, true);
      }
    }
  }
  // recalculate channel states and set relays, if forced from dashboard
  if (todo_in_loop_set_relays)
  {
    Serial.println("queue task todo_in_loop_set_relays");
    todo_in_loop_set_relays = false;
    update_channel_states();
    set_relays(false);
  }

  // just in case check the wifi and reconnect/restart if needed
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
      log_msg(MSG_TYPE_FATAL, PSTR("Restarting due to wifi error."), true);
      delay(10000);
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
    Serial.printf("\nPeriod changed %ld -> %ld, grid protection delay %d secs\n", previous_period_start, current_period_start, grid_protection_delay_interval);
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
    next_query_price_data = now + (got_external_data_ok ? (1200 + random(0, 300)) : (120 + random(0, 60))); // random, to prevent query peak
    Serial.printf("next_query_price_data: %ld \n", next_query_price_data);
  }

  if (todo_in_loop_update_price_rank_variables)
  {
    todo_in_loop_update_price_rank_variables = false;
    updated_ok = update_price_rank_variables();
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
    Serial.printf(PSTR("%02d:%02d:%02d Reading sensor and meter data... "), tm_struct.tm_hour, tm_struct.tm_min, tm_struct.tm_sec);
    if (s.energy_meter_type != ENERGYM_NONE)
      read_energy_meter();

    if (s.production_meter_type != PRODUCTIONM_NONE)
      read_production_meter();

#ifdef SENSOR_DS18B20_ENABLED
    read_ds18b20_sensors();
#endif
    update_time_based_variables();
    update_meter_based_variables(); // TODO: if period change we could set write influx buffer after this?
    update_price_variables(current_period_start);

    time(&now);
    next_process_ts = max((time_t)(next_process_ts + PROCESS_INTERVAL_SECS), now + (PROCESS_INTERVAL_SECS / 2)); // max is just in case to allow skipping processing, if processing takes too long
    update_channel_states();
    set_relays(true); // grid protection delay active
    flush_noncritical_eeprom_cache();
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
    // experimental history processing
    /*
    net_imports[MAX_HISTORY_PERIODS - 1] = -vars.get_f(VARIABLE_SELLING_ENERGY);
    // roll the history
    for (int h_idx = 0; (h_idx + 1) < MAX_HISTORY_PERIODS; h_idx++)
      net_imports[h_idx] = net_imports[h_idx + 1];
    net_imports[MAX_HISTORY_PERIODS - 1] = 0;
*/
    vars.rotate_period();

    previous_period_start = current_period_start;
    period_changed = false;
  }
  period_changed = false;

#ifdef INVERTER_SMA_MODBUS_ENABLED
  mb.task(); // process modbuss event queue
#endif
  yield();
  delay(50);
}
