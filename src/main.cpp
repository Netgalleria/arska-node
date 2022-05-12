/*
(C) Netgalleria Oy, Olli Rinne, 2021-2022


Resource files (see data subfolder):
- arska.js - web UI Javascript routines
- style.css - web UI styles
- admin_template.html - html template file for admin web UI
- view_template.html - html template file for dashboard UI
- wifi_template.html - html template file for wifi settings

*/

#include <Arduino.h>
#include <math.h> //round

#include <EEPROM.h>
#define EEPROM_CHECK_VALUE 12346

#include <LittleFS.h>

const char compile_date[] = __DATE__ " " __TIME__;

//#include <DNSServer.h> // for captive portal

#ifdef ESP32 // not fully implemented with ESP32
//#pragma message("ESP32 version")
#include <WiFi.h>
#include <AsyncTCP.h>
#include <HTTPClient.h>
#elif defined(ESP8266) // tested only with ESP8266
//#pragma message("ESP8266 version")
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESP8266HTTPClient.h>
#endif

// features enabled
// #define SENSOR_DS18B20_ENABLED
#define QUERY_ARSKA_ENABLED
#define METER_SHELLY3EM_ENABLED
#define INVERTER_FRONIUS_SOLARAPI_ENABLED // can read Fronius inverter solarapi
#define INVERTER_SMA_MODBUS_ENABLED       // can read SMA inverter Modbus TCP
//#define RTC_DS3231_ENABLED

#define TARIFF_VARIABLES_FI // add Finnish tariffs (yösähkö,kausisähkö) to active states

#define OTA_UPDATE_ENABLED

#define eepromaddr 0
#define WATT_EPSILON 50

const char *config_file_name PROGMEM = "/config.json";
const char *wifis_file_name PROGMEM = "/wifis.json";

#define NETTING_PERIOD_MIN 60 // should be 60, later 15

#define STATES_DEVICE_MAX 999 // states belon (incl) are not replicated
#define STATES_LOCAL_MAX 9999



struct variable_st
{
  byte id;
  char code[20];
  byte type;
  long val_l;
};


#include <ESPAsyncWebServer.h>

// https://werner.rothschopf.net/202011_arduino_esp8266_ntp_en.htm
#include <time.h>

time_t now; // this is the epoch
tm tm_struct;

bool processing_states = false; // trying to be "thread-safe", do not give state query http replies while processing

// for timezone https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
char ntp_server[35];
char timezone_info[35]; // read from config.json "CET-1CEST,M3.5.0/02,M10.5.0/03", "EET-2EEST,M3.5.0/3,M10.5.0/4"
char price_area[8];

time_t forced_restart_ts = 0; // if wifi in forced ap-mode restart automatically to reconnect/start
bool backup_ap_mode_enabled = false;

#define FORCED_RESTART_DELAY 600 // If cannot create Wifi connection, goes to AP mode for 600 sec and restarts

/* System goes to  AP mode (config mode creates) if it cannot connect to existing wifi.
In AP mode system is restarted (to retry wifi connection) after a delay.
Call to check_forced_restart checks if it is time to restart or resets delay if reset_counter == true)
*/
void check_forced_restart(bool reset_counter = false)
{
  // TODO:tässä tapauksessa kello ei välttämättä ei kunnossa ellei rtc, käy läpi tapaukset
  if (!backup_ap_mode_enabled) // only valid if backup ap mode (no normal wifi)
    return;

  time_t now;
  time(&now);
  if (reset_counter)
  {
    forced_restart_ts = now + FORCED_RESTART_DELAY;
    Serial.print(F("forced_restart_ts:"));
    Serial.println(forced_restart_ts);
  }
  else if ((forced_restart_ts < now) && ((now - forced_restart_ts) < 7200)) // check that both values are same way synched
  {
    Serial.println(F("check_forced_restart restarting"));
    delay(2000); // EI KAI TOIMI OIKEIN JOS KELLO EI ASETTTU, mutta ei kai asetettu jos ei rtc eikä nettiyhteyttä
    ESP.restart();
  }
}

// DNSServer dnsServer;
AsyncWebServer server_web(80);

// client
#include <WiFiClient.h>
#include <ArduinoJson.h>

WiFiClient client;
// HTTPClient httpRelay;

// Clock functions, supports optional DS3231 RTC
// RTC based on https://werner.rothschopf.net/microcontroller/202112_arduino_esp_ntp_rtc_en.htm

bool rtc_found = false;
/*
    Sets the internal time
    epoch (seconds in GMT)
    microseconds
*/
// Set internal clock from RTC or browser
void setInternalTime(uint64_t epoch = 0, uint32_t us = 0)
{
  struct timeval tv;
  tv.tv_sec = epoch;
  tv.tv_usec = us;
  settimeofday(&tv, NULL);
}
const int force_up_hours[] = {0, 1, 2, 4, 8, 12, 24};

const char *price_data_filename PROGMEM = "/price_data.json";
const char *variables_filename PROGMEM = "/variables.json";
const char *fcst_filename PROGMEM = "/fcst.json";
// const char *fcst_variables PROGMEM = "/variables.json";

const int price_variable_blocks[] = {9, 24};
// const int price_variable_blocks[] = {9};

#define NETTING_PERIOD_MIN 60
#define NETTING_PERIOD_SEC 3600

#define PV_FORECAST_HOURS 24

#define MAX_PRICE_PERIODS 48
#define VARIABLE_LONG_UNKNOWN -2147483648
#define SECONDS_IN_DAY 86400

// kokeillaan globaalina, koska stackin kanssa ongelmia
long prices[MAX_PRICE_PERIODS];
time_t prices_first_period = 0;

// API
const char *host_prices PROGMEM = "transparency.entsoe.eu";

const char *host_fcst PROGMEM = "http://www.bcdcenergia.fi/wp-admin/admin-ajax.php?action=getChartData&loc=Espoo";

// String url = "/api?securityToken=41c76142-eaab-4bc2-9dc4-5215017e4f6b&documentType=A44&In_Domain=10YFI-1--------U&Out_Domain=10YFI-1--------U&processType=A16&outBiddingZone_Domain=10YCZ-CEPS-----N&periodStart=202204200000&periodEnd=202204200100";
String url_base = "/api?documentType=A44&processType=A16";
const char *securityToken = "41c76142-eaab-4bc2-9dc4-5215017e4f6b";
const char *queryInOutDomain = "10YFI-1--------U";
const char *outBiddingZone_Domain = "10YCZ-CEPS-----N";

tm tm_struct_g;
time_t next_price_fetch;
bool run_price_process = false;

// https://transparency.entsoe.eu/api?securityToken=41c76142-eaab-4bc2-9dc4-5215017e4f6b&documentType=A44&In_Domain=10YFI-1--------U&Out_Domain=10YFI-1--------U&processType=A16&outBiddingZone_Domain=10YCZ-CEPS-----N&periodStart=202104200000&periodEnd=202104200100
const int httpsPort = 443;

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
// #define I2CSDA_GPIO 0
// #define I2CSCL_GPIO 12

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

// print time of RTC to Serial, debugging function
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
    Serial.print(F(", temperature:"));
    Serial.println(rtc.getTemperature());
  }
}

// set date/time of external RTC
void setRTC()
{
  Serial.println(F("setRTC --> from internal time"));
  time_t now;          // this are the seconds since Epoch (1970) - seconds GMT
  tm tm;               // the structure tm holds time information in a more convient way
  time(&now);          // read the current time and store to now
  gmtime_r(&now, &tm); // update the structure tm with the current GMT
  rtc.adjust(DateTime(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec));
}

// callback function (registered with settimeofday_cb ) called when ntp time update received, sets RTC
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

// reads time from external RTC and set value to internal time
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

#endif

/*
Variables voisi olla tässä silloin kun tarvitaan eli kun periodi aktiivinen:
- tästä rakenteesta tehdään vertailu käyttäjän antamiin ehtoihin, confitions
- haetaan päivitys externaaliesta ja internaaleista kun uusi periodi,
- extrenaaleja voisi olla laskettu vähän etukäteenkin, jolloin mahdolliset kyselevätkin noodit saisivat jo tiedot etukäteen ettei tarvitse samalla sekunnilla kysellä
-  vai olisko kuitenkin selkeämpi, että valmiiksi laskettu vain sille tunnille, pitää vaan hoitaa, että jos "ala"noodi kysyelee liian aikaisin, niin annetaan lyhyt expiroitumisaika,
    jotta ei jää aina tuntia jälkeen. Tähän voisi tehdä alanoodeille myös jonkin viiveen, että ei kysy aina vanhoja tietoja
- externaalit muodostetaan valmiiksi haetuista json-filestä (prices, fcst) aina kun periodi vaihtuu
*/
/*
jos ei löydy mitään operaattoria niin oletetaan booleaniksi
"=" gt=false eq=true reverse=false.
">" gt=true eq=false reverse=false
"<"  gt=true eq=false reverse=true
"!",  reverse=true isboolean=true jos löytyy ekana "!" niin reverse boolean

">=" gt=true eq=true reverse=false
"<="  gt=true eq=true reverse=true
"!=" gt=false eq=true reverse=true




[no operator] reverse=false isboolean=true
*/
struct oper_st
{
  byte id;
  char code[4];
  bool gt;
  bool eq;
  bool reverse;
  bool boolean_only;
};

// shortest first,
#define OPER_COUNT 8
const oper_st opers[OPER_COUNT] = {{0, "=", false, true, false, false}, {1, ">", true, false, false, false}, {2, "<", true, false, true, false}, {3, ">=", true, true, false, false}, {4, "<=", true, true, true, false}, {5, "<>", false, true, true, false}, {6, "is", false, false, false, true}, {7, "not", false, false, true, true}};

/*constant_type, variable_type
long val_l
type = 0 default long
type = 1  10**1 stored to long  , ie. 1.5 -> 15
... 10
22 2 characters string
24 4 characters string stored to long, e.g. hhmm mmdd
50 boolean, no reverse allowed
51 boolean, reverse allowed
*/

struct statement_st
{
  int variable_id;
  byte oper_id;
  byte constant_type;
  long const_val;
};

// do not change variable id:s (will broke statements)
#define VARIABLE_COUNT 17

#define VARIABLE_PRICE 0
#define VARIABLE_PRICERANK_9 1
#define VARIABLE_PRICERANK_24 2
#define VARIABLE_PVFORECAST_SUM24 20
#define VARIABLE_PVFORECAST_VALUE24 21
#define VARIABLE_PVFORECAST_AVGPRICE24 22
#define VARIABLE_EXTRA_PRODUCTION 100
#define VARIABLE_PRODUCTION_POWER 101
#define VARIABLE_SELLING_POWER 102
#define VARIABLE_MM 110
#define VARIABLE_MMDD 111
#define VARIABLE_WDAY 112
#define VARIABLE_HH 115
#define VARIABLE_HHMM 116
#define VARIABLE_DAYENERGY_FI 130
#define VARIABLE_WINTERDAY_FI 140
#define VARIABLE_SENSOR_1 201
#define VARIABLE_LOCALTIME_TS 1001
class Variables
{
public:
  Variables()
  {
    for (int variable_idx = 0; variable_idx < VARIABLE_COUNT; variable_idx++)
      variables[variable_idx].val_l = VARIABLE_LONG_UNKNOWN;
  }
  void set(int id, long value_l);
  void set(int id, float val_f);
  //void set(int id, time_t val_time);
  // void set(int id, bool val_b);
  long get_l(int id);
  float get_f(int id);

  bool is_statement_true(statement_st *statement, bool default_value = false);
  int get_variable_by_id(int id, variable_st *variable);
  void get_variable_by_idx(int idx, variable_st *variable);
  long float_to_internal_l(int id, float val_float);
  // int to_str(int id, long val_l, char *strbuff);
  int to_str(int id, char *strbuff, bool use_overwrite_val = false, long overwrite_val = 0);
  int get_variable_count() { return VARIABLE_COUNT; };

private:
  // 24 4 characters string stored to long, e.g. hhmm mmdd
  variable_st variables[VARIABLE_COUNT] = {{VARIABLE_PRICE, "price", 1, false}, {VARIABLE_PRICERANK_9, "price rank 9h", 0}, {VARIABLE_PRICERANK_24, "price rank 24h", 0}, {VARIABLE_PVFORECAST_SUM24,"pv forecast 24 h",1 },{VARIABLE_PVFORECAST_VALUE24,"pv value 24 h",1 },{VARIABLE_PVFORECAST_AVGPRICE24,"pv price avg 24 h",1 },{VARIABLE_EXTRA_PRODUCTION, "extra production", 51}, {VARIABLE_PRODUCTION_POWER, "production (per) W", 0}, {VARIABLE_SELLING_POWER, "selling (per) W", 0}, {VARIABLE_MM, "mm, month", 22}, {VARIABLE_MMDD, "mmdd", 24}, {VARIABLE_WDAY, "weekday (1-7)", 0}, {VARIABLE_HH, "hh, hour", 22}, {VARIABLE_HHMM, "hhmm", 24}, {VARIABLE_DAYENERGY_FI, "day", 51}, {VARIABLE_WINTERDAY_FI, "winterday", 51}, {VARIABLE_SENSOR_1, "sensor 1", 1}};





  int get_variable_index(int id);
};

void Variables::set(int id, long val_l)
{
  // Serial.printf("Setting variable %s to %ld (long)\n", code, val_l);
  int idx = get_variable_index(id);
  Serial.printf("Variables:set index %d, new value = %ld\n", idx, val_l);
  if (idx != -1)
  {
    variables[idx].val_l = val_l;
  }
}

void Variables::set(int id, float val_f)
{
  this->set(id, this->float_to_internal_l(id, val_f));
}
/*
void Variables::set(int id, time_t val_time)
{
  this->set(id, val_time+LONG_MIN);
} */
/*
void Variables::set(int id, bool val_b)
{
  //  this->set(id, this->float_to_internal_l(id, val_b?1L:0L));
  Serial.printf("Variables::set - boolean %d, %d \n", val_b ? "true" : "false");
  if (val_b)
    this->set(id, 1L);
    else
      this->set(id, 0L);
} */

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
  if (idx != -1)
  {
    if (var.type < 10)
    {
      return (long)int(val_float * pow(10, var.type) + 0.5);
    }
    else if ((var.type == 22) || (var.type == 24))
    {
      return (long)int(val_float + 0.5);
    }
  }
  return -1;
}

int Variables::to_str(int id, char *strbuff, bool use_overwrite_val, long overwrite_val)
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
    if (val_l == VARIABLE_LONG_UNKNOWN) {
      strcpy(strbuff, "null");
      return -1;
    }
    if (var.type < 10)
    {
      float val_f = val_l / pow(10, var.type);
      dtostrf(val_f, 1, var.type, strbuff);
      return strlen(strbuff);
    }
    else if (var.type == 24)
    { // 4 char number, 0 padding, e.g. hhmm
      sprintf(strbuff, "\"%04ld\"", val_l);
      return strlen(strbuff);
    }
    else if (var.type == 22)
    { // 2 char number, 0 padding, e.g. hh
      sprintf(strbuff, "\"%02ld\"", val_l);
      return strlen(strbuff);
    }
    else if (var.type == 50 || var.type == 51)
    {
      sprintf(strbuff, "%s", (var.val_l == 1) ? "true" : "false");
      Serial.printf("Logical variable, internal value = %ld -> %s\n", var.val_l, strbuff);
      return strlen(strbuff);
    }
  }
  strcpy(strbuff, "null");
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
/*
byte Variables::get_oper_idx(const char *code)
  {
    for (int i = 0; i < OPER_COUNT; i++)
      if (strcmp(code, opers[i].code) == 0)
        return i;
    return -1;
  }
*/
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

void Variables::get_variable_by_idx(int idx, variable_st *variable)
{
  memcpy(variable, &variables[idx], sizeof(variable_st));
}

const char *statement_separator PROGMEM = ";";
bool Variables::is_statement_true(statement_st *statement, bool default_value)

{
  // pitäisikö olla jo tallennettu parsittu
  // try to match with opers
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
      Serial.printf("Matching with oper %s\n", opers[i].code);
    }
  }
  bool result = false;

  if (oper.eq && var.val_l == statement->const_val)
    result = true;

  if (oper.gt && var.val_l > statement->const_val)
    result = true;

  if (oper.boolean_only)
    result = (var.val_l == 1);

  // finally optional reverse
  if (oper.reverse)
    result = !result;

  Serial.printf("Statement: %ld  %s  %ld  results %s\n", var.val_l, oper.code, statement->const_val, result ? "true" : "false");

  return result;
}

Variables vars;

// Non-volatile memory https://github.com/CuriousTech/ESP-HVAC/blob/master/Arduino/eeMem.cpp
#ifdef INVERTER_SMA_MODBUS_ENABLED
#include <ModbusIP_ESP8266.h>
// Modbus registry offsets
#define SMA_DAYENERGY_OFFSET 30535
#define SMA_TOTALENERGY_OFFSET 30529
#define SMA_POWER_OFFSET 30775
#endif

#ifdef OTA_UPDATE_ENABLED
unsigned long server_ota_started;
#include <AsyncElegantOTA.h>
AsyncWebServer server_OTA(80);
#endif

#ifdef SENSOR_DS18B20_ENABLED
bool sensor_ds18b20_enabled = true;
// see: https://randomnerdtutorials.com/esp8266-ds18b20-temperature-sensor-web-server-with-arduino-ide/
#include <OneWire.h>
#include <DallasTemperature.h> // tätä ei ehkä välttämättä tarvita, jos käyttäisi onewire.h:n rutineeja

// moved to platform.ini
//#define ONEWIRE_DATA_GPIO 13
//#define ONEWIRE_VOLTAGE_GPIO 14

time_t temperature_updated = 0;

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(ONEWIRE_DATA_GPIO);

// Pass our oneWire reference to Dallas Temperature sensor
DallasTemperature sensors(&oneWire);
float ds18B20_temp_c;
#else
bool sensor_ds18b20_enabled = false;
#endif

#ifdef QUERY_ARSKA_ENABLED
const char *pg_state_cache_filename PROGMEM = "/pg_state_cache.json";
#endif

#define USE_POWER_TO_ESTIMATE_ENERGY_SECS 120 // use power measurement to estimate

unsigned process_interval_s = 60;                               // process interval
unsigned long sensor_last_refresh = -process_interval_s * 1000; // start reading as soon as you get to first loop

time_t recording_period_start = 0; // first period: boot time, later period starts
time_t current_period_start = 0;
time_t previous_period_start = 0;
time_t energym_read_last = 0;
time_t started = 0;
bool period_changed = true;
bool restart_required = false;
bool scan_and_store_wifis_requested = false;
bool set_relay_requested = false;
// data strcuture limits
//#define CHANNEL_COUNT 2  // moved to platformio.ini
#define CHANNEL_TYPES 3
#define CH_TYPE_UNDEFINED1 0
#define CH_TYPE_GPIO_ONOFF 1
#define CH_TYPE_SHELLY_ONOFF 2
#define CH_TYPE_DISABLED 255 // RFU, we could have disabled, but allocated channels (binary )

// channels type string for admin UI
const char *channel_type_strings[] PROGMEM = {
    "undefined",
    "GPIO",
    "Shelly",
};

// #define CHANNEL_CONDITIONS_MAX 3 //platformio.ini
#define CHANNEL_STATES_MAX 10

#define RULE_STATEMENTS_MAX 5

#define ACTIVE_STATES_MAX 20

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
  uint16_t upstates[CHANNEL_STATES_MAX];
  statement_st statements[RULE_STATEMENTS_MAX];
  float target_val;
  bool switch_on;
  bool condition_active; // for showing if the condition is currently active, for tracing
} condition_struct;

// Channel stucture, elements of channel array in setting, stored in non-volatile memory
typedef struct
{
  condition_struct conditions[CHANNEL_CONDITIONS_MAX];
  char id_str[MAX_CH_ID_STR_LENGTH];
  uint8_t gpio;
  bool is_up;
  bool wanna_be_up;
  byte type;
  time_t uptime_minimum;
  time_t toggle_last;
  time_t force_up_until;
  byte config_mode; // 0-rule, 1-template
  int template_id;
} channel_struct;

// TODO: add fixed ip, subnet?
// Setting stucture, stored in non-volatile memory
typedef struct
{
  int check_value;
  bool sta_mode; // removed, always, excect backup?
  char wifi_ssid[MAX_ID_STR_LENGTH];
  char wifi_password[MAX_ID_STR_LENGTH];
  char http_username[MAX_ID_STR_LENGTH];
  char http_password[MAX_ID_STR_LENGTH];
  channel_struct ch[CHANNEL_COUNT];
#ifdef QUERY_ARSKA_ENABLED
  char pg_host[MAX_URL_STR_LENGTH];
  uint16_t pg_cache_age; // not anymore in use, replaced by "expires" from the server, remove
  char pg_api_key[37];
#endif
#if defined(INVERTER_FRONIUS_SOLARAPI_ENABLED) || defined(INVERTER_SMA_MODBUS_ENABLED)
  uint32_t baseload; // production above baseload is "free" to use/store
#endif
#ifdef OTA_UPDATE_ENABLED
  bool next_boot_ota_update;
#endif
  byte energy_meter_type;
  char energy_meter_host[MAX_URL_STR_LENGTH];
  unsigned int energy_meter_port;
  byte energy_meter_id;
  float lat;
  float lon;
  char forecast_loc[MAX_ID_STR_LENGTH];
  uint8_t group_id;      // default 0
  uint8_t node_priority; // 0-100 for master, 255-leaf
} settings_struct;

// this stores settings also to eeprom
settings_struct s;

// uint16_t active_states[ACTIVE_STATES_MAX]; // current active states

// parse char array to uint16_t array (e.g. states, ip address)
// note: current version alter str_in, so use copy in calls if original still needed
// TÄMÄ KAATUU ESP32:ssa?
void str_to_uint_array(const char *str_in, uint16_t array_out[CHANNEL_STATES_MAX], const char *separator)
{
  char *ptr = strtok((char *)str_in, separator); // breaks string str into a series of tokens using the delimiter delim.
  byte i = 0;
  for (int ch_state_idx = 0; ch_state_idx < CHANNEL_STATES_MAX; ch_state_idx++)
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
    if (i == CHANNEL_STATES_MAX)
    {
      break;
    }
  }
  return;
}
// returns true there is a valid cache file (exist and  not expired)
bool is_cache_file_valid(const char *cache_file_name)
{
  if (!LittleFS.exists(cache_file_name))
  {
    Serial.println(F("No cache file. "));
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

  time(&now);
  unsigned long expires = doc_ts["expires"];
  return expires > now;
}

//
void scan_and_store_wifis()
{
  int n = WiFi.scanNetworks();
  Serial.println("Wifi scan ended");

  LittleFS.remove(wifis_file_name);                      // Delete existing file, otherwise the configuration is appended to the file
  File wifis_file = LittleFS.open(wifis_file_name, "w"); // Open file for writing
  wifis_file.printf("wifis = '[");

  int good_wifi_count = 0;

  for (int i = 0; i < n; ++i)
  {
    if (WiFi.RSSI(i) < -85) // too weak signals not listed, could be actually -75
      continue;
    good_wifi_count++;
    wifis_file.print("{\"id\":\"");
    wifis_file.print(WiFi.SSID(i));
    wifis_file.print("\",\"rssi\":");
    wifis_file.print(WiFi.RSSI(i));
    wifis_file.print("},");
  }
  wifis_file.printf("{}]';");
  wifis_file.close();
}

// Testing before saving if Wifi settings are ok
bool test_wifi_settings(char *wifi_ssid, char *wifi_password)
{
  WiFi.mode(WIFI_STA);
  WiFi.hostname("ArskaNode");
  WiFi.begin(wifi_ssid, wifi_password);
  bool success = (WiFi.waitForConnectResult() == WL_CONNECTED);
  WiFi.disconnect();
  return success;
}
#define CONFIG_JSON_SIZE_MAX 1600
// utility for read_config_file
bool copy_doc_str(StaticJsonDocument<CONFIG_JSON_SIZE_MAX> &doc, char *key, char *tostr)
{
  if (doc.containsKey(key))
  {
    strcpy(tostr, doc[key]);
    return true;
  }
  return false;
}

// read config variables from config.json file
bool read_config_file(bool read_all_settings)
{
  Serial.println(F("Reading config file"));
  if (!LittleFS.exists(config_file_name))
  {
    Serial.println(F("No config file. "));
    return false;
  }
  File file = LittleFS.open(config_file_name, "r");
  if (!file)
  { // failed to open the file, retrn empty result
    Serial.println(F("Failed to open config file. "));
    return false;
  }

  StaticJsonDocument<CONFIG_JSON_SIZE_MAX> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error)
  {
    Serial.print(F("deserializeJson() config file failed: "));
    return false;
  }

  // first read global settings (not stored to settings structure s)
  copy_doc_str(doc, (char *)"timezone_info", timezone_info);
  copy_doc_str(doc, (char *)"ntp_server", ntp_server);
  copy_doc_str(doc, (char *)"price_area", price_area);

  if (!read_all_settings)
  { // read only basic config
    Serial.println(F("Got basic config. Returning."));
    return true;
  }

  strcpy(s.http_username, "admin"); // use fixed name

  copy_doc_str(doc, (char *)"http_password", s.http_password);

  // do not copy non-working wifi settings
  if (doc.containsKey("wifi_ssid") && doc.containsKey("wifi_password"))
  {
    char wifi_ssid[MAX_ID_STR_LENGTH];
    char wifi_password[MAX_ID_STR_LENGTH];
    copy_doc_str(doc, (char *)"wifi_ssid", wifi_ssid);
    copy_doc_str(doc, (char *)"wifi_password", wifi_password);

    if (test_wifi_settings(wifi_ssid, wifi_password))
    {
      strcpy(s.wifi_ssid, wifi_ssid);
      strcpy(s.wifi_password, wifi_password);
      Serial.println("Getting wifi settings from config file.");
    }
    else
      Serial.println("Skipping invalid wifi settings from config file.");
  }

#ifdef QUERY_ARSKA_ENABLED
  copy_doc_str(doc, (char *)"pg_host", s.pg_host);
  copy_doc_str(doc, (char *)"pg_api_key", s.pg_api_key);
  /*if (doc.containsKey("pg_cache_age"))
    s.pg_cache_age = doc["pg_cache_age"];*/
#endif

#if defined(INVERTER_FRONIUS_SOLARAPI_ENABLED) || defined(INVERTER_SMA_MODBUS_ENABLED)
  if (doc.containsKey("baseload"))
    s.baseload = doc["baseload"];
#endif

  if (doc.containsKey("energy_meter_type"))
    s.energy_meter_type = doc["energy_meter_type"];
  copy_doc_str(doc, (char *)"energy_meter_host", s.energy_meter_host);
  if (doc.containsKey("energy_meter_port"))
    s.energy_meter_port = doc["energy_meter_port"];
  if (doc.containsKey("energy_meter_id"))
    s.energy_meter_id = doc["energy_meter_id"];

  if (doc.containsKey("lat"))
    s.lat = doc["lat"];
  if (doc.containsKey("lon"))
    s.lon = doc["lon"];
  copy_doc_str(doc, (char *)"forecast_loc", s.forecast_loc);
  if (doc.containsKey("group_id"))
    s.group_id = doc["group_id"];
  if (doc.containsKey("node_priority"))
    s.node_priority = doc["node_priority"];

  return true;
}

// reads sessing from eeprom
void readFromEEPROM()
{
  EEPROM.get(eepromaddr, s);
  Serial.println(F("readFromEEPROM: Reading settings from eeprom."));
}

// writes settigns to eeprom
void writeToEEPROM()
{
  EEPROM.put(eepromaddr, s); // write data to array in ram
  EEPROM.commit();
  Serial.print(F("writeToEEPROM: Writing settings to eeprom."));
}

// from https://github.com/me-no-dev/ESPAsyncWebServer/blob/master/examples/CaptivePortal/CaptivePortal.ino
/*class CaptiveRequestHandler : public AsyncWebHandler {
public:
  CaptiveRequestHandler() {}
  virtual ~CaptiveRequestHandler() {}

  bool canHandle(AsyncWebServerRequest *request){
    //request->addInterestingHeader("ANY");
    return true;
  }

void handleRequest(AsyncWebServerRequest *request) {
    AsyncResponseStream *response = request->beginResponseStream("text/html");
    response->print("<!DOCTYPE html><html><head><title>Captive Portal</title></head><body>");
    response->print("<p>This is out captive portal front page.</p>");
    response->printf("<p>You were trying to reach: http://%s%s</p>", request->host().c_str(), request->url().c_str());
    response->printf("<p>Try opening  <a href='http://%s'>this link %s</a> instead</p>", WiFi.softAPIP().toString().c_str(), WiFi.softAPIP().toString().c_str());
    response->print("</body></html>");
    request->send(response);
  }
};
*/

void notFound(AsyncWebServerRequest *request)
{
  request->send(404, "text/plain", "Not found");
}

// Utility function to make http request, stores result to a cache file if defined
String httpGETRequest(const char *url, const char *cache_file_name)
{
  WiFiClient client;

  HTTPClient http;
  http.setReuse(false);

  http.begin(client, url); //  IP address with path or Domain name with URL path
  // delay(1000);
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
        Serial.println(F("Failed to create file:"));
        Serial.print(cache_file_name);
        Serial.print(", ");
        Serial.println(cache_file);
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
    Serial.print(F("Error, httpResponseCode: "));
    Serial.println(httpResponseCode);
    http.end();
    return String("");
  }
  // Free resources
  http.end();
  return payload;
}

#ifdef SENSOR_DS18B20_ENABLED

// Read temperature values from DS18B20 senson
bool read_sensor_ds18B20()
{
  sensors.requestTemperatures();
  float value_read = sensors.getTempCByIndex(0);
  if (value_read < -126) // Trying to reset sensor
  {
    digitalWrite(ONEWIRE_VOLTAGE_GPIO, LOW);
    delay(5000);
    digitalWrite(ONEWIRE_VOLTAGE_GPIO, HIGH);
    delay(5000);
    value_read = sensors.getTempCByIndex(0);
    Serial.printf("Temperature after sensor reset: %f \n", ds18B20_temp_c);
  }
  if (value_read > 0.1) // TODO: check why it fails so often
  {                     // use old value if  cannot read new
    ds18B20_temp_c = value_read;
    vars.set(VARIABLE_SENSOR_1, ds18B20_temp_c);
    time(&temperature_updated);
    return true;
  }
  else
    return false;
}

#endif

#ifdef METER_SHELLY3EM_ENABLED
unsigned last_period = 0;
long last_period_last_ts = 0;
long meter_read_ts = 0;
float energyin_prev = 0;
float energyout_prev = 0;
float energyin = 0;
float energyout = 0;

// return energy/power values read from Shelly
void get_values_shelly3m(float &netEnergyInPeriod, float &netPowerInPeriod)
{
  netEnergyInPeriod = (energyin - energyout - energyin_prev + energyout_prev);
  if ((meter_read_ts - last_period_last_ts) != 0)
  {
    netPowerInPeriod = round(netEnergyInPeriod * 3600.0 / ((meter_read_ts - last_period_last_ts)));
  }
  else
  {
    netPowerInPeriod = 0;
  }
}

// reads grid export/import from Shelly 3EM
bool read_meter_shelly3em()
{
  if (strlen(s.energy_meter_host) == 0)
    return false;
  DynamicJsonDocument doc(2048);

  String url = "http://" + String(s.energy_meter_host) + "/status";
  DeserializationError error = deserializeJson(doc, httpGETRequest(url.c_str(), ""));
  Serial.println(url);

  if (error)
  {
    Serial.print(F("Shelly meter deserializeJson() failed: "));
    Serial.println(error.f_str());
    return false;
  }

  meter_read_ts = doc["unixtime"];
  unsigned now_period = int(meter_read_ts / (NETTING_PERIOD_MIN * 60));

  if (last_period != now_period and last_period > 0)
  { // new period
    Serial.println(F("Shelly - new period"));
    last_period = now_period; // riittäiskö ..._ts muutt
    // from previous call
    last_period_last_ts = meter_read_ts;
    energyin_prev = energyin;
    energyout_prev = energyout;
  }

  float power_tot = 0;
  int idx = 0;
  float power[3];
  energyin = 0;
  energyout = 0;
  for (JsonObject emeter : doc["emeters"].as<JsonArray>())
  {
    power[idx] = (float)emeter["power"];
    power_tot += power[idx];
    // float current = emeter["current"];
    //  is_valid = emeter["is_valid"];
    if (emeter["is_valid"])
    {
      energyin += (float)emeter["total"];
      energyout += (float)emeter["total_returned"];
    }
    idx++;
  }
  // first query since boot
  if (last_period == 0)
  {
    last_period = now_period - 1;
    last_period_last_ts = meter_read_ts - process_interval_s; // estimate
    energyin_prev = energyin;
    energyout_prev = energyout;
  }
  return true;
}
#endif

#ifdef INVERTER_FRONIUS_SOLARAPI_ENABLED

// Reads production data from Fronius invertes (http/json Solar API)
bool read_inverter_fronius_data(long int &total_energy, long int &current_power)
{
  //  globals updated: inverter_total_period_init
  if (strlen(s.energy_meter_host) == 0)
    return false;

  time(&now);
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

ModbusIP mb; // ModbusIP object
#define REG_COUNT 2
uint16_t buf[REG_COUNT];
uint16_t trans;

// callback for ModBus, curretnly just debugging
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

// gets ModBus Hreg value
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
// reads production data from SMA inverted (ModBus TCP)
bool read_inverter_sma_data(long int &total_energy, long int &current_power)
{
  uint16_t ip_octets[CHANNEL_STATES_MAX];
  char host_ip[16];
  strcpy(host_ip, s.energy_meter_host); // must be locally allocated
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

// read production data from inverters, calls inverter specific functions
void read_inverter(bool period_changed)
{
  // global: recording_period_start
  // 4 globals updated: inverter_total_period_init, inverter_total_period_init_ok, energy_produced_period, power_produced_period_avg
  long int total_energy = 0;
  long int current_power = 0;

  bool readOk = false;
  if (s.energy_meter_type == ENERGYM_FRONIUS_SOLAR)
  {
    readOk = read_inverter_fronius_data(total_energy, current_power);

    if (((long)inverter_total_period_init > total_energy) && readOk)
    {
      inverter_total_period_init = 0; // day have changed probably, reset counter, we get day totals from Fronius
      inverter_total_period_init_ok = true;
    }
  }
  else if (s.energy_meter_type == ENERGYM_SMA_MODBUS_TCP)
  {
    readOk = read_inverter_sma_data(total_energy, current_power);
  }

  if (readOk)
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

  long int time_since_recording_period_start = now - recording_period_start;
  if (time_since_recording_period_start > USE_POWER_TO_ESTIMATE_ENERGY_SECS) // in the beginning of period use current power to estimate energy generated
    power_produced_period_avg = energy_produced_period * 3600 / time_since_recording_period_start;
  else
    power_produced_period_avg = current_power;

  Serial.printf("energy_produced_period: %ld , time_since_recording_period_start: %ld , power_produced_period_avg: %ld , current_power:  %ld\n", energy_produced_period, time_since_recording_period_start, power_produced_period_avg, current_power);

} // read_inverter

void update_internal_variables()
{
  time(&now);
  localtime_r(&now, &tm_struct);

  //time_t now_suntime = now + s.lon * 240;
  //byte sun_hour = int((now_suntime % (3600 * 24)) / 3600);

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

#ifdef METER_SHELLY3EM_ENABLED
  // grid energy meter enabled
  if (s.energy_meter_type == ENERGYM_SHELLY3EM)
  {
  /*  float net_energy_out = (-energyin + energyout + energyin_prev - energyout_prev);
    vars.set(VARIABLE_EXTRA_PRODUCTION, (long)(net_energy_out > 0) ? 1L : 0L);
    vars.set(VARIABLE_SELLING_POWER, (long)(net_energy_out));
*/
    float netEnergyInPeriod;
    float netPowerInPeriod;
    get_values_shelly3m(netEnergyInPeriod, netPowerInPeriod);
    vars.set(VARIABLE_EXTRA_PRODUCTION, (long)(netEnergyInPeriod <0 ) ? 1L : 0L);
    vars.set(VARIABLE_SELLING_POWER, (long)(-netPowerInPeriod));
  }
#endif

#if defined(INVERTER_FRONIUS_SOLARAPI_ENABLED) || defined(INVERTER_SMA_MODBUS_ENABLED)
  // TODO: tsekkaa miksi joskus nousee ylös lyhyeksi aikaa vaikkei pitäisi - johtuu kai siitä että fronius sammuu välillä illalla, laita kuntoon...
  if ((s.energy_meter_type == ENERGYM_FRONIUS_SOLAR) || (s.energy_meter_type == ENERGYM_SMA_MODBUS_TCP)) {
    vars.set(VARIABLE_EXTRA_PRODUCTION, (long)(power_produced_period_avg > (s.baseload + WATT_EPSILON)) ? 1L : 0L);
    vars.set(VARIABLE_PRODUCTION_POWER, (long)(power_produced_period_avg));
  }
#endif
}


long get_price_for_time(time_t ts) {
  // returns VARIABLE_LONG_UNKNOWN if unavailable
  // use global prices, prices_first_period
  int price_idx = (int)(ts - prices_first_period) / (NETTING_PERIOD_MIN * 60);
  if (price_idx<0 || price_idx>=MAX_PRICE_PERIODS) {
    Serial.printf("price_idx: %i , prices_first_period: ", price_idx);
    Serial.println(prices_first_period);
    return VARIABLE_LONG_UNKNOWN;
  }
    
  else {
    return prices[price_idx];
  }
}

// stub...
void refresh_variables(time_t current_period_start)
{
  Serial.print(F(" refresh_variables "));
  Serial.print(F("  current_period_start: "));
  Serial.println(current_period_start);

  StaticJsonDocument<16> filter;
  char start_str[11];
  itoa(current_period_start, start_str, 10);
  filter[(const char *)start_str] = true;

  StaticJsonDocument<300> doc;
  DeserializationError error;

  // TODO: what happens if cache is expired and no connection to state server

  if (is_cache_file_valid(variables_filename))
  {
    Serial.println(F("Using cached data"));
    File cache_file = LittleFS.open(variables_filename, "r");
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

  //***

  // JsonArray state_list = doc[start_str];
  JsonObject variable_list = doc[start_str];
  Serial.print("p:");

  float price = (float)variable_list["p"];
  // vars.set(0, (long)(variable_list["p"]));
  vars.set(VARIABLE_PRICE, (long)(price + 0.5));

  vars.set(VARIABLE_PRICERANK_9, (long)variable_list["pr9"]);
  vars.set(VARIABLE_PRICERANK_24, (long)variable_list["pr24"]);

  Serial.print("p(float):");
  Serial.println(vars.get_f(0));
}

String getElementValue(String outerXML)
{
  int s1 = outerXML.indexOf(">", 0);
  int s2 = outerXML.substring(s1 + 1).indexOf("<");
  return outerXML.substring(s1 + 1, s1 + s2 + 1);
}
time_t ElementToUTCts(String elem)
{
  String str_val = getElementValue(elem);
  return getTimestamp(str_val.substring(0, 4).toInt(), str_val.substring(5, 7).toInt(), str_val.substring(8, 10).toInt(), str_val.substring(11, 13).toInt(), str_val.substring(14, 16).toInt(), 0);
}

bool getBCDCForecast()
{
  DynamicJsonDocument doc(3072);
  Serial.println("getBCDCForecast start");

  String query_data_raw = "action=getChartData&loc=Espoo";

  //    query_data_raw = 'action=getChartData&loc=' + location

  WiFiClient client;

  HTTPClient client_http;
  client_http.setReuse(false);
  client_http.useHTTP10(true); // for json input
  // Your Domain name with URL path or IP address with path
  client_http.begin(client, host_fcst);
  Serial.printf("host_fcst: %s\n", host_fcst);

  if (WiFi.status() == WL_CONNECTED)
    Serial.println("WiFi is connected.");

  // Specify content-type header
  client_http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  client_http.setUserAgent("ArskaNodeESP8266");

  // Send HTTP POST request
  int httpResponseCode = client_http.POST(query_data_raw);
  
/*
  String resp = client_http.getString();
  Serial.println(resp);
  return false;///TESTI
  
  DeserializationError error = deserializeJson(doc, resp);
  */

  Serial.printf("doc.capacity(): %d\n", doc.capacity());
  DeserializationError error = deserializeJson(doc, client_http.getStream());
  if (error)
  {
    Serial.print(F("deserializeJson() failed: "));
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

  for (JsonObject pvenergy_item : doc["pvenergy"].as<JsonArray>())
  {
    //TODO:FIX DST
 // bcdc antaa timestampin eet:ssä, ei utc:ssä; TODO: localize DST
  
    pvenergy_item_time = pvenergy_item["time"];
    pvenergy_time = (pvenergy_item_time/1000)-(3 * 3600);


    Serial.print(pvenergy_time);

    if (first_ts == 0)
      first_ts = pvenergy_time;

    if (j < PV_FORECAST_HOURS)
      pv_fcst[j] = pvenergy_item["value"];

    Serial.print("value:");
    Serial.println((float)pvenergy_item["value"]);
    pv_fcst_hour = (float)pvenergy_item["value"];

    sum_pv_fcst += pv_fcst_hour;
    price = get_price_for_time(pvenergy_time);

    if (price != VARIABLE_LONG_UNKNOWN)
    {
      sum_pv_fcst_with_price += (float)pv_fcst_hour;
      pv_value_hour = price * pv_fcst_hour / 1000;
      pv_value += pv_value_hour;
      Serial.printf("j: %d, price: %ld,  sum_pv_fcst_with_price: %f , pv_value_hour: %f, pv_value: %f\n", j, price, sum_pv_fcst_with_price, pv_value_hour, pv_value);
      }

      j++;
  }
    Serial.printf("avg solar price: %f\n",pv_value/sum_pv_fcst_with_price);

    vars.set(VARIABLE_PVFORECAST_SUM24, (float)sum_pv_fcst);
    vars.set(VARIABLE_PVFORECAST_VALUE24, (float)(pv_value));
    vars.set(VARIABLE_PVFORECAST_AVGPRICE24, (float)(pv_value / sum_pv_fcst_with_price));
    doc.clear();

    JsonArray pv_fcst_a = doc.createNestedArray("pv_fcst");

    for (int i = 0; i < PV_FORECAST_HOURS; i++)
    {
      pv_fcst_a.add(pv_fcst[i]);
  }

  time_t now;
  time(&now);
  doc["first_period"] = first_ts;
  doc["resolution_m"] = 3600;
  doc["ts"] = now;
  doc["expires"] = now + 3600; // time-to-live of the result, under construction, TODO: set to parameters

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

  return false;
}


int get_spot_sliding_window_rank(time_t time, int window_duration_hours, int time_price_idx, long prices[]) //[MAX_PRICE_PERIODS]
{
  // Serial.printf("get_spot_sliding_window_rank time: %lld, window_duration_hours: %d, time_price_idx: %d\n", time, window_duration_hours, time_price_idx);

  // get entries from now to requested duration in the future,
  // if not enough future exists, include periods from history to get full window size

  int window_end_excl_idx = min(MAX_PRICE_PERIODS, time_price_idx + window_duration_hours);
  // Serial.printf("window_end_excl_idx: %d min (%d, %d ) ", window_end_excl_idx, MAX_PRICE_PERIODS, time_price_idx + window_duration_hours);
  int window_start_incl_idx = window_end_excl_idx - window_duration_hours;

  int rank = 1;
  // Serial.printf("price_idx: %d -> %d) ", window_start_incl_idx, window_end_excl_idx-1);
  for (int price_idx = window_start_incl_idx; price_idx < window_end_excl_idx; price_idx++)
  {
    if (prices[price_idx] < prices[time_price_idx])
    {
      rank++;
      // Serial.printf("(%d:%ld) ", price_idx, prices[price_idx]);
    }
  }
  return rank;
}

void aggregate_dayahead_prices_timeser(time_t record_start, time_t record_end, int time_idx_now, long prices[MAX_PRICE_PERIODS], JsonDocument &doc) // [MAX_PRICE_PERIODS]
{
  Serial.printf("aggregate_dayahead_prices_timeser start: %lld, end: %lld, time_idx_now: %d\n", record_start, record_end, time_idx_now);

  int time_idx = time_idx_now;
  char var_code[25];

  for (time_t time = record_start + time_idx_now * NETTING_PERIOD_SEC; time < record_end; time += NETTING_PERIOD_SEC)
  {
    delay(5);

    snprintf(var_code, sizeof(var_code), "%lld", time);
    JsonObject json_obj = doc.createNestedObject(var_code);

    float energyPriceSpot = prices[time_idx] / 100;
    json_obj["p"] = prices[time_idx] / 100;

    localtime_r(&time, &tm_struct_g);

    Serial.printf("time_idx: %d , %04d%02d%02dT%02d00, ", time_idx, tm_struct_g.tm_year + 1900, tm_struct_g.tm_mon + 1, tm_struct_g.tm_mday, tm_struct_g.tm_hour);
    Serial.printf("energyPriceSpot: %f \n", energyPriceSpot);

    int price_block_count = (int)(sizeof(price_variable_blocks) / sizeof(*price_variable_blocks));
    for (int block_idx = 0; block_idx < price_block_count; block_idx++)
    {
      int rank = get_spot_sliding_window_rank(time, price_variable_blocks[block_idx], time_idx, prices);

      if (rank > 0)
      {
        snprintf(var_code, sizeof(var_code), "pr%d", price_variable_blocks[block_idx]);
        json_obj[var_code] = rank;
     //   Serial.printf("%s: %d\n", var_code, rank);
      }
    }
    time_idx++;
  }
  Serial.println("aggregate_dayahead_prices_timeser finished");
  return;
}

bool get_price_data()
{
  // WiFiClientSecure client_https;

  time_t period_start=0, period_end=0;
  time_t record_start = 0, record_end = 0;
  // long prices[MAX_PRICE_PERIODS];
  char date_str_start[13];
  char date_str_end[13];
  WiFiClientSecure client_https;

  // WiFiClient client;
  Serial.printf("ESP.getFreeHeap():%d\n", ESP.getFreeHeap());

  bool end_reached = false;
  int price_rows = 0;

  time_t start_ts, end_ts; // this is the epoch
  tm tm_struct;
  time(&start_ts);
  start_ts -= SECONDS_IN_DAY;
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
  client_https.setInsecure();
  client_https.setTimeout(15000); // 15 Seconds
  delay(1000);

  // initiate prices
 // for (int price_idx = 0; price_idx < MAX_PRICE_PERIODS; price_idx++)
   // prices[price_idx] = VARIABLE_LONG_UNKNOWN;

  // Use WiFiClientSecure class to create TLS connection
  //int r = 0; // retry counter
  Serial.println(F("Connecting"));

  Serial.printf("before connect millis(): %lu\n", millis());
  if (client_https.connect(host_prices, httpsPort))
  {
    Serial.println(F("Connected A"));
  }
  else
  {
    Serial.println(F("NOT connected A"));
    return false;
  }

  Serial.println(F("Connected"));

  String url = url_base + String("&securityToken=") + securityToken + String("&In_Domain=") + queryInOutDomain + String("&Out_Domain=") + queryInOutDomain + "&outBiddingZone_Domain=" + outBiddingZone_Domain + String("&periodStart=") + date_str_start + String("&periodEnd=") + date_str_end;
  Serial.print("requesting URL: ");

  Serial.println(url);

  client_https.print(String("GET ") + url + " HTTP/1.1\r\n" +
                     "Host: " + host_prices + "\r\n" +
                     "User-Agent: ArskaNoderESP8266\r\n" +
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
    String line = client_https.readStringUntil('\n');

    if (line == "\r")
    {
      Serial.println("headers received");
      break;
    }
  }

  Serial.println(F("Waiting the document"));
  while (client_https.available())
  {
    String line = client_https.readStringUntil('\n'); // \n oli alunperin \r, \r tulee vain dokkarin lopussa, siellä ole kuitenkaan \n
    if (line.indexOf("<Publication_MarketDocument") > -1)
      save_on = true;
    /*    if (save_on)
        {
          price_data_file.println(line);
        } */
    if (line.indexOf("</Publication_MarketDocument>") > -1)
    {
      save_on = false;
      read_ok = true;
    }

    if (line.endsWith(F("</period.timeInterval>")))
    { // header dates
      record_end = period_end;
      record_start = record_end - (NETTING_PERIOD_SEC * MAX_PRICE_PERIODS);
      prices_first_period = record_start;

      Serial.printf("period_start: %lld record_start: %lld - period_end: %lld\n", period_start, record_start, period_end);
    }

    if (line.endsWith(F("</start>")))
      period_start = ElementToUTCts(line);

    if (line.endsWith(F("</end>")))
      period_end = ElementToUTCts(line);

    if (line.endsWith(F("</position>")))
      pos = getElementValue(line).toInt();

    else if (line.endsWith(F("</price.amount>")))
    {
      price = int(getElementValue(line).toFloat() * 100);
      price_rows++;
    }
    else if (line.endsWith("</Point>"))
    {
      int period_idx = pos - 1 + (period_start - record_start) / NETTING_PERIOD_SEC;
      if (period_idx >= 0 && period_idx < MAX_PRICE_PERIODS)
      {
      
        prices[period_idx] = price;
        Serial.printf("period_idx %d, price: %f\n", period_idx, (float)price / 100);
        price_array.add(price);
      }
      else
        Serial.printf("Cannot store price, period_idx: %d\n", period_idx);

      pos = -1;
      price = VARIABLE_LONG_UNKNOWN;
    }

    if (line.indexOf(F("</Publication_MarketDocument")) > -1)
    { // this signals the end of the response from XML API
      end_reached = true;
      save_on = false;
      read_ok = true;
      Serial.println("end_reached");
      break;
    }

    if (line.indexOf(F("Service Temporarily Unavailable")) > 0)
    {
      Serial.println(F("Service Temporarily Unavailable"));
      read_ok = false;
      break;
    }
  }

  // price_data_file.close();

  client_https.stop();

  if (end_reached && (price_rows >= MAX_PRICE_PERIODS))
  {

    time_t now;
    time(&now);
    doc["record_start"] = record_start;
    doc["record_end"] = record_end;
    doc["resolution_m"] = NETTING_PERIOD_MIN;
    doc["ts"] = now;
    doc["expires"] = now + 3600; // time-to-live of the result, under construction, TODO: set to parameters

    File prices_file = LittleFS.open(price_data_filename, "w"); // Open file for writing
    serializeJson(doc, prices_file);
    prices_file.close();
    Serial.println(F("Finished succesfully get_price_data."));
    return true;
  }
  else
  {
    Serial.printf("Prices are not saved, end_reached %d, price_rows %d \n", end_reached, price_rows);
  }
  return read_ok;
}


bool update_external_variables()
{
  // WiFiClientSecure client_https;

 // time_t period_start, period_end;
  time_t record_start = 0, record_end = 0;
 // char date_str_start[13];
 // char date_str_end[13];

  // WiFiClient client;
  Serial.printf("ESP.getFreeHeap():%d", ESP.getFreeHeap());

 // bool end_reached = false;
 // int price_rows = 0;

  time_t start_ts, end_ts; // this is the epoch

  time(&start_ts);
  start_ts -= SECONDS_IN_DAY;
  end_ts = start_ts + SECONDS_IN_DAY * 2;

  //int pos = -1;
  //long price = VARIABLE_LONG_UNKNOWN;

  DynamicJsonDocument doc(3072);

  File prices_file_in = LittleFS.open(price_data_filename, "r"); // Open file for writing
  DeserializationError error = deserializeJson(doc, prices_file_in);
  prices_file_in.close();

  if (error)
  {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return false;
  }

  record_start = doc["first_period"];
  //prices_first_period = doc["first_period"];
  JsonArray prices_array = doc["prices"];

  for (unsigned int i = 0; (i < prices_array.size() && i < MAX_PRICE_PERIODS); i++)
  {
    prices[i] = (long)prices_array[i];
    // Serial.printf("prices[%d]: %ld\n", i, prices[i]);
  }

  time(&now);
  record_end = (time_t)doc["record_end"];
  record_start = (time_t)doc["record_start"];

  int time_idx_now = int((now - record_start) / NETTING_PERIOD_SEC);
  Serial.printf("time_idx_now: %d, price now: %f\n", time_idx_now, (float)prices[time_idx_now] / 100);
  Serial.printf("record_start: %lld, record_end: %lld\n", record_start, record_end);
  aggregate_dayahead_prices_timeser(record_start, record_end, time_idx_now, prices, doc);

  doc["record_start"] = record_start;
  doc["resolution_m"] = NETTING_PERIOD_MIN;
  doc["ts"] = now;
  doc["expires"] = now + 3600; // time-to-live of the result, under construction, TODO: set to parameters

  File prices_file_out = LittleFS.open(variables_filename, "w"); // Open file for writing
  serializeJson(doc, prices_file_out);
  prices_file_out.close();
  Serial.println(F("Finished succesfully update_external_variables."));





  return true;

 
}

// https://github.com/me-no-dev/ESPAsyncWebServer#send-large-webpage-from-progmem-containing-templates
/*
// returns a string from state integer array

String state_array_string(uint16_t state_array[CHANNEL_STATES_MAX])
{
  String states = String();
  for (int i = 0; i < CHANNEL_STATES_MAX; i++)
  {
    if (state_array[i] > 0)
    {
      states += String(state_array[i]);
      if (i + 1 < CHANNEL_STATES_MAX && (state_array[i + 1] > 0))
        states += String(",");
    }
    else
      break;
  }
  return states;
}
*/
// channel config fields for the admin form
void get_channel_config_fields(char *out, int channel_idx)
{
  char buff[200];
  snprintf(buff, 200, "<div><div class='fldshort'>id: <input name='id_ch_%d' type='text' value='%s' maxlength='9'></div>", channel_idx, s.ch[channel_idx].id_str);
  strcat(out, buff);

  snprintf(buff, 200, "<div class='fldtiny' id='d_uptimem_%d'></span>mininum up (s):</span><input name='ch_uptimem_%d'  type='text' value='%d'></div></div>", channel_idx, channel_idx, (int)s.ch[channel_idx].uptime_minimum);
  strcat(out, buff);

  snprintf(buff, sizeof(buff), "<div class='flda'>type:<br><select id='chty_%d' name='chty_%d' onchange='setChannelFields(this)'>", channel_idx, channel_idx);
  strcat(out, buff);

  bool is_gpio_channel;
  for (int channel_type_idx = 0; channel_type_idx < CHANNEL_TYPES; channel_type_idx++)
  {
    // if gpio channels and non-gpio channels can not be mixed
    //  tässä kai pitäisi toinen ottaa channelista, toinen loopista
    is_gpio_channel = (s.ch[channel_idx].gpio != 255);
    // Serial.printf("is_gpio_channel %d %d %d\n",channel_idx,is_gpio_channel,s.ch[channel_idx].gpio);

    if ((channel_type_idx == 1 && is_gpio_channel) || !(channel_type_idx == 1 || is_gpio_channel))
    {
      snprintf(buff, sizeof(buff), "<option value='%d' %s>%s</option>", channel_type_idx, (s.ch[channel_idx].type == channel_type_idx) ? "selected" : "", channel_type_strings[channel_type_idx]);
      strcat(out, buff);
    }
  }
  strcat(out, "</select></div>\n");

  //  radio-toolbar
  snprintf(buff, 200, "<div class='secbr'>\n<input type='radio' id='mo_%d_1' name='mo_%d' value='1' %s onchange='setRuleMode(%d, 1,true);'><label for='mo_%d_1'>Template mode</label>\n", channel_idx, channel_idx, (s.ch[channel_idx].config_mode==1)?"checked='checked'":"", channel_idx, channel_idx);
  strcat(out, buff);
  snprintf(buff, 200, "<input type='radio' id='mo_%d_0' name='mo_%d' value='0' %s onchange='setRuleMode(%d, 0,true);'><label for='mo_%d_0'>Advanced mode</label>\n</div>\n", channel_idx, channel_idx, (s.ch[channel_idx].config_mode==0)?"checked='checked'":"", channel_idx, channel_idx);
  strcat(out, buff);
  Serial.printf("s.ch[channel_idx].config_mode: %i", (byte)(s.ch[channel_idx].config_mode));

  //snprintf(buff, sizeof(buff), "<div id='rt_%d'><select id='rts_%d' name'rts_%d' onchange='templateChanged(this)'></select><input type='checkbox' id='rtl_%d' value='1' %s></div>\n", channel_idx, channel_idx, channel_idx, channel_idx, "checked");
  snprintf(buff, sizeof(buff), "<div id='rt_%d'><select id='rts_%d' onfocus='saveVal(this)' name='rts_%d' onchange='templateChanged(this)'></select></div>\n", channel_idx, channel_idx, channel_idx);
  strcat(out, buff);

  Serial.printf("get_channel_config_fields strlen(out):%d\n", strlen(out));
}

// condition row fields for the admin form
void get_channel_rule_fields(char *out, int channel_idx, int condition_idx, int buff_len)
{
  char float_buffer[32]; // to prevent overflow if initiated with a long number...
  char suffix[10];
  snprintf(suffix, 10, "_%d_%d", channel_idx, condition_idx);

  dtostrf(s.ch[channel_idx].conditions[condition_idx].target_val, 3, 1, float_buffer);

  // name attributes  will be added in javascript before submitting
 /* snprintf(out, buff_len, "<div class='secbr'>\n<div id='sd%s' class='fldlong'><b>%s rule %i: </b></div>\n<div id='ctcbd%s'><input type='checkbox' id='ctcb%s' value='1' %s><label for='ctcbd%s'>Keep channel up if the rule is matching.</label></div>\n</div>\n"
  , suffix, s.ch[channel_idx].conditions[condition_idx].condition_active ? "* ACTIVE *" : "", condition_idx + 1, suffix, suffix, s.ch[channel_idx].conditions[condition_idx].switch_on ? "checked" : "",suffix);
  */
  snprintf(out, buff_len, "<div class='secbr'><span>rule %i: %s</span></div><div class='secbr'><input type='checkbox' id='ctcb%s' value='1' %s><label for='ctcbd%s'>Channel is up if the rule is matching</label></div>\n"
   , condition_idx + 1 ,s.ch[channel_idx].conditions[condition_idx].condition_active ? "* MATCHING *" : "",  suffix, s.ch[channel_idx].conditions[condition_idx].switch_on ? "checked" : "",suffix);
  return;
}

// energy meter fields for admin form
void get_meter_config_fields(char *out)
{
  char buff[200];
  strcpy(out, "<div class='secbr'><h3>Energy meter</h3></div>\n<div class='fld'><select name='emt' id='emt' onchange='setEnergyMeterFields(this.value)'>");

  for (int energym_idx = 0; energym_idx <= ENERGYM_MAX; energym_idx++)
  {
    snprintf(buff, sizeof(buff), "<option value='%d' %s>%s</>", energym_idx, (s.energy_meter_type == energym_idx) ? "selected" : "", energym_strings[energym_idx]);
    strcat(out, buff);
  }
  strcat(out, "</select></div>\n");
  snprintf(buff, sizeof(buff), "<div id='emhd' class='fld'><div class='fldlong'>host:<input name='emh' id='emh' type='text' value='%s'></div>\n", s.energy_meter_host);
  strcat(out, buff);
  snprintf(buff, sizeof(buff), "<div id='empd' class='fldtiny'>port:<input name='emp' id='emp' type='text' value='%d'></div>\n", s.energy_meter_port);
  strcat(out, buff);
  snprintf(buff, sizeof(buff), "<div id='emidd' class='fldtiny'>unit:<input name='emid' id='emid' type='text' value='%d'></div>\n</div>\n", s.energy_meter_id);
  strcat(out, buff);
  return;
}

// node priority is not yet in use, reserved for future use
void get_node_fields(char *out)
{
  char buff[150];

  snprintf(buff, sizeof(buff), "<div class='secbr'>node type:<br><select name='node_priority' id='node_priority'>");
  strcat(out, buff);
  snprintf(buff, sizeof(buff), " <option value='1' %s>master</>", s.node_priority == 1 ? "selected" : "");
  strcat(out, buff);
  snprintf(buff, sizeof(buff), " <option value='2' %s>backup</>", s.node_priority == 2 ? "selected" : "");
  strcat(out, buff);
  snprintf(buff, sizeof(buff), " <option value='255' %s>leaf</>", s.node_priority == 255 ? "selected" : "");
  strcat(out, buff);
  strcat(out, "</select></div>\n");

  return;
}

// get status info for admin / view forms
void get_status_fields(char *out)
{
  char buff[150];
  time_t current_time;
  time(&current_time);

 // time_t now_suntime = current_time + (s.lon * 240);
 // tm tm_sun;

  char time1[9];
  char time2[9];
  char eupdate[20];

  if (current_time < 1600000000)
  {
    strcat(out, "<div class='fld'>CLOCK UNSYNCHRONIZED!</div>\n");
  }
#ifdef SENSOR_DS18B20_ENABLED

 // localtime_r(&temperature_updated, &tm_struct);
 // snprintf(buff, 150, "<div class='fld'><div>Temperature: %s (%02d:%02d:%02d)</div>\n</div>\n", String(ds18B20_temp_c, 1).c_str(), tm_struct.tm_hour, tm_struct.tm_min, tm_struct.tm_sec);
 // strcat(out, buff);
  //
#endif
  char rtc_status[15];
#ifdef RTC_DS3231_ENABLED
  if (rtc_found)
    strcpy(rtc_status, "(RTC OK)");
  else
    strcpy(rtc_status, "(RTC FAILED)");
#else
  strcpy(rtc_status, "");
#endif
/*  localtime_r(&current_time, &tm_struct);
  gmtime_r(&now_suntime, &tm_sun);
  snprintf(buff, 150, "<div class='fld'><div>Local time: %02d:%02d:%02d, solar time: %02d:%02d:%02d %s</div>\n</div>\n", tm_struct.tm_hour, tm_struct.tm_min, tm_struct.tm_sec, tm_sun.tm_hour, tm_sun.tm_min, tm_sun.tm_sec, rtc_status);
  strcat(out, buff);
  */
  localtime_r(&recording_period_start, &tm_struct);
  snprintf(time1, sizeof(time1), "%02d:%02d:%02d", tm_struct.tm_hour, tm_struct.tm_min, tm_struct.tm_sec);
  localtime_r(&energym_read_last, &tm_struct);

  if (energym_read_last == 0)
  {
    strcpy(time2, "");
    strcpy(eupdate, ", not updated");
  }
  else
  {
    snprintf(time2, sizeof(time2), "%02d:%02d:%02d", tm_struct.tm_hour, tm_struct.tm_min, tm_struct.tm_sec);
    strcpy(eupdate, "");
  }

  if (s.energy_meter_type == ENERGYM_SHELLY3EM)
  {
#ifdef METER_SHELLY3EM_ENABLED
    float netEnergyInPeriod;
    float netPowerInPeriod;
    get_values_shelly3m(netEnergyInPeriod, netPowerInPeriod);
    snprintf(buff, 150, "<div class='fld'><div>Period %s-%s: net energy in %d Wh, power in  %d W %s</div>\n</div>\n", time1, time2, (int)netEnergyInPeriod, (int)netPowerInPeriod, eupdate);
    strcat(out, buff);
#endif
  }
  else if (s.energy_meter_type == ENERGYM_FRONIUS_SOLAR or (s.energy_meter_type == ENERGYM_SMA_MODBUS_TCP))
  {
#if defined(INVERTER_FRONIUS_SOLARAPI_ENABLED) || defined(INVERTER_SMA_MODBUS_ENABLED)
    snprintf(buff, 150, "<div class='fld'><div>Period %s-%s: produced %d Wh, power  %d W %s</div>\n</div>\n", time1, time2, (int)energy_produced_period, (int)power_produced_period_avg, eupdate);
    strcat(out, buff);
#endif
  }

  return;
}

// returns channel basic info html for the forms
void get_channel_status_header(char *out, int channel_idx, bool show_force_up)
{
  time(&now);
  char buff[200];
  char buff2[20];
  char buff4[20];
  tm tm_struct2;
  time_t from_now;
  strcpy(buff2, s.ch[channel_idx].is_up ? "🟥 up" : "⬜ down");
  if (s.ch[channel_idx].is_up != s.ch[channel_idx].wanna_be_up)
    strcat(buff2, s.ch[channel_idx].wanna_be_up ? " 🔺 rising" : "🔻 dropping");

  if (s.ch[channel_idx].force_up_until > now)
  {
    localtime_r(&s.ch[channel_idx].force_up_until, &tm_struct);
    from_now = s.ch[channel_idx].force_up_until - now;
    gmtime_r(&from_now, &tm_struct2);
    snprintf(buff4, 20, "-> %02d:%02d (%02d:%02d)", tm_struct.tm_hour, tm_struct.tm_min, tm_struct2.tm_hour, tm_struct2.tm_min);
  }
  else
  {
    strcpy(buff4, "");
  }


 // snprintf(buff, 200, "<div class='secbr'><h3>Channel %d - %s</h3></div><div class='fld'><div>%s</div></div><!-- gpio %d -->", channel_idx + 1, s.ch[channel_idx].id_str, buff2, s.ch[channel_idx].gpio);
  snprintf(buff, 200, "<div class='fld'><div>%s</div></div><!-- gpio %d -->",  buff2, s.ch[channel_idx].gpio);
  strcat(out, buff);

  if (show_force_up)
  {
    snprintf(buff, 200, "<div class='secbr radio-toolbar'>Set channel up for:<br>");
    strcat(out, buff);

    int hour_array_element_count = (int)(sizeof(force_up_hours) / sizeof(*force_up_hours));
    for (int hour_idx = 0; hour_idx < hour_array_element_count; hour_idx++)
    {

      snprintf(buff, 200, "<input type='radio' id='fup_%d_%d' name='fup_%d' value='%d' %s><label for='fup_%d_%d'>%d h</label>", channel_idx, force_up_hours[hour_idx], channel_idx, force_up_hours[hour_idx], ((s.ch[channel_idx].force_up_until < now) && (hour_idx == 0)) ? "checked" : "", channel_idx, force_up_hours[hour_idx], force_up_hours[hour_idx]);
      strcat(out, buff);

      // current force_up_until, if set
      if ((s.ch[channel_idx].force_up_until > now) && (s.ch[channel_idx].force_up_until - now > force_up_hours[hour_idx] * 3600) && (s.ch[channel_idx].force_up_until - now < force_up_hours[hour_idx + 1] * 3600))
      {
        snprintf(buff, 200, "<input type='radio' id='fup_%d_%d' name='fup_%d' value='%d' checked><label for='fup_%d_%d'>%s</label>", channel_idx, -1, channel_idx, -1, channel_idx, -1, buff4);
        strcat(out, buff);
      }
    }

    strcat(out, "</div>\n");
  }

  return;
}

String admin_form_processor(const String &var)
{
  if (var == "wifi_ssid")
    return s.wifi_ssid;

  if (var == "wifi_ssid_edit")
  {

    Serial.printf("Free Heap %d\n", ESP.getFreeHeap());

    return "";
  }

  if (var == "wifi_password")
    return s.wifi_password;
  if (var == "http_username")
    return s.http_username;
  if (var == "http_password")
    return s.http_password;

  return String();
}

String jscode_form_processor(const String &var)
{
  //Serial.printf("jscode_form_processor starting processing %s\n", var.c_str());
  char out[600];
  char buff[50];
  if (var == "RULE_STATEMENTS_MAX")
    return String(RULE_STATEMENTS_MAX);
  if (var == "CHANNEL_COUNT")
    return String(CHANNEL_COUNT);
  if (var == F("OPERS"))
  {
    strcpy(out, "[");
    for (int i = 0; i < OPER_COUNT; i++)
    {
      snprintf(buff, 40, "[%d, \"%s\", %s, %s, %s, %s]", opers[i].id, opers[i].code, opers[i].gt ? "true" : "false", opers[i].eq ? "true" : "false", opers[i].reverse ? "true" : "false", opers[i].boolean_only ? "true" : "false");
      strcat(out, buff);
      if (i < OPER_COUNT - 1)
        strcat(out, ", ");
    }
    strcat(out, "]");
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
      strcat(out, buff);
      if (channel_idx < CHANNEL_COUNT - 1)
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
      // YYY
      vars.get_variable_by_idx(variable_idx, &variable);
      snprintf(buff, 40, "[%d, \"%s\", %d]", variable.id, variable.code, variable.type);
      strcat(out, buff);
      if (variable_idx < variable_count - 1)
        strcat(out, ", ");
    }
    strcat(out, "]");
    return out;
  };
  return String();
}
// varibles for the admin form
String setup_form_processor(const String &var)
{
  // Javascript replacements
  if (var == "CHANNEL_CONDITIONS_MAX")
    return String(CHANNEL_CONDITIONS_MAX);
  if (var == "backup_ap_mode_enabled")
    return String(backup_ap_mode_enabled ? 1 : 0);

  if (var == "emt")
    return String(s.energy_meter_type);

  if (var == "energy_meter_fields")
  {
    char out[600];
    get_meter_config_fields(out);
    return out;
  }

  if (var == "baseload")
#if defined(INVERTER_FRONIUS_SOLARAPI_ENABLED) || defined(INVERTER_SMA_MODBUS_ENABLED)
    return String(s.baseload);
#else
    return F("(disabled)")
#endif

  if (var == F("prog_data"))
  {
    return String(compile_date);
  }

  if (var.startsWith("chi_"))
  {
    char out[1200];
    int channel_idx = var.substring(4, 5).toInt();
    if (channel_idx >= CHANNEL_COUNT)
      return String();

    snprintf(out,1200,"<div id='chdiv_%d' class='hb'><h3 class='hh3'><span>%d - %s</span></h3>",channel_idx,channel_idx+1, s.ch[channel_idx].id_str); // close on "cht_"
    get_channel_status_header(out, channel_idx, false);
    get_channel_config_fields(out, channel_idx);
   
    return out;
  }
  if (var.startsWith("vch_"))
  {
    char out[1000];
    int channel_idx = var.substring(4, 5).toInt();

    if (channel_idx >= CHANNEL_COUNT)
      return String();

    if (s.ch[channel_idx].type < CH_TYPE_GPIO_ONOFF) // undefined
      return String();

    snprintf(out,100,"<div id='chdiv_%d' class='hb'>",channel_idx); // close on "cht_"
    get_channel_status_header(out, channel_idx, true);
    strcat(out,"</div>"); //chdiv_
    return out;
  }

  if (var.startsWith("cht_"))
  {
    char out[1800];
    char buff[500];
    char buffstmt[50];
    char buffstmt2[200];
    int channel_idx = var.substring(4, 5).toInt();
    // if (channel_idx >= 1)
    if (channel_idx >= CHANNEL_COUNT)

      return String();

    // Serial.printf("var: %s\n", var.c_str());
    snprintf(out, 1800, "<div id='rd_%d'>", channel_idx);
    for (int condition_idx = 0; condition_idx < CHANNEL_CONDITIONS_MAX; condition_idx++)
    {
      strcpy(buff, "");
      strcpy(buffstmt2, "");

      get_channel_rule_fields(buff, channel_idx, condition_idx, sizeof(buff) - 1);
      strncat(out, buff, 2000 - strlen(out) - 1);

      int stmt_count = 0;
      char floatbuff[20];
      for (int stmt_idx = 0; stmt_idx < RULE_STATEMENTS_MAX; stmt_idx++)
      {
        // statement_st stmt_this = s.ch[channel_idx].conditions[condition_idx].statements;
        //   TODO: CONSTANT CONVERSION
        int variable_id = s.ch[channel_idx].conditions[condition_idx].statements[stmt_idx].variable_id;
        int oper_id = s.ch[channel_idx].conditions[condition_idx].statements[stmt_idx].oper_id;
        long const_val = s.ch[channel_idx].conditions[condition_idx].statements[stmt_idx].const_val;

        vars.to_str(variable_id, floatbuff, true, const_val);

        // TODO: alusta tai jotain
        if (variable_id == -1 || oper_id == -1)
          continue;

        // snprintf(buffstmt, 30, "%s[%d, %d, %ld]", (stmt_count > 0) ? ", " : "", variable_id, oper_id, const_val);
        snprintf(buffstmt, 30, "%s[%d, %d, %s]", (stmt_count > 0) ? ", " : "", variable_id, oper_id, floatbuff);
        stmt_count++;
        strcat(buffstmt2, buffstmt);
      }
      // no name in stmts_ , copy later in js
      snprintf(buff, sizeof(buff), "<div id='stmtd_%d_%d'><input type='button' class='addstmtb' id='addstmt_%d_%d' onclick='addStmt(this);' value='+'><input type='hidden' id='stmts_%d_%d' value='[%s]'>\n</div>\n<div class='secbr'></div>", channel_idx, condition_idx, channel_idx, condition_idx, channel_idx, condition_idx, buffstmt2);

      strcat(out, buff);
    }
    // snprintf(buff, sizeof(buff), "<div id='rt_%d'><select id='rts_%d' name'rts_%d'></select><input type='checkbox' id='rtl_%d' value='1' %s></div>\n", channel_idx, channel_idx, channel_idx, channel_idx,"checked" );

    strcat(out, "</div>\n"); // rd_X div

    strcat(out,"</div>"); //chdiv_
    Serial.printf("cht_ out with %d\n", strlen(out));
    return out;
  }

  if (var == "status_fields")
  {
    char out[500];
    memset(out, 0, 500);
    get_status_fields(out);
    return out;
  }
  if (var == "node_fields")
  {
    char out[500];
    memset(out, 0, 500);
    get_node_fields(out);
    // return out;
    return ""; // toistaiseksi
  }

  if (var == "lat")
  {
    return String(s.lat, 2);
  }
  if (var == "lon")
  {
    return String(s.lon, 2);
  }
  if (var == "forecast_loc")
  {
    return String(s.forecast_loc);
  }

#ifdef QUERY_ARSKA_ENABLED
  /*if (var == "pg_url")
    return s.pg_url;*/
  if (var == "pg_host")
    return s.pg_host;
  if (var == "pg_api_key")
    return s.pg_api_key;
/* removed
  if (var == "pg_cache_age")
    return String("7200"); // now fixed */
#endif

  for (int i = 0; i < CHANNEL_COUNT; i++)
  {
    if (var.equals(String("ch_uptimem_") + i))
    {
      return String(s.ch[i].uptime_minimum);
    }

    if (var.equals(String("id_ch_") + i))
    {
      return String(s.ch[i].id_str);
    }

    if (var.equals(String("up_ch_") + i))
    {
      if (s.ch[i].is_up == s.ch[i].wanna_be_up)
        return String(s.ch[i].is_up ? "up" : "down");
      else
        return String(s.ch[i].is_up ? F("up but dropping") : F("down but rising"));
    }
  }
  return String();
}

// ...
// read grid or production info from energy meter/inverter
void read_energy_meter()
{
  if (s.energy_meter_type == ENERGYM_SHELLY3EM)
  {
#ifdef METER_SHELLY3EM_ENABLED
    bool readOk = read_meter_shelly3em();
    if (readOk)
      time(&energym_read_last);
#endif
  }
  else if (s.energy_meter_type == ENERGYM_FRONIUS_SOLAR or (s.energy_meter_type == ENERGYM_SMA_MODBUS_TCP))
  {
#if defined(INVERTER_FRONIUS_SOLARAPI_ENABLED) || defined(INVERTER_SMA_MODBUS_ENABLED)
    read_inverter(period_changed);
#endif
  }
}

// returns channel to switch
// There can be multiple channels which could be switched but not all are switched at the same round
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

// switch channel up/down
bool set_channel_switch(int channel_idx, bool up)
{
  int channel_type_group = (s.ch[channel_idx].type >> 1 << 1); // we do not care about the last bit
  // Serial.printf("set_channel_switch channel_type_group %d \n",channel_type_group);
  if (channel_type_group == CH_TYPE_GPIO_ONOFF)
  {
    /*  Serial.print(F("CH_TYPE_GPIO_ONOFF:"));
      Serial.print(s.ch[channel_idx].gpio);
      Serial.print(up); */
    digitalWrite(s.ch[channel_idx].gpio, (up ? HIGH : LOW));
    return true;
  }
  else if (channel_type_group == CH_TYPE_SHELLY_ONOFF && s.energy_meter_type == ENERGYM_SHELLY3EM)
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
  // else
  //   Serial.print(F("Cannot switch this channel"));

  return false;
}

// set relays up and down,
// MAX_CHANNELS_SWITCHED_AT_TIME defines how many channel can be switched at time
void update_channel_states2()
{
  time(&now);
  // loop channels and check whether channel should be up
  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  { // reset condition_active variable

    bool wait_minimum_uptime = ((now - s.ch[channel_idx].toggle_last) < s.ch[channel_idx].uptime_minimum); // channel must stay up minimum time
    if (s.ch[channel_idx].force_up_until == -1)
    { // force down
      s.ch[channel_idx].force_up_until = 0;
      wait_minimum_uptime = false;
    }
    bool forced_up = (s.ch[channel_idx].force_up_until > now); // signal to keep it up

    if (s.ch[channel_idx].is_up && (wait_minimum_uptime || forced_up))
    {
      Serial.printf("Not yet time to drop channel %d . Since last toggle %d, force_up_until: %lld .\n", channel_idx, (int)(now - s.ch[channel_idx].toggle_last), s.ch[channel_idx].force_up_until);
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
      continue;
    }

    // now checking normal state based conditions
    // target_state_match_found = false;
    s.ch[channel_idx].wanna_be_up = false;
    // loop channel targets until there is match (or no more targets)
    bool statement_true;
    // if no statetements -> false (or defalt)
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

          Serial.printf("update_channel_states2 statement.variable_id: %d\n", statement->variable_id);
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
        s.ch[channel_idx].wanna_be_up = s.ch[channel_idx].conditions[condition_idx].switch_on;
        s.ch[channel_idx].conditions[condition_idx].condition_active = true;
        Serial.printf("update_channel_states2 condition true, wanna_be_up: %s\n",s.ch[channel_idx].wanna_be_up?"true":"false");
        break;
      }
      /*
      else
       {
        Serial.printf("update_channel_states2 condition false\n");
      }*/

    } // conditions loop

  } // channel loop
}

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
      Serial.printf("Switching ch %d  (%d) from %d .-> %d\n", ch_to_switch, s.ch[ch_to_switch].gpio, s.ch[ch_to_switch].is_up, is_rise);
      s.ch[ch_to_switch].is_up = is_rise;
      s.ch[ch_to_switch].toggle_last = now;

      // digitalWrite(s.ch[ch_to_switch].gpio, (s.ch[ch_to_switch].is_up ? HIGH : LOW));
      set_channel_switch(ch_to_switch, s.ch[ch_to_switch].is_up);
    }
  }
}

void sendForm(AsyncWebServerRequest *request, const char *template_name)
{
  Serial.printf("sendForm1: %s\n", template_name);
  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();
  check_forced_restart(true); // if in forced ap-mode, reset counter to delay automatic restart
  request->send(LittleFS, template_name, "text/html", false, setup_form_processor);
}
void sendForm(AsyncWebServerRequest *request, const char *template_name, AwsTemplateProcessor processor)
{
  Serial.printf("sendForm2: %s", template_name);
  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();
  check_forced_restart(true); // if in forced ap-mode, reset counter to delay automatic restart
  request->send(LittleFS, template_name, "text/html", false, processor);
}

void onWebDashboardGet(AsyncWebServerRequest *request)
{
  /* if (backup_ap_mode_enabled)
     request->redirect("/admin");
   else*/
  sendForm(request, "/dashboard_template.html");
}

void onWebInputsGet(AsyncWebServerRequest *request)
{
  sendForm(request, "/inputs_template.html");
}

void onWebChannelsGet(AsyncWebServerRequest *request)
{
  sendForm(request, "/channels_template.html");
}

// Web admin form

void onWebAdminGet(AsyncWebServerRequest *request)
{
  sendForm(request, "/admin_template.html", admin_form_processor);
}

void onWebTemplateGet(AsyncWebServerRequest *request)
{
  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();

  if (!request->hasParam("id"))
    request->send(404, "text/plain", "Not found");

  StaticJsonDocument<16> filter;
  AsyncWebParameter *p = request->getParam("id");

  Serial.printf("id: %s\n", p->value().c_str());

  filter[p->value().c_str()] = true;

  StaticJsonDocument<1024> doc;
  // xxx
  File template_file = LittleFS.open("/data/templates.json", "r");
  DeserializationError error = deserializeJson(doc, template_file, DeserializationOption::Filter(filter));
  String output;
  if (error)
  {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return;
  }
  template_file.close();
  JsonObject root = doc[p->value().c_str()];
  serializeJson(root, output);
  request->send(200, "application/json", output);
}

// Process channel force form
void onWebDashboardPost(AsyncWebServerRequest *request)
{
  time(&now);
  int params = request->params();
  int channel_idx;
  bool forced_up_changes = false;
  bool channel_already_forced;
  long forced_up_hours;
  for (int i = 0; i < params; i++)
  {
    AsyncWebParameter *p = request->getParam(i);
    if (p->isPost() && p->name().startsWith("fup_"))
    {
      channel_idx = p->name().substring(4, 5).toInt();
      channel_already_forced = (s.ch[channel_idx].force_up_until > now);
      forced_up_hours = p->value().toInt();
      Serial.printf("channel_idx: %d, forced_up_hours: %ld \n", channel_idx, forced_up_hours);

      // -1 - no change
      if ((forced_up_hours != -1) && (channel_already_forced || forced_up_hours > 0))
      { // there are changes
        if (forced_up_hours > 0)
        {
          s.ch[channel_idx].force_up_until = now + forced_up_hours * 3600 - 1;
          s.ch[channel_idx].wanna_be_up = true;
        }
        else
        {
          s.ch[channel_idx].force_up_until = -1; // forced down
          s.ch[channel_idx].wanna_be_up = false;
        }
        // forced_up_changes = true;
        if (s.ch[channel_idx].wanna_be_up != s.ch[channel_idx].is_up)
          forced_up_changes = true;
      }
    }
  }
  if (forced_up_changes)
    set_relay_requested = true;
  //  update_channel_statuses(); //tämä ei ole hyvä idea, voit laittaa kyllä looppiin requestin, mutta ei suoraa kutsua koska shelly tekee verkkokutsun
  request->redirect("/");
}

// restarts controller in update mode
void bootInUpdateMode(AsyncWebServerRequest *request)
{
  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();
  s.next_boot_ota_update = true;
  writeToEEPROM(); // save to non-volatile memory
  restart_required = true;
  request->send(200, "text/html", "<html><head><meta http-equiv='refresh' content='10; url=./update' /></head><body>Wait for update mode...</body></html>");
  return;
}

// INPUTS
void onWebInputsPost(AsyncWebServerRequest *request)
{
  // INPUTS
  if (s.energy_meter_type != request->getParam("emt", true)->value().toInt())
  {
    restart_required = true;
    s.energy_meter_type = request->getParam("emt", true)->value().toInt();
  }

  strcpy(s.energy_meter_host, request->getParam("emh", true)->value().c_str());
  s.energy_meter_port = request->getParam("emp", true)->value().toInt();
  s.energy_meter_id = request->getParam("emid", true)->value().toInt();

#ifdef INVERTER_FRONIUS_SOLARAPI_ENABLED
  s.baseload = request->getParam("baseload", true)->value().toInt();
#endif

#ifdef QUERY_ARSKA_ENABLED
  // strcpy(s.pg_url, request->getParam("pg_url", true)->value().c_str());
  strcpy(s.pg_host, request->getParam("pg_host", true)->value().c_str());
  strcpy(s.pg_api_key, request->getParam("pg_api_key", true)->value().c_str());

  // s.pg_cache_age = max((int)request->getParam("pg_cache_age", true)->value().toInt(), 3600);
#endif

  s.lat = request->getParam("lat", true)->value().toFloat();
  s.lon = request->getParam("lon", true)->value().toFloat();
  strcpy(s.forecast_loc, request->getParam("forecast_loc", true)->value().c_str());
  // END OF INPUTS
  writeToEEPROM();
  restart_required = true;
  request->send(200, "text/html", "<html><head><meta http-equiv='refresh' content='10; url=/inputs' /></head><body>restarting...wait...</body></html>");
  // request->redirect("/channels");
}
void readStatements(const char *s)
{
}
// Channels
void onWebChannelsPost(AsyncWebServerRequest *request)
{
  char ch_fld[20];
  char state_fld[20];
  char stmts_fld[20];
  char target_fld[20];
  char targetcb_fld[20];

  StaticJsonDocument<300> stmts_json;

  // bool stmts_emptied = false;
  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {
    snprintf(ch_fld, 20, "ch_uptimem_%i", channel_idx);
    if (request->hasParam(ch_fld, true))
    {
      s.ch[channel_idx].uptime_minimum = request->getParam(ch_fld, true)->value().toInt();
    }

    snprintf(ch_fld, 20, "id_ch_%i", channel_idx);
    if (request->hasParam(ch_fld, true))
      strcpy(s.ch[channel_idx].id_str, request->getParam(ch_fld, true)->value().c_str());

    snprintf(ch_fld, 20, "chty_%i", channel_idx);
    if (request->hasParam(ch_fld, true))
      s.ch[channel_idx].type = request->getParam(ch_fld, true)->value().toInt();

    snprintf(ch_fld, 20, "mo_%i", channel_idx);
    if (request->hasParam(ch_fld, true))
      s.ch[channel_idx].config_mode = request->getParam(ch_fld, true)->value().toInt();

    snprintf(ch_fld, 20, "rts_%i", channel_idx); 
    Serial.println(ch_fld);
    if (request->hasParam(ch_fld, true)) {
      Serial.println("found");
      Serial.println(request->getParam(ch_fld, true)->value().toInt());
      s.ch[channel_idx].template_id = request->getParam(ch_fld, true)->value().toInt();
    }


    for (int condition_idx = 0; condition_idx < CHANNEL_CONDITIONS_MAX; condition_idx++)
    {
      // statements

      snprintf(stmts_fld, 20, "stmts_%i_%i", channel_idx, condition_idx);
      if (request->hasParam(stmts_fld, true) && !request->getParam(stmts_fld, true)->value().isEmpty())
      {
        // empty all statements if there are somein the form post
        /*   if (!stmts_emptied)
           {
             stmts_emptied = true;*/
        for (int stmt_idx = 0; stmt_idx < RULE_STATEMENTS_MAX; stmt_idx++)
        {
          s.ch[channel_idx].conditions[condition_idx].statements[stmt_idx].variable_id = -1;
          s.ch[channel_idx].conditions[condition_idx].statements[stmt_idx].oper_id = -1;
          s.ch[channel_idx].conditions[condition_idx].statements[stmt_idx].const_val = 0;
        }
        //   }

        DeserializationError error = deserializeJson(stmts_json, request->getParam(stmts_fld, true)->value());
        if (error)
        {
          Serial.print(F("deserializeJson() failed: "));
          Serial.println(error.f_str());
        }
        else
        {

          if (stmts_json.size() > 0)
          {
            Serial.print(stmts_fld);
            Serial.print(": ");
            Serial.println(request->getParam(stmts_fld, true)->value());
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
                // TODO: conversion variables 10 exp

                // Serial.printf("C [%s]\n",val_float_str);
                // float val_f = atof(stmts_json[stmt_idx][2]);
                float val_f = stmts_json[stmt_idx][2];
                long long_val = vars.float_to_internal_l(variable_id, val_f);
                Serial.printf("float_to_internal_l: %f  -> %ld\n", val_f, long_val);
                Serial.printf("Saving statement value of variable %d: %ld\n", (int)stmts_json[stmt_idx][0], long_val);
                s.ch[channel_idx].conditions[condition_idx].statements[stmt_idx].const_val = long_val;
              }
              else
              {
                Serial.printf("Error, cannot find variable with index %d\n", (int)stmts_json[stmt_idx][0]);
              }
            }
          } /* struct statement_st
     {
       int variable_idx;
       byte oper_idx;
       byte constant_type;
       long const_val;
     };

     */
            /*  int root_0_0 = root_0[0]; // 0
              int root_0_1 = root_0[1]; // 0
              int root_0_2 = root_0[2]; // 1
              */
        }
      }

      snprintf(state_fld, 20, "st_%i_%i", channel_idx, condition_idx);
      snprintf(target_fld, 20, "t_%i_%i", channel_idx, condition_idx);
      snprintf(targetcb_fld, 20, "ctcb_%i_%i", channel_idx, condition_idx);
      // TODO:state_fld tallennus poistuu
      if (request->hasParam(state_fld, true))
      {
        str_to_uint_array(request->getParam(state_fld, true)->value().c_str(), s.ch[channel_idx].conditions[condition_idx].upstates, ",");
        s.ch[channel_idx].conditions[condition_idx].target_val = request->getParam(target_fld, true)->value().toFloat();
      }
      s.ch[channel_idx].conditions[condition_idx].switch_on = request->hasParam(targetcb_fld, true); // cb checked
    }
  }
  writeToEEPROM();
  // restart_required = true;
  // request->send(200, "text/html", "<html><head><meta http-equiv='refresh' content='10; url=/channels' /></head><body>restarting...wait...</body></html>");
  request->redirect("/channels");
}

// process admin form results
void onWebAdminPost(AsyncWebServerRequest *request)
{
  String message;
  // s.node_priority = request->getParam("node_priority", true)->value().toInt();

  if (!(request->getParam("wifi_ssid", true)->value().equals("NA")))
  {
    strcpy(s.wifi_ssid, request->getParam("wifi_ssid", true)->value().c_str());
    strcpy(s.wifi_password, request->getParam("wifi_password", true)->value().c_str());
  }

  // restart_required = true;

  // request->send(200, "text/html", F("<html><head></head><body><p>Wait about 10 seconds. If the parameters were correct you can soon connect to Arska in your wifi. Get IP address to connect from your router or monitor serial console.</p></body></html>"));

  if (request->hasParam("http_password", true) && request->hasParam("http_password2", true))
  {
    if (request->getParam("http_password", true)->value().equals(request->getParam("http_password2", true)->value()) && request->getParam("http_password", true)->value().length() >= 5)
      strcpy(s.http_password, request->getParam("http_password", true)->value().c_str());
  }
  // ADMIN
  if (request->getParam("op", true)->value().equals("ts"))
  {
    time_t ts = request->getParam("ts", true)->value().toInt();
    setInternalTime(ts);
#ifdef RTC_DS3231_ENABLED
    if (rtc_found)
      setRTC();
#endif
  }

#ifdef OTA_UPDATE_ENABLED
  if (request->getParam("op", true)->value().equals("ota"))
  {
    bootInUpdateMode(request);
  }
#endif

  writeToEEPROM();
  if (request->getParam("op", true)->value().equals("reboot"))
  {
    restart_required = true;
    request->send(200, "text/html", "<html><head><meta http-equiv='refresh' content='10; url=/admin' /></head><body>restarting...wait...</body></html>");
  }

  if (request->getParam("op", true)->value().equals("scan_wifis"))
  {
    scan_and_store_wifis_requested = true;
    request->send(200, "text/html", "<html><head><meta http-equiv='refresh' content='5; url=/admin' /></head><body>scanning queued</body></html>");
  }

  if (request->getParam("op", true)->value().equals("reset"))
  {
    s.check_value = 0; // EEPROM_CHECK_VALUE when initiated, so should init after restart
    writeToEEPROM();
    restart_required = true;
    request->send(200, "text/html", "<html><head><meta http-equiv='refresh' content='10; url=./' /></head><body>restarting...wait...</body></html>");
  }

  // END OF ADMIN

  // delete cache file
  LittleFS.remove(pg_state_cache_filename);
  request->redirect("/admin");
}

// returns status in json
void onWebStatusGet(AsyncWebServerRequest *request)
{
  if (!request->authenticate(s.http_username, s.http_password))
  {
    return request->requestAuthentication();
  }
  StaticJsonDocument<550> doc; // oli 128, lisätty heapille ja invertterille
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
  // var_obj["updated"] = meter_read_ts;
  // var_obj["freeHeap"] = ESP.getFreeHeap();
  // var_obj["uptime"] = (unsigned long)(millis() / 1000);

  char id_str[6];
  char buff_value[20];
  variable_st variable;
  for (int variable_idx = 0; variable_idx < vars.get_variable_count(); variable_idx++)
  {
    vars.get_variable_by_idx(variable_idx, &variable);
    snprintf(id_str, 6, "%d", variable.id);
    vars.to_str(variable.id, buff_value, false);
    var_obj[id_str] = buff_value;
  }
  time_t current_time;
  time(&current_time);
  localtime_r(&current_time, &tm_struct);
  snprintf(buff_value, 20, "%04d-%02d-%02d %02d:%02d:%02d", tm_struct.tm_year + 1900, tm_struct.tm_mon + 1, tm_struct.tm_mday, tm_struct.tm_hour, tm_struct.tm_min, tm_struct.tm_sec);
  doc["localtime"] = buff_value;
  

  serializeJson(doc, output);
  request->send(200, "application/json", output);
}

// TODO: do we need authentication?
// TODO: still in proto stage
// returns current states in json/rest
void onWebStateSeriesGet(AsyncWebServerRequest *request)
{
  char start_str[11];
  itoa(current_period_start, start_str, 10);
  DynamicJsonDocument doc(1024);
  String output;

  unsigned long currentMillis = millis();

  // testing, prevent simultaneous write and read of state data
  while (processing_states)
  {
    if (millis() - currentMillis > 3000) // timeout
      break;
  }

  JsonArray states_current = doc.createNestedArray((const char *)start_str);

  time(&now);
  doc["ts"] = now;
  doc["expires"] = now + 121; // time-to-live of the result, under construction, TODO: set to parameters
  doc["node_priority"] = s.node_priority;
  serializeJson(doc, output);
  request->send(200, "application/json", output);
}

// Everythign starts from here in while starting the controller
void setup()
{
  Serial.begin(115200);
  delay(2000); // wait for console settle - only needed when debugging

  randomSeed(analogRead(0)); // initiate random generator

#ifdef SENSOR_DS18B20_ENABLED

  // voltage to 1-wire bus
  // voltage from data pin so we can reset the bus (voltage low) if needed
  pinMode(ONEWIRE_VOLTAGE_GPIO, OUTPUT);
  Serial.printf("Setting channel ONEWIRE_VOLTAGE_GPIO with gpio %d to OUTPUT mode\n", ONEWIRE_VOLTAGE_GPIO);

  digitalWrite(ONEWIRE_VOLTAGE_GPIO, HIGH);
  sensors.begin();

#endif

#ifdef QUERY_ARSKA_ENABLED
  // TODO: pitäisikö olla jo kevyempi
  while (!LittleFS.begin())
  {
    Serial.println(F("Failed to initialize LittleFS library"));
    delay(1000);
  }
  Serial.println(F("LittleFS initialized"));
#endif
  Serial.println(F("setup() starting"));
  Serial.print(F("sizeof(s):"));
  Serial.print(sizeof(s));

  EEPROM.begin(sizeof(s));
  readFromEEPROM();

  if (s.check_value != EEPROM_CHECK_VALUE) // setup not initiated
  {
    Serial.println(F("Initiating eeprom"));
    s.check_value = EEPROM_CHECK_VALUE; // this is indication that eeprom is initiated
    // get init values from config.json
    read_config_file(true);
    for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
    {
      s.ch[channel_idx].type = 0; // GPIO_ONOFF
      s.ch[channel_idx].uptime_minimum = 60;
      s.ch[channel_idx].force_up_until = 0;
      s.ch[channel_idx].config_mode = 0;
      s.ch[channel_idx].template_id = -1;

      snprintf(s.ch[channel_idx].id_str, sizeof(s.ch[channel_idx].id_str), "channel %d", channel_idx + 1);
      for (int condition_idx = 0; condition_idx < CHANNEL_CONDITIONS_MAX; condition_idx++)
      {
        s.ch[channel_idx].conditions[condition_idx] = {{}, 0};
        for (int stmt_idx = 0; stmt_idx < RULE_STATEMENTS_MAX; stmt_idx++)
        {
          s.ch[channel_idx].conditions[condition_idx].statements[stmt_idx].variable_id = -1;
          s.ch[channel_idx].conditions[condition_idx].statements[stmt_idx].oper_id = -1;
          s.ch[channel_idx].conditions[condition_idx].statements[stmt_idx].const_val = 0;
        }
      }
    }

#ifdef OTA_UPDATE_ENABLED
    s.next_boot_ota_update = false;
#endif

    writeToEEPROM();
  }

  read_config_file(false); // read the rest

  /*
    if (1 == 2) //Softap should be created if cannot connect to wifi (like in init), redirect
    { // check also https://github.com/me-no-dev/ESPAsyncWebServer/blob/master/examples/CaptivePortal/CaptivePortal.ino
      if (WiFi.softAP("arska-node", "arska", 1, false, 1) == true)
      {
        Serial.println(F("WiFi AP created!"));
      }
    }*/
  WiFi.mode(WIFI_STA);

  WiFi.begin(s.wifi_ssid, s.wifi_password);
  Serial.printf("Trying to connect wifi [%s] with password [%s]\n", s.wifi_ssid, s.wifi_password);

  if (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    Serial.println(F("WiFi Failed!"));
    WiFi.disconnect();
    delay(100);

    scan_and_store_wifis();

    backup_ap_mode_enabled = true;
    check_forced_restart(true); // schedule restart
  }
  else
  {
    Serial.printf("Connected to wifi [%s] with IP Address:", s.wifi_ssid);
    Serial.println(WiFi.localIP());
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);

    if (!LittleFS.exists(wifis_file_name))
    { // no wifi list found
      Serial.println("No wifi list found - rescanning...");
      scan_and_store_wifis();
    }
  }

  if (backup_ap_mode_enabled) // Softap should be created if  cannot connect to wifi
  {                           // TODO: check also https://github.com/me-no-dev/ESPAsyncWebServer/blob/master/examples/CaptivePortal/CaptivePortal.ino
    WiFi.mode(WIFI_OFF);
    delay(2000);
    WiFi.mode(WIFI_AP);
    // create ap-mode ssid for config wifi
    String mac = WiFi.macAddress();
    for (int i = 14; i > 0; i -= 3)
    {
      mac.remove(i, 1);
    }
    String APSSID = String("ARSKANODE-") + mac;
    Serial.print(F("Creating AP:"));
    Serial.println(APSSID);

    //    if (WiFi.softAP(APSSID.c_str(), "arskanode", (int)random(1, 14), false, 3) == true)
    if (WiFi.softAP(APSSID.c_str(), "", (int)random(1, 14), false, 3) == true)
    {
      Serial.print(F("WiFi AP created with ip:"));
      Serial.println(WiFi.softAPIP().toString());

      // dnsServer.start(53, "*", WiFi.softAPIP());
      // server_web.on(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER,)
    }
    else
    {
      Serial.println(F("Cannot create AP, restarting"));
      delay(2000); // cannot create AP, restart
      ESP.restart();
    }
  }

  // init relays
  //  split comma separated gpio string to an array
  uint16_t channel_gpios[CHANNEL_COUNT];
  char ch_gpios_local[35];
  strcpy(ch_gpios_local, CH_GPIOS);
  str_to_uint_array(ch_gpios_local, channel_gpios, ","); // ESP32: first param must be locally allocated to avoid memory protection crash
  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {
    s.ch[channel_idx].gpio = channel_gpios[channel_idx];
    s.ch[channel_idx].toggle_last = now;
    // reset values fro eeprom
    s.ch[channel_idx].wanna_be_up = false;
    s.ch[channel_idx].is_up = false;
    if ((s.ch[channel_idx].type >> 1 << 1) == CH_TYPE_GPIO_ONOFF)
    { // gpio channel
      pinMode(s.ch[channel_idx].gpio, OUTPUT);
      Serial.printf("Setting channel %d with gpio %d to OUTPUT mode\n", channel_idx, s.ch[channel_idx].gpio);
    }
    // Serial.println((s.ch[channel_idx].is_up ? "HIGH" : "LOW"));
    set_channel_switch(channel_idx, s.ch[channel_idx].is_up);
  }

  Serial.printf("Web admin: [%s], password: [%s]\n", s.http_username, s.http_password);

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
    if (time(nullptr) < 1600000000)
      getRTC(); // Fallback to RTC on startup if we are before 2020-09-13
  }

#endif

#ifdef OTA_UPDATE_ENABLED
  // wait for update
  if (s.next_boot_ota_update)
  {
    // TODO: password protection
    s.next_boot_ota_update = false; // next boot is normal
    writeToEEPROM();

    server_OTA.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                  { request->send(200, "text/html", "<html><body><h2>Update mode</h2><a href='/update'>update</a> | <a href='/restart'>restart</a></body></html>"); });

    server_OTA.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request)
                  {  request->send(200, "text/html", "<html><head><meta http-equiv='refresh' content='10; url=./' /></head><body></body></html>"); 
                    ESP.restart(); });

    AsyncElegantOTA.begin(&server_OTA, s.http_username, s.http_password); // Start ElegantOTA
    server_ota_started = millis();
    server_OTA.begin();
    while (true)
    {
      delay(1000); // just wait here, until uploaded or restarted manually
    }
  }
#endif

// configTime ESP32 and ESP8266 libraries differ
#ifdef ESP32
  configTime(0, 0, ntp_server); // First connect to NTP server, with 0 TZ offset
  struct tm timeinfo;
  /*
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("  Failed to obtain time");
    return;
  } */
  setenv("TZ", timezone_info, 1); //  Now adjust the TZ.  Clock settings are adjusted to show the new local time
  tzset();
#elif defined(ESP8266)
  // TODO: prepare for no internet connection? -> channel defaults probably, RTC?
  // https://werner.rothschopf.net/202011_arduino_esp8266_ntp_en.htm
  configTime(timezone_info, ntp_server); // --> Here is the IMPORTANT ONE LINER needed in your sketch!
#endif

  server_web.on("/status", HTTP_GET, onWebStatusGet);
  server_web.on("/state_series", HTTP_GET, onWebStateSeriesGet);

  server_web.on("/", HTTP_GET, onWebDashboardGet);
  server_web.on("/", HTTP_POST, onWebDashboardPost);
  /*
    server_web.on("/inputs", HTTP_GET, [](AsyncWebServerRequest *request) {
      if (!request->authenticate(s.http_username, s.http_password))
        return request->requestAuthentication();
      check_forced_restart(true); // if in forced ap-mode, reset counter to delay automatic restart
      request->send(LittleFS, "/inputs_template.html", "text/html", false, setup_form_processor); });
  */
  server_web.on("/inputs", HTTP_GET, onWebInputsGet);
  server_web.on("/inputs", HTTP_POST, onWebInputsPost);

  server_web.on("/channels", HTTP_GET, onWebChannelsGet);
  server_web.on("/channels", HTTP_POST, onWebChannelsPost);

  server_web.on("/admin", HTTP_GET, onWebAdminGet);
  server_web.on("/admin", HTTP_POST, onWebAdminPost);

  server_web.on("/update", HTTP_GET, bootInUpdateMode); // now we should restart in update mode

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

  server_web.on("/data/template-list.json", HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, F("/data/template-list.json"), F("application/json")); });

  server_web.on("/data/templates", HTTP_GET, onWebTemplateGet);

  //**
  server_web.on(variables_filename, HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, variables_filename, F("application/json")); });
  server_web.on(fcst_filename, HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, fcst_filename, F("application/json")); });
  server_web.on(price_data_filename, HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, price_data_filename, F("text/plain")); });

  //**

  // debug
  server_web.on("/wifis.json", HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, "/wifis.json", "text/json"); });

  /* if (backup_ap_mode_enabled) {
     server_web.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);//only when requested from AP
   }
   */

  server_web.onNotFound(notFound);

  server_web.begin();

  Serial.print(F("setup() finished:"));
  Serial.println(ESP.getFreeHeap());

  // wifi_request.status = 0u; // listening //TESTING...

} // end of setup()

// returns start time period (first second of an hour if 60 minutes netting period) of time stamp,
long get_period_start_time(long ts)
{
  return long(ts / (NETTING_PERIOD_MIN * 60UL)) * (NETTING_PERIOD_MIN * 60UL);
}

// This function is executed repeatedly after setpup()
void loop()
{
  // Serial.print(F("Starting loop"));
  /*if (!s.sta_mode) {
    dnsServer.processNextRequest();
  }*/

#ifdef OTA_UPDATE_ENABLED
  // resetting and rebooting in update more
  if (s.next_boot_ota_update || restart_required)
  {
    delay(1000);
    ESP.restart();
  }
#endif
  if (scan_and_store_wifis_requested)
  {
    scan_and_store_wifis_requested = false;
    scan_and_store_wifis();
  }
  if (set_relay_requested)
  { // relays forced up or so...
    set_relay_requested = false;
    // update_channel_statuses();
    update_channel_states2(); // new
    set_relays();
  }

  check_forced_restart(); // if in forced ap-mode restart if scheduled so
  time(&now);
  if (now < 1600000000) // we need clock set
    return;
  if (started < 1600000000)
    started = now;

  current_period_start = get_period_start_time(now); // long(now / (NETTING_PERIOD_MIN * 60UL)) * (NETTING_PERIOD_MIN * 60UL);
  if (get_period_start_time(now) == get_period_start_time(started))
    recording_period_start = started;
  else
    recording_period_start = current_period_start;

  if (previous_period_start != current_period_start)
    period_changed = true; // more readable

  if (WiFi.waitForConnectResult(10000) != WL_CONNECTED)
  {
    for (int wait_loop = 0; wait_loop < 10; wait_loop++)
    {
      delay(1000);
      Serial.print('W');
      if (WiFi.waitForConnectResult(10000) == WL_CONNECTED)
        break;
    }
    if (WiFi.waitForConnectResult(10000) != WL_CONNECTED)
    {
      Serial.println(F("Restarting."));
      ESP.restart(); // boot if cannot recover wifi in time
    }
  }

  bool ok;
  // getLocalTime(&timeinfo);
  // time(&now);

  if (run_price_process)
  {
    run_price_process = false;
    Serial.println("Run update_external_variables");
    ok = update_external_variables();
    Serial.println("Returned from update_external_variables");
    return;
  }
  // Serial.printf("now: %ld \n", now);
  // next_price_fetch = now + ok ? 1200 : 120;
  if (next_price_fetch < now)
  {

    Serial.printf("now: %lld \n", now);
    next_price_fetch = now + 1200;
    ok = get_price_data();
    run_price_process = ok;
    time(&now);
    next_price_fetch = now + (ok ? 1200 : 120);
    Serial.printf("next_price_fetch: %lld \n", next_price_fetch);

    // this could be in an own branch
    getBCDCForecast();
  }

  // TODO: all sensor /meter reads could be here?, do we need diffrent frequencies?
  if (((millis() - sensor_last_refresh) > process_interval_s * 1000) || period_changed)
  {
    Serial.print(F("Reading sensor and meter data..."));
#ifdef SENSOR_DS18B20_ENABLED
    read_sensor_ds18B20();
#endif

    read_energy_meter();

    processing_states = true;

    update_internal_variables();

    refresh_variables(current_period_start);
    // refresh_states(current_period_start);
    processing_states = false;

    sensor_last_refresh = millis();
    // update_channel_statuses(); // tässä voisi katsoa onko tarvetta mennä tähän eli onko tullut muutosta
    update_channel_states2(); // new pilot
    set_relays();
  }
  if (period_changed)
  {
    previous_period_start = current_period_start;
    period_changed = false;
  }

#ifdef INVERTER_SMA_MODBUS_ENABLED
  mb.task(); // process modbuss event queue
#endif

  // delay(5000);
}
