/*
(C) Netgalleria Oy, 2021-2022
Olli Rinne

Resource files (see data subfolder):
- arska.js - web UI Javascript routines
- style.css - web UI styles
- admin_template.html - html template file for admin web UI
- view_template.html - html template file for dashboard UI

*/

#include <Arduino.h>
#include <math.h> //round
#include <EEPROM.h>

#include <LittleFS.h>

const char compile_date[] = __DATE__ " " __TIME__;

//#include <DNSServer.h> // for captive portal

#ifdef ESP32 // not fully implemented with ESP32
#include <WiFi.h>
#include <AsyncTCP.h>
#elif defined(ESP8266) // tested only with ESP8266
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#endif

// features enabled
// #define SENSOR_DS18B20_ENABLED
#define QUERY_ARSKA_ENABLED
#define METER_SHELLY3EM_ENABLED
#define INVERTER_FRONIUS_SOLARAPI_ENABLED // can read Fronius inverter solarapi
#define INVERTER_SMA_MODBUS_ENABLED       // can read SMA inverter Modbus TCP
//#define RTC_DS3231_ENABLED


#define TARIFF_STATES_FI // add Finnish tariffs (yösähkö,kausisähkö) to active states

#define OTA_UPDATE_ENABLED

#define eepromaddr 0
#define WATT_EPSILON 50

const char *config_file_name PROGMEM = "/config.json";

#define NETTING_PERIOD_MIN 60 // should be 60, later 15

#define STATES_DEVICE_MAX 999 // states belon (incl) are not replicated
#define STATES_LOCAL_MAX 9999

// Arska states generated internally
#define STATE_BUYING 1001
#define STATE_SELLING 1005
#define STATE_SELLING_BNOON 1006
#define STATE_SELLING_ANOON 1007
#define STATE_EXTRA_PRODUCTION 1010
#define STATE_EXTRA_PRODUCTION_BNOON 1011
#define STATE_EXTRA_PRODUCTION_ANOON 1012

#define STATE_DAYENERGY_FI 130
#define STATE_NIGHTENERGY_FI 131
#define STATE_WINTERDAY_FI 140
#define STATE_WINTERDAY_NO_FI 141

#include <ESPAsyncWebServer.h>

// https://werner.rothschopf.net/202011_arduino_esp8266_ntp_en.htm
#include <time.h>

time_t now; // this is the epoch
tm tm_struct;


// for timezone https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
char ntp_server[35];
char timezone_info[35]; // read from config.json "CET-1CEST,M3.5.0/02,M10.5.0/03", "EET-2EEST,M3.5.0/3,M10.5.0/4"
char price_area[8];

time_t forced_restart_ts = 0; // if wifi in forced ap-mode restart automatically to reconnect/start
bool backup_ap_mode_on = false;

#define FORCED_RESTART_DELAY 600 // If cannot create Wifi connection, goes to AP mode for 600 sec and restarts

void check_forced_restart(bool reset_counter = false)
{
  // tässä tapauksessa kello ei välttämättä ei kunnossa ellei rtc, käy läpi tapaukset
  if (!backup_ap_mode_on) // only valid if forced ap-mode (no normal wifi)
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
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

// Clock functions, supports optional DS3231 RTC
// RTC based on https://werner.rothschopf.net/microcontroller/202112_arduino_esp_ntp_rtc_en.htm
bool rtc_found = false;
/*
    Sets the internal time
    epoch (seconds in GMT)
    microseconds
*/
void setInternalTime(uint64_t epoch = 0, uint32_t us = 0)
{
  struct timeval tv;
  tv.tv_sec = epoch;
  tv.tv_usec = us;
  settimeofday(&tv, NULL);
}
const int force_up_hours[] = {1, 2, 4, 8, 12, 24};

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
int64_t getTimestamp(int year, int mon, int mday, int hour, int min, int sec)
{
  const uint16_t ytd[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};                /* Anzahl der Tage seit Jahresanfang ohne Tage des aktuellen Monats und ohne Schalttag */
  int leapyears = ((year - 1) - 1968) / 4 - ((year - 1) - 1900) / 100 + ((year - 1) - 1600) / 400; /* Anzahl der Schaltjahre seit 1970 (ohne das evtl. laufende Schaltjahr) */
  int64_t days_since_1970 = (year - 1970) * 365 + leapyears + ytd[mon - 1] + mday - 1;
  if ((mon > 2) && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)))
    days_since_1970 += 1; /* +Schalttag, wenn Jahr Schaltjahr ist */
  return sec + 60 * (min + 60 * (hour + 24 * days_since_1970));
}
/*
    print time of RTC to Serial
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
    Serial.print(F(", temperature:"));
    Serial.println(rtc.getTemperature());
  }
}

/*
   set date/time of external RTC
*/
void setRTC()
{
  Serial.println(F("setRTC --> from internal time"));
  time_t now;          // this are the seconds since Epoch (1970) - seconds GMT
  tm tm;               // the structure tm holds time information in a more convient way
  time(&now);          // read the current time and store to now
  gmtime_r(&now, &tm); // update the structure tm with the current GMT
  rtc.adjust(DateTime(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec));
}
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
    // Serial.print(F("UTC:")); Serial.println(newTime);
    printRTC();
  }
}

#endif

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

// data strcuture limits
//#define CHANNELS 2
#define CHANNEL_TYPES 6
#define CH_TYPE_UNDEFINED1 0
#define CH_TYPE_UNDEFINED2 1
#define CH_TYPE_GPIO_ONOFF 2
#define CH_TYPE_GPIO_TARGET 3
#define CH_TYPE_SHELLY_ONOFF 4
#define CH_TYPE_SHELLY_TARGET 5
#define CH_TYPE_DISABLED 255 // RFU, we could have disabled allocated channels (binary )

const char *channel_type_strings[] PROGMEM = {
    "undefined",
    "undefined",
    "GPIO ON/OFF",
    "GPIO target",
    "Shelly ON/OFF",
    "Shelly target",
};

// #define CHANNEL_TARGETS_MAX 3 //platformio.ini
#define CHANNEL_STATES_MAX 10
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
unsigned long energy_produced_period = 0;
unsigned long power_produced_period_avg = 0;
#endif

typedef struct
{
  uint16_t upstates[CHANNEL_STATES_MAX];
  float target;
  bool switch_on;
  bool target_active; // for showing if the target is currently active
} target_struct;

typedef struct
{
  target_struct target[CHANNEL_TARGETS_MAX];
  char id_str[MAX_CH_ID_STR_LENGTH];
  uint8_t gpio;
  bool is_up;
  bool wanna_be_up;
  byte type;
  time_t uptime_minimum;
  time_t toggle_last;
  time_t force_up_until;
} channel_struct;

// TODO: add fixed ip, subnet?
typedef struct
{
  int check_value;
  bool sta_mode; // removed, always, excect backup?
  char wifi_ssid[MAX_ID_STR_LENGTH];
  char wifi_password[MAX_ID_STR_LENGTH];
  char http_username[MAX_ID_STR_LENGTH];
  char http_password[MAX_ID_STR_LENGTH];
  channel_struct ch[CHANNELS];
#ifdef QUERY_ARSKA_ENABLED
  char pg_host[MAX_URL_STR_LENGTH];
  uint16_t pg_cache_age;
  char pg_api_key[37];
#endif
#if defined(INVERTER_FRONIUS_SOLARAPI_ENABLED) || defined(INVERTER_SMA_MODBUS_ENABLED)
  uint32_t baseload; // production above base load is "free" to use/store
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

uint16_t active_states[ACTIVE_STATES_MAX];

// parse char array to uint16_t array (e.g. states, ip address)
// note: current version alter str_in, so use copy in calls if original still needed
void str_to_uint_array(const char *str_in, uint16_t array_out[CHANNEL_STATES_MAX], const char *separator)
{
  char *ptr = strtok((char *)str_in, separator);
  byte i = 0;

  for (int ch_state_idx = 0; ch_state_idx < CHANNEL_STATES_MAX; ch_state_idx++)
  {
    array_out[ch_state_idx] = 0;
  }

  while (ptr)
  {
    Serial.print(atol(ptr));
    Serial.print(",");
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
// Testing before saving if Wifi settings are ok
// Use only when there is no existing connection - this will break one
bool test_wifi_settings(char *wifi_ssid, char *wifi_password)
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    WiFi.disconnect();
    return false;
  }
  else
  {
    WiFi.disconnect();
    return true;
  }
}
#define CONFIG_JSON_SIZE_MAX 1000
bool copy_doc_str(StaticJsonDocument<CONFIG_JSON_SIZE_MAX> &doc, char *key, char *tostr)
{
  if (doc.containsKey(key))
  {
    strcpy(tostr, doc[key]);
    return true;
  }
  return false;
}

bool read_config_file(bool init_settings)
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
  

  if (!init_settings) // read only basic config
    return true;

  copy_doc_str(doc, (char *)"http_username", s.http_username);
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
  if (doc.containsKey("pg_cache_age"))
    s.pg_cache_age = doc["pg_cache_age"];
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
  Serial.print(F("readFromEEPROM:"));
}

// writes settigns to eeprom
void writeToEEPROM()
{
  // channel
  EEPROM.put(eepromaddr, s); // write data to array in ram
  EEPROM.commit();
  Serial.print(F("writeToEEPROM:"));
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

String httpGETRequest(const char *url, const char *cache_file_name)
{
  WiFiClient client;
  HTTPClient http;

  http.begin(client, url);           //  IP address with path or Domain name with URL path
  int httpResponseCode = http.GET(); //  Send HTTP POST request

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
    Serial.print(F("Error code: "));
    Serial.println(httpResponseCode);
    http.end();
    return String("");
  }
  // Free resources
  http.end();
  return payload;
}

#ifdef SENSOR_DS18B20_ENABLED

// TODO: reset (Voltage low) if value not within range
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

// new version under construction
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
  // Serial.println();

  return true;
} // read_inverter_fronius
#endif

#ifdef INVERTER_SMA_MODBUS_ENABLED

ModbusIP mb; // ModbusIP object
#define REG_COUNT 2
uint16_t buf[REG_COUNT];
uint16_t trans;
// IPAddress remote(84,231,164,210);
// IPAddress remote();

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
    Serial.println(F("Modbus read succesfull"));
  }

  return true;
}

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

bool read_inverter_sma_data(long int &total_energy, long int &current_power)
{
  // IPAddress remote(); // veikkola.duckdns.org 84.231.164.210
  // IPAddress remote(84,231,164,210);
  // remote.fromString(s.energy_meter_host);
  // tässä voisi olla ip
  uint16_t ip_octets[CHANNEL_STATES_MAX];
  char host_ip[16];
  strcpy(host_ip, s.energy_meter_host); // seuraava kutsu sotkee, siksi siksi kopio
  // char const *sep_point = ".";
  str_to_uint_array(host_ip, ip_octets, ".");
  // str_to_uint_array(host_ip, ip_octets, sep_point);

  IPAddress remote(ip_octets[0], ip_octets[1], ip_octets[2], ip_octets[3]);

  uint16_t ip_port = s.energy_meter_port;
  uint8_t modbusip_unit = s.energy_meter_id;

  Serial.print(F("ip_port, modbusip_unit: "));
  Serial.print(ip_port);
  Serial.println(modbusip_unit);

  if (!mb.isConnected(remote))
  {
    Serial.print(F("Connecting Modbus TCP"));
    bool cresult = mb.connect(remote, ip_port);
    Serial.println(cresult);
  }

  if (mb.isConnected(remote))
  { // Check if connection to Modbus Slave is established
    total_energy = get_mbus_value(remote, SMA_TOTALENERGY_OFFSET, 2, modbusip_unit);
    Serial.print(F(" total energy Wh:"));
    Serial.print(total_energy);

    current_power = get_mbus_value(remote, SMA_POWER_OFFSET, 2, modbusip_unit);
    Serial.print(F(", current power W:"));
    Serial.println(current_power);

    mb.disconnect(remote); // disconect in the end

    return true;
  }
  else
  {
    Serial.println(F("NOT CONNECTED"));
    return false;
  }

} // read_inverter_sma_data
#endif

void read_inverter()
{
  // global: recording_period_start
  // three globals updated: inverter_total_period_init, energy_produced_period, power_produced_period_avg

  long int total_energy = 0;
  long int current_power = 0;

  bool reakOk = false;
  if (s.energy_meter_type == ENERGYM_FRONIUS_SOLAR)
  {
    reakOk = read_inverter_fronius_data(total_energy, current_power);
    if ((long)inverter_total_period_init > total_energy)
      inverter_total_period_init = 0; // day have changed probably, reset counter, we get day totals from Fronius
  }

  else if (s.energy_meter_type == ENERGYM_SMA_MODBUS_TCP)
    reakOk = read_inverter_sma_data(total_energy, current_power);

  if (reakOk)
  {
    time(&energym_read_last);

    if (period_changed)
    {
      Serial.println(F("PERIOD CHANGED"));
      inverter_total_period_init = total_energy; // global
    }

    energy_produced_period = total_energy - inverter_total_period_init;
    long int time_since_recording_period_start = now - recording_period_start;

    if (time_since_recording_period_start > USE_POWER_TO_ESTIMATE_ENERGY_SECS) // in the beginning of period use current power to estimate energy generated
      power_produced_period_avg = energy_produced_period * 3600 / time_since_recording_period_start;
    else
    {
      power_produced_period_avg = current_power;
    }

    Serial.printf("energy_produced_period: %ld , time_since_recording_period_start: %ld , power_produced_period_avg: %ld , current_power:  %ld\n", energy_produced_period, time_since_recording_period_start, power_produced_period_avg, current_power);
  }

} // read_inverter

#ifdef QUERY_ARSKA_ENABLED
bool is_cache_file_valid(const char *cache_file_name, unsigned long max_age_sec)
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
  filter["ts"] = true; // first get timestamp field

  StaticJsonDocument<50> doc_ts;

  DeserializationError error = deserializeJson(doc_ts, cache_file, DeserializationOption::Filter(filter));
  cache_file.close();

  if (error)
  {
    Serial.print(F("Arska server deserializeJson() failed: "));
    Serial.println(error.f_str());
    return false;
  }

  unsigned long ts = doc_ts["ts"];
  time(&now);

  unsigned long age = now - ts;

  if (age > max_age_sec)
  {
    return false;
  }
  else
  {
    return true;
  }
}
#endif

// returns next index ie number of elements
byte get_internal_states(uint16_t state_array[CHANNEL_STATES_MAX])
{
  time(&now);
  localtime_r(&now, &tm_struct);

  time_t now_suntime = now + s.lon * 240;
  byte sun_hour = int((now_suntime % (3600 * 24)) / 3600);
  //  clean old
  for (int i = 0; i < CHANNEL_STATES_MAX; i++)
  {
    state_array[i] = 0;
  }
  // add internally generated states
  byte idx = 0;
  state_array[idx++] = 1;                       // 1 is always on
  state_array[idx++] = 100 + tm_struct.tm_hour; // time/hour based

#ifdef METER_SHELLY3EM_ENABLED
  // grid energy meter enabled
  if (s.energy_meter_type == ENERGYM_SHELLY3EM)
  {
    float net_energy_in = (energyin - energyout - energyin_prev + energyout_prev);
    if (net_energy_in < -WATT_EPSILON)
    {
      state_array[idx++] = STATE_SELLING;
      if (sun_hour < 12)
        state_array[idx++] = STATE_SELLING_BNOON;
      else
        state_array[idx++] = STATE_SELLING_ANOON;
    }
    else if (net_energy_in > WATT_EPSILON)
    {
      state_array[idx++] = STATE_BUYING;
    }
  }
#endif

#ifdef TARIFF_STATES_FI
  // päiväsähkö/yösähkö (Finnish day/night tariff)
  if (6 < tm_struct.tm_hour && tm_struct.tm_hour < 22)
  { // day
    state_array[idx++] = STATE_DAYENERGY_FI;
  }
  else
  {
    state_array[idx++] = STATE_NIGHTENERGY_FI;
  }
  // Finnish seasonal tariff, talvipäivä/winter day
  if ((6 < tm_struct.tm_hour && tm_struct.tm_hour < 22) && (tm_struct.tm_mon > 9 || tm_struct.tm_mon < 3) && tm_struct.tm_wday != 0)
  {
    state_array[idx++] = STATE_WINTERDAY_FI;
  }
  else
  {
    state_array[idx++] = STATE_WINTERDAY_NO_FI;
  }
#endif

#if defined(INVERTER_FRONIUS_SOLARAPI_ENABLED) || defined(INVERTER_SMA_MODBUS_ENABLED)
  // TODO: tsekkaa miksi joskus nousee ylös lyhyeksi aikaa vaikkei pitäisi
  if (power_produced_period_avg > (s.baseload + WATT_EPSILON))
  { //"extra" energy produced, more than estimated base load
    state_array[idx++] = STATE_EXTRA_PRODUCTION;
    if (sun_hour < 12)
      state_array[idx++] = STATE_EXTRA_PRODUCTION_BNOON;
    else
      state_array[idx++] = STATE_EXTRA_PRODUCTION_ANOON;
  }

#endif
  return idx;
}

void refresh_states(time_t current_period_start)
{
  // get first internal states, then add  more from PG server
  byte idx = get_internal_states(active_states);

#ifndef QUERY_ARSKA_ENABLED
  return; // fucntionality disabled
#endif
  if (strlen(s.pg_host) == 0)
    return;

  Serial.print(F(" refresh_states "));
  Serial.print(F("  current_period_start: "));
  Serial.println(current_period_start);

  StaticJsonDocument<16> filter;
  char start_str[11];
  itoa(current_period_start, start_str, 10);
  filter[(const char *)start_str] = true;

  StaticJsonDocument<300> doc;
  DeserializationError error;

  // TODO: what happens if cache is expired and no connection to state server

  if (is_cache_file_valid(pg_state_cache_filename, s.pg_cache_age))
  {
    Serial.println(F("Using cached data"));
    File cache_file = LittleFS.open(pg_state_cache_filename, "r");
    error = deserializeJson(doc, cache_file, DeserializationOption::Filter(filter));
    cache_file.close();
  }
  else
  {
    Serial.println(F("Cache not valid. Querying..."));
    // TODO:hardcoded price area
    //  String url_to_call = String(s.pg_url) + "&states=";
    String url_to_call = "http://" + String(s.pg_host) + "/state_series?price_area=" + price_area + "&location=" + String(s.forecast_loc) + "&api_key=" + String(s.pg_api_key);
    Serial.println(url_to_call);
    error = deserializeJson(doc, httpGETRequest(url_to_call.c_str(), pg_state_cache_filename), DeserializationOption::Filter(filter));
  }

  if (error)
  {
    Serial.print(F("DeserializeJson() state query failed: "));
    Serial.println(error.f_str());
    return;
  }

  JsonArray state_list = doc[start_str];

  for (unsigned int i = 0; i < state_list.size(); i++)
  {
    active_states[idx++] = (uint16_t)state_list[i];
    if (idx == CHANNEL_STATES_MAX)
      break;
  }
}

// https://github.com/me-no-dev/ESPAsyncWebServer#send-large-webpage-from-progmem-containing-templates

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

void get_channel_config_fields(char *out, int channel_idx)
{
  char buff[200];
  snprintf(buff, 200, "<div><div class='fldshort'>id: <input name='id_ch_%d' type='text' value='%s' maxlength='9'></div>", channel_idx, s.ch[channel_idx].id_str);
  // Serial.println(strlen(out));
  strcat(out, buff);

  snprintf(buff, 200, "<div class='fldtiny' id='d_uptimem_%d'>mininum up (sec): <input name='ch_uptimem_%d'  type='text' value='%d'></div></div>", channel_idx, channel_idx, (int)s.ch[channel_idx].uptime_minimum);
  strcat(out, buff);

  snprintf(buff, sizeof(buff), "<div class='fldshort'>type:<select name='chty_%d' id='chty_%d' onchange='setChannelFields(this)'>", channel_idx, channel_idx);
  strcat(out, buff);
  bool is_gpio_channel;
  for (int channel_type_idx = 0; channel_type_idx < CHANNEL_TYPES; channel_type_idx++)
  {
    // if gpio channels and non-gpio channels can not be mixed
    //  tässä kai pitäisi toinen ottaa channelista, toinen loopista
    is_gpio_channel = (s.ch[channel_idx].gpio != 255);
    // Serial.printf("is_gpio_channel %d %d %d\n",channel_idx,is_gpio_channel,s.ch[channel_idx].gpio);

    if ((channel_type_idx >> 1 << 1 == CH_TYPE_GPIO_ONOFF && is_gpio_channel) || (channel_type_idx >> 1 << 1 != CH_TYPE_GPIO_ONOFF && !is_gpio_channel && channel_type_idx != 1))
    {
      bool target_based_channel = ((channel_type_idx & 1) == 1);
      if ((sensor_ds18b20_enabled && target_based_channel) || !target_based_channel) { // cannot have target without sensors
        snprintf(buff, sizeof(buff), "<option value='%d' %s>%s</>", channel_type_idx, (s.ch[channel_idx].type == channel_type_idx) ? "selected" : "", channel_type_strings[channel_type_idx]);
        strcat(out, buff);
        }
    }
  }
  strcat(out, "</select></div>");
}

void get_channel_target_fields(char *out, int channel_idx, int target_idx, int buff_len)
{
  String states = state_array_string(s.ch[channel_idx].target[target_idx].upstates);
  char float_buffer[32]; // to prevent overflow if initiated with a long number...
  dtostrf(s.ch[channel_idx].target[target_idx].target, 3, 1, float_buffer);
  snprintf(out, buff_len, "<div class='secbr'><div id='sd_%i_%i' class='fldlong'>condition row %s #%i states:  <input name='st_%i_%i' type='text' value='%s'></div><div class='fldtiny' id='td_%i_%i'>Target:<input class='inpnum' name='t_%i_%i' type='text' value='%s'></div><div id='ctcbd_%i_%i'>on:<br><input type='checkbox' id='ctcb_%i_%i' name='ctcb_%i_%i' value='1' %s></div></div>", channel_idx, target_idx, s.ch[channel_idx].target[target_idx].target_active ? "* ACTIVE *" : "", target_idx + 1, channel_idx, target_idx, states.c_str(), channel_idx, target_idx, channel_idx, target_idx, float_buffer, channel_idx, target_idx, channel_idx, target_idx, channel_idx, target_idx, s.ch[channel_idx].target[target_idx].switch_on ? "checked" : "");
  return;
}

void get_meter_config_fields(char *out)
{
  char buff[200];
  strcpy(out, "<div class='secbr'><h3>Energy meter</h3></div><div class='fld'><select name='emt' id='emt' onchange='setEnergyMeterFields(this.value)'>");

  for (int energym_idx = 0; energym_idx <= ENERGYM_MAX; energym_idx++)
  {
    snprintf(buff, sizeof(buff), "<option value='%d' %s>%s</>", energym_idx, (s.energy_meter_type == energym_idx) ? "selected" : "", energym_strings[energym_idx]);
    strcat(out, buff);
  }
  strcat(out, "</select></div>");
  snprintf(buff, sizeof(buff), "<div id='emhd' class='fld'><div class='fldlong'>host:<input name='emh' id='emh' type='text' value='%s'></div>", s.energy_meter_host);
  strcat(out, buff);
  snprintf(buff, sizeof(buff), "<div id='empd' class='fldtiny'>port:<input name='emp' id='emp' type='text' value='%d'></div>", s.energy_meter_port);
  strcat(out, buff);
  snprintf(buff, sizeof(buff), "<div id='emidd' class='fldtiny'>unit:<input name='emid' id='emid' type='text' value='%d'></div></div>", s.energy_meter_id);
  strcat(out, buff);
  // Serial.println(out);
  return;
}
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
  strcat(out, "</select></div>");

  return;
}

void get_status_fields(char *out)
{
  char buff[150];
  time_t current_time;
  time(&current_time);

  time_t now_suntime = current_time + (s.lon * 240);
  tm tm_sun;

  char time1[9];
  char time2[9];
  char eupdate[20];

  if (current_time < 1600000000)
  {
    strcat(out, "<div class='fld'>CLOCK UNSYNCHRONIZED!</div>");
  }
#ifdef SENSOR_DS18B20_ENABLED

  localtime_r(&temperature_updated, &tm_struct);
  snprintf(buff, 150, "<div class='fld'><div>Temperature: %s (%02d:%02d:%02d)</div></div>", String(ds18B20_temp_c, 2).c_str(), tm_struct.tm_hour, tm_struct.tm_min, tm_struct.tm_sec);
  strcat(out, buff);
  //
#endif
  localtime_r(&current_time, &tm_struct);
  gmtime_r(&now_suntime, &tm_sun);
  snprintf(buff, 150, "<div class='fld'><div>Local time: %02d:%02d:%02d, solar time: %02d:%02d:%02d</div></div>", tm_struct.tm_hour, tm_struct.tm_min, tm_struct.tm_sec, tm_sun.tm_hour, tm_sun.tm_min, tm_sun.tm_sec);
  strcat(out, buff);

  localtime_r(&recording_period_start, &tm_struct);
  sprintf(time1, "%02d:%02d:%02d", tm_struct.tm_hour, tm_struct.tm_min, tm_struct.tm_sec);
  localtime_r(&energym_read_last, &tm_struct);

  if (energym_read_last == 0)
  {
    strcpy(time2, "");
    strcpy(eupdate, ", not updated");
  }
  else
  {
    sprintf(time2, "%02d:%02d:%02d", tm_struct.tm_hour, tm_struct.tm_min, tm_struct.tm_sec);
    strcpy(eupdate, "");
  }

  if (s.energy_meter_type == ENERGYM_SHELLY3EM)
  {
#ifdef METER_SHELLY3EM_ENABLED
    float netEnergyInPeriod;
    float netPowerInPeriod;
    get_values_shelly3m(netEnergyInPeriod, netPowerInPeriod);
    snprintf(buff, 150, "<div class='fld'><div>Period %s-%s: net energy in %d Wh, power in  %d W %s</div></div>", time1, time2, (int)netEnergyInPeriod, (int)netPowerInPeriod, eupdate);
    strcat(out, buff);
#endif
  }
  else if (s.energy_meter_type == ENERGYM_FRONIUS_SOLAR or (s.energy_meter_type == ENERGYM_SMA_MODBUS_TCP))
  {
#if defined(INVERTER_FRONIUS_SOLARAPI_ENABLED) || defined(INVERTER_SMA_MODBUS_ENABLED)
    snprintf(buff, 150, "<div class='fld'><div>Period %s-%s: produced %d Wh, power  %d W %s</div></div>", time1, time2, (int)energy_produced_period, (int)power_produced_period_avg, eupdate);
    strcat(out, buff);
#endif
  }

  return;
}

void get_channel_status_header(char *out, int channel_idx, bool show_force_up)
{
  time(&now);
  char buff[200];
  char buff2[20];
  char buff3[40];
  tm tm_struct2;
  time_t from_now;
  strcpy(buff2, s.ch[channel_idx].is_up ? "up" : "down");
  if (s.ch[channel_idx].is_up != s.ch[channel_idx].wanna_be_up)
    strcat(buff2, s.ch[channel_idx].wanna_be_up ? " (rising)" : "(dropping)");

  if (s.ch[channel_idx].force_up_until > now) {
    localtime_r(&s.ch[channel_idx].force_up_until, &tm_struct);
    from_now =  s.ch[channel_idx].force_up_until-now;
    gmtime_r(&from_now, &tm_struct2); 
    snprintf(buff3, 40, ", forced -> %02d:%02d, left: %02d:%02d ", tm_struct.tm_hour, tm_struct.tm_min, tm_struct2.tm_hour, tm_struct2.tm_min);
  }
  else {
    strcpy(buff3, "");
  }

  snprintf(buff, 200, "<div class='secbr'><h3>Channel %d - %s</h3></div><div class='fld'><div>Current status: %s %s</div></div><!-- gpio %d -->", channel_idx + 1, s.ch[channel_idx].id_str, buff2,buff3,s.ch[channel_idx].gpio);
  strcat(out, buff);
  if (show_force_up)
  {
    snprintf(buff, 200, "<div class='secbr'>Force up:<input type='radio' id='fup_%d_none' name='fup_%d' value='0' checked><label for='fup_%d_none'>0</label>", channel_idx, channel_idx, channel_idx);
    strcat(out, buff);
    int hour_array_element_count = (int)(sizeof(force_up_hours) / sizeof(*force_up_hours));
    for (int hour_idx = 0; hour_idx < hour_array_element_count; hour_idx++)
    {
      snprintf(buff, 200, "<input type='radio' id='fup_%d_%d' name='fup_%d' value='%d'><label for='fup_%d_%d'>%d h</label>", channel_idx, force_up_hours[hour_idx], channel_idx, force_up_hours[hour_idx], channel_idx, force_up_hours[hour_idx], force_up_hours[hour_idx]);
      strcat(out, buff);
    }
    strcat(out, "</div>");
  }

  return;
}

String setup_form_processor(const String &var)
{
  // Javascript replacements
  if (var == "CHANNELS")
    return String(CHANNELS);
  if (var == "CHANNEL_TARGETS_MAX")
    return String(CHANNEL_TARGETS_MAX);

  if (var == "wifi_ssid")
    return s.wifi_ssid;
  if (var == "wifi_password")
    return s.wifi_password;
  if (var == "http_username")
    return s.http_username;
  if (var == "http_password")
    return s.http_password;

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

  if (var == "prog_data")
  {
    return String(compile_date);
  }
  if (var.startsWith("chi_"))
  {
    char out[800];
    int channel_idx = var.substring(4, 5).toInt();
    if (channel_idx >= CHANNELS)
      return String();
    get_channel_status_header(out, channel_idx, false);
    get_channel_config_fields(out, channel_idx);
    return out;
  }
  if (var.startsWith("vch_"))
  {
    char out[1000];
    int channel_idx = var.substring(4, 5).toInt();
    if (channel_idx >= CHANNELS)
      return String();
    get_channel_status_header(out, channel_idx, true);

    return out;
  }

  if (var.startsWith("cht_"))
  {
    char out[2000];
    char buff[500];
    int channel_idx = var.substring(4, 5).toInt();
    if (channel_idx >= CHANNELS)
      return String();

    for (int target_idx = 0; target_idx < CHANNEL_TARGETS_MAX; target_idx++)
    {
      get_channel_target_fields(buff, channel_idx, target_idx, 500 - 1);
      strncat(out, buff, 2000 - strlen(out) - 1);
    }
    return out;
  }
  if (var.startsWith("target_ch_"))
  {
    // e.g target_ch_0_1
    char out[500];
    int channel_idx = var.substring(10, 11).toInt();
    int target_idx = var.substring(12, 13).toInt();
    get_channel_target_fields(out, channel_idx, target_idx, 500);
    return out;
  }
  // TODO:remnove old
  if (var.startsWith("info_ch_"))
  {
    char out[500];
    int channel_idx = var.substring(8, 9).toInt();
    Serial.printf("debug target_ch_: %s %d \n", var.c_str(), channel_idx);
    get_channel_config_fields(out, channel_idx);
    return out;
  }

  if (var == "states")
  {
    return state_array_string(active_states);
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

  if (var == "pg_cache_age")
    return String("7200"); // now fixed
                           // return String(s.pg_cache_age);

#endif

  for (int i = 0; i < CHANNELS; i++)
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
    read_inverter();
#endif
  }
}

int get_channel_to_switch(bool is_rise, int switch_count)
{
  int nth_channel = random(0, switch_count) + 1;
  int match_count = 0;
  for (int channel_idx = 0; channel_idx < CHANNELS; channel_idx++)
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

bool set_channel_switch(int channel_idx, bool up)
{
  int channel_type_group = (s.ch[channel_idx].type >> 1 << 1); // we do not care about the last bit
  // Serial.printf("set_channel_switch channel_type_group %d \n",channel_type_group);
  if (channel_type_group == CH_TYPE_GPIO_ONOFF)
  {
    Serial.print(F("CH_TYPE_GPIO_ONOFF:"));
    Serial.print(s.ch[channel_idx].gpio);
    Serial.print(up);
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
    DeserializationError error = deserializeJson(doc, httpGETRequest(url_to_call.c_str(), ""));

    if (error)
    {
      Serial.print(F("Shelly relay call deserializeJson() failed: "));
      Serial.println(error.f_str());
      return false;
    }
    else
      return true;
  }
  else 
    Serial.print(F("Cannot switch"));

  return false;
}

void set_relays()
{
  int active_state_count = 0;
  bool target_state_match_found;
  time(&now);

  // how many current active states we do have
  for (int i = 0; i < CHANNEL_STATES_MAX; i++)
  {
    if (active_states[i] > 0)
      active_state_count++;
    else
      break;
  }

  // loop channels and check whether channel should be up
  for (int channel_idx = 0; channel_idx < CHANNELS; channel_idx++)
  { // reset target_active variable

    // if channel is up, keep it up at least minimum time
    // if force_up_until defined keep it up till that time

    bool wait_minimum_uptime =  ((now - s.ch[channel_idx].toggle_last) < s.ch[channel_idx].uptime_minimum); // channel must stay up minimum time
    if (s.ch[channel_idx].force_up_until == -1) {// force down
      s.ch[channel_idx].force_up_until = 0;
      wait_minimum_uptime = false;
    }
    bool forced_up = (s.ch[channel_idx].force_up_until > now);                                             // signal to keep it up

    if (s.ch[channel_idx].is_up && (wait_minimum_uptime || forced_up))
    {
      Serial.printf("Not yet time to drop channel %d . Since last toggle %d, force_up_until: %lld .\n", channel_idx, (int)(now - s.ch[channel_idx].toggle_last), s.ch[channel_idx].force_up_until);
      s.ch[channel_idx].wanna_be_up = true;
      continue;
    }

    for (int target_idx = 0; target_idx < CHANNEL_TARGETS_MAX; target_idx++)
    {
      s.ch[channel_idx].target[target_idx].target_active = false;
    }

    if (!s.ch[channel_idx].is_up && forced_up) { // the channel is now down but should be forced up
      s.ch[channel_idx].wanna_be_up = true;
      continue;
    }

    // now checking normal state based conditions
    target_state_match_found = false;
    s.ch[channel_idx].wanna_be_up = false;
    // loop channel targets until there is match (or no more targets)
    for (int target_idx = 0; target_idx < CHANNEL_TARGETS_MAX; target_idx++)
    {
      // check matching states, i.e. if any of target states matches current active states
      for (int act_state_idx = 0; act_state_idx < active_state_count; act_state_idx++)
      {
        for (int ch_state_idx = 0; ch_state_idx < CHANNEL_STATES_MAX; ch_state_idx++)
        {
          if (active_states[act_state_idx] == s.ch[channel_idx].target[target_idx].upstates[ch_state_idx])
          {
            target_state_match_found = true;
#ifdef SENSOR_DS18B20_ENABLED
            // TODO: check that sensor value is valid
            //  states are matching, check if the sensor value is below given target (channel should be up) or reached (should be down)
            // ONOFF
            bool target_based_channel = ((s.ch[channel_idx].type & 1) == 1);
            bool target_not_reached = (ds18B20_temp_c < s.ch[channel_idx].target[target_idx].target);

            if ((!target_based_channel && s.ch[channel_idx].target[target_idx].switch_on) || (target_based_channel && target_not_reached))
            {
              s.ch[channel_idx].wanna_be_up = true;
              s.ch[channel_idx].target[target_idx].target_active = true;
            }
#else
            if (((s.ch[channel_idx].type & 1) == 0 && s.ch[channel_idx].target[target_idx].switch_on))
            {
              s.ch[channel_idx].wanna_be_up = true;
              s.ch[channel_idx].target[target_idx].target_active = true;
            }
#endif
            if (target_state_match_found)
              break;
          }
        }
        if (target_state_match_found)
          break;
      }
      if (target_state_match_found)
        break;
    } // target loop

  } // channel loop

  // random
  int rise_count = 0;
  int drop_count = 0;
  for (int channel_idx = 0; channel_idx < CHANNELS; channel_idx++)
  {
    if (!s.ch[channel_idx].is_up && s.ch[channel_idx].wanna_be_up)
      rise_count++;
    if (s.ch[channel_idx].is_up && !s.ch[channel_idx].wanna_be_up)
      drop_count++;
  }

  //
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
// Web response functions
void onWebAdminGet(AsyncWebServerRequest *request)
{
  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();
  String message;

  check_forced_restart(true); // if in forced ap-mode, reset counter to delay automatic restart

  // request->send_P(200, "text/html", setup_form_html, setup_form_processor);
  request->send(LittleFS, "/admin_template.html", "text/html", false, setup_form_processor);
}
void onWebViewGet(AsyncWebServerRequest *request)
{
  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();
  String message;
  check_forced_restart(true); // if in forced ap-mode, reset counter to delay automatic restart
  // large char array, tested with 14k
  // request->send_P(200, "text/html", channel_view_html, setup_form_processor);

  request->send(LittleFS, "/view_template.html", "text/html", false, setup_form_processor);
}

/*
void reset_board()
{

  delay(1000);
  // write a char(255) / hex(FF) from startByte until endByte into the EEPROM

  for (unsigned int i = eepromaddr; i < eepromaddr + sizeof(s); ++i)
  {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  s.check_value = 0; // 12345 when initiated, so should init after restart

  writeToEEPROM();
  delay(1000);
  ESP.restart();
}
*/
/*
void onWebResetGet(AsyncWebServerRequest *request)
{
  Serial.println(F("Resetting"));
  request->send(200, "text/plain", F("Resetting... Reload after a few seconds."));
  reset_board();
}
*/

void onWebViewPost(AsyncWebServerRequest *request)
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

      if (channel_already_forced || forced_up_hours > 0)
      { // there are  changes
        
        if (forced_up_hours > 0)
        {
          s.ch[channel_idx].force_up_until = now + forced_up_hours * 3600 - 1;
          s.ch[channel_idx].wanna_be_up = true;
        }
        else
        {
          s.ch[channel_idx].force_up_until = -1; //forced down
          s.ch[channel_idx].wanna_be_up = false;
        }
        //forced_up_changes = true;
        if (s.ch[channel_idx].wanna_be_up != s.ch[channel_idx].is_up)
          forced_up_changes = true;
      }

    //  Serial.printf("%d _POST[%s]: %s\n", channel_idx, p->name().c_str(), p->value().c_str());
    }
  }
  if (forced_up_changes)
    set_relays();
  request->redirect("/");
}
void onWebAdminPost(AsyncWebServerRequest *request)
{
  String message;
  // s.node_priority = request->getParam("node_priority", true)->value().toInt();

  strcpy(s.wifi_ssid, request->getParam("wifi_ssid", true)->value().c_str());
  strcpy(s.wifi_password, request->getParam("wifi_password", true)->value().c_str());
  // strcpy(s.http_username, request->getParam("http_username", true)->value().c_str());
  if (request->hasParam("http_password", true) && request->hasParam("http_password2", true))
  {
    if (request->getParam("http_password", true)->value().equals(request->getParam("http_password2", true)->value()) && request->getParam("http_password", true)->value().length() > 5)
      strcpy(s.http_password, request->getParam("http_password", true)->value().c_str());
  }

  if (s.energy_meter_type != request->getParam("emt", true)->value().toInt())
  {
    restart_required = true;
    s.energy_meter_type = request->getParam("emt", true)->value().toInt();
  }

  // Serial.println(request->getParam("emh", true)->value().c_str());
  strcpy(s.energy_meter_host, request->getParam("emh", true)->value().c_str());
  // Serial.println(s.energy_meter_host);
  s.energy_meter_port = request->getParam("emp", true)->value().toInt();
  s.energy_meter_id = request->getParam("emid", true)->value().toInt();

  s.lat = request->getParam("lat", true)->value().toFloat();
  s.lon = request->getParam("lon", true)->value().toFloat();
  strcpy(s.forecast_loc, request->getParam("forecast_loc", true)->value().c_str());

#ifdef INVERTER_FRONIUS_SOLARAPI_ENABLED
  // strcpy(s.fronius_address, request->getParam("fronius_address", true)->value().c_str());
  s.baseload = request->getParam("baseload", true)->value().toInt();

#endif

#ifdef QUERY_ARSKA_ENABLED
  // strcpy(s.pg_url, request->getParam("pg_url", true)->value().c_str());
  strcpy(s.pg_host, request->getParam("pg_host", true)->value().c_str());
  strcpy(s.pg_api_key, request->getParam("pg_api_key", true)->value().c_str());

  s.pg_cache_age = max((int)request->getParam("pg_cache_age", true)->value().toInt(), 3600);
#endif

  // channel/target fields
  char ch_fld[20];
  char state_fld[20];
  char target_fld[20];
  char targetcb_fld[20];

  for (int channel_idx = 0; channel_idx < CHANNELS; channel_idx++)
  {
    snprintf(ch_fld, 20, "ch_uptimem_%i", channel_idx);
    if (request->hasParam(ch_fld, true))
    {
      s.ch[channel_idx].uptime_minimum = request->getParam(ch_fld, true)->value().toInt();
    }

    snprintf(ch_fld, 20, "id_ch_%i", channel_idx);
    if (request->hasParam(ch_fld, true))
      strcpy(s.ch[channel_idx].id_str, request->getParam(ch_fld, true)->value().c_str());
    //  else
    //    Serial.println(ch_fld);

    snprintf(ch_fld, 20, "chty_%i", channel_idx);

    if (request->hasParam(ch_fld, true))
      s.ch[channel_idx].type = request->getParam(ch_fld, true)->value().toInt();

    for (int target_idx = 0; target_idx < CHANNEL_TARGETS_MAX; target_idx++)
    {
      snprintf(state_fld, 20, "st_%i_%i", channel_idx, target_idx);
      snprintf(target_fld, 20, "t_%i_%i", channel_idx, target_idx);
      snprintf(targetcb_fld, 20, "ctcb_%i_%i", channel_idx, target_idx);

      if (request->hasParam(state_fld, true))
      {
        str_to_uint_array(request->getParam(state_fld, true)->value().c_str(), s.ch[channel_idx].target[target_idx].upstates, ",");
        s.ch[channel_idx].target[target_idx].target = request->getParam(target_fld, true)->value().toFloat();
      }

      s.ch[channel_idx].target[target_idx].switch_on = request->hasParam(targetcb_fld, true); // cb checked
    }
  }

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
    s.next_boot_ota_update = true;
    writeToEEPROM(); // save to non-volatile memory
    request->send(200, "text/html", "<html><head><meta http-equiv='refresh' content='10; url=./update' /></head><body>wait...</body></html>");
  }
#endif

  writeToEEPROM();
  if (request->getParam("op", true)->value().equals("ts"))
  {
    restart_required = true;
    request->send(200, "text/html", "<html><head><meta http-equiv='refresh' content='10; url=./' /></head><body>restarting...wait...</body></html>");
  }

  if (request->getParam("op", true)->value().equals("reset"))
  {
    s.check_value = 0; // 12345 when initiated, so should init after restart
    writeToEEPROM();
    restart_required = true;
    request->send(200, "text/html", "<html><head><meta http-equiv='refresh' content='10; url=./' /></head><body>restarting...wait...</body></html>");
  }

  // delete cache file
  LittleFS.remove(pg_state_cache_filename);
  request->redirect("/admin");
}

void onWebStatusGet(AsyncWebServerRequest *request)
{
  if (!request->authenticate(s.http_username, s.http_password))
  {
    return request->requestAuthentication();
  }
  StaticJsonDocument<250> doc; // oli 128, lisätty heapille ja invertterille
  String output;
  JsonObject variables = doc.createNestedObject("variables");

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

  variables["updated"] = meter_read_ts;
  variables["freeHeap"] = ESP.getFreeHeap();
  variables["uptime"] = (unsigned long)(millis() / 1000);
  // TODO: näistä puuttu nyt sisäiset, pitäisikö lisätä vai poistaa kokonaan, onko tarvetta debugille
  for (int i = 0; i < CHANNEL_STATES_MAX; i++)
  {
    // doc["states"][i] = active_states[i];
    if (active_states[i] > 0)
      doc["states"][i] = active_states[i];
    else
      break;
  }

  serializeJson(doc, output);
  request->send(200, "application/json", output);
}

// TODO: do we need authentication?
void onWebStatesGet(AsyncWebServerRequest *request)
{
  if (!LittleFS.exists(pg_state_cache_filename))
  {
    Serial.println(F("No cache file. "));
    return;
  }

  File cache_file = LittleFS.open(pg_state_cache_filename, "r");
  if (!cache_file)
  { // failed to open the file, retrn empty result
    Serial.println(F("Failed to open cache file. "));
    return;
  }

  StaticJsonDocument<16> filter;
  char start_str[11];
  itoa(current_period_start, start_str, 10);
  filter[(const char *)start_str] = true;

  StaticJsonDocument<200> doc_ts;
  DeserializationError error = deserializeJson(doc_ts, cache_file, DeserializationOption::Filter(filter));
  cache_file.close();

  if (error)
  {
    Serial.print(F("Arska server deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }
  String output;
  time(&now);
  doc_ts["ts"] = now;
  doc_ts["node_priority"] = s.node_priority;
  serializeJson(doc_ts, output);
  request->send(200, "application/json", output);
}

void setup()
{
  Serial.begin(115200);
  randomSeed(analogRead(0));

#ifdef SENSOR_DS18B20_ENABLED

  // voltage to 1-wire bus
  // voltage from data pin so we can reset the bus (voltage low) if needed
  pinMode(ONEWIRE_VOLTAGE_GPIO, OUTPUT);
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

  EEPROM.begin(sizeof(s));
  readFromEEPROM();

  if (s.check_value != 12345) // setup not initiated
  {
    Serial.println(F("Initiating eeprom"));
    s.check_value = 12345; // this is indication that eeprom is initiated
    // get init values from config.json
    read_config_file(true);
    for (int channel_idx = 0; channel_idx < CHANNELS; channel_idx++)
    {
      s.ch[channel_idx].type = 0; // GPIO_ONOFF
      s.ch[channel_idx].uptime_minimum = 60;
      s.ch[channel_idx].force_up_until = 0;
      sprintf(s.ch[channel_idx].id_str, "channel %d", channel_idx + 1);
      for (int target_idx = 0; target_idx < CHANNEL_TARGETS_MAX; target_idx++)
      {
        s.ch[channel_idx].target[target_idx] = {{}, 0};
      }
    }

#ifdef OTA_UPDATE_ENABLED
    s.next_boot_ota_update = false;
#endif

    writeToEEPROM();
  }
  
  read_config_file(false); //read the rest

  // split comma separated gpio string to an array
  uint16_t channel_gpios[CHANNELS];
  str_to_uint_array(CH_GPIOS, channel_gpios, ",");
  for (int channel_idx = 0; channel_idx < CHANNELS; channel_idx++)
  {
    s.ch[channel_idx].gpio = channel_gpios[channel_idx];
    s.ch[channel_idx].toggle_last = now;
    // reset values fro eeprom
    s.ch[channel_idx].wanna_be_up = false;
    s.ch[channel_idx].is_up = false;

    if ((s.ch[channel_idx].type >> 1 << 1) == CH_TYPE_GPIO_ONOFF) {// gpio channel
      pinMode(s.ch[channel_idx].gpio, OUTPUT);
    }
    
    Serial.println((s.ch[channel_idx].is_up ? "HIGH" : "LOW"));
    set_channel_switch(channel_idx, s.ch[channel_idx].is_up);
  }

  /*
    if (1 == 2) //Softap should be created if cannot connect to wifi (like in init), redirect
    { // check also https://github.com/me-no-dev/ESPAsyncWebServer/blob/master/examples/CaptivePortal/CaptivePortal.ino
      if (WiFi.softAP("arska-node", "arska", 1, false, 1) == true)
      {
        Serial.println(F("WiFi AP created!"));
      }
    }*/
  bool create_ap = false; //! s.sta_mode;
  WiFi.mode(WIFI_STA);
  WiFi.begin(s.wifi_ssid, s.wifi_password);

  if (WiFi.waitForConnectResult() != WL_CONNECTED)
  {
    Serial.println(F("WiFi Failed!"));
    create_ap = true; // try to create AP instead
    backup_ap_mode_on = true;
    check_forced_restart(true); // schedule restart
  }
  else
  {
    Serial.print(F("IP Address: "));
    Serial.println(WiFi.localIP());
    Serial.println(WiFi.macAddress());
    Serial.println(s.http_username);
    Serial.println(s.http_password);

    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
  }

  if (create_ap) // Softap should be created if  cannot connect to wifi
  {              // TODO: check also https://github.com/me-no-dev/ESPAsyncWebServer/blob/master/examples/CaptivePortal/CaptivePortal.ino

    String mac = WiFi.macAddress();
    for (int i = 14; i > 0; i -= 3)
    {
      mac.remove(i, 1);
    }
    String APSSID = String("ARSKANODE-") + mac;
    Serial.print(F("Creating AP:"));
    Serial.println(APSSID);
    if (WiFi.softAP(APSSID.c_str(), "arskanode", (int)random(1, 14), false, 3) == true)
    {
      Serial.println(F("WiFi AP created with ip"));
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

  // TODO: prepare for no internet connection? -> channel defaults probably, RTC?
  // https://werner.rothschopf.net/202011_arduino_esp8266_ntp_en.htm
  configTime(timezone_info, ntp_server); // --> Here is the IMPORTANT ONE LINER needed in your sketch!

  // server_web.on("/reset", HTTP_GET, onWebResetGet);
  server_web.on("/status", HTTP_GET, onWebStatusGet);
  server_web.on("/state_series", HTTP_GET, onWebStatesGet);
  server_web.on("/admin", HTTP_GET, onWebAdminGet);
  server_web.on("/admin", HTTP_POST, onWebAdminPost);

  server_web.on("/", HTTP_GET, onWebViewGet);
  server_web.on("/", HTTP_POST, onWebViewPost);

  server_web.on("/restart", HTTP_GET, [](AsyncWebServerRequest *request)
                { request->redirect("/"); }); // redirect url, if called from OTA

  server_web.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, "/style.css", "text/css"); });
  server_web.on("/arska.js", HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(LittleFS, "/arska.js", "text/javascript"); });

  /* if (create_ap) {
     server_web.addHandler(new CaptiveRequestHandler()).setFilter(ON_AP_FILTER);//only when requested from AP
   }
   */

  server_web.onNotFound(notFound);

  server_web.begin();

  Serial.print(F("setup() finished:"));
  // Serial.println(s.http_username);
  // Serial.println(s.http_password);
  Serial.println(ESP.getFreeHeap());

} // end of setup()

long get_period_start_time(long ts)
{
  return long(ts / (NETTING_PERIOD_MIN * 60UL)) * (NETTING_PERIOD_MIN * 60UL);
}

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

  // TODO: all sensor /meter reads could be here?, do we need diffrent frequencies?
  if (((millis() - sensor_last_refresh) > process_interval_s * 1000) || period_changed)
  {
    Serial.print(F("Reading sensor and meter data..."));
#ifdef SENSOR_DS18B20_ENABLED
    read_sensor_ds18B20();
#endif

    read_energy_meter();
    refresh_states(current_period_start);
    sensor_last_refresh = millis();
    set_relays(); // tässä voisi katsoa onko tarvetta mennä tähän eli onko tullut muutosta
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
