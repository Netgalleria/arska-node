/*
(C) Netgalleria Oy, Olli Rinne, 2021-2023

Resource files (see data subfolder):
- arska-ui.js - web UI Javascript routines
- ui3.html
- js/arska-ui.js - main javascript code template //TODO:separate variable(constant) and code
- js/jquery-3.6.0.min.js - jquery library
- data/version.txt - file system version info
- data/templates.json - rule template definitions
DEVEL BRANCH


 build options defined in platform.ini
#define ESP32 //if ESP32 platform
#define CHANNEL_COUNT 5 //number of channels, GPIO+virtual
#define CHANNEL_RULES_MAX 4 //max rules/rules per channel
#define ONEWIRE_DATA_GPIO 13
#define INFLUX_REPORT_ENABLED
#define SENSOR_DS18B20_ENABLED // DS18B20 funtionality
#define RTC_DS3231_ENABLED //real time clock functionality
*/

// Features enabled
#define METER_SHELLY3EM_ENABLED           //!< Shelly 3EM functionality enabled
#define INVERTER_FRONIUS_SOLARAPI_ENABLED // can read Fronius inverter solarapi
#define INVERTER_SMA_MODBUS_ENABLED       // can read SMA inverter Modbus TCP
#define METER_HAN_ENABLED
#define METER_HAN_DIRECT_ENABLED
#define LOAD_MGMT_ENABLED
#define PING_ENABLED          // for testing if internet connection etc ok
#define PRICE_ELERING_ENABLED // Experimental price query from Elering
#define OTA_UPDATE_ENABLED    // OTA general
#define DEBUG_FILE_ENABLED
// #define HW_EXTENSIONS_ENABLED //!<uncomment  line to enable SN74HC595, LED and reset button functionality (Shelly),The device must be defined in  hw_templates array
#define DEBUG_MODE_ENABLED
#define NVS_CACHE_ENABLED    // experimental
#define RESET_BUTTON_ENABLED // HomeWizard & Shelly hardware

#ifdef NVS_CACHE_ENABLED
#include "nvs_flash.h"
#include "nvs.h"
#endif

#define FILESYSTEM_LITTLEFS

#ifdef FILESYSTEM_LITTLEFS
#include <LittleFS.h>
#define FILESYSTEM LittleFS
#define fs_filename "littlefs.bin"
#else
#include "SPIFFS.h"
#define FILESYSTEM SPIFFS
#define fs_filename "spiffs.bin"
#endif

#include <Arduino.h>
#include <math.h> //round
#include <EEPROM.h>

#include "WebAuthentication.h"

#include "version.h"

const char compile_date[] = __DATE__ " " __TIME__;
char version_fs[45];
String version_fs_base; //= "";

#include <WiFi.h>
#include <AsyncTCP.h>
#include <HTTPClient.h>

#include <ESPAsyncWebServer.h>

#include <time.h>
#include <WiFiClient.h>

#define DYNAMIC_JSON_DOCUMENT_SIZE 4096
#include "AsyncJson.h" //https://github.com/me-no-dev/ESPAsyncWebServer#json-body-handling-with-arduinojson
#include <ArduinoJson.h>
// 1024 default in non 5

#ifdef PING_ENABLED
#include <ESP32Ping.h>
#endif

#ifdef RTC_DS3231_ENABLED
#include <RTClib.h>
#include <coredecls.h>
#endif

#ifdef HW_EXTENSIONS_ENABLED
#include "driver/adc.h"
#endif

#ifdef SENSOR_DS18B20_ENABLED
#include <OneWire.h>
#include <DallasTemperature.h> // tätä ei ehkä välttämättä tarvita, jos käyttäisi onewire.h:n rutineeja
#endif

#ifdef INVERTER_SMA_MODBUS_ENABLED
#include <ModbusIP_ESP8266.h>
#endif

#ifdef INFLUX_REPORT_ENABLED
#include <InfluxDbClient.h>
#endif

#ifdef OTA_DOWNLOAD_ENABLED
#include <HTTPUpdate.h> //
#endif

#include <Update.h>
#include "esp_idf_version.h"

#define EEPROM_CHECK_VALUE 10103 //!< increment this is data structure changes
#define eepromaddr 0
#define MAX_DS18B20_SENSORS 3         //!< max number of sensors
#define SENSOR_VALUE_EXPIRE_TIME 1200 //!< if new value cannot read in this time (seconds), sensor value is set to 0
// experimental,move this to correct place, get from parameters/hw templates
#define DELAY_AFTER_EXTERNAL_DATA_UPDATE_MS 2000 // delay after longer queries (price, energy forecast) to keep more responsive in the init
#define WATTS_TO_AMPERES_FACTOR 230.0
#define TARIFF_VARIABLES_FI // add Finnish tariffs (yösähkö,kausisähkö) to variables

#define PRICE_RESOLUTION_SEC 3600
#define SOLAR_FORECAST_RESOLUTION_SEC 3600

#define SECONDS_IN_DAY 86400
#define SECONDS_IN_HOUR 3600
#define SECONDS_IN_MINUTE 60
#define HOURS_IN_DAY 24

#define FIRST_BLOCK_START_HOUR 23
#define DAY_BLOCK_SIZE_HOURS 8 //!< a day is divided into 3 blocks of 8 hours, starting from local hour 23

#define PV_FORECAST_HOURS 24 //!< solar forecast consist of this many hours

#define MAX_PRICE_PERIODS 48 //!< number of price period in the memory array
#define MAX_HISTORY_PERIODS 24
#define HISTORY_VARIABLE_COUNT 2

#define HW_TEMPLATE_COUNT 8
#define HW_TEMPLATE_GPIO_COUNT 4 //!< template  max number of hardcoded gpio relays

#define CH_TYPE_UNDEFINED 0
#define CH_TYPE_GPIO_FIXED 1
#define CH_TYPE_GPIO_USER_DEF 3
#define CH_TYPE_SHELLY_1GEN 2        // new, was CH_TYPE_SHELLY_ONOFF
#define CH_TYPE_SHELLY_2GEN 4        //
#define CH_TYPE_TASMOTA 5            //
#define CH_TYPE_GPIO_USR_INVERSED 10 // inversed
#define CH_TYPE_MODBUS_RTU 20        // RFU
#define CH_TYPE_DISABLED 255         // RFU, we could have disabled, but allocated channels (binary )

// #define CHANNEL_RULES_MAX 3 //platformio.ini
#define RULE_STATEMENTS_MAX 5
#define MAX_CHANNELS_SWITCHED_AT_TIME 1

#define MAX_CH_ID_STR_LENGTH 20
#define MAX_ID_STR_LENGTH 30
#define MAX_PWD_STR_LENGTH 15

// Current phase of OTA update, s.ota_update_phase
#define OTA_PHASE_NONE 0
#define OTA_PHASE_FWUPDATED_CHECKFS 100

// Energy metering types
#define ENERGYM_NONE 0
#define ENERGYM_SHELLY3EM 1
#define ENERGYM_SHELLY_GEN2 2
#define ENERGYM_HAN_WIFI 4
#define ENERGYM_HAN_DIRECT 5

// Production metering (inverter) types
#define PRODUCTIONM_NONE 0
#define PRODUCTIONM_FRONIUS_SOLAR 1
#define PRODUCTIONM_SMA_MODBUS_TCP 2

#define ID_NA 255
#define GPIO_STATE_NA -1
#define CHANNEL_TYPE_COUNT 6

#define WIFI_FAILED_RECONNECT_INTERVAL_SEC 300
#define WIFI_FAILED_RESTART_RECONNECT_INTERVAL_SEC 3600
#define ERROR_MSG_LEN 100

// API
// const char *host_prices PROGMEM = "transparency.entsoe.eu"; //!< EntsoE reporting server for day-ahead prices
// fixed 14.2.2023, see https://transparency.entsoe.eu/news/widget?id=63eb9d10f9b76c35f7d06f2e
const char *host_prices PROGMEM = "web-api.tp.entsoe.eu";
const char *host_prices_elering PROGMEM = "dashboard.elering.ee";
const char *host_fcst_fmi PROGMEM = "cdn.fmi.fi";

const char *entsoe_ca_filename PROGMEM = "/data/sectigo_ca.pem";
const char *fmi_ca_filename PROGMEM = "/data/GEANTOVRSACA4.cer";
const char *host_releases PROGMEM = "iot.netgalleria.fi";

// #define OTA_BOOTLOADER "d2ccd8b68260859296c923437d702786"
#define RELEASES_HOST "iot.netgalleria.fi"
#define RELEASES_URL_BASE "/arska-install/releases.php?pre_releases=true"
#define VERSION_SEPARATOR "&version="

#define STR2(a) #a
#define STR(a) STR2(a)
// #define RELEASES_URL RELEASES_URL_BASE OTA_BOOTLOADER VERSION_SEPARATOR VERSION_BASE "&esp_idf_version=" ESP_IDF_VERSION_CUSTOM
#define RELEASES_URL RELEASES_URL_BASE VERSION_SEPARATOR VERSION_BASE "&esp_idf_version=" ESP_IDF_VERSION_CUSTOM

// Channel state info, Work in Progress...

#define CH_STATE_NONE 0
#define CH_STATE_BYRULE 1
#define CH_STATE_BYFORCE 2
#define CH_STATE_BYLMGMT 4
#define CH_STATE_BYDEFAULT 5
#define CH_STATE_BYLMGMT_MORATORIUM 6
#define CH_STATE_BYLMGMT_NOCAPACITY 7
// #define OTA_DOWNLOAD_ENABLED // OTA download from web site, OTA_UPDATE_ENABLED required, ->define in  platform.ini
#define WATT_EPSILON 50

#define MAX_LED_COUNT 3

const char *default_http_password PROGMEM = "arska";
const char *filename_config_in = "/cache/config_in.json";
const char *template_filename PROGMEM = "/data/templates.json";
const char *shadow_settings_filename PROGMEM = "/shadow_settings.json";

const char *ntp_server_1 PROGMEM = "europe.pool.ntp.org";
const char *ntp_server_2 PROGMEM = "time.google.com";
const char *ntp_server_3 PROGMEM = "time.windows.com";

#define MSG_TYPE_INFO 0
#define MSG_TYPE_WARN 1
#define MSG_TYPE_ERROR 2
#define MSG_TYPE_FATAL 3

// Non-volatile memory https://github.com/CuriousTech/ESP-HVAC/blob/master/Arduino/eeMem.cpp
#ifdef INVERTER_SMA_MODBUS_ENABLED
// Modbus registry offsets
#define SMA_DAYENERGY_OFFSET 30535
#define SMA_TOTALENERGY_OFFSET 30529
#define SMA_POWER_OFFSET 30775
#endif

#define USE_POWER_TO_ESTIMATE_ENERGY_SECS 120 // use power measurement to estimate

#define PROCESS_INTERVAL_SECS 60              //!< process interval, eg. energy meter polling and variable calculation
#define ACCEPTED_TIMESTAMP_MINIMUM 1700000000 // if timestamp is greater, we assume it is from a real-time clock

#define CONFIG_JSON_SIZE_MAX 6144

/* Application variable constants */
#define VARIABLE_COUNT 48
#define VARIABLE_LONG_UNKNOWN -2147483648 //!< variable with this value is undefined
// do not change variable id:s (will broke statements)

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
#define VARIABLE_OVERPRODUCTION 100
#define VARIABLE_PRODUCTION_POWER 101 //!< average production power during this period, measured W
#define VARIABLE_SELLING_POWER 102
#define VARIABLE_SELLING_ENERGY 103
#define VARIABLE_SELLING_POWER_NOW 104
#define VARIABLE_PRODUCTION_ENERGY 105
#define VARIABLE_MM 110
#define VARIABLE_MMDD 111
#define VARIABLE_WDAY 112
#define VARIABLE_HH 115
#define VARIABLE_HHMM 116
#define VARIABLE_MINUTES 117
#define VARIABLE_DAYENERGY_FI 130            //!< true if day, (07:00-22:00 Finnish tariffs), logical
#define VARIABLE_WINTERDAY_FI 140            //!< true if winterday, (Finnish tariffs), logical
#define VARIABLE_CHANNEL_UTIL_PERIOD 150     //!< channel utilization this period, minutes
#define VARIABLE_CHANNEL_UTIL_8H 152         //!< channel utilization this hour and 7 previous, minutes
#define VARIABLE_CHANNEL_UTIL_24H 153        //!< channel utilization this hour and 23 previous, minutes
#define VARIABLE_CHANNEL_UTIL_BLOCK_0 155    //!< channel utilization, this block, minutes
#define VARIABLE_CHANNEL_UTIL_BLOCK_M1_0 156 //!< channel utilization, this and previous blocks, minutes
#define VARIABLE_CHANNEL_UTIL_BLOCK_M2_0 157 //!< channel utilization, this and 2 previous blocks, minutes

#define VARIABLE_ESTIMATED_CHANNELS_CONSUMPTION 160 // Wh
#define VARIABLE_SENSOR_1 201                       //!< sensor1 value, float, 1 decimal
// #define VARIABLE_BEEN_UP_AGO_HOURS_0 170 // RFU
// #define VARIABLE_LOCALTIME_TS 1001
#define VARIABLE_SOLAR_MINUTES_TUNED 401 //!< minute counter increasing 0-...(theor 24*60)
#define VARIABLE_SOLAR_PRODUCTION_ESTIMATE_PERIOD 402
#define VARIABLE_WIND_AVG_DAY1_FI 411
#define VARIABLE_WIND_AVG_DAY2_FI 412
#define VARIABLE_WIND_AVG_DAY1B_FI 421
#define VARIABLE_WIND_AVG_DAY2B_FI 422
#define VARIABLE_SOLAR_RANK_FIXED_24 430

#define VARIABLE_NET_ESTIMATE_SOURCE 701 //!< 0-no estimate,1-grid measurement, 2-production measurement - baseload, 3-production estimate - baseload
#define VARIABLE_NET_ESTIMATE_SOURCE_NONE 0L
#define VARIABLE_NET_ESTIMATE_SOURCE_MEAS_ENERGY 1L
#define VARIABLE_NET_ESTIMATE_SOURCE_MEAS_PRODUCTION 2L
#define VARIABLE_NET_ESTIMATE_SOURCE_SOLAR_FORECAST 3L
#define VARIABLE_SELLING_ENERGY_ESTIMATE 702 // Estimate/measured selling energy in period

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

#define CONSTANT_BITMASK_NONE 0
#define CONSTANT_BITMASK_MONTH 112
#define CONSTANT_BITMASK_WEEKDAY 207
#define CONSTANT_BITMASK_HOUR 324
#define CONSTANT_BITMASK_BLOCK8H 403

// io_tasks states
#define STATE_NA 0
#define STATE_NONE 1
#define STATE_INIT 2
#define STATE_CONNECTING 10
#define STATE_PROCESSING 50
#define STATE_UPLOADING 90
#define STATE_COOLING 99

// Led pulse patterns, read from right!
#define LED_PATTERN_SOS 0b10101011101110111010101UL; //testing
#define LED_PATTERN_DARK 0b0UL
#define LED_PATTERN_SHORT 0b1UL
#define LED_PATTERN_2SHORT 0b101L
#define LED_PATTERN_3SHORT 0b10101UL
#define LED_PATTERN_SHORT_LONG 0b11101UL
#define LED_PATTERN_SHORT_2LONG 0b111011101UL
#define LED_PATTERN_SHORT_LONG_SHORT 0b1011101UL



#define STATUS_LED_TYPE_NONE 0
#define STATUS_LED_TYPE_SINGLE_HIGHACTIVE 10
#define STATUS_LED_TYPE_SINGLE_LOWACTIVE 11
#define STATUS_LED_TYPE_RGB3_HIGHACTIVE 30
#define STATUS_LED_TYPE_RGB3_LOWACTIVE 31 // output down do activate led

#define RGB_IDX_RED 0
#define RGB_IDX_GREEN 1
#define RGB_IDX_BLUE 2

#define RGB_NONE 0
#define RGB_BLUE 1
#define RGB_GREEN 2
#define RGB_CYAN 3
#define RGB_RED 4
#define RGB_PURPLE 5
#define RGB_YELLOW 6
#define RGB_WHITE 7

#ifdef HW_EXTENSIONS_ENABLED
#pragma message("HW_EXTENSIONS_ENABLED with SN74hc595 support")
// Hardware extension / Shelly

#define BIT_RELAY 0     // Qa
#define BIT_LED_OUT2 1  // Qb
#define BIT_LED_BLUE 2  // Qc
#define BIT_LED_GREEN 3 // Qd
#define BIT_LED_RED 4   // Qe

#define MAX_REGISTER_BITS 8
// HW extensions shift register, led etc...

#define COOLING_PANIC_SHUTDOWN_F 203 // 95C
#define COOLING_START_F 194          // 90C
#define COOLING_RECOVER_TO_F 185     // 85 C
/*  Testing
#define COOLING_PANIC_SHUTDOWN_F 167 // 75 C
#define COOLING_START_F 149          // 65C
#define COOLING_RECOVER_TO_F 131     // 55
*/
#endif

#define CHANNEL_CONFIG_MODE_RULE 0
#define CHANNEL_CONFIG_MODE_TEMPLATE 1

#define TIMESERIES_ELEMENT_MAX 72

#define OPER_COUNT 11
// #pragma message("Testing with selected oper")

/**
 * @brief Statament checking rules
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
10, "selected", if tru can be multiselected
 */
// TODO: maybe operator NA - not available

/* Application variable definitions */
struct variable_st
{
  uint16_t id;
  uint8_t type;
  uint16_t bitmask_config;
  long val_l;
};

/**
 * @brief Statement structure, rules (rules) consist of one or more statements
 *
 */
struct statement_st
{
  int variable_id;
  uint8_t oper_id;
  uint8_t constant_type;
  long const_val;
};

struct msg_st
{
  uint8_t type; // 0 info, 1-warn, 2 - error, 3-fatal
  time_t ts;
  char msg[ERROR_MSG_LEN];
};

struct hw_io_struct
{
  uint8_t reset_button_gpio;
  int reset_button_normal_state;
  bool output_register; // false
  uint8_t rclk_gpio;    // if shifted
  uint8_t ser_gpio;
  uint8_t srclk_gpio;
  uint8_t status_led_type; // STATUS_LED_TYPE_...
  uint8_t status_led_ids[MAX_LED_COUNT];
};

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
  bool boolean_only; //!< handle variable value as boolean (1=true), eq and reverse possible
  bool has_value;    //!< true if not value not VARIABLE_LONG_UNKNOWN, reverse possible
  bool multiselect;  //!< selected values creating bit combo to value field
};

const oper_st opers[OPER_COUNT] = {{10, "select", false, false, false, false, false, true}, {0, "=", false, true, false, false, false, false}, {1, ">", true, false, false, false, false, false}, {2, "<", true, true, true, false, false, false}, {3, ">=", true, true, false, false, false, false}, {4, "<=", true, false, true, false, false, false}, {5, "<>", false, true, true, false, false, false}, {6, "is", false, false, false, true, false, false}, {7, "not", false, false, true, true, false, false}, {8, "defined", false, false, false, false, true, false}, {9, "undefined", false, false, true, false, true, false}};

struct channel_type_st
{
  uint8_t id;
  const char *name;
  bool inversed;
};

// later , {CH_TYPE_MODBUS_RTU, "Modbus RTU"}
/*
struct device_db_struct
{
  const char *app;
  uint8_t switch_type;
  uint8_t outputs;
};

device_db_struct device_db[] PROGMEM = {{"shelly1l", CH_TYPE_SHELLY_1GEN, 1}, {"shellyswitch", CH_TYPE_SHELLY_1GEN, 2}, {"shellyswitch25", CH_TYPE_SHELLY_1GEN, 2}, {"shelly4pro", CH_TYPE_SHELLY_1GEN, 4}, {"shellyplug", CH_TYPE_SHELLY_1GEN, 1}, {"shellyplug-s", CH_TYPE_SHELLY_1GEN, 1}, {"shellyem", CH_TYPE_SHELLY_1GEN, 1}, {"shellyem3", CH_TYPE_SHELLY_1GEN, 1}, {"shellypro2", CH_TYPE_SHELLY_2GEN, 2}};
*/
struct hw_template_st
{
  int id;
  const char *name;
  uint8_t locked_channels;
  uint8_t relay_id[HW_TEMPLATE_GPIO_COUNT];
  uint8_t energy_meter_gpio;
  hw_io_struct hw_io;
};

// Target/rule row stucture, elements of target array in channel, stored in non-volatile memory
typedef struct
{
  statement_st statements[RULE_STATEMENTS_MAX];
  float target_val; // TODO: remove
  bool on;
  bool rule_active; // for showing if the rule is currently active, for tracing
} rule_struct;      // size 88

// Channel stucture, elements of channel array in setting, stored in non-volatile memory
typedef struct
{
  rule_struct rules[CHANNEL_RULES_MAX];
  char id_str[MAX_CH_ID_STR_LENGTH];
  uint8_t relay_id;         //!< relay id, eg. gpio, number modbus server id
  uint8_t relay_unit_id;    //!<  unit id, eg. port id in a relay device
  uint8_t relay_iface_id;   // RFU, interface, eg eth, wifi
  IPAddress relay_ip;       //!< relay ip address
  bool is_up;               //!< is channel currently up
  bool wanna_be_up;         //!< should channel be switched up (when the time is right)
  bool default_state;       //!< channel up/down value if no rule matches
  uint8_t type;             //!< channel type, for values see constants CH_TYPE_...
  time_t uptime_minimum;    //!< minimum time channel should be up
  time_t up_last_ts;        //!< last time up time
  time_t force_up_from_ts;  //<! force channel up starting from
  time_t force_up_until_ts; //<! force channel up until
  uint8_t config_mode;      //<! rule config mode: CHANNEL_CONFIG_MODE_RULE, CHANNEL_CONFIG_MODE_TEMPLATE
  int template_id;          //<! template id if config mode is CHANNEL_CONFIG_MODE_TEMPLATE
  uint32_t channel_color;   //<! channel UI color in graphs etc
  uint8_t priority;         //<! channel switching priority, channel with the lowest priority value is switched on first and off last
  uint16_t load;            //<! estimated device load in Watts
} channel_struct;

#ifdef SENSOR_DS18B20_ENABLED
typedef struct
{
  DeviceAddress address;             //!< 1-wire hardware address of the sensor device
  char id_str[MAX_CH_ID_STR_LENGTH]; //!< sensor id string, RFU
} sensor_struct;
#endif

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
  uint8_t ota_update_phase;                //!< Phase of curent OTA update, if updating
  uint16_t netting_period_sec;             //!< Variable netting period in seconds, was in constant NETTING_PERIOD_SEC
  uint8_t energy_meter_type;               //!< energy metering type, see constants: ENERGYM_
  IPAddress energy_meter_ip;               //!< enerygy meter address string
  uint16_t energy_meter_port;              //!< energy meter port,  tcp port if energy_meter_type in (ENERGYM_HAN_WIFI,  ENERGYM_SHELLY3EM,ENERGYM_SHELLY_GEN2)
  uint8_t energy_meter_pollingfreq;        //!< polling based meters, frequency in secs
#ifdef METER_HAN_DIRECT_ENABLED
  uint8_t energy_meter_gpio; //!< energy meter gpio , ENERGYM_HAN_DIRECT
#endif
  char energy_meter_password[MAX_PWD_STR_LENGTH];
  uint8_t production_meter_type;
  IPAddress production_meter_ip;
  uint16_t production_meter_port;
  uint8_t production_meter_id;
  char forecast_loc[MAX_ID_STR_LENGTH]; //!< Energy forecast location, FMI-energy location
  char lang[3];                         //<! preferred language
#ifdef SENSOR_DS18B20_ENABLED
  sensor_struct sensors[MAX_DS18B20_SENSORS]; //!< 1-wire temperature sensors
#endif
  int hw_template_id; //!< hardware template defining channel gpios, see hw_templates
  // bool mdns_activated; //!< is mDSN device discovery active, currently deactivatated due to stability concerns
#ifdef INFLUX_REPORT_ENABLED
  char influx_url[70];
  char influx_token[100];
  char influx_org[30];
  char influx_bucket[20];
#endif
  uint32_t baseload; //!< production above baseload is "free" to use/store, used to estimate own consumption when production is read from inverter and no ebergy meter is connected
  uint32_t pv_power; //!<
#ifdef LOAD_MGMT_ENABLED
  bool load_manager_active;                    //!< //
  uint8_t load_manager_phase_count;            //!< 1 or 3 (Europe) //not yet export/import
  uint8_t load_manager_current_max;            //!< max current per phase in Amperes, eg. 25 (A) not yet export/import
  uint16_t load_manager_reswitch_moratorium_m; //<!
#endif
} settings_struct;

typedef struct
{
  bool state;
  time_t this_state_started_period_ts; //!< current state start time within period (minimum period start)
  time_t this_state_started_epoch_ts;
  int on_time;
  int off_time;
} channel_log_struct;

struct wifi_st
{
  char ssid[MAX_ID_STR_LENGTH]; //!< WiFi SSID
  int32_t rssi;
};

/**
 * @brief Class defines variables defined by measurements, calculations and used to define channel statuses
 *
 */

class Variables
{
public:
  Variables();
  bool is_set(int id);
  void set(int id, long value_l);
  void set(int id, float val_f);
  void set_NA(int id);
  long get_l(int id);
  long get_l(int id, long default_val);
  float get_f(int id);
  bool is_statement_true(statement_st *statement, bool default_value, int channel_idx);
  int get_variable_by_id(int id, variable_st *variable);
  int get_variable_by_id(int id, variable_st *variable, int channel_idx);
  void get_variable_by_idx(int idx, variable_st *variable);
  long float_to_internal_l(int id, float val_float);
  float const_to_float(int id, long const_in);
  int to_str(int id, char *strbuff, bool use_overwrite_val = false, long overwrite_val = 0, size_t buffer_length = 1);
  int get_variable_count() { return VARIABLE_COUNT; };
  void rotate_period();

private:
  variable_st variables[VARIABLE_COUNT] = {{VARIABLE_PRICE, CONSTANT_TYPE_DEC1, 0}, {VARIABLE_PRICERANK_9, CONSTANT_TYPE_INT, CONSTANT_BITMASK_NONE}, {VARIABLE_PRICERANK_24, CONSTANT_TYPE_INT, CONSTANT_BITMASK_NONE}, {VARIABLE_PRICERANK_FIXED_24, CONSTANT_TYPE_INT, CONSTANT_BITMASK_NONE}, {VARIABLE_PRICERANK_FIXED_8, CONSTANT_TYPE_INT, CONSTANT_BITMASK_NONE}, {VARIABLE_PRICERANK_FIXED_8_BLOCKID, CONSTANT_TYPE_INT, CONSTANT_BITMASK_BLOCK8H}, {VARIABLE_PRICEAVG_9, CONSTANT_TYPE_DEC1}, {VARIABLE_PRICEAVG_24, CONSTANT_TYPE_DEC1}, {VARIABLE_PRICERATIO_9, CONSTANT_TYPE_DEC1}, {VARIABLE_PRICEDIFF_9, CONSTANT_TYPE_DEC1}, {VARIABLE_PRICEDIFF_24, CONSTANT_TYPE_DEC1}, {VARIABLE_PRICERATIO_24, CONSTANT_TYPE_DEC1}, {VARIABLE_PRICERATIO_FIXED_24, CONSTANT_TYPE_DEC1}, {VARIABLE_PVFORECAST_SUM24, CONSTANT_TYPE_DEC1}, {VARIABLE_PVFORECAST_VALUE24, CONSTANT_TYPE_DEC1}, {VARIABLE_PVFORECAST_AVGPRICE24, CONSTANT_TYPE_DEC1}, {VARIABLE_AVGPRICE24_EXCEEDS_CURRENT, CONSTANT_TYPE_DEC1}, {VARIABLE_OVERPRODUCTION, CONSTANT_TYPE_BOOLEAN_REVERSE_OK}, {VARIABLE_PRODUCTION_POWER, 0}, {VARIABLE_SELLING_POWER, 0, 0}, {VARIABLE_SELLING_ENERGY, 0, 0}, {VARIABLE_SELLING_POWER_NOW, 0, 0}, {VARIABLE_PRODUCTION_ENERGY, 0}, {VARIABLE_MM, CONSTANT_TYPE_CHAR_2, CONSTANT_BITMASK_MONTH}, {VARIABLE_MMDD, CONSTANT_TYPE_CHAR_4, 0}, {VARIABLE_WDAY, 0, CONSTANT_BITMASK_WEEKDAY}, {VARIABLE_HH, CONSTANT_TYPE_CHAR_2, CONSTANT_BITMASK_HOUR}, {VARIABLE_HHMM, CONSTANT_TYPE_CHAR_4, 0}, {VARIABLE_MINUTES, CONSTANT_TYPE_CHAR_2, 0}, {VARIABLE_DAYENERGY_FI, CONSTANT_TYPE_BOOLEAN_REVERSE_OK, 0}, {VARIABLE_WINTERDAY_FI, CONSTANT_TYPE_BOOLEAN_REVERSE_OK, 0}, {VARIABLE_SENSOR_1, CONSTANT_TYPE_DEC1, 0}, {VARIABLE_SENSOR_1 + 1, CONSTANT_TYPE_DEC1, 0}, {VARIABLE_SENSOR_1 + 2, CONSTANT_TYPE_DEC1, 0}, {VARIABLE_CHANNEL_UTIL_PERIOD, CONSTANT_TYPE_INT, 0}, {VARIABLE_CHANNEL_UTIL_8H, CONSTANT_TYPE_INT, 0}, {VARIABLE_CHANNEL_UTIL_24H, CONSTANT_TYPE_INT, 0}, {VARIABLE_CHANNEL_UTIL_BLOCK_M2_0, CONSTANT_TYPE_INT, 0}, {VARIABLE_ESTIMATED_CHANNELS_CONSUMPTION, CONSTANT_TYPE_INT, 0}, {VARIABLE_SOLAR_MINUTES_TUNED, CONSTANT_TYPE_INT, 0}, {VARIABLE_SOLAR_PRODUCTION_ESTIMATE_PERIOD, CONSTANT_TYPE_INT, 0}, {VARIABLE_WIND_AVG_DAY1_FI, CONSTANT_TYPE_INT, 0}, {VARIABLE_WIND_AVG_DAY2_FI, CONSTANT_TYPE_INT, 0}, {VARIABLE_WIND_AVG_DAY1B_FI, CONSTANT_TYPE_INT, 0}, {VARIABLE_WIND_AVG_DAY2B_FI, CONSTANT_TYPE_INT, 0}, {VARIABLE_SOLAR_RANK_FIXED_24, CONSTANT_TYPE_INT, CONSTANT_BITMASK_NONE}, {VARIABLE_NET_ESTIMATE_SOURCE, CONSTANT_TYPE_INT, 0}, {VARIABLE_SELLING_ENERGY_ESTIMATE, CONSTANT_TYPE_INT, 0}};
  int get_variable_index(int id);
};

/**
 * @brief Handling channel utilization (uptime/downtime) statistics
 *
 */
class ChannelCounters
{
public:
  ChannelCounters();
  void init();
  void new_log_period(time_t ts_report);
  void update_times(int channel_idx);
  void set_state(int channel_idx, bool new_state);
  time_t get_duration_in_this_state(int channel_idx);
  uint16_t get_period_uptime(int channel_idx);
  void update_utilization(int channel_idx);

private:
  channel_log_struct channel_logs[CHANNEL_COUNT];
};

typedef int32_t T; // timeSeries data type
/**
 * @brief time series support class proving cached store, statistics etc.
 *
 */
class timeSeries
{
public:
  timeSeries(uint8_t id, time_t start, int n, uint16_t resolution_sec, T init_value);

#define STORAGE_NAMESPACE "cache"

  time_t expires() { return store.expires; }
  bool save_to_cache(time_t expires);
  bool read_from_cache(time_t expires);
  void clear_store(bool reset_cache);
  void set(time_t ts, T new_value);
  int n() { return store.n; };
  uint16_t resolution_sec() { return store.resolution_sec; };
  time_t start() { return store.start; };
  time_t end() { return store.start + (int)store.resolution_sec * (store.n - 1); };

  time_t first_set_period_ts() { return store.start + store.min_value_idx * store.resolution_sec; };
  time_t last_set_period_ts();
  time_t last_update() { return store.last_update_ts; };
  T get(time_t ts);
  T get_pos(int idx);
  T get(time_t ts, T default_value);
  T avg(time_t start_ts, time_t end_ts_incl);
  void stats(time_t ts, time_t start_ts, time_t end_ts_incl, T *avg_, T *differs_avg, long *ratio_avg);
  int32_t sum(time_t start_ts, time_t end_ts_incl);
  int32_t sum();
  void debug_print(time_t start_ts, time_t end_ts_incl);
  void debug_print();
  void set_store_start(time_t new_start);
  // new experimental version of time series ranking
  int get_period_rank(time_t period_ts, time_t start_ts, time_t end_ts_incl, bool);

private:
  uint8_t id_;
  struct
  {
    time_t start;
    int n;
    uint16_t resolution_sec;
    T init_value;
    T arr[TIMESERIES_ELEMENT_MAX];
    time_t expires;
    int min_value_idx;
    int max_value_idx;
    time_t last_update_ts;
  } store;

  int get_idx(time_t ts);
}; // Time series
// ****** Function declarations
bool is_chunksize_line(String line);
void setInternalTime(uint64_t epoch, uint32_t us);
int64_t getTimestamp(int year, int mon, int mday, int hour, int min, int sec);

/**
 * @brief Return true if line is chunk size line in the http response
 *
 * @param line
 * @return true
 * @return false
 */
bool is_chunksize_line(String line)
{
  // if line length between 2 and 5, ends with cr and all chars except the last one are hex numbersx
  if (line.charAt(line.length() - 1) != 13 || line.length() > 4 || line.length() < 2) // garbage line ends with cr
    return false;
  for (int i = 0; i < line.length() - 2; i++)
  {
    if (!isxdigit(line.charAt(i)))
      return false;
  }
  Serial.printf(PSTR("Garbage/chunk size removed [%s] (%d) %d\n"), line.substring(0, line.length() - 1).c_str(), line.length(), (int)line.charAt(0));
  return true;
}

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
int64_t getTimestamp(int year, int mon, int mday, int hour, int min, int sec)
{
  const uint16_t ytd[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};                /* Anzahl der Tage seit Jahresanfang ohne Tage des aktuellen Monats und ohne Schalttag */
  int leapyears = ((year - 1) - 1968) / 4 - ((year - 1) - 1900) / 100 + ((year - 1) - 1600) / 400; /* Anzahl der Schaltjahre seit 1970 (ohne das evtl. laufende Schaltjahr) */
  int64_t days_since_1970 = (year - 1970) * 365 + leapyears + ytd[mon - 1] + mday - 1;
  if ((mon > 2) && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)))
    days_since_1970 += 1; /* +Schalttag, wenn Jahr Schaltjahr ist */
  return sec + 60 * (min + 60 * (hour + 24 * days_since_1970));
}

// *** Application Logic Core Functions
// * Variables calculations
void calculate_forecast_variables();
void calculate_meter_based_variables();
void calculate_price_rank_variables();
void calculate_time_based_variables();

// * Channel handling
void calculate_channel_states();
void ch_prio_sort();
long channel_history_cumulative_minutes(int channel_idx, int periods);
int get_channel_active_rule(int channel_idx);
int get_channel_to_switch_prio(bool is_rise);
bool is_force_up_valid(int channel_idx);

// * Relay handling
void set_relays(bool grid_protection_delay_used);
bool apply_relay_state(int channel_idx, bool init_relay);
bool switch_http_relay(int channel_idx, bool up);
bool is_local_relay(uint8_t type);
bool is_wifi_relay(uint8_t type);
bool test_set_gpio_pinmode(int channel_idx, bool set_pinmode);
void updateShiftRegister();

time_t get_block_start_ts(const time_t time);
int get_variable_history_idx(int id);
time_t get_netting_period_start_time(time_t ts);
bool set_netting_source();

// *** Application Interface Functions
// * Get data from external sources
bool get_price_data_elering();
bool get_price_data_entsoe();
// bool get_renewable_forecast(uint8_t forecast_type, timeSeries *time_series);

void read_energy_meter();
bool read_energy_meter_han_wifi();
bool read_energy_meter_shelly3em();
bool receive_energy_meter_han_direct();
void process_energy_meter_readings();

void read_production_meter();
bool read_inverter_fronius_data(long int &total_energy, long int &current_power);
bool read_inverter_sma_data(long int &total_energy, long int &current_power);

#ifdef SENSOR_DS18B20_ENABLED
void print_onewire_address(DeviceAddress deviceAddress);
bool read_ds18b20_sensors();
bool scan_sensors();
#endif

// * Read Utilities
String httpGETRequest(const char *url, int32_t connect_timeout_s);
String read_http11_line(WiFiClientSecure *client_https);
// bool cb(Modbus::ResultCode event, uint16_t transactionId, void *data);
long int get_mbus_value(IPAddress remote, const int reg_offset, uint16_t reg_num, uint8_t modbusip_unit);

// * HAN P1 message parsing
bool get_han_dbl(String *strp, const char *obis_code, double *returned);
bool get_han_ts(String *strp, time_t *returned);
bool parse_han_row(String *row_in_p);

// * Json node values to memory
bool ajson_str_to_mem(JsonVariant parent_node, char *doc_key, char *tostr, size_t buffer_length);
bool ajson_bool_get(JsonVariant parent_node, char *doc_key, bool default_val);
int32_t ajson_int_get(JsonVariant parent_node, char *doc_key, int32_t default_val);
IPAddress ajson_ip_get(JsonVariant parent_node, char *doc_key, IPAddress default_val);
time_t ElementToUTCts(String elem);
String getElementValue(String outerXML);

// * Write to external destinations, Influx
bool update_prices_to_influx();
void add_period_variables_to_influx_buffer(time_t ts_report);
bool write_buffer_to_influx();
bool write_point_buffer_influx(InfluxDBClient *ifclient, Point *point_buffer);

// * Config store/read, Backup, restore
void readFromEEPROM();
void writeToEEPROM();
void reset_config();
void create_settings_doc(DynamicJsonDocument &doc, bool include_password);
bool create_shadow_settings();
bool read_shadow_settings();
bool store_settings_from_json_doc_dyn(DynamicJsonDocument doc);

// * Update Firmware, filesystem
void flash_update_ended();
void fs_update_ended();
bool get_releases();
void update_firmware_partition(uint8_t partition_type);
t_httpUpdate_return update_fs();
void handleFirmwareUpdate(AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final);
t_httpUpdate_return update_program();

// * System time
void set_timezone_ntp_settings(bool set_tz, bool set_ntp);

// * Real-time-clock, currently deprecated
void getRTC();
void printRTC();
void setRTC();
void ntp_time_is_set(bool from_sntp);

// * System management
void loop_watchdog(void *pvParameters);
void cooling(uint8_t cool_down_to_f, uint32_t max_wait_ms);
void io_tasks(uint8_t state);
void log_msg(uint8_t type, const char *msg, bool write_to_file);

// * Web interface, get
void onWebApplicationGet(AsyncWebServerRequest *request);
void onWebPricesGet(AsyncWebServerRequest *request);
void onWebSeriesGet(AsyncWebServerRequest *request);
void onWebSettingsGet(AsyncWebServerRequest *request);
void onWebStatusGet(AsyncWebServerRequest *request);
void onWebUpdateGet(AsyncWebServerRequest *request);
void onWebUploadConfigPost(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void onWebWifisGet(AsyncWebServerRequest *request);

// * Web interface, post
void onWebSettingsPost(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);
void onWebUpdatePost(AsyncWebServerRequest *request);
void onScheduleUpdatePost(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

// * Utility
long round_divide(long lval, long divider);
int get_hw_template_idx(int id);
void ts_to_date_str(time_t *tsp, char *out_str);

// wifi connectivity
bool connect_wifi();
void scan_and_store_wifis(bool print_out, bool store);
void wifi_event_handler(WiFiEvent_t event);
bool test_host(IPAddress hostip, uint8_t);

//* System main functions, naming from Arduino platform
void setup();
void loop();

IPAddress IP_UNDEFINED(0, 0, 0, 0);

/**/
#ifdef PING_ENABLED
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
  yield();
  bool success = Ping.ping(hostip, count);
  Serial.println(success ? "success" : "no response");
  return success;
}
#else
bool ping_enabled = false;
bool test_host(IPAddress hostip, uint8_t count = 5) return true; // stub, not in use

#endif

tm tm_struct;

// for timezone https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv

bool wifi_sta_connection_required = false;
bool wifi_sta_connected = false;
uint32_t wifi_sta_disconnected_ms = 0;   //!< restart if enough time since first try
uint32_t wifi_connect_tried_last_ms = 0; //!< reconnect if enough time since first try

bool config_resetted = false; // true if configuration cleared when version upgraded
bool fs_mounted = false;      // true
bool cooling_down_state = false;

#ifdef DEBUG_FILE_ENABLED
const char *debug_filename PROGMEM = "/data/log.txt";
#endif

msg_st last_msg; //!< last system message
tm tm_struct_g;  //!< time structure variables for splitting time stamp
tm tm_struct_l;

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
  last_msg.ts = time(nullptr);

#ifdef DEBUG_FILE_ENABLED
  char datebuff[35];
  if (write_to_file && fs_mounted)
  {
    File log_file = FILESYSTEM.open(debug_filename, "a");
    if (!log_file)
    {
      Serial.println(F("Cannot open the log file."));
      return;
    }
    gmtime_r(&last_msg.ts, &tm_struct_g);
    sprintf(datebuff, "%04d-%02d-%02dT%02d:%02d:%02dZ (%lu)", tm_struct_g.tm_year + 1900, tm_struct_g.tm_mon + 1, tm_struct_g.tm_mday, tm_struct_g.tm_hour, tm_struct_g.tm_min, tm_struct_g.tm_sec, millis());
    log_file.printf("%s %d %s\n", datebuff, (int)type, msg);
    log_file.close();
    // debug debug
    Serial.println("Writing to log file:");
    Serial.printf("%s %d %s\n", datebuff, (int)type, msg);
  }
#endif
}

AsyncWebServer server_web(80);

// Clock functions, supports optional DS3231 RTC
bool rtc_found = false;

const int force_up_hours[] = {0, 1, 2, 4, 8, 12, 24}; //!< dashboard forced channel duration times
const int price_variable_blocks[] = {9, 24};          //!< price ranks are calculated in 9 and 24 period windows

time_t prices_first_period = 0;

time_t prices_record_start;
// time_t prices_record_end_excl;
time_t prices_expires_ts = 0;

uint8_t chstate_transit[CHANNEL_COUNT]; // logging why the channel is in this state, latest transit decision (also blocking)
bool channel_forced_down[CHANNEL_COUNT];

// Jos LMGMT blokkaa niin palataan entiseen eli käytännössä syyksi muuttuu CH_STATE_BYLMGMT
// Mutta ehdot lasketaan uusiksi, joten ei syytä jättää transitioon pidemmäksi aikaa, mutta priorisointi tehdään vasta myöhemmin
//  katso calculate_channel_states, get_channel_to_switch_prio
//  voisi olla paras tehdä aina prio-sortattu taulukko kanavista

// String url_base = "/api?documentType=A44&processType=A16";
const char *url_base PROGMEM = "/api?documentType=A44&processType=A16";
// API documents: https://transparency.entsoe.eu/content/static_content/Static%20content/web%20api/Guide.html#_areas

time_t next_query_price_data_ts = 0;
time_t next_query_fcst_data_ts = 0;

// https://transparency.entsoe.eu/api?securityToken=XXX&documentType=A44&In_Domain=10YFI-1--------U&Out_Domain=10YFI-1--------U&processType=A16&outBiddingZone_Domain=10YCZ-CEPS-----N&periodStart=202104200000&periodEnd=202104200100
const int httpsPort = 443;

#ifdef RTC_DS3231_ENABLED

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
  time_t now_ts = time(nullptr); // this are the seconds since Epoch (1970) - seconds GMT
  tm tm;                         // the structure tm holds time information in a more convient way
  gmtime_r(&now_ts, &tm);        // update the structure tm with the current GMT
  rtc.adjust(DateTime(tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec));
}

/**
 * @brief Callback function (registered with settimeofday_cb ) called when ntp time update received, sets RTC
 *
 * @param from_sntp true if update is from sntp service
 */
void ntp_time_is_set(bool from_sntp)
{
  if (from_sntp) // needs Core 3.0.0 or higher!
  {
    Serial.println(F("The internal time is set from NTP."));
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

// internal temperature, updated only if hw extensions
uint8_t cpu_temp_f = 128;

#ifdef HW_EXTENSIONS_ENABLED
uint8_t register_out = 0;

#ifdef __cplusplus
extern "C"
{
#endif
  uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif
uint8_t temprature_sens_read();

uint32_t last_temp_read = millis();

#endif // hw_extensions

/**
 * @brief Check whether file system is up-to-date. Compares version info in the code and a filesystem file.
 *
 * @return true
 * @return false
 */
bool check_filesystem_version()
{
  bool is_ok;
  if (!fs_mounted)
    return false;

  String current_version;
  File info_file = FILESYSTEM.open("/data/version.txt", "r");

  if (info_file.available())
  {
    String current_fs_version = info_file.readStringUntil('\n'); // e.g. 0.92.0-rc1.1102 - 2022-09-28 12:24:56
    version_fs_base = current_fs_version.substring(0, current_fs_version.lastIndexOf('.'));
    Serial.printf("version_fs_base: %s ,  VERSION_BASE %s\n", version_fs_base.c_str(), VERSION_BASE);
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

long variable_history[HISTORY_VARIABLE_COUNT][MAX_HISTORY_PERIODS];

uint8_t channel_attr[CHANNEL_COUNT];

uint16_t channel_history_s[CHANNEL_COUNT][MAX_HISTORY_PERIODS];
int history_variables[HISTORY_VARIABLE_COUNT] = {VARIABLE_SELLING_ENERGY, VARIABLE_PRODUCTION_ENERGY};

/**
 * @brief Returns idx of history variable based on variable id in history variable array
 *
 * @param id
 * @return int
 */
int get_variable_history_idx(int id)
{
  for (int i = 0; i < HISTORY_VARIABLE_COUNT; i++)
    if (history_variables[i] == id)
      return i;
  return -1;
}

#ifdef SENSOR_DS18B20_ENABLED
bool sensor_ds18b20_enabled = true;
// see: https://randomnerdtutorials.com/esp8266-ds18b20-temperature-sensor-web-server-with-arduino-ide/

time_t temperature_updated_ts = 0;

// Setup a oneWire instance to communicate with any OneWire devices
OneWire oneWire(ONEWIRE_DATA_GPIO);

// Pass our oneWire reference to Dallas Temperature sensor
DallasTemperature sensors(&oneWire);
float ds18B20_temp_c;
int sensor_count = 0;
#else
bool sensor_ds18b20_enabled = false;
#endif

time_t next_process_ts = 0;           // start reading as soon as you get to first loop
time_t next_energy_meter_read_ts = 0; // start reading as soon as you get to first loop

// experimental 0.93
int grid_protection_delay_max = 0;  // TODO: still disabled (=0),later set to admin parameters
int grid_protection_delay_interval; // random, init in setup()

time_t recorded_period_start_ts = 0; // first period: boot time, later period starts
time_t current_period_start_ts = 0;  //!< updated in loop
time_t previous_period_start_ts = 0;
time_t day_start_local = 0; //<! epoch time stamp of 00:00 in local time, TODO: DST change days

time_t processing_started_ts = 0; //<! timestamp of time clock was set and actual data processing started
bool first_loop_ended = false;    //<! true after running first time loop() to the and start to serve UI

time_t production_meter_read_last_ts = 0; //!< ts of last succesfull inverter read
long int production_meter_read_last = 0;  //!< last succesfull inverter value

// Energy meter globals
// Values directly read from the meter
time_t energy_meter_ts_latest;
double energy_meter_cumulative_latest_in = 0;  //!< Energy meter last import value
double energy_meter_cumulative_latest_out = 0; //!< Energy meter last export value
double energy_meter_power_latest_in = 0;
double energy_meter_power_latest_out = 0;
double energy_meter_current_latest[3] = {0, 0, 0};

// Previous cumulative values, used to set period start value when period changes
double energy_meter_value_previous_in = 0;  //!< Energy meter last import value
double energy_meter_value_previous_out = 0; //!< Energy meter last export value

// Energy meter cumulative values in the beginning of the period (or last measurement in the end of the previous period)
double energy_meter_cumulative_periodstart_in = 0;  //!< Energy meter import value in the end of last period
double energy_meter_cumulative_periodstart_out = 0; //!< Energy meter export value in the end of last period

// Calculated values
float energy_meter_power_netin = 0;    //!< Energy meter last, momentary power value
float energy_meter_period_power_netin; // short/no history, using momentary value
float energy_meter_period_netin = 0;   //< Netted incoming energy during this period

// Energy meter read timestamps and counters
uint32_t energy_meter_last_read_started_ms = 0; // starting time of latest reading process
time_t energy_meter_period_first_read_ts = 0;
time_t energy_meter_read_succesfully_ts = 0; //!< Last time meter was read successfully, set in process_energy_meter_readings
uint32_t energy_meter_read_ok_count = 0;
uint32_t energy_meter_read_all_count = 0; //!< all meter reads
// time_t energy_meter_read_last_ts = 0; // vain shellyllä, voisi kai korvata energy_meter_read_succesfully_ts

// task requests to be fullfilled in loop asyncronously
// bool todo_in_loop_update_price_rank_variables = false;
bool todo_in_loop_influx_write = false;
bool todo_in_loop_restart = false;
bool todo_calculate_ranks_period_variables = false;

// bool todo_in_loop_test_gpio = false; //!< gpio should be tested in loop
// int gpio_to_test_in_loop = -1;       //!< if not -1 then gpio should be tested in loop
bool todo_in_loop_scan_wifis = false;
bool todo_in_loop_scan_sensors = false;
bool todo_in_loop_set_relays = false;
bool todo_in_loop_get_releases = false;
bool todo_in_loop_write_to_eeprom = false;
bool todo_in_loop_update_firmware_partition = false;

bool todo_in_loop_reapply_relay_states = false;
bool relay_state_reapply_required[CHANNEL_COUNT]; // if true channel parameters have been changed and r

bool todo_in_loop_process_energy_meter_readings = false; //!< do rest of the energy meter processing in the loop

channel_type_st channel_types[CHANNEL_TYPE_COUNT] = {{CH_TYPE_UNDEFINED, "undefined", false}, {CH_TYPE_GPIO_USER_DEF, "GPIO", false}, {CH_TYPE_SHELLY_1GEN, "Shelly Gen 1", false}, {CH_TYPE_SHELLY_2GEN, "Shelly Gen 2", false}, {CH_TYPE_TASMOTA, "Tasmota", false}, {CH_TYPE_GPIO_USR_INVERSED, "GPIO, inversed", true}};

// hw_template_st hw_templates[HW_TEMPLATE_COUNT] = {{0, "manual", {ID_NA, ID_NA, ID_NA, ID_NA}}, {1, "esp32lilygo-4ch", {21, 19, 18, 5}}, {2, "esp32wroom-4ch-a", {32, 33, 25, 26}}, {3, "devantech-esp32lr42", {33, 25, 26, 27}}};
/*
{
  int id;
  const char *name;
  uint8_t locked_channels;
  uint8_t relay_id[HW_TEMPLATE_GPIO_COUNT];
  uint8_t energy_meter_gpio;
  hw_io_struct hw_io;
};

|------------------------------------------------------------------------------------------------------------------|
|    |                     |  fixed  | relay gpios/ids |  energy meter |           led  | rst  | rst    | shift    |
| id | board code          |  relays | relay gpios/ids |         gpios |        id/gpio | gpio | normal | reg gpio |
|--------------------------|---------|-----------------|---------------|----------------|------|--------|----------|
| 0  | manual              |       0 |               ? |             ? |              - |    - |      - |        - |
| 1  | esp32lilygo-4ch     |       4 |   21, 19, 18, 5 |            36 | single 25,-, - |    - |      - |        - |
| 2  | esp32wroom-4ch-a    |       4 |  32, 33, 25, 26 |            35 |              - |    - |      - |        - |
| 3  | devantech-esp32lr42 |       4 |  33, 25, 26, 27 |            NA |              - |    - |      - |        - |
| 4  | shelly-pro-1        |       1 |          ids: 0 |            NA |        4, 3, 2 |   35 |    LOW |  4,13,14 |
| 5  | olimex-esp32-evb    |       2 |          32, 33 |  36 (uext rx) |                |    - |      - |        - |
| 6  | shelly-pro-2        |       2 |       ids: 0, 1 |            NA |        4, 3, 2 |   35 |    LOW |  4,13,14 |
| 7  | hw-p1-meter         |       0 |       ids: 0, 1 |            16 |rev 33R,25G,B26 |    2 |   HIGH |        - |
|--------------------------|---------|-----------------|---------------|----------------|------|--------|----------|

Extra notes about the boards:
- manual: all pins configured from the UI
- devantech-esp32lr42: no extra pins

Attributes:
- fixed relays: number of relays on hw, which cannot be edited
- energy_meter_gpio: gpio of HAN P1 meter input (RXD)
- extended: SN74hc595 shift register and leds supported,  current some Shelly pro models

Additional reserved gpios:
-  ONEWIRE_DATA_GPIO 27

  uint8_t reset_button_gpio;
  bool output_register; // false
  uint8_t rclk_gpio;    // if shifted
  uint8_t ser_gpio;
  uint8_t srclk_gpio;

*/
hw_template_st hw_templates[HW_TEMPLATE_COUNT] = {
    {0, "manual", 0, {ID_NA, ID_NA, ID_NA, ID_NA}, ID_NA, {ID_NA, GPIO_STATE_NA, false, ID_NA, ID_NA, ID_NA, STATUS_LED_TYPE_NONE, {ID_NA, ID_NA, ID_NA}}},
    {1, "esp32lilygo-4ch", 4, {21, 19, 18, 5}, 36, {ID_NA, GPIO_STATE_NA, false, ID_NA, ID_NA, ID_NA, STATUS_LED_TYPE_SINGLE_LOWACTIVE, {25, ID_NA, ID_NA}}},
    {2, "esp32wroom-4ch-a", 4, {32, 33, 25, 26}, 35, {ID_NA, GPIO_STATE_NA, false, ID_NA, ID_NA, ID_NA, STATUS_LED_TYPE_NONE, {ID_NA, ID_NA, ID_NA}}},
    {3, "devantech-esp32lr42", 4, {33, 25, 26, 27}, ID_NA, {ID_NA, GPIO_STATE_NA, false, ID_NA, ID_NA, ID_NA, STATUS_LED_TYPE_NONE, {ID_NA, ID_NA, ID_NA}}},
    {4, "shelly-pro-1", 1, {0, ID_NA, ID_NA, ID_NA}, ID_NA, {35, LOW, true, 4, 13, 14, STATUS_LED_TYPE_RGB3_HIGHACTIVE, {4, 3, 2}}},
    {5, "olimex-esp32-evb", 2, {32, 33, ID_NA, ID_NA}, 36, {ID_NA, GPIO_STATE_NA, false, ID_NA, ID_NA, ID_NA, STATUS_LED_TYPE_NONE, {ID_NA, ID_NA, ID_NA}}},
    {6, "shelly-pro-2", 2, {0, 1, ID_NA, ID_NA}, ID_NA, {35, LOW, true, 4, 13, 14, STATUS_LED_TYPE_RGB3_HIGHACTIVE, {4, 3, 2}}},
    {7, "hw-p1-meter", 0, {ID_NA, ID_NA, ID_NA, ID_NA}, 16, {2, HIGH, false, ID_NA, ID_NA, ID_NA, STATUS_LED_TYPE_RGB3_LOWACTIVE, {26, 25, 33}}}};

#if defined(INVERTER_FRONIUS_SOLARAPI_ENABLED) || defined(INVERTER_SMA_MODBUS_ENABLED)
// inverter productuction info fields
uint32_t inverter_total_period_init = 0;
long inverter_total_value_last = 0; //<! compare reading with previous, validity check

uint32_t energy_produced_period = 0;
uint32_t power_produced_period_avg = 0;
#endif

// #ifdef SENSOR_DS18B20_ENABLED

// this stores settings also to eeprom
settings_struct s;

int hw_template_idx = -1; // cached hw_io copied from hw_template if id > 0
/**
 * @brief Get index in template array based on template id
 *
 * @param id
 * @return int
 */
int get_hw_template_idx(int id)
{
  for (int i = 0; i < HW_TEMPLATE_COUNT; i++)
  {
    if (id == hw_templates[i].id)
    {
      return i;
    }
  }
  return -1;
}

int ch_prio_sorted[CHANNEL_COUNT];
//
/**
 * @brief Sort channel indexes to priority order to array ch_prio_sorted
 *
 */
void ch_prio_sort()
{
  // init
  for (int i = 0; i < CHANNEL_COUNT; i++)
    ch_prio_sorted[i] = -1;

  // pienin, toiseksi pienin, huom.: samaa prioa voi olla useampi
  int least, least_idx;
  bool isthere;
  for (int i = 0; i < CHANNEL_COUNT; i++)
  {
    least = 256;
    for (int j = 0; j < CHANNEL_COUNT; j++) // is j smallest of remaining
    {
      isthere = false;
      for (int k = 0; k < i; k++)
      {
        if (ch_prio_sorted[k] == j)
        { // already in sorted array
          isthere = true;
          break;
        }
      }
      if (!isthere)
      {
        if (s.ch[j].priority < least)
        {
          least = s.ch[j].priority;
          least_idx = j;
        }
      }
    }
    if (least < 256)
    {
      ch_prio_sorted[i] = least_idx;
      //    Serial.printf("ch_prio_sorted[%d], least %d, least_idx %d \n", i, least, least_idx);
    }
  }

  Serial.print("Channels in priority order: [");
  for (int i = 0; i < CHANNEL_COUNT; i++)
    Serial.printf("[%d, %d],", ch_prio_sorted[i], s.ch[ch_prio_sorted[i]].priority);
  Serial.println("]");
}

// Utility functions, channel attributes based on type
bool is_wifi_relay(uint8_t type)
{
  return (type == CH_TYPE_SHELLY_1GEN || type == CH_TYPE_SHELLY_2GEN || type == CH_TYPE_TASMOTA);
}
bool is_local_relay(uint8_t type)
{
  return (type == CH_TYPE_GPIO_FIXED || type == CH_TYPE_GPIO_USER_DEF || type == CH_TYPE_GPIO_USR_INVERSED);
}

// experimental for response led on/off

byte led_rgb[3];

// LED blinking
// https://circuitdigest.com/microcontroller-projects/esp32-timers-and-timer-interrupts
hw_timer_t *led_timer = NULL;
u32_t led_pattern = LED_PATTERN_DARK;
int led_noshow_ticks = 9;
//int led_show_ticks = 1;
int led_tick_count_cyclic = 0;
#define LED_TIMER_INTERVAL_US 100000 // 100ms

void led_write_color(bool show = true)
{
  // experimental, blink led on HomeWizard P1 Meter when receiving data
  byte element_val;

  if (!hw_templates[hw_template_idx].hw_io.output_register && hw_templates[hw_template_idx].hw_io.status_led_type == STATUS_LED_TYPE_SINGLE_HIGHACTIVE)
  {
    digitalWrite(hw_templates[hw_template_idx].hw_io.status_led_ids[0], show ? HIGH : LOW);
  }
  if (!hw_templates[hw_template_idx].hw_io.output_register && hw_templates[hw_template_idx].hw_io.status_led_type == STATUS_LED_TYPE_SINGLE_LOWACTIVE)
  {
    digitalWrite(hw_templates[hw_template_idx].hw_io.status_led_ids[0], show ? LOW : HIGH);
  }
  else if (!hw_templates[hw_template_idx].hw_io.output_register && hw_templates[hw_template_idx].hw_io.status_led_type == STATUS_LED_TYPE_RGB3_LOWACTIVE)
  {
    for (int i = 0; i < 3; i++)
    {
      element_val = show ? led_rgb[i] : 0;
      digitalWrite(hw_templates[hw_template_idx].hw_io.status_led_ids[i], element_val > 0 ? LOW : HIGH);
    }
  }
}
void led_set_color_rgb(byte r, byte g, byte b)
{
  led_rgb[0] = r;
  led_rgb[1] = g;
  led_rgb[2] = b;
  led_write_color();
}
/* old version, no pattern, remove when new tested
void IRAM_ATTR on_led_timer()
{
  // Serial.printf("on_led_timer  %d, %lu\n",led_tick_count_cyclic,millis());
  bool led_on = (led_tick_count_cyclic < led_show_ticks); // led is on during first ticks
  if (led_on && led_tick_count_cyclic == 0)
  {
    led_write_color(true);
    //    Serial.print("1");
  }
  else if (!led_on && led_tick_count_cyclic == led_show_ticks)
  {
    led_write_color(false);
    //    Serial.print("0");
  }
  led_tick_count_cyclic++;
  led_tick_count_cyclic = led_tick_count_cyclic % (led_show_ticks + led_noshow_ticks);
}
*/
void IRAM_ATTR on_led_timer()
{
  // Serial.printf("on_led_timer  %d, %lu\n",led_tick_count_cyclic,millis());
  int pattern_length = (int)log2(led_pattern)+1;
  bool pattern_on = (led_tick_count_cyclic < pattern_length); // led pattern blinking during first ticks
  bool led_on= (1 == ((led_pattern >> led_tick_count_cyclic) & 1));

  if (pattern_on)
  {
    led_write_color(led_on);
  }
  else if (!pattern_on && led_tick_count_cyclic == pattern_length)
  {
    led_write_color(false);
  }
  led_tick_count_cyclic++;
  led_tick_count_cyclic = led_tick_count_cyclic % (pattern_length + led_noshow_ticks);
}


/**
 * @brief Set led blink pattern, pause and color
 * 
 * @param r 
 * @param g 
 * @param b 
 * @param show_ticks 
 * @param noshow_ticks 
 * @param pattern 
 */
/* Old version, remove
void blink_led(byte r, byte g, byte b, int show_ticks, int noshow_ticks)
{
  led_set_color_rgb(r, g, b);
  led_noshow_ticks = noshow_ticks;
  led_show_ticks = show_ticks;
  led_tick_count_cyclic = 0;
} */

void set_led(byte r, byte g, byte b, int noshow_ticks,u32_t pattern )
{
  led_set_color_rgb(r, g, b);
  led_pattern = pattern;
  led_noshow_ticks = noshow_ticks;
 // led_show_ticks = show_ticks;
  led_tick_count_cyclic = 0;
}



// check if reset button has pressed and for how long, act if needed
#ifdef RESET_BUTTON_ENABLED
uint32_t reset_button_push_started_ms;

void IRAM_ATTR check_reset_button()
{
  // reset buutton gpio defined in hardware template, skip if undefined, ski
  if (hw_templates[hw_template_idx].hw_io.reset_button_gpio == ID_NA)
  {
    return; // should not end up here if called from irq
  }

  int reset_button_current_state = digitalRead(hw_templates[hw_template_idx].hw_io.reset_button_gpio);

  // push, record start time
  if (reset_button_push_started_ms == 0 && reset_button_current_state != hw_templates[hw_template_idx].hw_io.reset_button_normal_state)
  {
    reset_button_push_started_ms = millis();
    Serial.println("DEBUG  check_reset_button pushed");
    return;
  }
  // release
  if (reset_button_push_started_ms > 0 && reset_button_current_state == hw_templates[hw_template_idx].hw_io.reset_button_normal_state)
  {
    // check how long was up
    if (millis() - reset_button_push_started_ms > 10000)
    {
      // reset and restart, we cannot do in the loop() - maybe not started yet

      led_set_color_rgb(255, 0, 0);
      reset_config();
      log_msg(MSG_TYPE_FATAL, PSTR("Resetting config due to user activity (settings/cmd/button)."), true);
      writeToEEPROM();
      led_set_color_rgb(0, 0, 0);
      ESP.restart();
    }
    else if (millis() - reset_button_push_started_ms > 500) // was 5000
    {
      // restart
      led_set_color_rgb(0, 0, 255);
      todo_in_loop_restart = true;
    } // else do nothing, was up not long enough

    reset_button_push_started_ms = 0;
    Serial.println("DEBUG exiting check_reset_button");
  }
}
#endif

#ifdef HW_EXTENSIONS_ENABLED

/*
 * updateShiftRegister()
 */
void updateShiftRegister()
{
  if (hw_template_idx == -1)
    return; // no valid template idx in cached
  // Sets the hw_io.rclk_gpio to low, hides register write results so far
  digitalWrite(hw_templates[hw_template_idx].hw_io.rclk_gpio, LOW);

  // Arduino 'shiftOut' to shifts out contents of variable 'register_out' in the shift register
  shiftOut(hw_templates[hw_template_idx].hw_io.ser_gpio, hw_templates[hw_template_idx].hw_io.srclk_gpio, MSBFIRST, register_out);

  // hw_io.rclk_gpio high shows latest register content in the output pins
  digitalWrite(hw_templates[hw_template_idx].hw_io.rclk_gpio, HIGH); // makes changes visible to output
                                                                     // Serial.printf("register_out %d\n", (int)register_out);
}

uint32_t io_tasks_last = 0;
bool led_swing = false;
uint8_t rgb_value_prev = 0;

bool test_set_gpio_pinmode(int channel_idx, bool set_pinmode);
void reset_config();
void cooling(uint8_t cool_down_to_f, uint32_t max_wait_ms);

void io_tasks(uint8_t state = STATE_NA)
{
  if (!(millis() - io_tasks_last > 500)) // this should handle overflow https://www.norwegiancreations.com/2018/10/arduino-tutorial-avoiding-the-overflow-issue-when-using-millis-and-micros/
    return;
  io_tasks_last = millis();

  // Serial.print(".");
  uint8_t cpu_temp_read;
  if (!cooling_down_state && (millis() - last_temp_read) > 30000)
  {
    adc_power_acquire();
    cpu_temp_read = temprature_sens_read();
    adc_power_release();
    if (cpu_temp_read != 128)
    {
      cpu_temp_f = cpu_temp_read;
      // DEBUG
      Serial.printf("io_tasks(), temp %dF / %dC\n", (int)cpu_temp_f, (int)((cpu_temp_f - 32) * 5 / 9));

      if (cpu_temp_f > COOLING_START_F && state != STATE_COOLING)
      { // avoid recursion
        cooling(COOLING_RECOVER_TO_F, 900000LU);
      }
    }
    else
      Serial.print(".");
    last_temp_read = millis();
  }

  if (hw_template_idx < 1)
    return; // no hw_template defined (0-manual is currently undefined too)

  // led
  led_swing = !led_swing;
  if (hw_templates[hw_template_idx].hw_io.status_led_type == STATUS_LED_TYPE_RGB3_HIGHACTIVE)
  {
    uint8_t rgb_value = 0;
    if (state == STATE_NONE)
      rgb_value = RGB_NONE;
    else if (state == STATE_CONNECTING)
      rgb_value = led_swing ? RGB_YELLOW : RGB_NONE;
    else if (state == STATE_PROCESSING)
      rgb_value = RGB_WHITE;
    else if (state == STATE_UPLOADING)
      rgb_value = led_swing ? RGB_PURPLE : RGB_NONE;
    else if (state == STATE_COOLING)
      rgb_value = led_swing ? RGB_RED : RGB_YELLOW;
    else if (!wifi_sta_connected) // Blue -AP mode.
      rgb_value = RGB_BLUE;
    else if (processing_started_ts > 0) // global timestamp
      rgb_value = RGB_GREEN;
    else if (wifi_sta_connected)
    { // Yellow Wifi succeeded
      rgb_value = RGB_YELLOW;
    }
    // More to come, could indicate with green if internet connections are ok (last query eg..)
    //  also ota update
    //  to be defined
    if (rgb_value_prev != rgb_value)
    {
      bitWrite(register_out, hw_templates[hw_template_idx].hw_io.status_led_ids[0], !bitRead(rgb_value, 2)); // Red
      bitWrite(register_out, hw_templates[hw_template_idx].hw_io.status_led_ids[1], !bitRead(rgb_value, 1)); // Green
      bitWrite(register_out, hw_templates[hw_template_idx].hw_io.status_led_ids[2], !bitRead(rgb_value, 0)); // Blue

      // Serial.printf("register_out %d, rgb_value: %d\n", (int)register_out, (int)rgb_value);
      // Serial.printf("R: %d, G: %d, B: %d\n", (int)bitRead(rgb_value, 2), (int)bitRead(rgb_value, 1), (int)bitRead(rgb_value, 0));

      updateShiftRegister();
      rgb_value_prev = rgb_value;
    }
  }

  // #ifdef RESET_BUTTON_ENABLED
  //   check_reset_button();
  // #endif
}
/**
 * @brief Handles cool down to given temperature
 *
 * @param cool_down_to_f
 * @param max_wait_ms
 */
void cooling(uint8_t cool_down_to_f, uint32_t max_wait_ms)
{
  // TODO: check what to do with external relays when cooling..., now there is no changes during cooling period
  uint8_t cpu_temp_read;
  cooling_down_state = true; // global cooling state variable true,
  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {
    if (is_local_relay(s.ch[channel_idx].type))
    {
      relay_state_reapply_required[channel_idx] = true; // try to recover after cool down
      Serial.printf(PSTR("Taking local channel %d down for cooling.\n"), channel_idx);
      if (hw_template_idx > 0 && hw_templates[hw_template_idx].hw_io.output_register)
      {
        if (s.ch[channel_idx].relay_id < MAX_REGISTER_BITS)
        {
          bitWrite(register_out, s.ch[channel_idx].relay_id, LOW);
          updateShiftRegister();
        }
      }
      else
      {
        if (test_set_gpio_pinmode(channel_idx, false))
          digitalWrite(s.ch[channel_idx].relay_id, LOW);
      }
    }
  }
  uint32_t wait_started = millis();
  log_msg(MSG_TYPE_FATAL, PSTR("Cooling down, all local relays switched off."), true);
  while (cpu_temp_f > cool_down_to_f)
  {
    io_tasks(STATE_COOLING);
    // Goes down for immediately if over PINIC limit or tried to cool down too long without success
    //  It is possible that temprature_sens_read() reading cannot go down without reset, so this would be the harder way
    if ((cpu_temp_f > COOLING_PANIC_SHUTDOWN_F) || ((millis() - wait_started) > max_wait_ms))
    {
      esp_sleep_enable_timer_wakeup(900 * 1000000ULL);
      log_msg(MSG_TYPE_FATAL, PSTR("HOT SHUTDOWN! Panic deep-sleep for 15 minutes cooling down period."), true);
      delay(1000);
      Serial.flush();
      esp_deep_sleep_start();
    }

    adc_power_acquire();
    delay(2000);
    cpu_temp_read = temprature_sens_read();
    adc_power_release();

    if (cpu_temp_read != 128)
    {
      Serial.printf("cooling(), temp %dF / %dC\n", (int)cpu_temp_f, (int)((cpu_temp_f - 32) * 5 / 9));
    }
    else
      Serial.println("cooling(), cannot read cpu temperature");

    delay(30000);
  }

  log_msg(MSG_TYPE_FATAL, PSTR("Recovering after cooling."), true);
  cooling_down_state = false;
  todo_in_loop_reapply_relay_states = true;
};
#else
uint8_t state_prev = STATE_NA;
void io_tasks(uint8_t state = STATE_NA)
{
  // #ifdef RESET_BUTTON_ENABLED
  //   check_reset_button();
  // #endif
  // experimental state color with homewizard, todo: other led types
  if (state == STATE_NA || state_prev == state)
  {
    return;
  }
  if (hw_templates[hw_template_idx].hw_io.status_led_type == STATUS_LED_TYPE_RGB3_LOWACTIVE || hw_templates[hw_template_idx].hw_io.status_led_type == STATUS_LED_TYPE_SINGLE_LOWACTIVE)
  {
    if (state == STATE_NONE)
     // blink_led(255, 255, 255, 2, 50);
      set_led(255, 255, 255,50,LED_PATTERN_SHORT);
    else if (state == STATE_CONNECTING)
    //  blink_led(255, 255, 0, 1, 9);
      set_led(255, 255, 0,9,LED_PATTERN_3SHORT);
    else if (state == STATE_PROCESSING)
     // blink_led(255, 255, 255, 1, 29);
       set_led(255, 255, 255,9,LED_PATTERN_SHORT_LONG);
    else if (state == STATE_UPLOADING)
  //    blink_led(0, 255, 255, 1, 4);
      set_led(0, 255, 255,4,LED_PATTERN_SHORT_2LONG);
    else if (!wifi_sta_connected) // Blue -AP mode.
    //  blink_led(0, 0, 255, 5, 15);
     set_led(0, 0, 255,15,LED_PATTERN_2SHORT);
  }
  state_prev = state;
  return;
}; // do nothing if extensions are not enabled
#endif // HW_EXTENSIONS_ENABLED

#ifdef INFLUX_REPORT_ENABLED
const char *influx_device_id_prefix PROGMEM = "arska-";
String wifi_mac_short;

Point point_sensor_values("sensors");
Point point_period_avg("period_avg"); //!< Influx buffer
#endif
ChannelCounters ch_counters;

Variables::Variables()
{
  for (int variable_idx = 0; variable_idx < VARIABLE_COUNT; variable_idx++)
    variables[variable_idx].val_l = VARIABLE_LONG_UNKNOWN;
}
/**
 * @brief Rotates history variables value in the array to one index down, 0 earliest, MAX_HISTORY_PERIODS - 1 is current
 *
 */
void Variables::rotate_period()
{
  // rotate to variable history
  Serial.println("Variables::rotate_period");
  for (int v_idx = 0; v_idx < HISTORY_VARIABLE_COUNT; v_idx++)
  {
    variable_history[v_idx][MAX_HISTORY_PERIODS - 1] = this->get_l(history_variables[v_idx]);
    for (int h_idx = 0; (h_idx + 1) < MAX_HISTORY_PERIODS; h_idx++)
      variable_history[v_idx][h_idx] = variable_history[v_idx][h_idx + 1];
    variable_history[v_idx][MAX_HISTORY_PERIODS - 1] = 0; // current period
  }
  this->set(VARIABLE_ESTIMATED_CHANNELS_CONSUMPTION, 0L); // DO WE NEED THIS?
}

/**
 * @brief Returns true if variable is set e.g. then value is not VARIABLE_LONG_UNKNOWN
 *
 * @param id
 * @return true
 * @return false
 */
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

/**
 * @brief Get variable store value without conversions
 *
 * @param id
 * @return long
 */
long Variables::get_l(int id)
{
  int idx = get_variable_index(id);
  if (idx != -1)
  {
    return variables[idx].val_l;
  }
  return -1;
}
/**
 * @brief Get variable store value without conversions, default value if undefined
 *
 * @param id
 * @param default_val
 * @return long
 */
long Variables::get_l(int id, long default_val)
{
  int idx = get_variable_index(id);
  if (idx != -1)
  {
    if (variables[idx].val_l == VARIABLE_LONG_UNKNOWN)
      return default_val;
    else
      return variables[idx].val_l;
  }
  return -1;
}

/**
 * @brief Converts/returns a float value to variable internal long value. Conversion depends on the variable
 *
 * @param id
 * @param val_float
 * @return long
 */
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
//
/**
 * @brief Converts given value to float based on variable definition
 *
 * @param id
 * @param const_in
 * @return float
 */
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

/**
 * @brief Returns variable value as string, format depends on variable type
 *
 * @param id
 * @param strbuff
 * @param use_overwrite_val
 * @param overwrite_val
 * @param buffer_length
 * @return int
 */
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
/**
 * @brief Returns variable value as float.
 *
 * @param id
 * @return float
 */
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
/**
 * @brief Returns variable index (idx) for accessing directly the variable array.
 *
 * @param id
 * @return int
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

/**
 * @brief Returns variable index (idx) and copies variable content to given memory address
 *
 * @param id
 * @param variable
 * @return int
 */
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

timeSeries::timeSeries(uint8_t id, time_t start, int n, uint16_t resolution_sec, T init_value)
{
  id_ = id;
  store.start = start;
  store.n = n;
  store.resolution_sec = resolution_sec;
  store.init_value = init_value;
  clear_store(false);
}

// T operator [](int i) const    {return registers[i];}
void timeSeries::clear_store(bool reset_cache = true)
{
  store.min_value_idx = store.n;
  store.max_value_idx = -1;

  Serial.println(store.n);
  for (int i = 0; i < store.n; i++)
  {
    store.arr[i] = store.init_value;
  }
  store.last_update_ts = 0;
  if (reset_cache)
  {
    save_to_cache(0);
  }
};
/**
 * @brief Save timeseries to cache (non-volatile memory)
 *
 * @param expires Expiry timestamp of the data
 * @return true
 * @return false
 */
bool timeSeries::save_to_cache(time_t expires) // save to nvs,true if successfull
{
  store.expires = expires;
  nvs_handle_t my_handle;
  esp_err_t err;
  char key[6];
  sprintf(key, "TS%du", (int)id_);

  // Open
  err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
  if (err != ESP_OK)
    return false;

  // Write blob
  err = nvs_set_blob(my_handle, key, &this->store, sizeof(store));
  if (err != ESP_OK)
    return false;

  // Commit
  err = nvs_commit(my_handle);
  if (err != ESP_OK)
    return false;
  // Close

  nvs_close(my_handle);
  // Serial.printf("save_to_cache, wrote time series %d, expiration %lu\n", (int)id_, expires);
  //  if (id_==0) Serial.printf("save_to_cache, got time series %d, expiration %lu,  store.min_value_idx %d, store.max_value_idx %d\n", (int)id_, store.expires, store.min_value_idx, store.max_value_idx);

  return true;
}

/**
 * @brief  read from cache, true if succesfull and not expired
 *
 * @param expires expiry timestamp, expired data is not valid
 * @return true if found and not expired
 * @return false
 */
bool timeSeries::read_from_cache(time_t expires) //  https://github.com/espressif/esp-idf/blob/b4268c874a4cf8fcf7c0c4153cffb76ad2ddda4e/examples/storage/nvs_rw_blob/main/nvs_blob_example_main.c
{
  nvs_handle_t my_handle;
  char key[6];
  sprintf(key, "TS%du", (int)id_);
  esp_err_t err;
  // Open
  err = nvs_open(STORAGE_NAMESPACE, NVS_READWRITE, &my_handle);
  if (err != ESP_OK)
    return err;

  // Read run time blob
  size_t required_size = 0; // value will default to 0, if not set yet in NVS
  // obtain required memory space to store blob being read from NVS
  err = nvs_get_blob(my_handle, key, NULL, &required_size);
  if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
    return false;
  if (required_size == 0)
  {
    printf("Nothing saved yet!\n");
    return false;
  }
  else
  {
    err = nvs_get_blob(my_handle, key, &store, &required_size);
    if (err != ESP_OK)
    {
      return false;
    }
  }
  // Close
  nvs_close(my_handle);
  // if (id_==0) Serial.printf("read_from_cache, got time series %d, expiration %lu,  store.min_value_idx %d, store.max_value_idx %d\n", (int)id_, store.expires, store.min_value_idx, store.max_value_idx);
  return (store.expires > expires); // check expiration
};
void timeSeries::set(time_t ts, T new_value)
{
  int idx = get_idx(ts);
  if (idx == -1)
    return;

  store.arr[idx] = new_value;
  store.min_value_idx = min(store.min_value_idx, idx);
  store.max_value_idx = max(store.max_value_idx, idx);
  //  if (id_==0) Serial.printf("debug set ts %ld %d = max(store.max_value_idx %d, idx %d)\n",ts,store.max_value_idx, store.max_value_idx, idx);
  store.last_update_ts = time(nullptr);
};
int timeSeries::get_idx(time_t ts)
{
  int index_candidate = (ts - store.start) / store.resolution_sec;

  if (index_candidate < 0 || index_candidate >= store.n)
  {
    if (abs(index_candidate) > store.n * 10)
    { // something wrong
      Serial.printf("***Invalid timeSeries (%d) index %d, ts %lu, start_ %lu\n", (int)id_, index_candidate, ts, store.start);
    }
    // Serial.printf("DEBUG -1 --- get_idx (%d),ts %ld, store.start %ld, index_candidate %d \n", (int)id_, ts, store.start, index_candidate);

    return -1;
  }
  else
  {
    //    Serial.printf("DEBUG get_idx (%d),ts %ld, store.start %ld, index_candidate %d \n", (int)id_, ts, store.start, index_candidate);
    return index_candidate;
  }
};
time_t timeSeries::last_set_period_ts()
{

  //   if (id_==0) Serial.printf("last_set_period_ts: store.start %ld, store.max_value_idx %d, store.resolution_sec %d\n",store.start, (int)store.max_value_idx , (int)store.resolution_sec);
  return store.start + store.max_value_idx * store.resolution_sec;
};

T timeSeries::get(time_t ts = time(nullptr))
{
  int idx = get_idx(ts);
  if (idx == -1)
    return store.init_value;
  else
    return store.arr[idx];
}
T timeSeries::get_pos(int idx)
{
  if (idx < 0 || idx >= store.n)
    return store.init_value;
  else
    return store.arr[idx];
}

T timeSeries::get(time_t ts, T default_value)
{
  int idx = get_idx(ts);
  if (idx == -1)
    return default_value;
  else
    return store.arr[idx];
}

T timeSeries::avg(time_t start_ts, time_t end_ts_incl)
{
  return sum(start_ts, end_ts_incl) / ((end_ts_incl - start_ts) / store.resolution_sec + 1);
}

void timeSeries::stats(time_t ts, time_t start_ts, time_t end_ts_incl, T *avg_, T *differs_avg, long *ratio_avg)
{
  Serial.printf("stats ts %ld, start_ts %ld, end_ts_incl %ld \n", ts, start_ts, end_ts_incl);
  *avg_ = avg(start_ts, end_ts_incl);
  *differs_avg = get(ts) - *avg_;
  T suma = sum(start_ts, end_ts_incl);
  if (abs(suma) > 0)
  {
    *ratio_avg = ((end_ts_incl - start_ts) / resolution_sec() + 1) * (get(ts) * 1000) / suma;
  }
  else
    *ratio_avg = VARIABLE_LONG_UNKNOWN;
}

int32_t timeSeries::sum(time_t start_ts, time_t end_ts_incl)
{ // TODO: check DST change nights
  int32_t cum_sum = 0;
  int start_idx = max(0, get_idx(start_ts));
  int end_idx = min(get_idx(end_ts_incl), store.n);
  if (start_idx <= end_idx)
  {
    for (int i = start_idx; i <= end_idx; i++)
      cum_sum += store.arr[i];
  }
  return cum_sum;
}

int32_t timeSeries::sum()
{
  return sum(store.start, store.start + (store.n - 1) * store.resolution_sec);
}
void timeSeries::debug_print(time_t start_ts, time_t end_ts_incl)
{
  Serial.printf("Debug print store.start %lu -> %lu, store.resolution_sec %d, store.n: %d \n", start_ts, end_ts_incl, store.resolution_sec, (end_ts_incl - start_ts) / store.resolution_sec + 1);

  yield();
  for (int i = 0; i < store.n; i++)
  {
    Serial.printf("%d, %lu  ", i, store.start + i * store.resolution_sec);
    Serial.println(store.arr[i]);
  }
  Serial.print("Cumulative sum:");
  Serial.println(sum(start_ts, end_ts_incl));
  Serial.print("Avg:");
  Serial.println(avg(start_ts, end_ts_incl));
}

void timeSeries::debug_print()
{
  debug_print(store.start, store.start + (store.n - 1) * store.resolution_sec);
}
void timeSeries::set_store_start(time_t new_start)
{
  int index_delta = (int)((store.start - new_start) / store.resolution_sec);
  //   Serial.printf("DEBUG set_store_start  %lu -> %lu, index_delta %d\n", store.start, new_start, index_delta);
  if (index_delta == 0)
    return;

  store.start = new_start;
  yield();

  if (abs(index_delta) >= store.n)
  {
    // huge shift, nothing to save, just init
    clear_store(false);
  }
  else if (index_delta < 0)
  {
    if (store.min_value_idx != store.n)
    {
      store.min_value_idx = max(store.min_value_idx + index_delta, 0);
    }

    store.max_value_idx = max(store.max_value_idx + index_delta, -1);
    //  Serial.printf("set_store_start 1 set %d = max(store.max_value_idx+index_delta %d, -1),  index_delta %d\n",store.max_value_idx, store.max_value_idx+index_delta,index_delta);

    for (int i = 0; i < store.n; i++)
    {
      if ((i - index_delta >= 0) && (i - index_delta < store.n))
        store.arr[i] = store.arr[i - index_delta];
      else
        store.arr[i] = store.init_value;
    }
  }
  else if (index_delta > 0)
  {
    store.min_value_idx = min(store.max_value_idx + index_delta, store.n);
    if (store.max_value_idx != -1)
    {
      store.max_value_idx = min(store.max_value_idx + index_delta, store.n - 1);
      //    Serial.printf("Debug now store.max_value_idx %d, index_delta %d, store.n %d\n",index_delta, store.n,store.max_value_idx);
    }

    for (int i = store.n - 1; i >= 0; i--)
    {
      if (i - index_delta >= 0 && i - index_delta < store.n)
        store.arr[i] = store.arr[i - index_delta];
      else
        store.arr[i] = store.init_value;
    }
  }
  //    Serial.println(PSTR("DEBUG set_store_start ended"));
}

int timeSeries::get_period_rank(time_t period_ts, time_t start_ts, time_t end_ts_incl, bool descending = false)
{
  yield();
  int rank = 1;
  int start_idx = max(0, get_idx(start_ts));
  int end_idx = min(get_idx(end_ts_incl), store.n);
  int this_period_idx = get_idx(period_ts);
  // Serial.printf("get_period_rank period_ts %ld, start_idx %d, this_period_idx %d, end_idx %d\n", period_ts, start_idx, this_period_idx, end_idx);

  if (start_idx <= this_period_idx && this_period_idx <= end_idx)
  {
    for (int i = start_idx; i <= end_idx; i++)
    {
      // TODO: should it be <= to have 24 as 0 rank
      if (store.arr[i] < store.arr[this_period_idx] && i != this_period_idx)
      {
        rank++;
      }
    }
    if (descending)
      return end_idx - start_idx + 2 - rank;
    else
      return rank;
  }
  else
    return -1;

  yield();
  return rank;
}

// Time series globals
timeSeries prices2(0, 0, MAX_PRICE_PERIODS, PRICE_RESOLUTION_SEC, 0);
timeSeries solar_forecast(1, 0, 72, SOLAR_FORECAST_RESOLUTION_SEC, 0);
timeSeries wind_forecast(2, 0, 72, SOLAR_FORECAST_RESOLUTION_SEC, 0);

/**
 * @brief Returns start time of period of given time stamp (first second of an hour if 60 minutes netting period) ,
 *
 * @param ts
 * @return long
 */
time_t get_netting_period_start_time(time_t ts)
{
  return long(ts / (s.netting_period_sec)) * (s.netting_period_sec);
}

// experimental with channel_idx, resolve ch_counters reference
/**
 * @brief Return cumulative recorded history uptime minutes of given channel and nbr of history period
 *
 * @param channel_idx
 * @param periods
 * @return long
 */
long channel_history_cumulative_minutes(int channel_idx, int periods)
{
  time_t now_local;
  time(&now_local);
  time_t current_period_start_ts = get_netting_period_start_time(now_local);

  // u32_t util_history_pros_cum;
  u32_t history_cum_secs;
  u32_t period_time_ts;
  time_t period_start, period_end;
  u32_t periods_from_current;

  // ch_counters.update_utilization(channel_idx);
  ch_counters.update_times(channel_idx);

  // this period
  period_time_ts = now_local - max(current_period_start_ts, processing_started_ts);
  history_cum_secs = ch_counters.get_period_uptime(channel_idx);

  for (int h_idx = MAX_HISTORY_PERIODS - 2; h_idx > MAX_HISTORY_PERIODS - periods - 1; h_idx--)
  {
    periods_from_current = MAX_HISTORY_PERIODS - 1 - h_idx;
    period_end = (current_period_start_ts - (periods_from_current - 1) * 3600);
    if (period_end < processing_started_ts) // not yet history from that
      continue;

    period_start = max(processing_started_ts, (period_end - 3600));
    period_time_ts = period_end - period_start;
    // util_history_pros_cum += channel_history[channel_idx][h_idx] * period_time_ts / 3600;
    history_cum_secs += channel_history_s[channel_idx][h_idx];
  }

  return (long)((history_cum_secs + 30) / 60);
}

time_t get_block_start_ts(const time_t time)
{
  localtime_r(&time, &tm_struct_l);
  int block_start_before_this_idx = (24 + tm_struct_l.tm_hour - FIRST_BLOCK_START_HOUR) % DAY_BLOCK_SIZE_HOURS;
  return (current_period_start_ts - block_start_before_this_idx * SECONDS_IN_HOUR);
}

/**
 * @brief Returns variable index (idx) and copies variable content to given memory address based on variable id and channel idx
 *
 * @param id
 * @param variable
 * @param channel_idx
 * @return int
 */
int Variables::get_variable_by_id(int id, variable_st *variable, int channel_idx)
{
  int idx = get_variable_index(id);
  int now_nth_period_in_hour;

  if (idx != -1)
  {
    memcpy(variable, &variables[idx], sizeof(variable_st));

    // experimental channel variables
    if (id == VARIABLE_CHANNEL_UTIL_PERIOD)
    {
      variable->val_l = (long)(ch_counters.get_period_uptime(channel_idx) + 30) / 60;
    }
    else if (id == VARIABLE_CHANNEL_UTIL_8H) // update value for channel variables
    {                                        // 8h utilization
      variable->val_l = channel_history_cumulative_minutes(channel_idx, 8);
    }
    else if (id == VARIABLE_CHANNEL_UTIL_24H) // update value for channel variables
    {                                         // 24h utilization
      variable->val_l = channel_history_cumulative_minutes(channel_idx, 24);
    }
    else if (VARIABLE_CHANNEL_UTIL_BLOCK_0 <= id && id <= VARIABLE_CHANNEL_UTIL_BLOCK_M2_0)
    {
      now_nth_period_in_hour = (current_period_start_ts - get_block_start_ts(current_period_start_ts)) / SECONDS_IN_HOUR;
      variable->val_l = channel_history_cumulative_minutes(channel_idx, now_nth_period_in_hour + (id - VARIABLE_CHANNEL_UTIL_BLOCK_0) * DAY_BLOCK_SIZE_HOURS); // this block hours + optional previous blocks
    }
    else if (id == VARIABLE_ESTIMATED_CHANNELS_CONSUMPTION)
    {
      uint32_t load_watt_seconds = 0;
      ch_counters.update_times(channel_idx);
      for (int channel_idx_local = 0; channel_idx_local < CHANNEL_COUNT; channel_idx_local++)
      {
        if (s.ch[channel_idx_local].type != 0) // do not count where no defined relay
          load_watt_seconds += ch_counters.get_period_uptime(channel_idx_local) * s.ch[channel_idx_local].load;
      }
      variable->val_l = (long)(load_watt_seconds / SECONDS_IN_HOUR);
      // update also to mem array for later use
      variables[idx].val_l = variable->val_l;
    }

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
bool Variables::is_statement_true(statement_st *statement, bool default_value, int channel_idx)
{
  // kelaa operaattorit läpi, jos löytyy match niin etene sen kanssa, jos ei niin palauta default
  variable_st var;
  if (statement->variable_id == -1)
  {
    return default_value;
  }

  int variable_idx = get_variable_by_id(statement->variable_id, &var, channel_idx); // if channel variable, updates it
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

  if (oper.multiselect)
  {
    int elem_count = variables[variable_idx].bitmask_config % 100;
    u_int32_t cur_idx_value;
    // hours are 0-indexed(range: 0..), the others 1-indexed (range: 1..)
    if (int(variables[variable_idx].bitmask_config / 100) == 3)
    {
      cur_idx_value = var.val_l;
    }
    else
    {
      cur_idx_value = var.val_l - 1;
    }
    for (int i = 0; (i < elem_count && i <= var.val_l); i++)
    {
      // 1) i:th bit in constant is set, i.e. i:th element (0-indexed) in the list would make condition true
      // 2) variable value is i
      if ((bitRead(statement->const_val, i) == 1) && (cur_idx_value == i))
      {
        Serial.printf("DEBUG variable %d multiselect match const_val %ld, cur_idx_value %ud, i %d\n", statement->variable_id, statement->const_val, cur_idx_value, i);
        return true;
      }
    }
    Serial.printf("DEBUG variable %d NO multiselect match const_val %ld\n", statement->variable_id, statement->const_val);
    return false; // if oper is multiselect no other rule apply
  }

  if (oper.has_value) // check only if variable is defined
  {
    if (oper.reverse)
      return (var.val_l == VARIABLE_LONG_UNKNOWN);
    else
      return (var.val_l != VARIABLE_LONG_UNKNOWN);
  }

  // we want to have value, but it is undefined so return default
  if ((var.val_l == VARIABLE_LONG_UNKNOWN))
    return default_value;

  if (oper.boolean_only)
    result = (var.val_l == 1);
  else
  {
    // checking if variable value is equal to statement constant ,
    if (oper.eq && var.val_l == statement->const_val)
      result = true;
    // handles lest than, greater than...
    if (oper.gt && var.val_l > statement->const_val)
      result = true;
  }

  // finally optional reverse
  if (oper.reverse)
    result = !result;

  return result;
}

Variables vars;

ChannelCounters::ChannelCounters()
{
  init();
}
/**
 * @brief Initiate channle updatime counters
 *
 */
void ChannelCounters::init()
{
  for (int i = 0; i < CHANNEL_COUNT; i++)
  {
    channel_logs[i].off_time = 0;
    channel_logs[i].on_time = 0;
    channel_logs[i].state = false;
    channel_logs[i].this_state_started_period_ts = time(nullptr);
    channel_logs[i].this_state_started_epoch_ts = time(nullptr);
  }
}

time_t ChannelCounters::get_duration_in_this_state(int channel_idx)
{
  return (time(nullptr) - channel_logs[channel_idx].this_state_started_epoch_ts);
};
/**
 * @brief Updates channel utilization
 *
 * @param channel_idx
 */

/**
 * @brief Returns channel uptime in current period in minutes
 *
 * @param channel_idx
 * @return uint16_t
 */
uint16_t ChannelCounters::get_period_uptime(int channel_idx)
{
  update_times(channel_idx);
  return channel_logs[channel_idx].on_time;
}
/**
 * @brief Changing channel uptime counters to a new period. Updates influx buffer and rotates channel history logs.
 *
 * @param ts_report
 */
void ChannelCounters::new_log_period(time_t ts_report)
{
  // influx buffer
  if (!point_period_avg.hasTime())
    point_period_avg.setTime(ts_report);
  char field_name[10];
  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {
    update_times(channel_idx);

    snprintf(field_name, sizeof(field_name), "chup%d", channel_idx + 1);      // 1-indexed channel numbers in UI
    point_period_avg.addField(field_name, channel_logs[channel_idx].on_time); // now minutes

    // rotate to channel history
    // channel_history[channel_idx][MAX_HISTORY_PERIODS - 1] = (uint8_t)(utilization * 100 + 0.001);
    channel_history_s[channel_idx][MAX_HISTORY_PERIODS - 1] = channel_logs[channel_idx].on_time;
    for (int h_idx = 0; (h_idx + 1) < MAX_HISTORY_PERIODS; h_idx++)
    {
      channel_history_s[channel_idx][h_idx] = channel_history_s[channel_idx][h_idx + 1];
    }
    channel_history_s[channel_idx][MAX_HISTORY_PERIODS - 1] = 0;
  }
  // then reset this period time counters
  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {
    channel_logs[channel_idx].off_time = 0;
    channel_logs[channel_idx].on_time = 0;
    channel_logs[channel_idx].this_state_started_period_ts = time(nullptr);
  }
  yield();

  // write buffer
}

/**
 * @brief Updates channel state and updates channel utilization.
 *
 * @param channel_idx
 * @param new_state
 */
void ChannelCounters::update_times(int channel_idx) // no state change, just update times
{
  int previous_state_duration = (time(nullptr) - channel_logs[channel_idx].this_state_started_period_ts);
  if (channel_logs[channel_idx].state)
    channel_logs[channel_idx].on_time += previous_state_duration;
  else
    channel_logs[channel_idx].off_time += previous_state_duration;
  channel_logs[channel_idx].this_state_started_period_ts = time(nullptr);
}

void ChannelCounters::set_state(int channel_idx, bool new_state)
{
  update_times(channel_idx);
  if (channel_logs[channel_idx].state != new_state)
  {
    channel_logs[channel_idx].state = new_state;
    channel_logs[channel_idx].this_state_started_epoch_ts = time(NULL);
  }
}
#ifdef INFLUX_REPORT_ENABLED
/**
 * @brief Add time-series point values to buffer for later database insert
 *
 * @param ts timestamp written to buffer point
 */
void add_period_variables_to_influx_buffer(time_t ts_report)
{
  point_period_avg.setTime(ts_report);
  Serial.printf("add_period_variables_to_influx_buffer ts_report %lu\n", ts_report);

  // remove?
  if (vars.is_set(VARIABLE_PRODUCTION_POWER))
    point_period_avg.addField("productionW", vars.get_f(VARIABLE_PRODUCTION_POWER));

  if (vars.is_set(VARIABLE_PRODUCTION_ENERGY))
    point_period_avg.addField("productionWh", vars.get_f(VARIABLE_PRODUCTION_ENERGY));

  // remove?
  if (vars.is_set(VARIABLE_SELLING_POWER))
    point_period_avg.addField("sellingW", vars.get_f(VARIABLE_SELLING_POWER));

  if (vars.is_set(VARIABLE_SELLING_ENERGY))
    point_period_avg.addField("sellingWh", vars.get_f(VARIABLE_SELLING_ENERGY));
}
/**
 * @brief Writes Influx point buffer to the server
 *
 * @param ifclient
 * @param point_buffer
 * @return true
 * @return false
 */
bool write_point_buffer_influx(InfluxDBClient *ifclient, Point *point_buffer)
{
  Serial.println(F("Starting write_point_buffer_influx"));
  bool write_ok = true;
  if (!point_buffer->hasTime())
  {
    point_buffer->setTime(time(nullptr));
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

    point_buffer->clearFields();
    //  Serial.println("clearFields ok");
  }
  return write_ok;
}

/**
 * @brief Writes Influx data to specified server. Calls write_point_buffer_influx
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
#ifdef DEBUG_MODE_ENABLED
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
  return influx_write_ok; //
}

/**
 * @brief Updates new price data to the Influx server
 *
 * @return true
 * @return false
 */
bool update_prices_to_influx()
{
  long current_price;
  // int resolution_secs;
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

  last_price_in_file_ts = prices_record_start + (PRICE_RESOLUTION_SEC * (MAX_PRICE_PERIODS - 1));

  ts_to_date_str(&last_price_in_file_ts, datebuff);

  Serial.print("Last ts in memory");
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
      Serial.print("Last ts in the file greater than one in the influxdb");
      return false;
    }
    else
    {
      Serial.print("We have new prices to write to influx db. ");
    }
  }

  ifclient.setWriteOptions(WriteOptions().writePrecision(WritePrecision::S).batchSize(12).bufferSize(24));
  ifclient.setHTTPOptions(HTTPOptions().connectionReuse(true));

  if (ifclient.isBufferEmpty())
    Serial.print("isBufferEmpty yes ");
  else
    Serial.print("isBufferEmpty no ");

  Point point_period_price("period_price");

  for (time_t current_period_start_ts = prices2.start(); current_period_start_ts <= prices2.end(); current_period_start_ts += prices2.resolution_sec())
  {
    //  Serial.printf("DEBUG current_period_start_ts %lu \n",current_period_start_ts);

    ts_to_date_str(&current_period_start_ts, datebuff);

    if (!(last_price_in_db < String(datebuff))) // already in the influxDb
      continue;

#ifdef PRICE_SERIES_OLD
    current_price = (long)prices[i];
#else
    current_price = (long)prices2.get(current_period_start_ts);

#endif
    Serial.println(current_price);
    if (current_price != VARIABLE_LONG_UNKNOWN) // do not write undefined values
    {
      point_period_price.addField("price", (float)(current_price / 1000.0));
      point_period_price.setTime(current_period_start_ts);
      Serial.print("Writing: ");
      Serial.println(ifclient.pointToLineProtocol(point_period_price));
      // Write point
      write_ok = ifclient.writePoint(point_period_price);
      Serial.println(write_ok ? "write_ok" : "write not ok");
      point_period_price.clearFields();
    }
    else
      break; // do not continue pushing stuff to influx
  }

  ifclient.flushBuffer();
  delay(100);

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

// Serial command interface
String serial_command;
uint8_t serial_command_state = 0;
int network_count = 0;

#define WIFI_LIST_COUNT 6

wifi_st wifis[WIFI_LIST_COUNT];
#define WIFI_OPTION_NOWIFI_SERIAL 1 // 1 if no wifi option in serial console input, else 0

/**
 * @brief Scans wireless networks on the area and stores list to a file.
 * @details description Started from loop-function. Do not run interactively (from a http call).
 *
 */
void scan_and_store_wifis(bool print_out, bool store)
{
  int array_i = 0;
  network_count = WiFi.scanNetworks();
  if (store)
    memset(wifis, 0, sizeof(wifis));

  if (print_out)
    Serial.println("Available WiFi networks:\n");

  if (WIFI_OPTION_NOWIFI_SERIAL == 1)
  {
    Serial.println(PSTR("0 - WiFi disabled"));
  }

  for (int i = 0; i < network_count; ++i)
  {
    if (WiFi.RSSI(i) < -80) // too weak signals not listed, could be actually -75
      continue;
    if (print_out)
      Serial.printf("%d - %s (%ld)\n", i + WIFI_OPTION_NOWIFI_SERIAL, WiFi.SSID(i).c_str(), WiFi.RSSI(i));
    if (store & array_i < WIFI_LIST_COUNT)
    {
      wifis[array_i].rssi = WiFi.RSSI(i);
      strncpy(wifis[array_i].ssid, WiFi.SSID(i).c_str(), MAX_ID_STR_LENGTH - 1);
    }
    array_i++;
  }

  if (print_out)
  {
    Serial.println("-");
    Serial.flush();
  }
}

/**
 * @brief Set the netting source variable based on settings, called after setting change/init
 *
 * @return true
 * @return false
 */
bool set_netting_source()
{
  // TODO: tähän voisi lisätä myös päättelyn onko sitä ylimääräistä vai ei, vai?

  // primary grid measurement
  if (s.energy_meter_type != ENERGYM_NONE)
  {
    vars.set(VARIABLE_NET_ESTIMATE_SOURCE, VARIABLE_NET_ESTIMATE_SOURCE_MEAS_ENERGY);
    return true;
  }
  // secondary production measurement
  else if (s.production_meter_type != PRODUCTIONM_NONE)
  {
    vars.set(VARIABLE_NET_ESTIMATE_SOURCE, VARIABLE_NET_ESTIMATE_SOURCE_MEAS_PRODUCTION);
    return true;
  }
  // tertiary local solar forecast
  else if (strlen(s.forecast_loc) > 2)
  {
    vars.set(VARIABLE_NET_ESTIMATE_SOURCE, VARIABLE_NET_ESTIMATE_SOURCE_SOLAR_FORECAST);
    return true;
  }
  else
  {
    vars.set(VARIABLE_NET_ESTIMATE_SOURCE, 0L);
    return false;
  }
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

  hw_template_idx = get_hw_template_idx(s.hw_template_id); // update cached variable

  if (s.netting_period_sec == 0) // TODO: should not happen normally, only if not well resetted
    s.netting_period_sec = 3600;

  set_netting_source();

  ch_prio_sort(); // experimental, keep sorted channel array up to date
}

/**
 * @brief Writes settings to eeprom
 *
 */
void writeToEEPROM()
{
  set_netting_source();
  // is directly called for critical update
  int eeprom_used_size = sizeof(s);
  EEPROM.begin(eeprom_used_size);
  EEPROM.put(eepromaddr, s); // write data to array in ram
  bool commit_ok = EEPROM.commit();
  Serial.printf(PSTR("writeToEEPROM: Writing %d bytes to eeprom. Result %s\n"), eeprom_used_size, commit_ok ? "OK" : "FAILED");
  if (eeprom_used_size > 4096)
    Serial.println(PSTR("DATA STRUCTURE WRITTEN TO EEPROM IS TOO BIG"));

  EEPROM.end();

  ch_prio_sort(); // experimental, keep sorted channel array up to date
}

/**
 * @brief Utility function to make http request, stores result to a cache file if defined
 *
 * @param url Url to call
 * //@param cache_file_name optional cache file name to store result
 * @return String
 */
#define CONNECT_TIMEOUT_EXTERNAL 30 // unsecure http is not really used in external communication
#define CONNECT_TIMEOUT_INTERNAL 5

String httpGETRequest(const char *url, int32_t connect_timeout_s)
{
  uint32_t call_started = millis();
  WiFiClient wifi_client;

  HTTPClient http;
  http.setReuse(false);
  // http.useHTTP10(true); // for json input, maybe also for disable chunked response

  http.setConnectTimeout(connect_timeout_s * 1000);
  http.begin(wifi_client, url); //  IP address with path or Domain name with URL path

  delay(100);
  int httpResponseCode = http.GET(); //  Send HTTP GET request

  String payload = "{}";

  if (httpResponseCode > 0)
  {
    payload = http.getString();
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

/**
 * @brief Print (debug)  onewire (DS18B20 sensor) address
 *
 * @param deviceAddress
 */
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

#define MINUTES_BETWEEN_SENSOR_READS 2
/**
 * @brief Read DS18B20 sensor values.
 *
 * @return true
 * @return false
 */
bool read_ds18b20_sensors()
{
  // temperature_updated_ts PROCESS_INTERVAL_SECS
  yield();
  if (sensor_count == 0)
    return false;

  if ((time(nullptr) % MINUTES_BETWEEN_SENSOR_READS) != 0) // read one in two minutes, experimental
    return false;

  Serial.printf(PSTR("Starting read_ds18b20_sensors, sensor_count: %d\n"), sensor_count);
  DeviceAddress device_address;
  sensors.requestTemperatures();
  delay(150);
  int32_t temp_raw;
  float temp_c;

  // Loop through each device, print out temperature data
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
        time(&temperature_updated_ts); // TODO: per sensor?
      }
      else
      { //

        if ((time(nullptr) - temperature_updated_ts) < SENSOR_VALUE_EXPIRE_TIME)
        {
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
#define WARNING_AFTER_FAILED_READING_SECS 240  //!< If all energy/production meter readings are failed within this period, log/react

#ifdef LOAD_MGMT_ENABLED
time_t load_manager_overload_last_ts = 0;

void set_relays(bool grid_protection_delay_used); // defined later

float load_manager_capacity = 0; // global
// returns available load in the most loaded phase, use energy_meter_current_latest[], update global
float check_current_load()
{
  int drop_count = 0;
  load_manager_capacity = 9999;
  for (int i = 0; i < s.load_manager_phase_count; i++)
  {
    load_manager_capacity = min(load_manager_capacity, (float)(s.load_manager_current_max - energy_meter_current_latest[i]));
  }

  if (load_manager_capacity < 0)
  {
    Serial.println("System overload, do something, buy a new fuse...");
    load_manager_overload_last_ts = time(nullptr);

    for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
    {
      if (s.ch[channel_idx].is_up && s.ch[channel_idx].load > 0)
      {
        s.ch[channel_idx].wanna_be_up = false;
        chstate_transit[channel_idx] = CH_STATE_BYLMGMT;

        drop_count++;
      }
    }
    if (drop_count > 0)
    {

      Serial.printf("Dropping %d channels\n", drop_count);
    }
    set_relays(false);
  }
  // Serial.println(load_manager_capacity);
  return load_manager_capacity;
}

#endif

// update variable etc...
// postprocessing after succesfully received measure date
void process_energy_meter_readings()
{

  bool period_changed_since_last_read = ((energy_meter_period_first_read_ts / s.netting_period_sec) != (time(nullptr) / s.netting_period_sec));
  energy_meter_read_ok_count++; // global
  time_t energy_meter_read_previous_ts = energy_meter_read_succesfully_ts;
  energy_meter_read_succesfully_ts = time(nullptr);

#ifdef LOAD_MGMT_ENABLED
  if (s.load_manager_active)
  {
    check_current_load();
  }
#endif

  Serial.print(energy_meter_power_latest_in);
  Serial.print("W (in), ");
  Serial.print(energy_meter_power_latest_out);
  Serial.println("W (out), ");
  Serial.print(energy_meter_cumulative_latest_in);
  Serial.print("Wh (in), ");
  Serial.print(energy_meter_cumulative_latest_out);
  Serial.println("Wh (out), ");
  Serial.print(energy_meter_current_latest[0]);
  Serial.print("A, ");
  Serial.print(energy_meter_current_latest[1]);
  Serial.print("A, ");
  Serial.print(energy_meter_current_latest[2]);
  Serial.println("A");

  //  Serial.printf("HAN readings: energy_meter_power_latest_in %f W, power_out %f W, energy_meter_cumulative_latest_in %f Wh, energy_meter_cumulative_latest_out %f Wh, [%f A, %f A, %f A]", energy_meter_power_latest_in, energy_meter_power_latest_out, energy_meter_cumulative_latest_in, energy_meter_cumulative_latest_out, energy_meter_current_latest[0], energy_meter_current_latest[1], energy_meter_current_latest[2]);

  // first succesfull measurement since boot, record only initial values
  if (energy_meter_read_previous_ts == 0)
  {
    energy_meter_period_first_read_ts = time(nullptr);
    energy_meter_cumulative_periodstart_in = energy_meter_cumulative_latest_in;
    energy_meter_cumulative_periodstart_out = energy_meter_cumulative_latest_out;
    return; // skip other processing in the first measurement
  }

  if (period_changed_since_last_read)
  {
    energy_meter_period_first_read_ts = energy_meter_read_previous_ts; // earlier
    energy_meter_cumulative_periodstart_in = energy_meter_value_previous_in;
    energy_meter_cumulative_periodstart_out = energy_meter_value_previous_out;
  }

  yield();

  // Serial.printf("DEBUG: calculate_energy_meter_period_values energy_meter_read_ok_count %d, energy_meter_period_power_netin %f \n", energy_meter_read_ok_count, (float)energy_meter_period_power_netin);

  energy_meter_period_netin = (energy_meter_cumulative_latest_in - energy_meter_cumulative_latest_out - energy_meter_cumulative_periodstart_in + energy_meter_cumulative_periodstart_out);
  energy_meter_period_power_netin = round(energy_meter_period_netin * 3600.0 / ((energy_meter_read_succesfully_ts - energy_meter_period_first_read_ts)));

  vars.set(VARIABLE_OVERPRODUCTION, (long)(energy_meter_period_netin < 0) ? 1L : 0L);
  vars.set(VARIABLE_SELLING_POWER, (long)round(-energy_meter_period_power_netin));
  vars.set(VARIABLE_SELLING_ENERGY, (long)round(-energy_meter_period_netin));
  vars.set(VARIABLE_SELLING_ENERGY_ESTIMATE, (long)round(-energy_meter_period_netin));
  vars.set(VARIABLE_SELLING_POWER_NOW, (long)round(-energy_meter_power_netin)); // momentary

  energy_meter_value_previous_in = energy_meter_cumulative_latest_in;
  energy_meter_value_previous_out = energy_meter_cumulative_latest_out;
}

#ifdef METER_HAN_ENABLED
bool get_han_ts(String *strp, time_t *returned)
{
  if (!strp->startsWith("0-0:1.0.0("))
    return false;

  int32_t tz_secs = 3600; // CET, EET supported
  if (strcmp("EET", s.timezone) == 0)
    tz_secs = 7200;

  // Serial.print("ts:");
  int splitted[6];
  for (int i = 0; i < 6; i++)
  {
    splitted[i] = strp->substring(10 + i * 2, 12 + i * 2).toInt();
  }
  *returned = getTimestamp(2000 + splitted[0], splitted[1], splitted[2], splitted[3], splitted[4], splitted[5]) - tz_secs;

  // assume that we get clock from direct connection
  if (s.energy_meter_type == ENERGYM_HAN_DIRECT && time(nullptr) < ACCEPTED_TIMESTAMP_MINIMUM && *returned > ACCEPTED_TIMESTAMP_MINIMUM)
  {
    Serial.println("get_han_ts: set internal clock:");
    Serial.println(*returned);
    setInternalTime(*returned);
  }

  time_t ts_age_s = time(nullptr) - (*returned);
  Serial.printf("han_ts: %ld, ts: %ld , %ld s ago\n", (time_t)(*returned), time(nullptr), ts_age_s);
  // experimental, try to adjust polling time to meter updates, could be removed, does not work well if meter and mcuu times are not in sync
  if ((energy_meter_read_ok_count % 2 == 0) && s.energy_meter_type == ENERGYM_HAN_WIFI & (ts_age_s > 2))
  {
    next_energy_meter_read_ts -= 1;
    Serial.println("Tuning polling time ***.");
  }

  return true;
}

bool get_han_dbl(String *strp, const char *obis_code, double *returned)
{
  int factor_w = 1;

  if (!strp->startsWith(obis_code))
    return false;

  int vs_idx = strp->indexOf('(');
  int va_idx = strp->indexOf('*', vs_idx);

  // check OBIS basic format
  if (vs_idx == -1 || va_idx == -1 || strp->indexOf(')') == -1)
    return false;

  if (strp->indexOf("*kW") > -1)
    factor_w = 1000;

  *returned = strp->substring(vs_idx + 1, va_idx).toDouble() * factor_w;
  // Serial.println(*returned);

  return true;
}

#ifdef METER_HAN_DIRECT_ENABLED
// #define HAN_P1_SERIAL Serial2
#define HAN_P1_SERIAL Serial1
// #pragma message("Testing with Serial 1 should be 2")

/**
 * @brief Parses given string to a memory variable if OBIS code in the string matches
 *
 * @param row_in_p
 * @return true
 * @return false
 */
bool parse_han_row(String *row_in_p)
{

  if (get_han_ts(row_in_p, &energy_meter_ts_latest))
    return true;

  if (get_han_dbl(row_in_p, "1-0:1.7.0", &energy_meter_power_latest_in))
    return true;

  if (get_han_dbl(row_in_p, "1-0:2.7.0", &energy_meter_power_latest_out))
    return true;

  if (get_han_dbl(row_in_p, "1-0:1.8.0", &energy_meter_cumulative_latest_in))
    return true;

  if (get_han_dbl(row_in_p, "1-0:2.8.0", &energy_meter_cumulative_latest_out))
    return true;

  if (get_han_dbl(row_in_p, "1-0:31.7.0", &energy_meter_current_latest[0]))
    return true;

  if (get_han_dbl(row_in_p, "1-0:51.7.0", &energy_meter_current_latest[1]))
    return true;

  if (get_han_dbl(row_in_p, "1-0:71.7.0", &energy_meter_current_latest[2]))
    return true;

  return false;
}

uint16_t han_direct_error_count = 0;
/**
 * @brief UART callback function called when there is new data from HAN P1 Serial porrt
 *
 * @return true , successful if there were enough values in the buffer
 * @return false , unsuccessful, not enough data or too few values
 */
bool receive_energy_meter_han_direct() // direct
{
  String row_in;
  int value_count = 0;

  // experimental, blink led on HomeWizard P1 Meter when receiving data, todo: use compatible calls: set_led etc
  if (!hw_templates[hw_template_idx].hw_io.output_register && hw_templates[hw_template_idx].hw_io.status_led_type == STATUS_LED_TYPE_RGB3_LOWACTIVE)
  {
    digitalWrite(hw_templates[hw_template_idx].hw_io.status_led_ids[RGB_IDX_GREEN], LOW);
  }

  // This is a callback function that will be activated on UART RX events
  delay(100); // there should be some delay to fill the buffer...

  size_t available = HAN_P1_SERIAL.available();

  // experimental, prevent loops if port not connected
  // junk if RXD is floating, ie not connected to an adapter
  if (han_direct_error_count > 2)
  {
    Serial.println("Disabling HAN P1 Serial onReceive");
    HAN_P1_SERIAL.onReceive(NULL, false); // too many errors, probably serial not connected to an adapter ie rxd is floating, disconnect to prevent watchdog timeouts
    // log_msg(MSG_TYPE_ERROR, PSTR("Errors in reading HAN P1 Serial, disabled."), true); //too long operation for irq,TODO: add to loop...
  }

  if (available < 200)
  {
    han_direct_error_count++;
    HAN_P1_SERIAL.flush();
    return false;
  }

  // Serial.printf("DEBUG: next process in %d \n", int(next_process_ts - time(nullptr)));
  Serial.printf("\n%d bytes available, ts:  ", (int)available);

  yield();

  while (HAN_P1_SERIAL.available()) // SerialPort
  {
    row_in = HAN_P1_SERIAL.readStringUntil('\n');
    row_in.replace('\r', '\0');
    if (parse_han_row(&row_in))
    {
      value_count++;
      han_direct_error_count = 0; // we got something..
    }
  }
  if (value_count < 5)
  {
    Serial.println("Cannot read all HAN P1 port values \n");
    return false;
  }

  energy_meter_power_netin = energy_meter_power_latest_in - energy_meter_power_latest_out;
  // read done

  todo_in_loop_process_energy_meter_readings = true; // do rest of the processing in the loop
  yield();

  // led down
  if (!hw_templates[hw_template_idx].hw_io.output_register && hw_templates[hw_template_idx].hw_io.status_led_type == STATUS_LED_TYPE_RGB3_LOWACTIVE)
  {
    digitalWrite(hw_templates[hw_template_idx].hw_io.status_led_ids[RGB_IDX_GREEN], HIGH);
  }
  return true;
}

#endif
/**
 * @brief Read HAN P1 meter telegram message from tcp port (http)
 *
 * @return true
 * @return false
 */
bool read_energy_meter_han_wifi()
{
  char url[90];
  snprintf(url, sizeof(url), "http://%s:%d/api/v1/telegram", s.energy_meter_ip.toString().c_str(), s.energy_meter_port);
  Serial.println(url);

  yield();
  String telegram = httpGETRequest(url, CONNECT_TIMEOUT_INTERNAL);
  yield();

  int s_idx = 0, e_idx;
  int len = telegram.length();
  Serial.printf("Length of telegram: %i\n", len);

  // read
  energy_meter_cumulative_latest_in = 0;
  energy_meter_cumulative_latest_out = 0;
  int value_count = 0;

  // new
  String row_in;
  while (s_idx <= len)
  {
    e_idx = telegram.indexOf('\n', s_idx);
    if (e_idx == -1) // end of message reached
      break;

    row_in = telegram.substring(s_idx, e_idx);
    //  Serial.println(row_in);

    if (parse_han_row(&row_in))
      value_count++;
    s_idx = e_idx + 1;
  }

  if (value_count < 5)
  {
    Serial.printf("Cannot read all HAN P1 port values ( %d )\n", (int)value_count);
    return false;
  }

  yield();
  energy_meter_power_netin = energy_meter_power_latest_in - energy_meter_power_latest_out;
  // read done
  process_energy_meter_readings();

  yield();
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
bool read_energy_meter_shelly3em()
{
  if (s.energy_meter_ip == IP_UNDEFINED)
    return false;

  DynamicJsonDocument doc(2048);

  char url[90];
  char auth[35];
  if (strlen(s.energy_meter_password) > 0)
    snprintf(auth, sizeof(auth), "admin:%s@", s.energy_meter_password);
  else
    auth[0] = 0;

  if (s.energy_meter_type == ENERGYM_SHELLY3EM)
    snprintf(url, sizeof(url), "http://%s%s:%d/status", auth, s.energy_meter_ip.toString().c_str(), s.energy_meter_port);
  else if (s.energy_meter_type == ENERGYM_SHELLY_GEN2)
    // this version doesn not support authentication
    snprintf(url, sizeof(url), "http://%s:%d/rpc/EMData.GetStatus?id=0", s.energy_meter_ip.toString().c_str(), s.energy_meter_port);

  Serial.println(url);

  yield();
  DeserializationError error = deserializeJson(doc, httpGETRequest(url, CONNECT_TIMEOUT_INTERNAL));
  yield();

  if (error)
  {
    Serial.print(F("Shelly meter deserializeJson() failed: "));
    Serial.println(error.f_str());
    return false;
  }

  // read
  float power_tot = 0;
  int idx = 0;
  float power[3];
  energy_meter_cumulative_latest_in = 0;
  energy_meter_cumulative_latest_out = 0;
  energy_meter_power_latest_in = 0;
  energy_meter_power_latest_out = 0;
  if (s.energy_meter_type == ENERGYM_SHELLY3EM)
  {
    for (JsonObject emeter : doc["emeters"].as<JsonArray>())
    {
      power[idx] = (float)emeter["power"];
      power_tot += power[idx];
      if (emeter["is_valid"])
      {
        energy_meter_power_latest_in += (float)emeter["power"];
        energy_meter_cumulative_latest_in += (float)emeter["total"];
        energy_meter_cumulative_latest_out += (float)emeter["total_returned"];
        energy_meter_current_latest[idx] = (double)emeter["current"];
      }
      idx++;
    }
  }
  else if (s.energy_meter_type == ENERGYM_SHELLY_GEN2)
  {
    power_tot = 0; // not available in /status
    energy_meter_cumulative_latest_in = (float)doc["total_act"];
    energy_meter_cumulative_latest_out = (float)doc["total_act_ret"];
  }

  energy_meter_power_netin = power_tot;

  // read done

  process_energy_meter_readings();

  yield();
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

  StaticJsonDocument<64> filter;

  JsonObject filter_Body_Data = filter["Body"].createNestedObject("Data");
  // filter_Body_Data["DAY_ENERGY"] = true; //
  filter_Body_Data["TOTAL_ENERGY"] = true; //
  filter_Body_Data["PAC"] = true;

  StaticJsonDocument<256> doc;
  char inverter_url[190];
  snprintf(inverter_url, sizeof(inverter_url), "http://%s:%d/solar_api/v1/GetInverterRealtimeData.cgi?scope=Device&DeviceId=1&DataCollection=CumulationInverterData", s.production_meter_ip.toString().c_str(), s.production_meter_port);
  Serial.println(inverter_url);

  yield();
  DeserializationError error = deserializeJson(doc, httpGETRequest(inverter_url, CONNECT_TIMEOUT_INTERNAL), DeserializationOption::Filter(filter));
  yield();

  if (error)
  {
    Serial.print(F("Fronius inverter deserializeJson() failed: "));
    Serial.println(error.f_str());
    energy_produced_period = 0; // TODO: is this too radical for one random error?
    power_produced_period_avg = 0;
    return false;
  }
  uint8_t valid_values = 0;
  for (JsonPair Body_Data_item : doc["Body"]["Data"].as<JsonObject>())
  {
    if (Body_Data_item.key() == "PAC")
    {
      // Serial.print(F(", PAC:"));
      // Serial.print((long)Body_Data_item.value()["Value"]);
      current_power = Body_Data_item.value()["Value"]; // update and return new value
      valid_values++;
    }
    // use DAY_ENERGY (more accurate) instead of TOTAL_ENERGY?
    if (Body_Data_item.key() == "TOTAL_ENERGY")
    {
      // Serial.print(F("DAY_ENERGY:"));
      total_energy = Body_Data_item.value()["Value"]; // update and return new value
      inverter_total_value_last = total_energy;
      valid_values++;
    }
  }
  return (valid_values == 2);

} //
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
      Serial.print(F("Modbus request result: 0x"));
      Serial.println(event, HEX);
    }
    //  mb.disconnect( remote);
  }
  /* else
   {
     Serial.println(F(" Modbus read succesful"));
   }*/

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
    Serial.printf("buf[0] %d, buf[1] %d \n", (int)buf[0], (int)buf[1]);
    combined = buf[0] * (65536) + buf[1];
    if (buf[0] == 32768)
    { // special case
      combined = 0;
      Serial.println("buf[0] == 32768, disconnecting");
      mb.disconnect(s.production_meter_ip); // disconnect in the end
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
  uint16_t ip_port = s.production_meter_port;
  uint8_t modbusip_unit = s.production_meter_id;

  yield();
  Serial.printf("ModBus host: [%s], ip_port: [%d], unit_id: [%d] \n", s.production_meter_ip.toString().c_str(), ip_port, modbusip_unit);

  mb.task();
  if (!mb.isConnected(s.production_meter_ip))
  {
    Serial.print(F("Connecting Modbus TCP..."));
    bool cresult = mb.connect(s.production_meter_ip, ip_port);

    Serial.println(cresult);
    mb.task();
  }
  yield();

  long int total_energy_new;

  if (mb.isConnected(s.production_meter_ip))
  { // Check if connection to Modbus slave is established
    mb.task();

    Serial.println(F("Connection ok. Reading values from Modbus registries."));
    total_energy_new = get_mbus_value(s.production_meter_ip, SMA_TOTALENERGY_OFFSET, 2, modbusip_unit);
    // Serial.printf("total_energy_new %ld\n",total_energy_new);

    // validity check
    if (total_energy_new > 0 && (abs(total_energy_new - inverter_total_value_last) < 1000) || inverter_total_value_last == 0)
    {
      total_energy = total_energy_new;
      inverter_total_value_last = total_energy_new; // this variable is for checking validity
    }
    else
    {
      mb.task();
      return false;
    }

    mb.task();
    Serial.print(F(" total energy Wh:"));
    Serial.print(total_energy);

    current_power = get_mbus_value(s.production_meter_ip, SMA_POWER_OFFSET, 2, modbusip_unit);
    Serial.print(F(", current power W:"));
    Serial.println(current_power);

    // do not disconnect if no problems, disconnect+connect causes probably a memory leak or something
    ///   mb.disconnect(s.production_meter_ip); // disconnect in the end
    mb.task();
    yield();
    return true;
  }
  else
  {
    Serial.println(F("Connection failed."));
    return false;
  }
  mb.task();
}
#endif

/**
 * @brief Updates global variables based on date, time or time based tariffs
 *
 */
void calculate_time_based_variables()
{
  time_t now_ts = time(nullptr);
  localtime_r(&now_ts, &tm_struct);
  Serial.println("DEBUG start calculate_time_based_variables");

  yield();
  // update globals
  day_start_local = (((int)(time(nullptr) / SECONDS_IN_HOUR)) - tm_struct.tm_hour) * SECONDS_IN_HOUR; // TODO:DST

  vars.set(VARIABLE_MM, (long)(tm_struct.tm_mon + 1));
  vars.set(VARIABLE_MMDD, (long)(tm_struct.tm_mon + 1) * 100 + tm_struct.tm_mday);
  vars.set(VARIABLE_WDAY, (long)(tm_struct.tm_wday + 6) % 7 + 1);

  vars.set(VARIABLE_HH, (long)(tm_struct.tm_hour));
  vars.set(VARIABLE_HHMM, (long)(tm_struct.tm_hour) * 100 + tm_struct.tm_min);
  vars.set(VARIABLE_MINUTES, (long)tm_struct.tm_min);

  if (solar_forecast.last_update() > time(nullptr) - SECONDS_IN_DAY)
  { // we have a solar forecast
    time_t day_end_local = day_start_local + 23 * SECONDS_IN_HOUR;
    // uint16_t day_sum = solar_forecast.sum(day_start_local, day_end_local);
    long period_power_fcst = max((long)0, (long)(solar_forecast.get(time(nullptr)) * s.pv_power / 1000));
    long period_power_fcst_available = max((long)0, (long)(solar_forecast.get(time(nullptr)) * s.pv_power / 1000 - (s.baseload * s.netting_period_sec / SECONDS_IN_HOUR)));

    long day_sum_tuned = 0;

    for (time_t period = day_start_local; period <= day_end_local; period += solar_forecast.resolution_sec())
    {
      day_sum_tuned += max((long)0, (long)(solar_forecast.get(period) * s.pv_power / 1000 - s.baseload));
    }

    uint8_t isp_minutes = s.netting_period_sec / SECONDS_IN_MINUTE;
    if (period_power_fcst_available < WATT_EPSILON / 10 || day_sum_tuned < WATT_EPSILON)
      vars.set(VARIABLE_SOLAR_MINUTES_TUNED, (long)HOURS_IN_DAY * 60);
    else if (s.netting_period_sec == SECONDS_IN_HOUR)
    {
      vars.set(VARIABLE_SOLAR_MINUTES_TUNED, (long)(tm_struct.tm_min * day_sum_tuned / period_power_fcst_available));
    }
    else
    {
      // under construction, to be checked
      vars.set(VARIABLE_SOLAR_MINUTES_TUNED, (long)((tm_struct.tm_min % isp_minutes) * day_sum_tuned / period_power_fcst_available));
    }

    vars.set(VARIABLE_SOLAR_PRODUCTION_ESTIMATE_PERIOD, (long)(tm_struct.tm_min * period_power_fcst / 60));

    if (vars.get_l(VARIABLE_NET_ESTIMATE_SOURCE) == VARIABLE_NET_ESTIMATE_SOURCE_SOLAR_FORECAST)
    {
      time_t period_started_real = max(processing_started_ts, current_period_start_ts);

#define ALLOCATE_WHOLE_ESTIMATED_PERIOD
#ifndef ALLOCATE_WHOLE_ESTIMATED_PERIOD // will allocate forecatested production for use gradually, more dynamic and more switching
      long baseload_energy_period_sofar = (time(nullptr) - period_started_real) * s.baseload / 3600;
      long production_estimate_sofar = (time(nullptr) - period_started_real) * period_power_fcst / 3600;
      vars.set(VARIABLE_SELLING_ENERGY_ESTIMATE, (long)(production_estimate_sofar - (vars.get_l(VARIABLE_ESTIMATED_CHANNELS_CONSUMPTION, 0) + baseload_energy_period_sofar)));
      vars.set(VARIABLE_OVERPRODUCTION, (long)vars.get_l(VARIABLE_SELLING_ENERGY_ESTIMATE) > 0L ? 1L : 0L);
      Serial.printf("VARIABLE_SELLING_ENERGY_ESTIMATE %ld ; prod %ld , channels %ld , baseload so far %ld  \n", vars.get_l(VARIABLE_SELLING_ENERGY_ESTIMATE), production_estimate_sofar, vars.get_l(VARIABLE_ESTIMATED_CHANNELS_CONSUMPTION), baseload_energy_period_sofar);
#else // allocate all estimated available energy for use from actual start to the end of period, less swtiching
      long estimated_available_energy = max(0L, (current_period_start_ts + s.netting_period_sec - period_started_real) * (period_power_fcst - (long)s.baseload) / 3600);
      vars.set(VARIABLE_SELLING_ENERGY_ESTIMATE, estimated_available_energy - (vars.get_l(VARIABLE_ESTIMATED_CHANNELS_CONSUMPTION, 0)));
      vars.set(VARIABLE_OVERPRODUCTION, (long)vars.get_l(VARIABLE_SELLING_ENERGY_ESTIMATE) > 0L ? 1L : 0L);
      Serial.printf("VARIABLE_SELLING_ENERGY_ESTIMATE %ld ; available %ld , channels used  %ld  \n", vars.get_l(VARIABLE_SELLING_ENERGY_ESTIMATE), estimated_available_energy, vars.get_l(VARIABLE_ESTIMATED_CHANNELS_CONSUMPTION));
#endif
    };
  }
  yield();
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
void calculate_meter_based_variables()
{
#ifdef METER_SHELLY3EM_ENABLED
  // grid energy meter enabled
  // functionality in read_energy_meter_shelly3em
#endif

#if defined(INVERTER_FRONIUS_SOLARAPI_ENABLED) || defined(INVERTER_SMA_MODBUS_ENABLED)
  // TODO: tsekkaa miksi joskus nousee ylös lyhyeksi aikaa vaikkei pitäisi - johtuu kai siitä että fronius sammuu välillä illalla, laita kuntoon...
  if ((s.production_meter_type == PRODUCTIONM_FRONIUS_SOLAR) || (s.production_meter_type == PRODUCTIONM_SMA_MODBUS_TCP))
  {
    vars.set(VARIABLE_PRODUCTION_POWER, (long)(power_produced_period_avg));
    vars.set(VARIABLE_PRODUCTION_ENERGY, (long)(energy_produced_period));

    if (vars.get_l(VARIABLE_NET_ESTIMATE_SOURCE) == VARIABLE_NET_ESTIMATE_SOURCE_MEAS_PRODUCTION)
    {
      time_t period_started_real = max(processing_started_ts, current_period_start_ts);
      uint32_t baseload_energy_period_sofar = (time(nullptr) - period_started_real) * s.baseload / 3600;
      vars.set(VARIABLE_SELLING_ENERGY_ESTIMATE, (long)(energy_produced_period - (vars.get_l(VARIABLE_ESTIMATED_CHANNELS_CONSUMPTION, 0) + baseload_energy_period_sofar)));
      vars.set(VARIABLE_OVERPRODUCTION, (long)vars.get_l(VARIABLE_SELLING_ENERGY_ESTIMATE) > 0L ? 1L : 0L);
      Serial.printf("VARIABLE_SELLING_ENERGY_ESTIMATE %ld ; prod %ld , channels %ld , baseload so far %ld  \n", vars.get_l(VARIABLE_SELLING_ENERGY_ESTIMATE), (long)energy_produced_period, vars.get_l(VARIABLE_ESTIMATED_CHANNELS_CONSUMPTION, 0), baseload_energy_period_sofar);
    };
  }
  yield();
#endif
}

/**
 * @brief Utility function to round both positive and negative long values
 *
 * @param lval
 * @param divider
 * @return long
 */
long round_divide(long lval, long divider)
{
  long add_in_round = lval < 0 ? -divider / 2 : divider / 2;
  return (lval + add_in_round) / divider;
}

/**
 * @brief Calculate variables based on forecasts, eg. solar
 *
 */
void calculate_forecast_variables()
{
  long price;
  float sum_pv_fcst_with_price = 0;
  float pv_value_hour;
  float pv_value = 0;
  bool got_future_prices = false;

  long period_solar_rank = -1;
  if (solar_forecast.last_update() > time(nullptr) - SECONDS_IN_DAY) // do we have the location & data
    period_solar_rank = (long)solar_forecast.get_period_rank(current_period_start_ts, day_start_local, day_start_local + (HOURS_IN_DAY - 1) * SECONDS_IN_HOUR, true);
  if (period_solar_rank == -1)
    vars.set_NA(VARIABLE_SOLAR_RANK_FIXED_24);
  else
    vars.set(VARIABLE_SOLAR_RANK_FIXED_24, period_solar_rank);

  vars.set_NA(VARIABLE_PVFORECAST_SUM24);
  vars.set_NA(VARIABLE_PVFORECAST_VALUE24);
  vars.set_NA(VARIABLE_PVFORECAST_AVGPRICE24);
  yield();

  // FORECAST_TYPE_FI_LOCAL_SOLAR
  //  next 24 h
  if (solar_forecast.end() > time(nullptr))
  {
    for (time_t period = current_period_start_ts; period < current_period_start_ts + SECONDS_IN_DAY; period += SOLAR_FORECAST_RESOLUTION_SEC)
    {
      price = prices2.get(period, VARIABLE_LONG_UNKNOWN);
      if (price != VARIABLE_LONG_UNKNOWN)
      {
        sum_pv_fcst_with_price += (float)solar_forecast.get(period);
        pv_value_hour = price / 1000.0 * (float)solar_forecast.get(period);
        //  Serial.printf("period %lu, price %ld, pv_value_hour %f, forecast %f \n", period, price, pv_value_hour, (float)solar_forecast.get(period));
        pv_value += pv_value_hour;
        got_future_prices = true; // we got some price data
                                  //    Serial.printf("solar_forecast.end() %ld", solar_forecast.end());
        Serial.printf("j: %ld, price: %ld,  sum_pv_fcst_with_price: %f , pv_value_hour: %f, pv_value: %f\n", period, price, sum_pv_fcst_with_price, pv_value_hour, pv_value);
      }
      yield();
    }

    vars.set(VARIABLE_PVFORECAST_SUM24, (long)(solar_forecast.sum(current_period_start_ts, current_period_start_ts + 23 * SECONDS_IN_HOUR) * s.pv_power / 100) / 1000);

    // TODO: currently not levelized with
    if (got_future_prices && vars.get_l(VARIABLE_PVFORECAST_SUM24) > 1)
    {
      vars.set(VARIABLE_PVFORECAST_VALUE24, (float)(pv_value * (float)s.pv_power / 100000000));
      vars.set(VARIABLE_PVFORECAST_AVGPRICE24, (float)(pv_value / sum_pv_fcst_with_price));
    }
  }

  // FORECAST_TYPE_FI_WIND
  if (wind_forecast.start() != 0)
  {
    Serial.printf("day_start_local %lu \n", day_start_local);
    Serial.print("Finnish wind tomorrow avg, mWh:");
    Serial.println(wind_forecast.avg(day_start_local + SECONDS_IN_DAY, day_start_local + 47 * SECONDS_IN_HOUR));
    vars.set(VARIABLE_WIND_AVG_DAY1_FI, (long)wind_forecast.avg(day_start_local + SECONDS_IN_DAY, day_start_local + 47 * SECONDS_IN_HOUR));
    vars.set(VARIABLE_WIND_AVG_DAY2_FI, (long)wind_forecast.avg(day_start_local + 2 * SECONDS_IN_DAY, day_start_local + 71 * SECONDS_IN_HOUR));

    int block_start_before_this_idx = (24 + tm_struct_l.tm_hour - FIRST_BLOCK_START_HOUR) % DAY_BLOCK_SIZE_HOURS;
    time_t this_block_starts = (current_period_start_ts - block_start_before_this_idx * SECONDS_IN_HOUR);

    vars.set(VARIABLE_WIND_AVG_DAY1B_FI, (long)wind_forecast.avg(this_block_starts + 24 * SECONDS_IN_HOUR, this_block_starts + (24 + DAY_BLOCK_SIZE_HOURS - 1) * SECONDS_IN_HOUR));
    vars.set(VARIABLE_WIND_AVG_DAY2B_FI, (long)wind_forecast.avg(this_block_starts + 48 * SECONDS_IN_HOUR, this_block_starts + (48 + DAY_BLOCK_SIZE_HOURS - 1) * SECONDS_IN_HOUR));
    yield();
  }
}

/**
 * @brief Calculate price rank variables for current period
 * @details Price ranks tells how good is the price compared to other prices within window of periods \n
if rank is 1 then the price is best within the windows (e.g. from current period to next 9 hours) \n
windows/blocks are defined in variable price_variable_blocks, e.g. next 9 hours and 24 hours.
 *
 */
void calculate_price_rank_variables()
{
  int rank;
  long price_ratio_avg;

  int32_t window_price_avg;
  int32_t price_differs_avg;
  time_t now_infunc;
  time(&now_infunc);
  time_t current_period_start_ts = get_netting_period_start_time(time(nullptr));
  bool use_prices = (strncmp(s.entsoe_area_code, "#", 1) != 0);

  Serial.printf("calculate_price_rank_variables start: %ld, end: %ld, current_period_start_ts: %lu\n", prices_record_start, prices2.end() + PRICE_RESOLUTION_SEC, current_period_start_ts);
  if (prices2.get(now_infunc, VARIABLE_LONG_UNKNOWN) == VARIABLE_LONG_UNKNOWN)
  {
    if (use_prices)
    {
      Serial.printf("Cannot get price info for current period current_period_start_ts %lu , prices_expires_ts %lu, now_infunc %lu \n", current_period_start_ts, prices_expires_ts, now_infunc);
      log_msg(MSG_TYPE_ERROR, PSTR("Cannot get price info for current period."));
    }
    vars.set_NA(VARIABLE_PRICE);
    vars.set_NA(VARIABLE_PRICERANK_9);
    vars.set_NA(VARIABLE_PRICEAVG_9);
    vars.set_NA(VARIABLE_PRICEDIFF_9);
    vars.set_NA(VARIABLE_PRICERATIO_9);

    vars.set_NA(VARIABLE_PRICERANK_24);
    vars.set_NA(VARIABLE_PRICEAVG_24);
    vars.set_NA(VARIABLE_PRICEDIFF_24);
    vars.set_NA(VARIABLE_PRICERATIO_24);

    vars.set_NA(VARIABLE_PRICERANK_FIXED_24);
    vars.set_NA(VARIABLE_PRICERATIO_FIXED_24);

    vars.set_NA(VARIABLE_PRICERANK_FIXED_8);
    vars.set_NA(VARIABLE_PRICERANK_FIXED_8_BLOCKID);

    vars.set_NA(VARIABLE_AVGPRICE24_EXCEEDS_CURRENT);
    yield();

    return;
  }
  else if (prices_expires_ts + SECONDS_IN_HOUR * 1 < now_infunc)
  {
    if (strncmp(s.entsoe_area_code, "elering:", 8) == 0)
      log_msg(MSG_TYPE_ERROR, PSTR("Cannot get price data from Elering."));
    else
      log_msg(MSG_TYPE_ERROR, PSTR("Cannot get price data from Entso-E. Check availability from https://transparency.entsoe.eu/."));
  }

  localtime_r(&current_period_start_ts, &tm_struct_l);

  vars.set(VARIABLE_PRICE, (long)((prices2.get(now_infunc) + 50) / 100));
  Serial.printf("\n\n current_period_start_ts: %lu, %04d-%02d-%02d %02d:00, \n", current_period_start_ts, tm_struct_l.tm_year + 1900, tm_struct_l.tm_mon + 1, tm_struct_l.tm_mday, tm_struct_l.tm_hour);
  yield();

  // 9 h sliding
  time_t last_ts_in_window = min(current_period_start_ts + 8 * prices2.resolution_sec(), prices2.last_set_period_ts());
  Serial.printf("\n current_period_start_ts %ld last_ts_in_window %ld, A %ld,  B %ld\n", current_period_start_ts, last_ts_in_window, current_period_start_ts + 8 * prices2.resolution_sec(), prices2.last_set_period_ts());
  rank = prices2.get_period_rank(current_period_start_ts, last_ts_in_window - 8 * prices2.resolution_sec(), last_ts_in_window);
  prices2.stats(current_period_start_ts, last_ts_in_window - 8 * prices2.resolution_sec(), last_ts_in_window, &window_price_avg, &price_differs_avg, &price_ratio_avg);
  Serial.printf("9 h current_period_start_ts  %ld, rank %ld, avg %ld, diff %ld, ratio %ld\n", current_period_start_ts, (long)rank, window_price_avg, price_differs_avg, price_ratio_avg);

  vars.set(VARIABLE_PRICERANK_9, (long)rank);
  vars.set(VARIABLE_PRICEAVG_9, (long)round_divide(window_price_avg, 100));
  vars.set(VARIABLE_PRICEDIFF_9, (long)round_divide(price_differs_avg, 100));
  vars.set(VARIABLE_PRICERATIO_9, (long)price_ratio_avg);
  yield();

  // 24 h sliding
  last_ts_in_window = min(current_period_start_ts + 23 * prices2.resolution_sec(), prices2.last_set_period_ts());
  rank = prices2.get_period_rank(current_period_start_ts, last_ts_in_window - 23 * prices2.resolution_sec(), last_ts_in_window);
  prices2.stats(current_period_start_ts, last_ts_in_window - 23 * prices2.resolution_sec(), last_ts_in_window, &window_price_avg, &price_differs_avg, &price_ratio_avg);
  Serial.printf("New way 24 h rank %ld, avg %ld, diff %ld, ratio %ld\n", (long)rank, window_price_avg, price_differs_avg, price_ratio_avg);

  vars.set(VARIABLE_PRICERANK_24, (long)rank);
  vars.set(VARIABLE_PRICEAVG_24, (long)round_divide(window_price_avg, 100));
  vars.set(VARIABLE_PRICEDIFF_24, (long)round_divide(price_differs_avg, 100));
  vars.set(VARIABLE_PRICERATIO_24, (long)price_ratio_avg);
  yield();

  // 24 h fixed nychthemeron

  // 24 h fixed new,
  time_t first_ts_in_window = current_period_start_ts - tm_struct_l.tm_hour * prices2.resolution_sec();
  last_ts_in_window = first_ts_in_window + prices2.resolution_sec() * 23;
  rank = prices2.get_period_rank(current_period_start_ts, last_ts_in_window - 23 * prices2.resolution_sec(), last_ts_in_window);
  prices2.stats(current_period_start_ts, last_ts_in_window - 23 * prices2.resolution_sec(), last_ts_in_window, &window_price_avg, &price_differs_avg, &price_ratio_avg);
  Serial.printf("New way 24 h fixed rank %ld, avg %ld, diff %ld, ratio %ld\n", (long)rank, window_price_avg, price_differs_avg, price_ratio_avg);
  vars.set(VARIABLE_PRICERANK_FIXED_24, (long)rank);
  vars.set(VARIABLE_PRICERATIO_FIXED_24, (long)price_ratio_avg);
  yield();

  // 8 h blocks
  int block_idx = (int)((HOURS_IN_DAY + tm_struct_l.tm_hour - FIRST_BLOCK_START_HOUR) / DAY_BLOCK_SIZE_HOURS) % (HOURS_IN_DAY / DAY_BLOCK_SIZE_HOURS);
  int block_start_before_this_idx = (HOURS_IN_DAY + tm_struct_l.tm_hour - FIRST_BLOCK_START_HOUR) % DAY_BLOCK_SIZE_HOURS;
  first_ts_in_window = current_period_start_ts - PRICE_RESOLUTION_SEC * block_start_before_this_idx;
  last_ts_in_window = first_ts_in_window + 7 * PRICE_RESOLUTION_SEC;
  rank = prices2.get_period_rank(current_period_start_ts, first_ts_in_window, last_ts_in_window);
  Serial.printf("New way 8 h block rank %ld\n", (long)rank);
  vars.set(VARIABLE_PRICERANK_FIXED_8, (long)rank);
  vars.set(VARIABLE_PRICERANK_FIXED_8_BLOCKID, (long)block_idx + 1);
  yield();

  if (vars.is_set(VARIABLE_PVFORECAST_AVGPRICE24) && vars.is_set(VARIABLE_PRICE))
    vars.set(VARIABLE_AVGPRICE24_EXCEEDS_CURRENT, (long)vars.get_l(VARIABLE_PVFORECAST_AVGPRICE24) - (vars.get_l(VARIABLE_PRICE)));
  else
    vars.set_NA(VARIABLE_AVGPRICE24_EXCEEDS_CURRENT);
  return;
}
/**
 * @brief
 *
 *
 * @brief Get the Element Value from piece of xml
 *
 * @param outerXML
 * @return String
 */

/** Old, pior 16.11.2023
String getElementValue(String outerXML)
{
  int s1 = outerXML.indexOf(">", 0);
  int s2 = outerXML.substring(s1 + 1).indexOf("<");
  return outerXML.substring(s1 + 1, s1 + s2 + 1);
}*/

String getElementValue(String outerXML)
{
  int s, e;
  s = outerXML.indexOf(">", 0) + 1;
  e = outerXML.indexOf("</");
  if (e == -1) // end tag missing, try to read to the end
    e = outerXML.length();
  if (s > e) // no start tag found in the beging (but the end), get from the start of the string
    s = 0;

  //    Serial.printf("getElementValue: %d, %d\n", s, e);
  //    Serial.printf("getElementValue: [%s] %d, %d -> [%s]\n", outerXML.c_str(), s, e, outerXML.substring(s, e).c_str());
  return outerXML.substring(s, e);
}

/**
 * @brief Convert date time string YYYYMMDDTmmhhss to UTC time stamp
 *
 * @param elem
 * @return time_t
 */
time_t ElementToUTCts(String elem)
{ //
  String str_val = getElementValue(elem);
  return getTimestamp(str_val.substring(0, 4).toInt(), str_val.substring(5, 7).toInt(), str_val.substring(8, 10).toInt(), str_val.substring(11, 13).toInt(), str_val.substring(14, 16).toInt(), 0);
}

String read_http11_line_old(WiFiClientSecure *client_https)
{
  String line;
  String line2;
  bool line_incomplete = false;
  while (client_https->available())
  {
    if (!line_incomplete)
    {
      line = client_https->readStringUntil('\n'); //  \r tulee vain dokkarin lopussa (tai bufferin saumassa?)
                                                  // Serial.print("line:");Serial.println(line);
      if (line.charAt(line.length() - 1) == 13)
      {
        if (is_chunksize_line(line))
        { // skip error status "garbage" line, probably chuck size to read
          Serial.printf("chunksize line detected %s\n", line.c_str());
          continue;
        }
        line.trim(); // remove cr and mark line incomplete

        // line is complete if ends with >,6.8.2023 /OR
        if (line.charAt(line.length() - 1) == 62)
        {
          line_incomplete = false;
          return line;
        }

        line_incomplete = true; // we do not have whole line yet
        Serial.printf("[%s] %d\n", line.c_str(), (int)line.charAt(line.length() - 1));
      }
      else
      {
        line.trim();
        return line;
      }
    }
    else // line is incomplete, we will get more to add
    {
      line2 = client_https->readStringUntil('\n');
      // Serial.print("line2:");
      // Serial.println(line2);
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
      return line;
    }
  }
  return line;
}

// Initial testing 16.8.23 ok, but needs more testing. Headers should be handled before (possibly)

String read_http11_line(WiFiClientSecure *client_https)
{
  String line;
  String line2;
  if (client_https->available())
  {
    line = client_https->readStringUntil('\n');
    if (line.charAt(line.length() - 1) == 13)
    {
      line = line.substring(0, line.length() - 1); // remove cr
      if (client_https->available())
      {
        line2 = client_https->readStringUntil('\n');
        if (is_chunksize_line(line2))
        { // skip this line and read  next one
          Serial.print("Skipped line:");
          Serial.println(line2);
          if (client_https->available())
          {
            line2 = client_https->readStringUntil('\n');
          }
          else
            line2 = "";
        }
        return line + line2;
      }
      else
        return line;
    }
    else
      return line;
  }
  return "";
}

char in_buffer[2048]; // common buffer for multi chunk response and multiline input

#define FORECAST_TYPE_FI_LOCAL_SOLAR 1
#define FORECAST_TYPE_FI_WIND 2

/**
 * @brief Get the solar forecast from FMI open data.
 *
 * @return true
 * @return false
 */
bool get_renewable_forecast(uint8_t forecast_type, timeSeries *time_series)
{
  Serial.printf("get_renewable_forecast start getFreeHeap: %d\n", (int)ESP.getFreeHeap());
  //  if (forecast_type == FORECAST_TYPE_FI_LOCAL_SOLAR && strlen(s.forecast_loc) < 2)
  if (strlen(s.forecast_loc) < 2)
  {
    Serial.println(F("FMI forecast location undefined. Quitting"));
    return false;
  }

  WiFiClientSecure client_https;
  char fcst_url[120];
  DynamicJsonDocument doc(4096);
  // doc.garbageCollect();

  // reset variables

  if (forecast_type == FORECAST_TYPE_FI_LOCAL_SOLAR)
  {
    // adjust store window to start of the day,
    time_series->set_store_start(day_start_local); // assume day_start_local is up-to-date
  }
  else if (forecast_type == FORECAST_TYPE_FI_WIND)
  {
    time_series->set_store_start(day_start_local + 23 * SOLAR_FORECAST_RESOLUTION_SEC); // next day first block
  }

  if (!FILESYSTEM.exists(fmi_ca_filename))
  {
    log_msg(MSG_TYPE_ERROR, PSTR("Cannot connect to FMI server. Certificate file is missing."));
    return false;
  }

  String ca_cert = FILESYSTEM.open(fmi_ca_filename, "r").readString();

  // explicit close, better?
  // File ca_cert_file = FILESYSTEM.open(fmi_ca_filename, "r");
  // String ca_cert = ca_cert_file.readString();
  // ca_cert_file.close();

  // Serial.println(ca_cert);
  client_https.setCACert(ca_cert.c_str());

  client_https.setTimeout(5); // was 15 Seconds
  client_https.setHandshakeTimeout(5);
  yield();
  Serial.println(F("Connecting FMI with CA check."));
  Serial.println(host_fcst_fmi);
  delay(1000);

  if (!client_https.connect(host_fcst_fmi, httpsPort))
  {
    int err;
    char error_buf[70];
    err = client_https.lastError(error_buf, sizeof(error_buf) - 1);
    if (err != 0)
    {
      strncat(error_buf, "(connecting FMI)", sizeof(error_buf) - strlen(error_buf));
      log_msg(MSG_TYPE_ERROR, error_buf);
    }
    else
      log_msg(MSG_TYPE_ERROR, PSTR("Cannot connect to FMI server. Quitting forecast query."));
    client_https.stop();
    return false;
  }
  yield();

  if (forecast_type == FORECAST_TYPE_FI_LOCAL_SOLAR)
    snprintf(fcst_url, sizeof(fcst_url), "/products/renewable-energy-forecasts/solar/%s/solar_%s_fi_latest.json", s.forecast_loc, s.forecast_loc);
  else if (forecast_type == FORECAST_TYPE_FI_WIND)
    snprintf(fcst_url, sizeof(fcst_url), "/products/renewable-energy-forecasts/wind/windpower_fi_latest.json");

  Serial.printf("Requesting URL: %s\n", fcst_url);

  client_https.print(String("GET ") + fcst_url + " HTTP/1.0\r\n" +
                     "Host: " + host_fcst_fmi + "\r\n" +
                     "User-Agent: ArskaNodeESP\r\n" +
                     "Connection: close\r\n\r\n");

  // Serial.println("request sent");
  if (client_https.connected())
    Serial.println("client_https connected");
  else
    Serial.println("client_https not connected");
  // yield();
  unsigned long task_started = millis();
  while (client_https.connected())
  {
    String lineh = client_https.readStringUntil('\n');
    // Serial.println(lineh);
    if (lineh == "\r")
    {
      Serial.println("headers received");
      break;
    }
    if (millis() - task_started > 10000)
    {
      Serial.println(PSTR("Timeout in receiving headers"));
      client_https.stop();
      return false;
    }
    yield();
  }
  Serial.println(F("Waiting the document"));
  String line;

  memset(in_buffer, 0, sizeof(in_buffer));
  strcat(in_buffer, "[");
  yield();
  bool actual_data;

  while (client_https.available() > 1) // last byte in the end causes an error message
  {
    line = read_http11_line(&client_https);
    // Serial.println(line);
    line.trim();
    line.replace("000.0", ""); // timestamp millisec -> sec

    if (line.indexOf("\"data\":") > -1) // process only node "data"
      actual_data = true;
    else if (actual_data)
    {
      // Serial.print("*");
      strncat(in_buffer, (const char *)line.c_str(), sizeof(in_buffer) - strlen(in_buffer) - 2);
      if ((line.indexOf("]") > -1) && (line.indexOf("],") == -1)) // data array ends
      {
        actual_data = false;
        strcat(in_buffer, "]");
      }
    }
  }
  client_https.stop();
  Serial.println("in_buffer:");
  Serial.println(in_buffer);

  DeserializationError error = deserializeJson(doc, in_buffer);
  if (error)
  {
    Serial.print("deserializeJson() failed: ");
    Serial.println(error.c_str());
    return false;
  }

  time_t period;
  float energy;

  for (JsonArray elem : doc.as<JsonArray>())
  {
    period = (time_t)elem[0] - SECONDS_IN_HOUR; // The value represent previous hour, Anders Lindfors 3.5.2023
    energy = elem[1];
    if (energy > 0.001)
    {
      time_series->set(period, energy * 1000);
    }
  }
  // Free resources
  client_https.stop();

  yield();
  Serial.printf("get_renewable_forecast end getFreeHeap: %d\n", (int)ESP.getFreeHeap());
  return true;
}

/**
 * @brief Gets SPOT-prices from EntroE to a json file  (price_data_file_name)
 * @details If existing price data file is not expired use it and return immediately
 *
 * @return true
 * @return false
 */
bool get_price_data_entsoe()
{
  Serial.printf("get_price_data_entsoe start\n");
  if (prices_expires_ts > time(nullptr))
  {
    Serial.println(F("Price data not expired, returning"));
    return false;
  }
  if (strlen(s.entsoe_api_key) < 36 || strlen(s.entsoe_area_code) < 5)
  {
    log_msg(MSG_TYPE_WARN, PSTR("Check Entso-E parameters (API key and price area) for price updates."));
    return false;
  }

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
  start_ts = now_infunc - (SECONDS_IN_HOUR * 22); // no previous day after 22h, assume we have data ready for next day
  //  #pragma message("Testing with special date setting, REMOVE")
  //    start_ts = start_ts - 14 * 3600;
  //    start_ts = 1691107200;

  end_ts = start_ts + SECONDS_IN_DAY * 2;

  int pos = -1;
  long price = VARIABLE_LONG_UNKNOWN;

  // initiate prices
  localtime_r(&start_ts, &tm_struct);
  Serial.println(start_ts);
  snprintf(date_str_start, sizeof(date_str_start), "%04d%02d%02d0000", tm_struct.tm_year + 1900, tm_struct.tm_mon + 1, tm_struct.tm_mday);
  localtime_r(&end_ts, &tm_struct);
  snprintf(date_str_end, sizeof(date_str_end), "%04d%02d%02d0000", tm_struct.tm_year + 1900, tm_struct.tm_mon + 1, tm_struct.tm_mday);

  Serial.printf("Query period: %s - %s\n", date_str_start, date_str_end);
  if (!FILESYSTEM.exists(entsoe_ca_filename))
  {
    log_msg(MSG_TYPE_ERROR, PSTR("Cannot connect to Entso-E server. Certificate file is missing."));
    return false;
  }

  String ca_cert = FILESYSTEM.open(entsoe_ca_filename, "r").readString();
  client_https.setCACert(ca_cert.c_str());

  client_https.setTimeout(15); // was 5,15 Seconds
  client_https.setHandshakeTimeout(15);
  delay(1000);

  Serial.println(F("Connecting with CA check."));

  if (!client_https.connect(host_prices, httpsPort))
  {
    int err;
    char error_buf[70];
    err = client_https.lastError(error_buf, sizeof(error_buf) - 1);
    if (err != 0)
    {
      strncat(error_buf, "(connecting Entso-E)", sizeof(error_buf) - strlen(error_buf));
      log_msg(MSG_TYPE_ERROR, error_buf);
    }
    else
      log_msg(MSG_TYPE_ERROR, PSTR("Cannot connect to Entso-E server. Quitting price query."));
    client_https.stop();
    return false;
  }
  char url[220];
  snprintf(url, sizeof(url), "%s&securityToken=%s&In_Domain=%s&Out_Domain=%s&periodStart=%s&periodEnd=%s", url_base, s.entsoe_api_key, s.entsoe_area_code, s.entsoe_area_code, date_str_start, date_str_end);
  Serial.print("requesting URL: ");

  Serial.println(url);

  //
  client_https.print(String("GET ") + url + " HTTP/1.1\r\n" +
                     "Host: " + host_prices + "\r\n" +
                     "User-Agent: ArskaNodeESP\r\n" +
                     "Accept: */*\r\n" +
                     "Connection: close\r\n\r\n");

  Serial.println("request sent");

  bool save_on = false;
  bool read_ok = false;

  unsigned long task_started = millis();

  while (client_https.connected())
  {
    String lineh = client_https.readStringUntil('\n');
    // Serial.println(lineh);
    if (lineh == "\r")
    {
      Serial.println("headers received");
      break;
    }
    if (millis() - task_started > 10000)
    {
      Serial.println(PSTR("Timeout in receiving headers"));
      client_https.stop();
      return false;
    }
    yield();
  }

  Serial.println(F("Waiting the document"));
  String line;
  String line2;
  bool contains_suspicious_prices = false;
  // we must remove extra carbage cr (13) + "5xx" + cr lines
  // .available() is 1 or low when the "garbage" comes, no more/much to read, after about 8k buffer is read
  while (client_https.available())
  {
    line = read_http11_line(&client_https);
    // Serial.printf("[%s]\n", line.c_str());
    // Serial.print("[");
    // Serial.print(line);
    // Serial.println("]");

    if (line.indexOf("<Publication_MarketDocument") > -1)
      save_on = true;
    if (line.indexOf("</Publication_MarketDocument>") > -1)
    {
      save_on = false;
      read_ok = true;
    }

    if (line.endsWith(F("</period.timeInterval>")))
    // if (line.indexOf(F("</period.timeInterval>"))>-1)
    { // header dates
      record_end_excl = period_end;
      Serial.printf("Debug before get_price_data_entsoe %lu, %d", period_end, prices2.n());
      prices2.set_store_start(period_end - prices2.n() * prices2.resolution_sec());
      record_start = record_end_excl - (PRICE_RESOLUTION_SEC * MAX_PRICE_PERIODS);
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
    }

    // max price in NordPool is 4000€/MWh https://www.nordpoolgroup.com/en/trading/Operational-Message-List/2022/04/day-ahead-reminder---new-harmonised-maximum-clearing-price-from-delivery-day-wednesday-11th-may-20220427161200/
    // 4000€ -> 400000
    else if (line.endsWith(F("</price.amount>")))
    {
      price = int(getElementValue(line).toFloat() * 100);
      if ((abs(price) < 0.001) || (price > 400000))
      { // suspicious value, could be parsing/data error
        contains_suspicious_prices = true;
        Serial.print("Suspicious prices:");
        Serial.println(getElementValue(line));
      }
      price_rows++;
    }
    else if (line.endsWith("</Point>"))
    {

      prices2.set(period_start + (pos - 1) * PRICE_RESOLUTION_SEC, price);
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

    prices_record_start = record_start;

    if (contains_suspicious_prices)
    { // potential problem in the latest fetch, give shorter validity time
      Serial.println("Contains suspicious prices. Could be still ok. Retry in 4 hours.");
      prices_expires_ts = now_infunc + (4 * SECONDS_IN_HOUR);
    }
    else
    {
      prices_expires_ts = record_end_excl - (11 * SECONDS_IN_HOUR); // prices for next day should come after 12hUTC, so no need to query before that
      Serial.printf("No zero prices. Prices expires at %ld\n", prices_expires_ts);
    }

    Serial.println(F("Finished succesfully get_price_data_entsoe."));
    prices2.debug_print();

    // update to Influx if defined
    update_prices_to_influx();
    Serial.printf("get_price_data_entsoe end.\n");
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

bool is_force_up_valid(int channel_idx)
{
  return ((s.ch[channel_idx].force_up_from_ts <= time(nullptr)) && (time(nullptr) < s.ch[channel_idx].force_up_until_ts));
}
/**
 * @brief Returns active rule of the channel, -1 if no active
 *
 * @param channel_idx
 * @return int
 */
int get_channel_active_rule(int channel_idx)
{
  for (int i = 0; i < CHANNEL_RULES_MAX; i++)
  {
    if (s.ch[channel_idx].rules[i].rule_active)
      return i;
  }
  return -1;
}

#if defined(ARDUINO)
#define JSON_DOCUMENT DynamicJsonDocument
#define CREATE_JSON_DOCUMENT(doc) DynamicJsonDocument doc(CONFIG_JSON_SIZE_MAX)

#define ADD_JSON_TEXT(parent, node_name, text) parent[node_name] = text
#define ADD_JSON_NUMBER(parent, node_name, number) parent[node_name] = number
#define ADD_JSON_BOOL(parent, node_name, bool_value) parent[node_name] = bool_value

#define ADD_JSON_ARRAY_NUMBER(parent, value) parent.add(value)
#define ADD_JSON_ARRAY_TEXT(parent, value) parent.add(value)
#define ADD_JSON_ARRAY_BOOL(parent, value) parent.add(value)

#define JSON_ARRAY_NODE JsonArray
#define ADD_JSON_ARRAY(parent, node_name, node) parent.createNestedArray(node_name)
#define ADD_JSON_ARRAY_NODE(parent, node) parent.createNestedArray()

#define JSON_CHILD_NODE JsonObject
#define ADD_JSON_CHILD_NODE(parent, node) parent.createNestedObject()

#define JSON_SERIALIZE(doc, output_string) \
  String output_string;                    \
  serializeJson(doc, output_string);
#define JSON_SEND(request, output_string) request->send(200, "application/json", output_string);
#define JSON_FREE_RESOURCES(output_string) ;

#endif

/**
 * @brief Generate application constants for the user interface
 *
 * @param force_create
 * @return true
 * @return false
 */

//   /application
void onWebApplicationGet(AsyncWebServerRequest *request)
{
  if (!request->authenticate(s.http_username, s.http_password))
  {
    return request->requestAuthentication();
  }

  CREATE_JSON_DOCUMENT(doc);

  /*********************/

  /**
    CREATE_JSON_DOCUMENT(doc);
    ADD_JSON_TEXT(doc, "test", "testvalue");
    // list of  objects
    JSON_ARRAY_NODE p0 = ADD_JSON_ARRAY(doc, "p0", p0);
    JSON_CHILD_NODE n0;
    for (int i = 0; i < 2; i++)
    {
        n0 = ADD_JSON_CHILD_NODE(p0, n0);
        ADD_JSON_TEXT(n0, "f1", "f1value");
        ADD_JSON_NUMBER(n0, "f2", 2);
        ADD_JSON_BOOL(n0, "f3", true);
    }

    JSON_ARRAY_NODE p1 = ADD_JSON_ARRAY(doc, "test1", p1);
    JSON_ARRAY_NODE oper;
    for (int i = 0; i < 2; i++)
    {
        oper = ADD_JSON_ARRAY_NODE(p1, oper);
        ADD_JSON_ARRAY_NUMBER(oper, i);
        ADD_JSON_ARRAY_NUMBER(oper, i * 2);
        ADD_JSON_ARRAY_TEXT(oper, "OIoi2");
    }
    */
  /*********************/

  ADD_JSON_TEXT(doc, "compile_date", compile_date);
  ADD_JSON_TEXT(doc, "HWID", HWID);
  ADD_JSON_TEXT(doc, "VERSION", VERSION);
  ADD_JSON_TEXT(doc, "VERSION_SHORT", VERSION_SHORT);
  ADD_JSON_TEXT(doc, "version_fs", version_fs);

  ADD_JSON_NUMBER(doc, "RULE_STATEMENTS_MAX", RULE_STATEMENTS_MAX);
  ADD_JSON_NUMBER(doc, "CHANNEL_COUNT", CHANNEL_COUNT);
  ADD_JSON_NUMBER(doc, "CHANNEL_RULES_MAX", CHANNEL_RULES_MAX);

  // some debug info
  ADD_JSON_NUMBER(doc, "ts", time(nullptr));
  ADD_JSON_NUMBER(doc, "free_heap", ESP.getFreeHeap());
  ADD_JSON_BOOL(doc, "fs_mounted", fs_mounted);

#ifdef INFLUX_REPORT_ENABLED
  ADD_JSON_BOOL(doc, "INFLUX_REPORT_ENABLED", true);
#else
  ADD_JSON_BOOL(doc, "INFLUX_REPORT_ENABLED", false);
#endif

#ifdef PRICE_ELERING_ENABLED
  ADD_JSON_BOOL(doc, "PRICE_ELERING_ENABLED", true);
#endif

#ifdef DEBUG_MODE_ENABLED
  ADD_JSON_BOOL(doc, "DEBUG_MODE_ENABLED", true);
#else
  ADD_JSON_BOOL(doc, "DEBUG_MODE_ENABLED", false);
#endif

  // JsonArray json_opers = doc.createNestedArray("opers");
  JSON_ARRAY_NODE json_opers = ADD_JSON_ARRAY(doc, "opers", json_opers);
  for (int i = 0; i < OPER_COUNT; i++)
  {
    // JsonArray json_oper = json_opers.createNestedArray();
    JSON_ARRAY_NODE json_oper = ADD_JSON_ARRAY_NODE(json_opers, json_oper);
    // json_oper.add(opers[i].id);
    ADD_JSON_ARRAY_NUMBER(json_oper, opers[i].id);
    // json_oper.add(opers[i].code);
    ADD_JSON_ARRAY_TEXT(json_oper, opers[i].code);
    // json_oper.add(opers[i].gt);
    ADD_JSON_ARRAY_BOOL(json_oper, opers[i].gt);
    // json_oper.add(opers[i].eq);
    ADD_JSON_ARRAY_BOOL(json_oper, opers[i].eq);
    // json_oper.add(opers[i].reverse);
    ADD_JSON_ARRAY_BOOL(json_oper, opers[i].reverse);

    // json_oper.add(opers[i].boolean_only);
    ADD_JSON_ARRAY_BOOL(json_oper, opers[i].boolean_only);

    // json_oper.add(opers[i].has_value);
    ADD_JSON_ARRAY_BOOL(json_oper, opers[i].has_value);

    ADD_JSON_ARRAY_BOOL(json_oper, opers[i].multiselect);
  }

  int variable_count = vars.get_variable_count();
  variable_st variable;
  // String output;
  //  JsonArray json_variables = doc.createNestedArray("variables");
  JSON_ARRAY_NODE json_variables = ADD_JSON_ARRAY(doc, "variables", json_variables);
  for (int variable_idx = 0; variable_idx < variable_count; variable_idx++)
  {
    // JsonArray json_variable = json_variables.createNestedArray();
    JSON_ARRAY_NODE json_variable = ADD_JSON_ARRAY_NODE(json_variables, json_variable);
    vars.get_variable_by_idx(variable_idx, &variable);
    // json_variable.add(variable.id);
    ADD_JSON_ARRAY_NUMBER(json_variable, variable.id);
    // json_variable.add(variable.type);
    ADD_JSON_ARRAY_NUMBER(json_variable, variable.type);
    //  json_variable.add(variable.bitmask_config);
    ADD_JSON_ARRAY_NUMBER(json_variable, variable.bitmask_config);
  }

  // JsonArray json_channel_types = doc.createNestedArray("channel_types");
  JSON_ARRAY_NODE json_channel_types = ADD_JSON_ARRAY(doc, "channel_types", json_channel_types);
  for (int channel_type_idx = 0; channel_type_idx < CHANNEL_TYPE_COUNT; channel_type_idx++)
  {
    // JsonObject json_channel_type = json_channel_types.createNestedObject();
    JSON_CHILD_NODE json_channel_type = ADD_JSON_CHILD_NODE(json_channel_types, json_channel_type);
    // json_channel_type["id"] = (int)channel_types[channel_type_idx].id;
    ADD_JSON_NUMBER(json_channel_type, "id", (int)channel_types[channel_type_idx].id);
    // json_channel_type["name"] = channel_types[channel_type_idx].name;

    ADD_JSON_TEXT(json_channel_type, "name", channel_types[channel_type_idx].name);
  }

  // JsonArray json_hs_templates = doc.createNestedArray("hw_templates");
  JSON_ARRAY_NODE json_hs_templates = ADD_JSON_ARRAY(doc, "hw_templates", json_hs_templates);
  for (int hw_template_idx = 0; hw_template_idx < HW_TEMPLATE_COUNT; hw_template_idx++)
  {
#ifndef HW_EXTENSIONS_ENABLED // skip register templates
    if (hw_templates[hw_template_idx].hw_io.output_register)
      continue;
#endif
    //  JsonObject json_hs_template = json_hs_templates.createNestedObject();
    JSON_CHILD_NODE json_hs_template = ADD_JSON_CHILD_NODE(json_hs_templates, json_hs_template);

    //  json_hs_template["id"] = (int)hw_templates[hw_template_idx].id;
    ADD_JSON_NUMBER(json_hs_template, "id", (int)hw_templates[hw_template_idx].id);

    //  json_hs_template["name"] = hw_templates[hw_template_idx].name;
    ADD_JSON_TEXT(json_hs_template, "name", hw_templates[hw_template_idx].name);
  }

  JSON_SERIALIZE(doc, output);
  JSON_SEND(request, output);
  JSON_FREE_RESOURCES(output_string);
}

void onWebWifisGet(AsyncWebServerRequest *request)
{
  StaticJsonDocument<512> doc;
  String output;
  for (int i = 0; i < WIFI_LIST_COUNT; i++)
  {
    if (strlen(wifis[i].ssid) > 0)
    {
      JsonObject json_wifi = doc.createNestedObject();
      json_wifi["id"] = wifis[i].ssid;
      json_wifi["rssi"] = wifis[i].rssi;
    }
  }
  serializeJson(doc, output);
  request->send(200, "application/json", output);
}

/**
 * @brief Read grid or production info from energy meter/inverter
 *
 */
// see also receive_energy_meter_han_direct which handles incoming messages from HAN P1 port
void read_energy_meter()
{
  bool read_ok;
  energy_meter_last_read_started_ms = millis();
  energy_meter_read_all_count++;

  yield();
  // SHELLY
  if (s.energy_meter_type == ENERGYM_SHELLY3EM || s.energy_meter_type == ENERGYM_SHELLY_GEN2)
  {
#ifdef METER_SHELLY3EM_ENABLED
    read_ok = read_energy_meter_shelly3em();
#endif
  }
  else if (s.energy_meter_type == ENERGYM_HAN_WIFI)
  {
#ifdef METER_HAN_ENABLED
    read_ok = read_energy_meter_han_wifi();
#endif
  }
  // NO ENERGY METER DEFINED, function should not be called
  else
  {
    return;
  }
  bool internet_connection_ok = false;
  if (!read_ok)
  {
    if ((energy_meter_read_succesfully_ts + WARNING_AFTER_FAILED_READING_SECS < time(nullptr))) // if specified time since last succesful read before logging/reacting
    {
      if (ping_enabled)
      {
        internet_connection_ok = test_host(IPAddress(8, 8, 8, 8)); // Google DNS, TODO: set address to parameters
      }
      if (internet_connection_ok)
        log_msg(MSG_TYPE_FATAL, PSTR("Internet connection ok, but cannot read energy meter. Check the meter."));
      else if ((energy_meter_read_succesfully_ts + RESTART_AFTER_LAST_OK_METER_READ < time(nullptr)) && (energy_meter_read_succesfully_ts > 0))
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
  Serial.printf("read_energy_meter took %lu, ok %lu, failed %lu\n", millis() - energy_meter_last_read_started_ms, energy_meter_read_ok_count, energy_meter_read_all_count - energy_meter_read_ok_count);
}
//

/**
 * @brief Read  production info from an inverter
 *
 */
void read_production_meter()
{
  bool read_ok = false;
  bool internet_connection_ok = false;
  long int total_energy = 0;
  long int current_power = 0;
  bool period_changed_since_last_read = ((production_meter_read_last_ts / s.netting_period_sec) != (time(nullptr) / s.netting_period_sec));

  Serial.println("read_production_meter");

  yield();
  if (s.production_meter_type != PRODUCTIONM_FRONIUS_SOLAR && (s.production_meter_type != PRODUCTIONM_SMA_MODBUS_TCP))
  {
    return; // NO ENERGY METER DEFINED, function should not be called
  }

#if defined(INVERTER_FRONIUS_SOLARAPI_ENABLED) || defined(INVERTER_SMA_MODBUS_ENABLED)
  yield();
  if ((s.production_meter_type == PRODUCTIONM_FRONIUS_SOLAR))
  {
    read_ok = read_inverter_fronius_data(total_energy, current_power);
  }
  else if (s.production_meter_type == PRODUCTIONM_SMA_MODBUS_TCP)
  {
    read_ok = read_inverter_sma_data(total_energy, current_power);
  }

  if (read_ok)
  {
    yield();
    if (period_changed_since_last_read) // new period or earlier reads in this period were unsuccessfull
    {
      if (production_meter_read_last == 0) // first read after boot,  initiate
      {
        production_meter_read_last = total_energy;
      }
      Serial.println(F("read_production_meter period_changed_since_last_read"));
      inverter_total_period_init = production_meter_read_last; // last reading from previous period
    }
    energy_produced_period = total_energy - inverter_total_period_init;
    production_meter_read_last_ts = time(nullptr);
    production_meter_read_last = total_energy;
  }
  else
  { // read was not ok
    Serial.println(F("Cannot read from the inverter."));
    if (period_changed_since_last_read)
    {
      //  inverter_total_period_init = 0;  //TODO: check is the same logic is in em also
      energy_produced_period = 0;
    }
    yield();
    if ((production_meter_read_last_ts + WARNING_AFTER_FAILED_READING_SECS < time(nullptr)))
    {                   // 3 minutes since last succesful read before logging
      if (ping_enabled) // TODO: ping local gw
      {
        internet_connection_ok = test_host(WiFi.gatewayIP()); // test_host(IPAddress(8, 8, 8, 8)); // Google DNS, TODO: set address to parameters
      }
      if (internet_connection_ok)
        log_msg(MSG_TYPE_FATAL, PSTR("Wifi connection ok, but cannot read production meter/inverter. Check the meter."));
      else
        log_msg(MSG_TYPE_ERROR, PSTR("Failed to read production meter. Check Wifi, internet connection and the meter."));
    }
  }

  long int time_since_recording_period_start = time(nullptr) - recorded_period_start_ts;
  if (time_since_recording_period_start > USE_POWER_TO_ESTIMATE_ENERGY_SECS) // in the beginning of period use current power to estimate energy generated
    power_produced_period_avg = energy_produced_period * 3600 / time_since_recording_period_start;
  else
    power_produced_period_avg = current_power;

  Serial.printf("energy_produced_period: %ld , time_since_recording_period_start: %ld , power_produced_period_avg: %ld , current_power:  %ld\n", energy_produced_period, time_since_recording_period_start, power_produced_period_avg, current_power);
  yield();

#endif
  ;

  yield();
}
//

/**
 * @brief Get a channel to switch next, using channel priority
 * @details There can be multiple channels which could be switched but not all are switched at the same round
 *
 * @param is_rise
 * @return int
 */
int get_channel_to_switch_prio(bool is_rise)
{
  uint8_t matching_prio;
  int matching_prio_channel = -1;
  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {
    if (is_rise && !s.ch[channel_idx].is_up && s.ch[channel_idx].wanna_be_up)
    { // we should rise this up, select down channel with lowest priority value
      Serial.printf("get_channel_to_switch_prio ch %d wanna to be up \n", channel_idx);
      if (matching_prio_channel == -1 || matching_prio > s.ch[channel_idx].priority)
      {
        matching_prio = s.ch[channel_idx].priority;
        matching_prio_channel = channel_idx;
      }
    }
    if (!is_rise && s.ch[channel_idx].is_up && !s.ch[channel_idx].wanna_be_up)
    { // we should drop this channel, select up channel with highest priority value
      Serial.printf("get_channel_to_switch_prio ch %d wanna be down , matching_prio %d, priority %d\n", channel_idx, (int)matching_prio, s.ch[channel_idx].priority);
      if (matching_prio_channel == -1 || matching_prio < s.ch[channel_idx].priority)
      {
        matching_prio = s.ch[channel_idx].priority;
        matching_prio_channel = channel_idx;
      }
    }
  }
  return matching_prio_channel;
}

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
  if (is_local_relay(s.ch[channel_idx].type))
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

  Serial.printf("url_to_call: %s\n", url_to_call);

  StaticJsonDocument<256> doc;
  yield();
  DeserializationError error = deserializeJson(doc, httpGETRequest(url_to_call, CONNECT_TIMEOUT_INTERNAL)); // shorter connect timeout for a local switch
  yield();
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
  relay_state_reapply_required[channel_idx] = false;

  if (s.ch[channel_idx].type == CH_TYPE_UNDEFINED)
    return false;

  bool up = s.ch[channel_idx].is_up;

  if (!init_relay && !up)
  { // channel goes normally down,
    s.ch[channel_idx].up_last_ts = time(nullptr);
    Serial.printf("Channel %d seen up now at %ld \n", channel_idx, (long)s.ch[channel_idx].up_last_ts);
  }
  Serial.printf("ch%d ->%s", channel_idx, up ? "HIGH  " : "LOW  ");

  ch_counters.set_state(channel_idx, up); // counters
  if (is_local_relay(s.ch[channel_idx].type))
  {
    uint8_t pin_val;
    if ((s.ch[channel_idx].type == CH_TYPE_GPIO_USR_INVERSED))
      pin_val = (up ? LOW : HIGH);
    else
      pin_val = (up ? HIGH : LOW);
#ifdef HW_EXTENSIONS_ENABLED
    if (hw_template_idx > 0 && hw_templates[hw_template_idx].hw_io.output_register)
    {
      if (s.ch[channel_idx].relay_id < MAX_REGISTER_BITS)
      {
        Serial.printf("Setting register bit %d %s\n", s.ch[channel_idx].relay_id, pin_val == HIGH ? "HIGH" : "LOW");
        bitWrite(register_out, s.ch[channel_idx].relay_id, pin_val); // TODO: add mapping from relay_id to bit, it is not necessarily same bits, or lock the ui

        // Serial.printf("register_out %d\n", (int)register_out);
        updateShiftRegister();

        return true;
      }
      else
        return false;
    }
#else
    if (false)
    {
      ; // extensions not yet enabled
    }
#endif
    else
    {
      if (test_set_gpio_pinmode(channel_idx, init_relay))
      {
        Serial.printf("Setting gpio  %d %s\n", s.ch[channel_idx].relay_id, pin_val == HIGH ? "HIGH" : "LOW");
        digitalWrite(s.ch[channel_idx].relay_id, pin_val);
        return true;
      }
      else
        return false; // invalid gpio
    }
  }
  // do not try to connect if there is no wifi client initiated - or we should to get an error
  else if (wifi_sta_connected && is_wifi_relay(s.ch[channel_idx].type))
  {
    switch_http_relay(channel_idx, up);
    return true;
  }

  return false;
}

/**
 * @brief Check which channels can be raised /dropped
 *
 */
void calculate_channel_states()
{
  bool forced_up;
  float current_capacity_available = 9999;
#ifdef LOAD_MGMT_ENABLED
  current_capacity_available = load_manager_capacity;
#endif
  int channel_idx;
  // loop channels and check whether channel should be up
  for (int channel_idx_ = 0; channel_idx_ < CHANNEL_COUNT; channel_idx_++)
  {
    channel_idx = ch_prio_sorted[channel_idx_]; // handle in priority order - if capacity is limited only best priority can be switch on
    if (s.ch[channel_idx].type == CH_TYPE_UNDEFINED)
    {
      s.ch[channel_idx].wanna_be_up = false;
      chstate_transit[channel_idx] = CH_STATE_NONE;
      continue;
    }

    // reset rule_active variable
    bool wait_minimum_uptime = (ch_counters.get_duration_in_this_state(channel_idx) < s.ch[channel_idx].uptime_minimum); // channel must stay up minimum time

    if (s.ch[channel_idx].force_up_until_ts == -1)
    { // force down
      s.ch[channel_idx].force_up_until_ts = 0;
      wait_minimum_uptime = false;
    }

#ifdef LOAD_MGMT_ENABLED

    // channel down and under loadm management control
    if (s.load_manager_active && !s.ch[channel_idx].is_up && (s.ch[channel_idx].load > 0))
    {
      if ((s.ch[channel_idx].load / WATTS_TO_AMPERES_FACTOR / s.load_manager_phase_count) > current_capacity_available)
      {
        Serial.printf("DEBUG: Not available capacity for channel %d to get up\n", channel_idx);
        s.ch[channel_idx].wanna_be_up = false;
        chstate_transit[channel_idx] = CH_STATE_BYLMGMT_NOCAPACITY;
        continue; // cannot switch on
      }
      if (time(nullptr) - load_manager_overload_last_ts < s.load_manager_reswitch_moratorium_m * 60)
      {
        Serial.printf("DEBUG: Load manager moratorium , channel %d \n", channel_idx);
        s.ch[channel_idx].wanna_be_up = false;
        chstate_transit[channel_idx] = CH_STATE_BYLMGMT_MORATORIUM;
        continue; // cannot switch on
      }
    }

#endif

    forced_up = (is_force_up_valid(channel_idx));
    if (s.ch[channel_idx].is_up && (wait_minimum_uptime || forced_up))
    {
      //  Not yet time to drop channel
      s.ch[channel_idx].wanna_be_up = true;
      continue;
    }

    // reset
    for (int rule_idx = 0; rule_idx < CHANNEL_RULES_MAX; rule_idx++)
    {
      s.ch[channel_idx].rules[rule_idx].rule_active = false;
    }

    if (!s.ch[channel_idx].is_up && forced_up)
    { // the channel is now down but should be forced up
      s.ch[channel_idx].wanna_be_up = true;
      chstate_transit[channel_idx] = CH_STATE_BYFORCE;
#ifdef LOAD_MGMT_ENABLED
      current_capacity_available -= (s.ch[channel_idx].load / WATTS_TO_AMPERES_FACTOR / s.load_manager_phase_count);
#endif
      Serial.println("forcing up");
      continue; // forced, not checking channel rules
    }

    // Now checking normal state based rules
    s.ch[channel_idx].wanna_be_up = false;
    // loop channel targets until there is match (or no more targets)
    bool statement_true;
    // if no statetements -> false (or default)
    int nof_matching_rules = 0;
    int nof_valid_statements;
    bool one_or_more_failed;

    for (int rule_idx = 0; rule_idx < CHANNEL_RULES_MAX; rule_idx++)
    {

      nof_valid_statements = 0;
      one_or_more_failed = false;
      // now loop the statement until end or false statement
      for (int statement_idx = 0; statement_idx < RULE_STATEMENTS_MAX; statement_idx++)
      {
        statement_st *statement = &s.ch[channel_idx].rules[rule_idx].statements[statement_idx];
        if (statement->variable_id != -1) // statement defined
        {
          nof_valid_statements++;
          //   Serial.printf("calculate_channel_states statement.variable_id: %d\n", statement->variable_id);
          statement_true = vars.is_statement_true(statement, false, channel_idx);
          if (!statement_true)
          {
            one_or_more_failed = true;
            break;
          }
        }
      } // statement loop

      if (!(nof_valid_statements == 0) && !one_or_more_failed)
      { // rule  matches

        s.ch[channel_idx].wanna_be_up = s.ch[channel_idx].rules[rule_idx].on; // set
        chstate_transit[channel_idx] = CH_STATE_BYRULE;
        s.ch[channel_idx].rules[rule_idx].rule_active = true;
#ifdef LOAD_MGMT_ENABLED
        if (s.ch[channel_idx].is_up != s.ch[channel_idx].wanna_be_up)
        {
          current_capacity_available -= (s.ch[channel_idx].load / WATTS_TO_AMPERES_FACTOR / s.load_manager_phase_count);
        }
#endif
        if (!s.ch[channel_idx].rules[rule_idx].rule_active)
        {
          // report debug change
          Serial.printf("channel_idx %d, rule_idx %d matches, channel wanna_be_up: %s, tested %d rules.\n", channel_idx, rule_idx, s.ch[channel_idx].wanna_be_up ? "true" : "false", nof_valid_statements);
        }
        nof_matching_rules++;
        break; // no more rule testing
      }
    } // rules/rule loop
    //
    // no rules match, using default value
    if (nof_matching_rules == 0)
    {
      chstate_transit[channel_idx] = CH_STATE_BYDEFAULT;
      s.ch[channel_idx].wanna_be_up = s.ch[channel_idx].default_state; // set
    }
    yield();
  } // channel loop

  /* experimental code  VARIABLE_BEEN_UP_AGO_HOURS_0
  // first test, move to channel loop when ready
  time_t since_last_up_secs = time(nullptr) - s.ch[0].up_last_ts;
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
  if (grid_protection_delay_used && (time(nullptr) < (current_period_start_ts + grid_protection_delay_interval)))
  {
    Serial.printf(PSTR("Grid protection delay %ld of %d secs, %ld left\n"), grid_protection_delay_interval, (current_period_start_ts + grid_protection_delay_interval - time(nullptr)));
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

#ifdef LOAD_MGMT_ENABLED
    // overload, drop all channels marked
    if (load_manager_capacity < 0 && !(drop_rise == 0))
      switchings_to_todo = drop_count;
#endif

    for (int i = 0; i < switchings_to_todo; i++)
    {
      int ch_to_switch = get_channel_to_switch_prio(is_rise); // return in priority order
      Serial.printf("Switching ch %d  (%d) from %d .-> %d\n", ch_to_switch, s.ch[ch_to_switch].relay_id, s.ch[ch_to_switch].is_up, is_rise);
      s.ch[ch_to_switch].is_up = is_rise;
      apply_relay_state(ch_to_switch, false);
    }
  }
}

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

#ifdef PRICE_ELERING_ENABLED
bool get_price_data_elering()
{
  Serial.printf("get_price_data_elering \n");
#ifdef NVS_CACHE_ENABLED

  if (prices2.read_from_cache(time(nullptr)))
  {
    Serial.println("Got from prices from cache");
    prices_expires_ts = prices2.expires();
    return true;
  }
#endif

  WiFiClientSecure client_https;
  char url[120];
  char country_code[3];
  strncpy(country_code, &s.entsoe_area_code[8], 3);
  Serial.printf("Elering country code: %s\n", country_code);

  time_t start_ts, end_ts; // this is the epoch
  tm tm_struct;
  String line;
  int sep1, sep2;
  String ts_string, val_string;
  time_t ts;
  float price;
  char date_str_start[30];
  char date_str_end[30];
  int price_rows = 0, price_idx;
  time_t ts_min = 4102444800; // in the future
  time_t ts_max = 0;
  long prices_local[MAX_PRICE_PERIODS];

  client_https.setCACert(letsencrypt_ca_certificate);

  client_https.setTimeout(15); // was 15 Seconds
  client_https.setHandshakeTimeout(5);

  yield();
  Serial.println(F("Connecting Elering with CA check."));

  if (!client_https.connect(host_prices_elering, httpsPort))
  {
    int err;
    char error_buf[70];
    err = client_https.lastError(error_buf, sizeof(error_buf) - 1);
    if (err != 0)
    {
      strncat(error_buf, "(connecting Elering)", sizeof(error_buf) - strlen(error_buf));
      log_msg(MSG_TYPE_ERROR, error_buf);
    }
    else
      log_msg(MSG_TYPE_ERROR, PSTR("Cannot connect to Elering server. Quitting price query."));
    client_https.stop();
    return false;
  }
  yield();

  start_ts = time(nullptr) - (SECONDS_IN_HOUR * (22 + 24)); // no previous day after 22h, assume we have data ready for next day
  end_ts = start_ts + SECONDS_IN_DAY * 3;

  localtime_r(&start_ts, &tm_struct);
  Serial.println(start_ts);
  snprintf(date_str_start, sizeof(date_str_start), "%04d-%02d-%02dT21%%3A00%%3A00Z", tm_struct.tm_year + 1900, tm_struct.tm_mon + 1, tm_struct.tm_mday);
  localtime_r(&end_ts, &tm_struct);
  // hour 21-> 22 to get all wintertime
  snprintf(date_str_end, sizeof(date_str_end), "%04d-%02d-%02dT22%%3A00%%3A00Z", tm_struct.tm_year + 1900, tm_struct.tm_mon + 1, tm_struct.tm_mday);

  Serial.printf("Query period: %s - %s\n", date_str_start, date_str_end);

  client_https.setTimeout(15); // was 15 Seconds
  delay(1000);
  Serial.println(F("Connecting Elering with CA check."));

  /// api/nps/price/csv?start=2020-05-31T20%3A59%3A59.999Z&end=2020-06-30T20%3A59%3A59.999Z&fields=fi
  snprintf(url, sizeof(url), "/api/nps/price/csv?start=%s&end=%s&fields=%s", date_str_start, date_str_end, country_code);

  Serial.printf("Requesting URL: %s\n", url);

  client_https.print(String("GET ") + url + " HTTP/1.0\r\n" +
                     "Host: " + host_prices_elering + "\r\n" +
                     "User-Agent: ArskaNodeESP\r\n" +
                     "Connection: close\r\n\r\n");

  // Serial.println("request sent");
  if (client_https.connected())
    Serial.println("client_https connected");
  else
  {
    Serial.println("client_https not connected");
    return false;
  }
  yield();

  unsigned long task_started = millis();
  while (client_https.connected())
  {
    String lineh = client_https.readStringUntil('\n');
    // Serial.println(lineh);
    if (lineh == "\r")
    {
      Serial.println("headers received");
      break;
    }
    if (millis() - task_started > 10000)
    {
      Serial.println(PSTR("Timeout in receiving headers"));
      client_https.stop();
      return false;
    }
    yield();
  }

  Serial.println(F("Waiting the document"));

  delay(1000);
  while (client_https.available())
  {
    // line = client_https.readStringUntil('\n'); //  \r tulee vain dokkarin lopussa (tai bufferin saumassa?)

    line = read_http11_line(&client_https);
    Serial.println(line);

    line.trim();
    line.replace("\"", "");
    sep1 = line.indexOf(";");
    sep2 = line.lastIndexOf(";");
    // Serial.printf("%s  (%d,%d)\n ",line.c_str(),sep1,sep2);
    if (sep1 > -1 && sep2 > -1 && sep2 > sep1)
    {
      ts_string = line.substring(0, sep1);
      ts_string.trim(); // remove?

      val_string = line.substring(sep2 + 1);
      val_string.trim(); // remove?
      val_string.replace(",", ".");

      ts = ts_string.toInt();
      if (ts > ACCEPTED_TIMESTAMP_MINIMUM)
      {
        price = val_string.toFloat();
        // Serial.printf("-> |%s],  |%s| -> ",  ts_string.c_str(), val_string.c_str());
        Serial.printf("%lu,  %f\n", ts, price);
        price_idx = price_rows % MAX_PRICE_PERIODS;
        prices_local[price_idx] = (long)(price * 100 + 0.5);
        price_rows++;
        ts_min = min(ts_min, ts);
        ts_max = max(ts_max, ts);
      }
    }
  }
  client_https.stop();
  yield();

  Serial.printf("price_rows %d, price_idx %d\n", price_rows, price_idx);

  if (price_rows >= MAX_PRICE_PERIODS)
  {
    time_t ts_min_stored = ts_max - (MAX_PRICE_PERIODS - 1) * PRICE_RESOLUTION_SEC;
    int price_idx2 = ((price_idx + 1) % MAX_PRICE_PERIODS);
    prices2.set_store_start(ts_min_stored);
    for (int i = 0; i < MAX_PRICE_PERIODS; i++)
    { // cyclic use of prices_local array,
      ts = ts_min_stored + i * PRICE_RESOLUTION_SEC;
      prices2.set(ts, prices_local[price_idx2]);
      price_idx2++;
      price_idx2 = price_idx2 % MAX_PRICE_PERIODS;
    }

    // prices_expires_ts = prices_record_end_excl - (11 * SECONDS_IN_HOUR); // prices for next day should come after 12hUTC, so no need to query before that
    prices_expires_ts = ts_max - (10 * SECONDS_IN_HOUR); // prices for next day should come after 12hUTC, so no need to query before that
    Serial.printf("prices_expires_ts %lu\n", prices_expires_ts);
    Serial.println(F("Finished succesfully get_price_data_elering."));
    prices2.debug_print();

#ifdef NVS_CACHE_ENABLED
    prices2.save_to_cache(prices_expires_ts);
#endif

    // update to Influx if defined
    update_prices_to_influx();
    Serial.printf("get_price_data_elering end ok.\n");
    return true;
  }

  return false;
}
#endif

#ifdef OTA_DOWNLOAD_ENABLED

String update_releases = "{}"; // software releases for updates, cached in RAM
String update_release_selected = "";
time_t release_cache_expires_ts = 0;
/**
 * @brief Get firmware releases from download web server
 *
 * @return true
 * @return false
 */

bool get_releases()
{
  if (release_cache_expires_ts > time(nullptr))
  {
    Serial.println(F("Release cache still valid. No query."));
    return true;
  }

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
    release_cache_expires_ts = time(nullptr) + 2 * SECONDS_IN_HOUR;
  }

  client_https.stop();

  return true;
}
/**
 * @brief // Callback called after succesful flash(program) update
 *
 */
void flash_update_ended()
{
  Serial.println("flash_update_ended:HTTP update process finished");
  // set phase to enable fs version check after next boot
  s.ota_update_phase = OTA_PHASE_FWUPDATED_CHECKFS;
  writeToEEPROM();
  delay(1000);
  todo_in_loop_restart = true;
}

// declare
bool create_shadow_settings();

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
  // create shadow settings file
  create_shadow_settings();

  WiFiClientSecure client_https;
  Serial.println("update_program");
  client_https.setCACert(letsencrypt_ca_certificate);
  client_https.setTimeout(15); // timeout for SSL fetch
  String file_to_download = "/arska-install/files/" + String(HWID) + "/" + update_release_selected + "/firmware.bin";
  Serial.println(file_to_download);

  // TODO: maybe not call from web fetch update
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
  Serial.printf(PSTR("Updating filesystem to version %s\n"), VERSION_BASE); // oli VERSION_BASE
  if (String(VERSION_BASE).equals(version_fs_base))
  {
    Serial.println(F("No need for filesystem update."));
    return HTTP_UPDATE_NO_UPDATES;
  }
  WiFiClient wifi_client;
  FILESYSTEM.end();

  // TODO: update to new version without String when you can test it
  String file_to_download = "http://" + String(host_releases) + "/arska-install/files/" + String(HWID) + "/" + String(VERSION_BASE) + "/" + fs_filename;
  Serial.println(file_to_download);

  httpUpdate.onEnd(fs_update_ended); // change update phase after succesful update but before restart
  t_httpUpdate_return update_ok = httpUpdate.updateSpiffs(wifi_client, file_to_download.c_str(), "");
  if (update_ok == HTTP_UPDATE_FAILED)
  {
    Serial.println(F("Filesystem update failed!"));
    return update_ok;
  }
  if (update_ok == HTTP_UPDATE_OK)
  {
    Serial.println(F("Restarting after filesystem update."));
    log_msg(MSG_TYPE_FATAL, PSTR("Restarting after filesystem update."), false);

    ESP.restart(); // Restart to recreate cache files etc
  }
  return update_ok;
}
/**
 * @brief Update flash(program) or filesystem
 *
 * @param cmd
 */
void update_firmware_partition(uint8_t partition_type = U_FLASH)
{
  Serial.printf(PSTR("update_firmware_partition cmd %d, ota_update_phase %d"), (int)partition_type, s.ota_update_phase);
  t_httpUpdate_return update_result;
  if (partition_type == U_FLASH)
  {
    update_result = update_program();
  }
  else
  {
    if (s.ota_update_phase == OTA_PHASE_FWUPDATED_CHECKFS || true) // phase check disabled, maybe not always updated
    {
      update_result = update_fs();
    }
    else
    {
      Serial.printf(PSTR("Filesystem update requested but phase %d does not match.\n"), (int)s.ota_update_phase);
      Serial.println(s.ota_update_phase);
      return; // not correct update phase, should not normally end up here
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

// only manual update, automatic is integrated
const char update_page_html[] PROGMEM = "<html><head> <!-- Copyright Netgalleria Oy 2023, Olli Rinne, Unminimized version: /data/update.html --> <title>Arska update</title> <script src='https://cdnjs.cloudflare.com/ajax/libs/jquery/3.6.0/jquery.min.js'></script> <style> body { background-color: #fff; margin: 1.8em; font-size: 20px; font-family: lato, sans-serif; color: #485156; } .indent { margin-left: 2em; clear: left; } a { cursor: pointer; border-bottom: 3px dotted #485156; color: black; text-decoration: none; } </style></head><body> <script> window.addEventListener('load', (event) => { init_document(); }); let hw = ''; let load_count = 0; let VERSION_SHORT = ''; function init_document() { if (window.jQuery) { /* document.getElementById('frm2').addEventListener('submit', (event) => { return confirm('Update software, this can take several minutes.'); });*/ $.ajax({ url: '/application', dataType: 'json', async: false, success: function (data, textStatus, jqXHR) { VERSION_SHORT = data.VERSION_SHORT; $('#ver_sw').text(data.VERSION); $('#ver_fs').text(data.version_fs); }, error: function (jqXHR, textStatus, errorThrown) { console.log('Cannot get /application', textStatus, jqXHR.status); } }); } else { console.log('Cannot load jQuery library'); } } function _(el) { return document.getElementById(el); } function upload() { var file = _('firmware').files[0]; var formdata = new FormData(); formdata.append('firmware', file); var ajax = new XMLHttpRequest(); ajax.upload.addEventListener('progress', progressHandler, false); ajax.addEventListener('load', completeHandler, false); ajax.addEventListener('error', errorHandler, false); ajax.addEventListener('abort', abortHandler, false); ajax.open('POST', 'doUpdate'); ajax.send(formdata); } function progressHandler(event) { _('loadedtotal').innerHTML = 'Uploaded ' + event.loaded + ' bytes of ' + event.total; var percent = (event.loaded / event.total) * 100; _('progressBar').value = Math.round(percent); _('status').innerHTML = Math.round(percent) + '&percnt; uploaded... please wait'; } function reloadAdmin() { window.location.href = '/update'; } function completeHandler(event) { _('status').innerHTML = event.target.responseText; _('progressBar').value = 0; setTimeout(reloadAdmin, 20000); } function errorHandler(event) { _('status').innerHTML = 'Upload Failed'; } function abortHandler(event) { _('status').innerHTML = 'Upload Aborted'; } </script> <h1>Arska firmware and filesystem update</h1> <div class='indent'> <p><a href='/settings?format=file'>Backup configuration</a> before starting upgrade.</p><br> </div> <div id='div_upd1'> <h3>Upload firmware files</h3> <div class='indent'> <p>Download files from <a href='https://iot.netgalleria.fi/arska-install/'>the installation page</a> or build from <a href='https://github.com/Netgalleria/arska-node'>the source code</a>. Update software (firmware.bin) first and filesystem (littlefs.bin) after that. After update check version data from the bottom of the page - update could be succeeded even if you get an error message. </p> <form id='frm1' method='post' enctype='multipart/form-data'> <input type='file' name='firmware' id='firmware' onchange='upload()'><br> <progress id='progressBar' value='0' max='100' style='width:250px;'></progress> <h2 id='status'></h2> <p id='loadedtotal'></p> </form> </div> </div> Current versions:<br> <table><tr><td>Firmware:</td><td><span id='ver_sw'>*</span></td></tr><tr><td>Filesystem:</td><td><span id='ver_fs'>*</span></td></tr></table> <br><a href='/'>Return to Arska</a></body></html>";

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
void handleFirmwareUpdate(AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final)
{
  io_tasks(STATE_UPLOADING);
  size_t content_len;
  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();

  if (!index) // first
  {
    Serial.println("Update");

    content_len = request->contentLength();
    int cmd = (filename.indexOf("firmware") > -1) ? U_FLASH : U_PART;
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
    response->addHeader("REFRESH", "15;URL=/");
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
      create_shadow_settings();
      delay(2000);
      ESP.restart();
    }
  }
}

#endif

/**
 * @brief Reset config variables to defaults
 *
 *
 */
#define DEFAULT_COLOR_COUNT 9
void reset_config()
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

  bool first_reset = (memcmp(s.http_username, "admin", 5) != 0); // if "admin" in the memory
  if (first_reset)
  {
    Serial.println(F("Initializing the eeprom first time."));
  }
  else
  {
    strncpy(current_password, s.http_password, sizeof(current_password));
  }

  uint8_t ota_update_phase = s.ota_update_phase; // keep the phase over config reset
  memset(&s, 0, sizeof(s));
  s.ota_update_phase = ota_update_phase;

  // memset(&s_influx, 0, sizeof(s_influx));
  s.check_value = EEPROM_CHECK_VALUE;

  strncpy(s.http_username, "admin", sizeof(s.http_username)); // admin id is fixed

  if (first_reset)
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

  strncpy(s.custom_ntp_server, "", sizeof(s.custom_ntp_server));

  s.baseload = 0;
  s.pv_power = 5000;
  s.ota_update_phase = OTA_PHASE_NONE;
  s.energy_meter_type = ENERGYM_NONE;
  s.energy_meter_port = 80;
  s.netting_period_sec = 3600;
  s.production_meter_type = PRODUCTIONM_NONE;
  s.production_meter_port = 80;
  s.production_meter_id = 3;
  s.energy_meter_pollingfreq = 60;
#ifdef METER_HAN_DIRECT_ENABLED
  s.energy_meter_gpio = 255; //!< energy meter gpio , ENERGYM_HAN_DIRECT
#endif

#ifdef LOAD_MGMT_ENABLED
  s.load_manager_active = false;
  s.load_manager_phase_count = 3;
  s.load_manager_current_max = 25;
  s.load_manager_reswitch_moratorium_m = 5;
#endif
  strcpy(s.forecast_loc, "#");

  strcpy(s.lang, "EN");
  strcpy(s.timezone, "EET");

  s.hw_template_id = 0; // undefined my default
  hw_template_idx = -1;

  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {
    // if (gpios_defined)
    //   s.ch[channel_idx].relay_id = channel_gpios[channel_idx]; // TODO: check first type, other types available
    // else
    s.ch[channel_idx].relay_id = 255;

    s.ch[channel_idx].type = (s.ch[channel_idx].relay_id < 255) ? CH_TYPE_GPIO_USER_DEF : CH_TYPE_UNDEFINED;

    s.ch[channel_idx].force_up_from_ts = 0;
    s.ch[channel_idx].force_up_until_ts = 0;
    s.ch[channel_idx].up_last_ts = 0;
    s.ch[channel_idx].config_mode = CHANNEL_CONFIG_MODE_RULE;
    s.ch[channel_idx].template_id = -1;

    s.ch[channel_idx].channel_color = default_colors[channel_idx % DEFAULT_COLOR_COUNT];

    s.ch[channel_idx].uptime_minimum = 60;
    s.ch[channel_idx].priority = channel_idx;
    s.ch[channel_idx].load = 0;

    snprintf(s.ch[channel_idx].id_str, sizeof(s.ch[channel_idx].id_str), "channel %d", channel_idx + 1);

    for (int rule_idx = 0; rule_idx < CHANNEL_RULES_MAX; rule_idx++)
    {
      s.ch[channel_idx].rules[rule_idx].on = false;
      for (int stmt_idx = 0; stmt_idx < RULE_STATEMENTS_MAX; stmt_idx++)
      {
        s.ch[channel_idx].rules[rule_idx].statements[stmt_idx].variable_id = -1;
        s.ch[channel_idx].rules[rule_idx].statements[stmt_idx].oper_id = 255;
        s.ch[channel_idx].rules[rule_idx].statements[stmt_idx].const_val = 0;
      }
    }
  }
  Serial.println(F("Finishing reset_config"));
}

/**
 * @brief Copies string value from a json node to a char buffer.
 *
 * @param parent_node
 * @param doc_key
 * @param tostr
 * @param buffer_length
 * @return true
 * @return false
 */
bool ajson_str_to_mem(JsonVariant parent_node, char *doc_key, char *tostr, size_t buffer_length)
{
  JsonVariant element = parent_node[doc_key];
  if (!element.isNull())
  {
    Serial.println(element.as<const char *>());
    strncpy(tostr, element.as<const char *>(), buffer_length - 1); // leave one char for a null-character
    return true;
  }
  // Serial.printf("Element %s isNull.\n", doc_key);
  return false;
}
// new settings doc creation
void create_settings_doc(DynamicJsonDocument &doc, bool include_password)
{
  int active_rule_idx;
  char char_buffer[20];

  char export_time[20];
  time_t current_time_ts = time(nullptr);
  localtime_r(&current_time_ts, &tm_struct);
  snprintf(export_time, 20, "%04d-%02d-%02dT%02d:%02d:%02d", tm_struct.tm_year + 1900, tm_struct.tm_mon + 1, tm_struct.tm_mday, tm_struct.tm_hour, tm_struct.tm_min, tm_struct.tm_sec);

  doc["export_time"] = export_time;

  doc["check_value"] = s.check_value;

  if (include_password)
  { // no wifi parameters
    doc["wifi_ssid"] = s.wifi_ssid;
    doc["wifi_password"] = s.wifi_password;
  }
  doc["http_username"] = s.http_username;

  // current status, do not import
  doc["wifi_in_standalone_mode"] = !wifi_sta_connected;
  doc["using_default_password"] = String(s.http_password).equals(default_http_password);
  //  (strcmp(s.http_password, default_http_password) == 0) ? true : false;

  doc["entsoe_api_key"] = s.entsoe_api_key;
  doc["entsoe_area_code"] = s.entsoe_area_code;
  //  if (s.variable_mode == VARIABLE_MODE_REPLICA)
  //   doc["variable_server"] = s.variable_server;
  doc["custom_ntp_server"] = s.custom_ntp_server;
  doc["timezone"] = s.timezone;
  doc["baseload"] = s.baseload;
  doc["pv_power"] = s.pv_power;
  doc["energy_meter_type"] = s.energy_meter_type;
  doc["netting_period_sec"] = s.netting_period_sec;

  if (s.energy_meter_type != ENERGYM_NONE)
  {
    doc["energy_meter_ip"] = s.energy_meter_ip.toString();
    doc["energy_meter_password"] = s.energy_meter_password;
    doc["energy_meter_port"] = s.energy_meter_port;

    doc["energy_meter_pollingfreq"] = s.energy_meter_pollingfreq;
#ifdef METER_HAN_DIRECT_ENABLED
    doc["energy_meter_gpio"] = s.energy_meter_gpio;
#endif
  }

#ifdef HW_EXTENSIONS_ENABLED
  doc["output_register"] = (hw_template_idx > 0 && hw_templates[hw_template_idx].hw_io.output_register);
#endif

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

#ifdef LOAD_MGMT_ENABLED
  doc["load_manager_active"] = s.load_manager_active;
  doc["load_manager_phase_count"] = s.load_manager_phase_count;
  doc["load_manager_current_max"] = s.load_manager_current_max;
  doc["load_manager_reswitch_moratorium_m"] = s.load_manager_reswitch_moratorium_m;
#endif
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

    doc["ch"][channel_idx]["locked"] = (hw_template_idx != -1 && hw_templates[hw_template_idx].locked_channels >= (channel_idx + 1));

    doc["ch"][channel_idx]["id_str"] = s.ch[channel_idx].id_str;
    doc["ch"][channel_idx]["type"] = s.ch[channel_idx].type;
    doc["ch"][channel_idx]["config_mode"] = s.ch[channel_idx].config_mode;
    doc["ch"][channel_idx]["template_id"] = s.ch[channel_idx].template_id;
    doc["ch"][channel_idx]["uptime_minimum"] = int(s.ch[channel_idx].uptime_minimum);
    doc["ch"][channel_idx]["load"] = int(s.ch[channel_idx].load);
    snprintf(char_buffer, 8, "#%06x", s.ch[channel_idx].channel_color);
    doc["ch"][channel_idx]["channel_color"] = char_buffer;
    doc["ch"][channel_idx]["priority"] = s.ch[channel_idx].priority;

    doc["ch"][channel_idx]["up_last"] = s.ch[channel_idx].up_last_ts;
    // doc["ch"][channel_idx]["force_up"] = is_force_up_valid(channel_idx);
    doc["ch"][channel_idx]["force_up_from"] = s.ch[channel_idx].force_up_from_ts;
    doc["ch"][channel_idx]["force_up_until"] = s.ch[channel_idx].force_up_until_ts;
    doc["ch"][channel_idx]["is_up"] = s.ch[channel_idx].is_up;
    doc["ch"][channel_idx]["wanna_be_up"] = s.ch[channel_idx].wanna_be_up;
    doc["ch"][channel_idx]["r_id"] = s.ch[channel_idx].relay_id;
    doc["ch"][channel_idx]["r_ip"] = s.ch[channel_idx].relay_ip.toString();
    doc["ch"][channel_idx]["r_uid"] = s.ch[channel_idx].relay_unit_id;
    doc["ch"][channel_idx]["default_state"] = s.ch[channel_idx].default_state;

    // rules[rule_idx].rule_active
    active_rule_idx = -1;
    rule_idx_output = 0;
    int active_rule_count = 0;
    for (int rule_idx = 0; rule_idx < CHANNEL_RULES_MAX; rule_idx++)
    {
      if (s.ch[channel_idx].rules[rule_idx].rule_active)
        active_rule_idx = rule_idx_output;

      int stmt_count = 0;
      for (int stmt_idx = 0; stmt_idx < RULE_STATEMENTS_MAX; stmt_idx++)
      {
        statement_st *stmt = &s.ch[channel_idx].rules[rule_idx].statements[stmt_idx];
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
        doc["ch"][channel_idx]["rules"][rule_idx_output]["on"] = s.ch[channel_idx].rules[rule_idx].on;
        rule_idx_output++;
        active_rule_count++;
      }
    }
    // Serial.printf("Channel: %d,active_rule_count %d \n", channel_idx, active_rule_count);
    if (active_rule_count > 0)
    {
      doc["ch"][channel_idx]["active_rule_idx"] = active_rule_idx;
    }
  }
}

/**
 * @brief Export current configuration in json to web response
 *
 * @param request
 */

void onWebSettingsGet(AsyncWebServerRequest *request)
{
  uint8_t format = 0;
  String output;

  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();

  if (request->hasParam("format"))
  {
    if (request->getParam("format")->value() == "file")
      format = 1;
  }
  DynamicJsonDocument doc(CONFIG_JSON_SIZE_MAX);

  create_settings_doc(doc, (format == 0));

  serializeJson(doc, output);
  if (format == 0)
  {
    request->send(200, "application/json;charset=UTF-8", output);
  }
  else
  {
    char Content_Disposition[70];
    char export_time[20];

    ajson_str_to_mem(doc, (char *)"export_time", export_time, sizeof(export_time));

    snprintf(Content_Disposition, 70, "attachment; filename=\"arska-config-%s.json\"", export_time);
    // AsyncWebServerResponse *response = request->beginResponse(200, "application/json", output); //text
    AsyncWebServerResponse *response = request->beginResponse(200, "application/octet-stream", output); // file
    response->addHeader("Content-Disposition", Content_Disposition);
    request->send(response);
  }
}

/**
 * @brief Gets integer value from a json node.
 *
 * @param parent_node
 * @param doc_key
 * @param default_val
 * @return int32_t
 */
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

/**
 * @brief Gets an ip address from a json node.
 *
 * @param parent_node
 * @param doc_key
 * @param default_val
 * @return IPAddress
 */
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

/**
 * @brief Gets a boolean value from a json node.
 *
 * @param parent_node
 * @param doc_key
 * @param default_val
 * @return true
 * @return false
 */
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

/**
 * @brief Stores settings from a json document (from UI or restore upload)
 *
 * @param doc
 * @return true
 * @return false
 */
bool store_settings_from_json_doc_dyn(DynamicJsonDocument doc)
{
  char http_password[MAX_ID_STR_LENGTH];
  char http_password2[MAX_ID_STR_LENGTH];
  int channel_idx_loop = 0;

  ajson_str_to_mem(doc, (char *)"wifi_ssid", s.wifi_ssid, sizeof(s.wifi_ssid));
  ajson_str_to_mem(doc, (char *)"wifi_password", s.wifi_password, sizeof(s.wifi_password));
  ajson_str_to_mem(doc, (char *)"entsoe_api_key", s.entsoe_api_key, sizeof(s.entsoe_api_key));
  ajson_str_to_mem(doc, (char *)"entsoe_area_code", s.entsoe_area_code, sizeof(s.entsoe_area_code));

  ajson_str_to_mem(doc, (char *)"custom_ntp_server", s.custom_ntp_server, sizeof(s.custom_ntp_server));
  ajson_str_to_mem(doc, (char *)"timezone", s.timezone, sizeof(s.timezone));
  ajson_str_to_mem(doc, (char *)"lang", s.lang, sizeof(s.lang));

  ajson_str_to_mem(doc, (char *)"forecast_loc", s.forecast_loc, sizeof(s.forecast_loc));
  s.baseload = ajson_int_get(doc, (char *)"baseload", s.baseload);
  s.pv_power = ajson_int_get(doc, (char *)"pv_power", s.pv_power);

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
  hw_template_idx = get_hw_template_idx(s.hw_template_id); // update cached variable
  s.netting_period_sec = ajson_int_get(doc, (char *)"netting_period_sec", s.netting_period_sec);

  s.energy_meter_type = (uint8_t)ajson_int_get(doc, (char *)"energy_meter_type", s.energy_meter_type);
  Serial.printf("s.energy_meter_type %d\n", (int)s.energy_meter_type);
  s.energy_meter_gpio = ajson_int_get(doc, (char *)"energy_meter_gpio", s.energy_meter_gpio);

  s.energy_meter_ip = ajson_ip_get(doc, (char *)"energy_meter_ip", s.energy_meter_ip);
  ajson_str_to_mem(doc, (char *)"energy_meter_password", s.energy_meter_password, sizeof(s.energy_meter_password));
  s.energy_meter_port = ajson_int_get(doc, (char *)"energy_meter_port", s.energy_meter_port);
  s.energy_meter_pollingfreq = ajson_int_get(doc, (char *)"energy_meter_pollingfreq", s.energy_meter_pollingfreq);

  s.production_meter_type = ajson_int_get(doc, (char *)"production_meter_type", s.production_meter_type);
  s.production_meter_ip = ajson_ip_get(doc, (char *)"production_meter_ip", s.production_meter_ip);
  s.production_meter_port = ajson_int_get(doc, (char *)"production_meter_port", s.production_meter_port);
  s.production_meter_id = ajson_int_get(doc, (char *)"production_meter_id", s.production_meter_id);

#ifdef LOAD_MGMT_ENABLED
  s.load_manager_active = ajson_bool_get(doc, (char *)"load_manager_active", s.load_manager_active);
  s.load_manager_phase_count = ajson_int_get(doc, (char *)"load_manager_phase_count", s.load_manager_phase_count);
  s.load_manager_current_max = ajson_int_get(doc, (char *)"load_manager_current_max", s.load_manager_current_max);
  s.load_manager_reswitch_moratorium_m = ajson_int_get(doc, (char *)"load_manager_reswitch_moratorium_m", s.load_manager_reswitch_moratorium_m);
#endif

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

  if (!doc["hw_template_id"].isNull())
  {
    Serial.printf("hw_template_idx %d, s.hw_template_id %d\n", hw_template_idx, s.hw_template_id);
    if (hw_template_idx != -1)
    {
      s.energy_meter_gpio = hw_templates[hw_template_idx].energy_meter_gpio;
      // copy template id:s (gpio)
      for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
      {
        if (channel_idx < HW_TEMPLATE_GPIO_COUNT)
        { // touch only channel which could have gpio definitions
          if (hw_templates[hw_template_idx].relay_id[channel_idx] < ID_NA)
          {
            s.ch[channel_idx].type = CH_TYPE_GPIO_USER_DEF; // was CH_TYPE_GPIO_FIXED;
            s.ch[channel_idx].relay_id = hw_templates[hw_template_idx].relay_id[channel_idx];
            Serial.printf("New value for s.ch[channel_idx].relay_id  %d\n", (int)hw_templates[hw_template_idx].relay_id[channel_idx]);
          }
          else if (s.ch[channel_idx].type == CH_TYPE_GPIO_FIXED) // deprecate CH_TYPE_GPIO_FIXED
          {                                                      // fixed gpio -> user defined, new way
            s.ch[channel_idx].type = CH_TYPE_UNDEFINED;          // CH_TYPE_GPIO_USER_DEF;
          }
        }
      }
    }
    else
    {
      Serial.printf("Cannot find hw_template with id %d\n", s.hw_template_id);
    }
  }

  for (JsonObject ch : doc["ch"].as<JsonArray>())
  {
    channel_idx = ajson_int_get(ch, (char *)"idx", channel_idx_loop);
    ajson_str_to_mem(ch, (char *)"id_str", s.ch[channel_idx].id_str, sizeof(s.ch[channel_idx].id_str));
    Serial.printf("s.ch[channel_idx].id_str %s\n", s.ch[channel_idx].id_str);

    s.ch[channel_idx].type = ajson_int_get(ch, (char *)"type", s.ch[channel_idx].type);
    s.ch[channel_idx].config_mode = ajson_int_get(ch, (char *)"config_mode", s.ch[channel_idx].config_mode);
    s.ch[channel_idx].template_id = ajson_int_get(ch, (char *)"template_id", s.ch[channel_idx].template_id);
    s.ch[channel_idx].uptime_minimum = ajson_int_get(ch, (char *)"uptime_minimum", s.ch[channel_idx].uptime_minimum);
    s.ch[channel_idx].load = ajson_int_get(ch, (char *)"load", s.ch[channel_idx].load);

    if (ch.containsKey("channel_color"))
    {
      ajson_str_to_mem(ch, (char *)"channel_color", hex_buffer, sizeof(hex_buffer));
      s.ch[channel_idx].channel_color = (uint32_t)strtol(hex_buffer + 1, NULL, 16);
    }

    s.ch[channel_idx].priority = ajson_int_get(ch, (char *)"priority", s.ch[channel_idx].priority);
    s.ch[channel_idx].relay_id = ajson_int_get(ch, (char *)"r_id", s.ch[channel_idx].relay_id);
    s.ch[channel_idx].relay_ip = ajson_ip_get(ch, (char *)"r_ip", s.ch[channel_idx].relay_ip);
    s.ch[channel_idx].relay_unit_id = ajson_int_get(ch, (char *)"r_uid", s.ch[channel_idx].relay_unit_id);

    s.ch[channel_idx].default_state = ajson_bool_get(ch, (char *)"default_state", s.ch[channel_idx].default_state);

    // clear  statements
    // TODO: add to new version
    for (rule_idx = 0; rule_idx < CHANNEL_RULES_MAX; rule_idx++)
    {
      for (stmt_idx = 0; stmt_idx < RULE_STATEMENTS_MAX; stmt_idx++)
      {
        s.ch[channel_idx].rules[rule_idx].statements[stmt_idx].variable_id = -1;
        s.ch[channel_idx].rules[rule_idx].statements[stmt_idx].oper_id = -1;
        s.ch[channel_idx].rules[rule_idx].statements[stmt_idx].const_val = VARIABLE_LONG_UNKNOWN;
      }
    }

    // channel rulesq
    rule_idx = 0;

    // channel rules
    for (JsonObject ch_rule : ch["rules"].as<JsonArray>())
    {
      s.ch[channel_idx].rules[rule_idx].on = ch_rule["on"];
      stmt_idx = 0;
      Serial.printf("rule on %s", s.ch[channel_idx].rules[rule_idx].on ? "true" : "false");

      for (JsonArray ch_rule_stmt : ch_rule["stmts"].as<JsonArray>())
      {
        s.ch[channel_idx].rules[rule_idx].statements[stmt_idx].variable_id = ch_rule_stmt[0];
        s.ch[channel_idx].rules[rule_idx].statements[stmt_idx].oper_id = ch_rule_stmt[1];
        s.ch[channel_idx].rules[rule_idx].statements[stmt_idx].const_val = ch_rule_stmt[2]; // TODO: redundant?, remove if stms[3] is always upddated?

        // no float conversion & rounding for multiselect bitmask
        if (s.ch[channel_idx].rules[rule_idx].statements[stmt_idx].oper_id == 10)
        {
          s.ch[channel_idx].rules[rule_idx].statements[stmt_idx].const_val = ch_rule_stmt[3];
        }
        else
          s.ch[channel_idx].rules[rule_idx].statements[stmt_idx].const_val = vars.float_to_internal_l(ch_rule_stmt[0], ch_rule_stmt[3]);

        Serial.printf("rules/stmts: [%d, %d, %ld]", s.ch[channel_idx].rules[rule_idx].statements[stmt_idx].variable_id, (int)s.ch[channel_idx].rules[rule_idx].statements[stmt_idx].oper_id, s.ch[channel_idx].rules[rule_idx].statements[stmt_idx].const_val);
        stmt_idx++;
      }
      rule_idx++;
    }
    // just in case if the are changes in relay config
    relay_state_reapply_required[channel_idx] = true;
    todo_in_loop_reapply_relay_states = true;

    channel_idx++;
    channel_idx_loop++;
  }
  writeToEEPROM();
  return true;
}

// Write settings to a local file. Can be read after upgrade.
bool create_shadow_settings()
{
  Serial.printf(PSTR("create_shadow_settings %s\n"), shadow_settings_filename);
  DynamicJsonDocument doc(CONFIG_JSON_SIZE_MAX);
  File settings_file = FILESYSTEM.open(shadow_settings_filename, "w"); // Open file for writing
  create_settings_doc(doc, true);
  serializeJson(doc, settings_file);
  settings_file.close();
  delay(2000);
  return true;
}

bool read_shadow_settings()
{
  Serial.println("read_shadow_settings ");
  // StaticJsonDocument<CONFIG_JSON_SIZE_MAX> doc; //
  DynamicJsonDocument doc(CONFIG_JSON_SIZE_MAX);

  if (!FILESYSTEM.exists(shadow_settings_filename))
  {
    Serial.println("Shadow settings file does not exist.");
    return false;
  }
  File config_file = FILESYSTEM.open(shadow_settings_filename, "r");
  DeserializationError error = deserializeJson(doc, config_file);
  config_file.close();
  if (error)
  {
    Serial.println("Cannot get settings from shadow file");
    Serial.println(error.f_str());
    return false;
  }
  else
  {
    store_settings_from_json_doc_dyn(doc);
    Serial.println("Got settings from shadow file - dynamic");
    return true;
  }
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

void onWebUploadConfigPost(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{

  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();

  bool final = ((len + index) == total);

  if (!index) // first
  {
    //  open the file on first call and store the file handle in the request object
    request->_tempFile = FILESYSTEM.open(filename_config_in, "w");
  }

  if (len) // contains data
  {
    // stream the incoming chunk to the opened file
    request->_tempFile.write(data, len);
  }

  if (final) // last call
  {
    Serial.println("onWebUploadConfigPost final");
    // close the file handle as the upload is now done
    request->_tempFile.close();

    File config_file = FILESYSTEM.open(filename_config_in, "r");
    DynamicJsonDocument doc(CONFIG_JSON_SIZE_MAX);
    DeserializationError error = deserializeJson(doc, config_file);
    config_file.close();
    if (error)
    {
      Serial.println(error.f_str());
      request->send(500, "application/json", "{\"status\":\"failed\"}");
    }
    else
    {
      reset_config();
      store_settings_from_json_doc_dyn(doc);
      todo_in_loop_restart = true; // restart
      request->send(200, "application/json", "{\"status\":\"ok\", \"refresh\" : 30}");
    }
  }
}

/**
 * @brief Process dashboard form, forcing channels up, JSON update, work in progress
 *
 * @param request
 */

void onScheduleUpdatePost(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  int channel_idx;
  bool force_up_changes = false;
  bool channel_already_forced;
  long force_up_minutes;
  time_t force_up_from_ts = 0;
  time_t force_up_until_ts;

  StaticJsonDocument<2048> doc; //
  bool final = ((len + index) == total);

  if (!index) // first chunk, initiate
  {
    memset(in_buffer, 0, sizeof(in_buffer));
  }
  if (len && index + len < sizeof(in_buffer)) // contains data, add it to the buffer
    strncat(in_buffer, (const char *)data, len);

  if (final) // last chunk, process
  {
    DeserializationError error = deserializeJson(doc, (const char *)in_buffer);
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
        force_up_from_ts = time(nullptr);
      else
        force_up_from_ts = max(time(nullptr), from); // absolute unix ts is waited

      Serial.printf("onScheduleUpdatePost channel_idx: %d, force_up_minutes: %ld , force_up_from_ts %ld  \n", channel_idx, force_up_minutes, force_up_from_ts);

      if (force_up_minutes > 0)
      {
        force_up_until_ts = force_up_from_ts + force_up_minutes * 60; //-1;
        s.ch[channel_idx].force_up_from_ts = force_up_from_ts;
        s.ch[channel_idx].force_up_until_ts = force_up_until_ts;
        if (is_force_up_valid(channel_idx))
        {
          s.ch[channel_idx].wanna_be_up = true;
          chstate_transit[channel_idx] = CH_STATE_BYFORCE;
        }
      }
      else
      {
        s.ch[channel_idx].force_up_from_ts = -1;  // forced down
        s.ch[channel_idx].force_up_until_ts = -1; // forced down
        s.ch[channel_idx].wanna_be_up = false;
        chstate_transit[channel_idx] = CH_STATE_BYFORCE;
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
}

// experimental actions, uses "AsyncJson.h",
//  see https://github.com/me-no-dev/ESPAsyncWebServer#json-body-handling-with-arduinojson,
//  https://arduino.stackexchange.com/questions/89526/use-of-espasyncwebserver-h-with-arduinojson-version-6-for-master-client-transact
/**
 * @brief "/actions" url handler for asyncronous admin actions
 *
 */
AsyncCallbackJsonWebHandler *ActionsPostHandler = new AsyncCallbackJsonWebHandler("/actions", [](AsyncWebServerRequest *request, JsonVariant json)
                                                                                  {
  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();

  JsonObject doc = json.as<JsonObject>();
  String action = doc["action"];
  Serial.println(action);

  bool todo_in_loop_restart_local = false; // set global variable in the end when data is set

  if (action == "update")
  {
    update_release_selected = String((const char *)doc["version"]);
    todo_in_loop_update_firmware_partition = true;
    Serial.println(update_release_selected);
    request->send(200, "application/json", "{\"status\":\"ok\", \"refresh\" : 240}");
  }

  if (doc["action"] == "restart")
  {
    todo_in_loop_restart_local = true;
    // expire caches
    prices2.clear_store();
    //prices2.save_to_cache(0); 
   // solar_forecast.save_to_cache(0);
   // wind_forecast.save_to_cache(0);
    
  }
  if (doc["action"] == "scan_sensors")
  {
    todo_in_loop_scan_sensors = true;
    request->send(200, "application/json", "{\"status\":\"ok\", \"refresh\" : 0}");
    return;
  }

  if (doc["action"] == "reset")
  {
    reset_config();
    todo_in_loop_restart_local = true;
    writeToEEPROM();
  }
  if (doc["action"] == "scan_wifis")
  {
    todo_in_loop_scan_wifis = true;
  }

  todo_in_loop_restart = todo_in_loop_restart_local;

  if (todo_in_loop_restart)
  {
    request->send(200, "application/json", "{\"status\":\"ok\", \"refresh\" : 30}");
  }
  else
    request->send(200, "application/json", "{\"status\":\"ok\", \"refresh\" : 0}"); });

/**
 * @brief Handles chunks from post to /settings, called by UI Settings
 *
 * @param request pointer to the request object
 * @param data data of this chunk
 * @param len data length of this chunk
 * @param index Index of the chunk
 * @param total Total size
 */
void onWebSettingsPost(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication();

  bool final = ((len + index) == total);

  if (!index) // first chunk, initiate
  {
    memset(in_buffer, 0, sizeof(in_buffer));
  }
  if (len && index + len < sizeof(in_buffer)) // contains data, add it to the buffer
    strncat(in_buffer, (const char *)data, len);

  if (final) // last chunk, process
  {
    StaticJsonDocument<256> out_doc; // global doc to discoveries
    String output;

    DynamicJsonDocument doc(CONFIG_JSON_SIZE_MAX);

    DeserializationError error = deserializeJson(doc, in_buffer);
    if (error)
    {
      Serial.print(F("onWebSettingsPost deserializeJson() failed: "));
      Serial.println(in_buffer);
      Serial.println(error.f_str());
      out_doc["rc"] = -1;
      out_doc["msg"] = "deserializeJson() failed: ";
      out_doc["msg2"] = error.f_str();
      serializeJson(out_doc, output);
      request->send(200, "application/json", output);
      return;
    }

    store_settings_from_json_doc_dyn(doc);

    out_doc["rc"] = 0;
    out_doc["msg"] = "ok";

    serializeJson(out_doc, output);
    request->send(200, "application/json", output);
    ;
  }
  else
    return;
}

/**
 * @brief "/prices" ur handler for getting price data in json format
 *
 * @param request
 */
void onWebPricesGet(AsyncWebServerRequest *request)
{
  StaticJsonDocument<1024> doc; //
  String output;

  JsonArray prices_a = doc.createNestedArray("prices");

  Serial.printf("DEBUG onWebPricesGet %lu - %lu (%d)\n", prices2.start(), prices2.end(), prices2.n());

  if (prices2.start() > 0)
  {
    for (int i = 0; i < MAX_PRICE_PERIODS; i++)
    {
      prices_a[i] = prices2.get(prices2.start() + prices2.resolution_sec() * i);
    }
  }

  // new time series, mieti miten nämä korvataan, myös UI
  doc["record_start"] = prices2.start();                         // prices_record_start;
  doc["record_end_excl"] = prices2.end() + PRICE_RESOLUTION_SEC; // prices_record_end_excl;
  doc["resolution_sec"] = PRICE_RESOLUTION_SEC;
  doc["ts"] = time(nullptr);
  doc["expires"] = prices_expires_ts;

  serializeJson(doc, output);
  request->send(200, "application/json", output);
}

void onWebSeriesGet(AsyncWebServerRequest *request)
{
  //  DynamicJsonDocument doc(CONFIG_JSON_SIZE_MAX);
  StaticJsonDocument<2048> doc;

  String output;
  if (request->hasParam("solar_fcst") && request->getParam("solar_fcst")->value() == "true" && s.pv_power > 0)
  {
    JsonObject series_obj = doc.createNestedObject("solar_forecast");
    JsonArray series_a = series_obj.createNestedArray("s");
    if (solar_forecast.start() != 0)
    { // initiated
      for (int i = 0; i < solar_forecast.n(); i++)
      {
        series_a[i] = solar_forecast.get(solar_forecast.start() + i * solar_forecast.resolution_sec()) * s.pv_power / 1000;
      }

      series_obj["start"] = solar_forecast.start();
      series_obj["first_set_period_ts"] = solar_forecast.first_set_period_ts();
      series_obj["last_set_period_ts"] = solar_forecast.last_set_period_ts();
      series_obj["resolution_sec"] = solar_forecast.resolution_sec();
      //   Serial.println("DEBUG");
      //   solar_forecast.debug_print();
    }
  }
  doc["ts"] = time(nullptr);
  serializeJson(doc, output);
  request->send(200, "application/json", output);
}

// loop wathcdog functionality to check that loop() function is called in timely fashion
#define LOOP_WATCHDOG_ENABLED
#ifdef LOOP_WATCHDOG_ENABLED
#define BOOT_AFTER_NO_LOOP_START 300000000 // microseconds 5 mins
static uint64_t last_loop_started = 0;     //

void check_loop_is_called()
{
  if (last_loop_started == 0) // not timestamp value yet
    return;

  if (esp_timer_get_time() - last_loop_started > BOOT_AFTER_NO_LOOP_START)
  {
    log_msg(MSG_TYPE_FATAL, PSTR("Loop watchdog launch system restart."), true);
    delay(2000);
    ESP.restart();
  }
  /* else
   {
     Serial.printf("DEBUG: check_loop_is_called ok\n");
   }
   */
}

void loop_watchdog(void *pvParameters)
{
  for (;;)
  {
    Serial.print("loop_watchdog() running at core ");
    Serial.println(xPortGetCoreID());
    check_loop_is_called();
    delay(100000);
  }
}
#endif

/**
 * @brief "/status" url handler, returns status in json
 *
 * @param request
 */
void onWebStatusGet(AsyncWebServerRequest *request)
{
#ifdef LOOP_WATCHDOG_ENABLED
//  check_loop_is_called(); // call watchdog also here, if task not working
#endif
  if (!request->authenticate(s.http_username, s.http_password))
  {
    return request->requestAuthentication();
  }
  DynamicJsonDocument doc(CONFIG_JSON_SIZE_MAX);
  String output;

  JsonObject var_obj = doc.createNestedObject("variables");

  char id_str[6];
  char buff_value[20];
  variable_st variable;
  for (int variable_idx = 0; variable_idx < vars.get_variable_count(); variable_idx++)
  {
    vars.get_variable_by_idx(variable_idx, &variable);
    if (VARIABLE_CHANNEL_UTIL_PERIOD <= variable.id && variable.id <= VARIABLE_CHANNEL_UTIL_BLOCK_M2_0) // channel variables still separate handling
      continue;

    // calculated variables, TODO: one variable get should be enough...
    if (VARIABLE_ESTIMATED_CHANNELS_CONSUMPTION == variable.id)
    {
      vars.get_variable_by_id(variable.id, &variable, -1);
    }

    //  vars.to_str(variable.id, buff_value, false, 0, sizeof(buff_value));
    vars.to_str(variable.id, buff_value, true, variable.val_l, sizeof(buff_value));
    snprintf(id_str, 6, "%d", variable.id);
    var_obj[id_str] = buff_value;
  }

  // TODO: voisi hakea get_variable_by_id() niin ei tarvitsisi monistaa laskentaa?
  // vars.get_variable_by_idx(variable_idx, &variable);
  char var_id_str[5];
  sprintf(var_id_str, "%d", (int)VARIABLE_CHANNEL_UTIL_PERIOD);

  JsonArray v_channel_array = var_obj.createNestedArray(var_id_str); //(;
  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {
    v_channel_array.add((long)(ch_counters.get_period_uptime(channel_idx) + 30) / 60);
  }
  sprintf(var_id_str, "%d", (int)VARIABLE_CHANNEL_UTIL_8H);
  v_channel_array = var_obj.createNestedArray(var_id_str); // 152 (VARIABLE_CHANNEL_UTIL_8H);
  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {
    v_channel_array.add(channel_history_cumulative_minutes(channel_idx, 8));
  }
  sprintf(var_id_str, "%d", (int)VARIABLE_CHANNEL_UTIL_24H);
  v_channel_array = var_obj.createNestedArray(var_id_str); //(VARIABLE_CHANNEL_UTIL_24H);
  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {
    v_channel_array.add(channel_history_cumulative_minutes(channel_idx, 24));
  }

  sprintf(var_id_str, "%d", (int)VARIABLE_CHANNEL_UTIL_BLOCK_M2_0);
  v_channel_array = var_obj.createNestedArray(var_id_str);
  int now_nth_period_in_hour = (current_period_start_ts - get_block_start_ts(current_period_start_ts)) / SECONDS_IN_HOUR;
  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  { // this and previous 2 blocks utilization
    v_channel_array.add(channel_history_cumulative_minutes(channel_idx, now_nth_period_in_hour + DAY_BLOCK_SIZE_HOURS * 2));
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
    doc["ch"][channel_idx]["wanna_be_up"] = s.ch[channel_idx].wanna_be_up;

    doc["ch"][channel_idx]["active_rule"] = get_channel_active_rule(channel_idx);
    // doc["ch"][channel_idx]["force_up"] = is_force_up_valid(channel_idx);
    doc["ch"][channel_idx]["force_up_from"] = s.ch[channel_idx].force_up_from_ts;
    doc["ch"][channel_idx]["force_up_until"] = s.ch[channel_idx].force_up_until_ts;
    doc["ch"][channel_idx]["up_last"] = s.ch[channel_idx].up_last_ts;

    doc["ch"][channel_idx]["transit"] = chstate_transit[channel_idx];

    for (int h_idx = 0; h_idx < MAX_HISTORY_PERIODS - 1; h_idx++)
    {
      //  doc["channel_history"][channel_idx][h_idx] = channel_history[channel_idx][h_idx];
      doc["channel_history"][channel_idx][h_idx] = (uint8_t)((channel_history_s[channel_idx][h_idx] + 30) / 60);
    }

    ch_counters.update_times(channel_idx);
    // this could cause too big value, maybe overflow, was  uint8_t
    doc["channel_history"][channel_idx][MAX_HISTORY_PERIODS - 1] = (int16_t)((ch_counters.get_period_uptime(channel_idx) + 30) / 60);
  }

  doc["ts"] = time(nullptr);
  doc["started"] = processing_started_ts;
  doc["temp_f"] = cpu_temp_f;
  doc["last_msg_msg"] = last_msg.msg;
  doc["last_msg_ts"] = last_msg.ts;
  doc["last_msg_type"] = last_msg.type;
  doc["energy_meter_read_last_ts"] = energy_meter_read_succesfully_ts;
  doc["production_meter_read_last_ts"] = production_meter_read_last_ts;
  doc["next_process_in"] = max((long)0, (long)next_process_ts - time(nullptr));

#ifdef LOAD_MGMT_ENABLED
  for (int l = 0; l < s.load_manager_phase_count; l++)
  {
    doc["energy_meter_current_latest"][l] = energy_meter_current_latest[l];
  }
  doc["load_manager_overload_last_ts"] = load_manager_overload_last_ts;
#endif

  doc["free_heap"] = ESP.getFreeHeap();

  serializeJson(doc, output);
  request->send(200, "application/json", output);
}

// TODO: check how it works with RTC
/**
 * @brief Set the timezone info etc after wifi connected
 *
 */
void set_timezone_ntp_settings(bool set_tz, bool set_ntp)
{
  //  Set timezone info
  if (set_tz)
  {
    char timezone_info[35];
    if (strcmp("EET", s.timezone) == 0)
      strcpy(timezone_info, "EET-2EEST,M3.5.0/3,M10.5.0/4");
    else // CET default
      strcpy(timezone_info, "CET-1CEST,M3.5.0/02,M10.5.0/03");

    setenv("TZ", timezone_info, 1);
    Serial.printf(PSTR("timezone_info: %s, %s\n"), timezone_info, s.timezone);
    tzset();
  }

  if (set_ntp)
  {
    // assume working wifi
    configTime(0, 0, ntp_server_1, ntp_server_2, ntp_server_3);
    // configTime(0, 0, ntp_server_1);
  }

  struct tm timeinfo;
  //
  if (!getLocalTime(&timeinfo, 30000) && (time(nullptr) < ACCEPTED_TIMESTAMP_MINIMUM))
  {
    log_msg(MSG_TYPE_ERROR, PSTR("Failed to obtain time"));
  }
  else
  {
    Serial.printf(PSTR("Setup waiting for clock time, current ts: %ld\n"), time(nullptr));
  }
}

/**
 * @brief Reports (acts on) changing wifi states
 *
 * @param event
 */
void wifi_event_handler(WiFiEvent_t event)
{
  switch (event)
  {
  case SYSTEM_EVENT_STA_CONNECTED:
    //  Serial.println(F("Connected to WiFi Network"));
    break;
  case SYSTEM_EVENT_STA_GOT_IP:
    wifi_sta_connected = true;
    //  Serial.println(F("Got IP"));
    set_timezone_ntp_settings(true, true);
    break;
  case SYSTEM_EVENT_STA_DISCONNECTED:
    wifi_sta_connected = false;
    wifi_sta_disconnected_ms = millis();
    Serial.println(F("Disconnected from WiFi Network"));
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

uint16_t wifi_connect_count = 0;

bool connect_wifi()
{
  bool create_wifi_ap = false;
  uint32_t connect_started;
  wifi_connect_count++;
  wifi_sta_connection_required = strlen(s.wifi_ssid) > 0;

  if (wifi_sta_disconnected_ms == 0)
  {
    wifi_sta_disconnected_ms = millis();
  }
  wifi_connect_tried_last_ms = millis();

  if (wifi_connect_count == 1)
  { // one handle should be enough
    WiFi.onEvent(wifi_event_handler);
  }

  if (wifi_sta_connection_required)
  {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("arska");
    Serial.printf(PSTR("Trying to connect wifi [%s] with password [%s]\n"), s.wifi_ssid, s.wifi_password);
    WiFi.begin(s.wifi_ssid, s.wifi_password);
    connect_started = millis();
    while (WiFi.status() != WL_CONNECTED)
    { // Wait for the Wi-Fi to connect
      if (millis() - connect_started > 60000)
      {
        Serial.println(F("WiFi Failed!"));
        delay(1000);
        WiFi.disconnect();
        delay(3000);

        break;
      }
      io_tasks(STATE_CONNECTING); // leds, reset
      delay(500);
    }
  }
  else
  {
    Serial.println(F("WiFi SSID undefined."));
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.printf(PSTR("Connected to wifi [%s] with IP Address: %s, gateway: %s \n"), s.wifi_ssid, WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str());
    wifi_sta_connected = true;
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
    // Now wifi is up, set wifi relays to default state
    if (wifi_connect_count == 1)
    {
      for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
      {
        if (is_wifi_relay(s.ch[channel_idx].type))
        {
          Serial.printf(PSTR("Reapply wifi relay state %d in init\n"), channel_idx);
          apply_relay_state(channel_idx, true);
        }
      }
    }
    return true;
  }

  wifi_sta_connected = false;
  wifi_sta_disconnected_ms = millis();
  create_wifi_ap = true;

  // TODO: check also https://github.com/me-no-dev/ESPAsyncWebServer/blob/master/examples/CaptivePortal/CaptivePortal.ino
  // create ap-mode ssid for config wifi
  Serial.print("Creating AP");

  String APSSID = String("ARSKA-") + wifi_mac_short;
  int wifi_channel = (int)random(1, 14);
  if (WiFi.softAP(APSSID.c_str(), (const char *)__null, wifi_channel, 0, 3) == true)
  {
    if (wifi_sta_connection_required)
    {
      Serial.printf(PSTR("\nEnter valid WiFi SSID and password:, two methods:\n 1) Give WiFi number (see the list above) <enter> and give WiFi password <enter>.\n 2) Connect to WiFi %s and go to url http://%s to update your WiFi info.\n"), APSSID.c_str(), WiFi.softAPIP().toString());
      Serial.println();
      Serial.flush();
    }
  }
  else
  {
    Serial.println(F("Cannot create AP, restarting"));
    log_msg(MSG_TYPE_FATAL, PSTR("Cannot create AP, restarting."), true);
    delay(20000); // cannot create AP, or connect existing wifi , restart
    ESP.restart();
  }
  return false;
}

//************** S E T U P *****************/

/**
 * @brief Arduino framework function.  Everything starts from here while starting the controller.
 *
 */

void setup()
{

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  Serial.begin(115200);
  delay(2000);               // wait for console to settle - only needed when debugging
  randomSeed(analogRead(0)); // initiate random generator
  Serial.printf(PSTR("ARSKA VERSION_BASE %s, Version: %s, compile_date: %s\n"), VERSION_BASE, VERSION, compile_date);

  // String
  wifi_mac_short = WiFi.macAddress();
  for (int i = 14; i > 0; i -= 3)
  {
    wifi_mac_short.remove(i, 1);
  }
  Serial.printf(PSTR("Device mac address: %s\n"), WiFi.macAddress().c_str());

  // Experimental
  grid_protection_delay_interval = random(0, grid_protection_delay_max / PROCESS_INTERVAL_SECS) * PROCESS_INTERVAL_SECS;
  Serial.printf(PSTR("Grid protection delay after interval change %d seconds.\n"), grid_protection_delay_interval);

  // mount filesystem
  if (!FILESYSTEM.begin(false))
  {
    delay(5000);
    if (!FILESYSTEM.begin(false))
    {
      Serial.println(F("Failed to initialize filesystem library,."));
      log_msg(MSG_TYPE_FATAL, "Cannot use corrupted filesystem! Update the system!");
      // delay(5000);
      //  ESP.restart();
    }
  }
  else
  {
    fs_mounted = true;
    Serial.println(F("Filesystem initialized"));
  }

  if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER)
    log_msg(MSG_TYPE_INFO, "Wakeup caused by timer", true);

  log_msg(MSG_TYPE_INFO, PSTR("Initializing the system."), true);

#ifdef SENSOR_DS18B20_ENABLED
  sensors.begin();
  delay(1000); // let the sensors settle
  // get a count of devices on the wire
  sensor_count = min(sensors.getDeviceCount(), (uint8_t)MAX_DS18B20_SENSORS);
  Serial.printf(PSTR("sensor_count:%d\n"), sensor_count);
#endif

  // Check if filesystem update is needed
  Serial.println(F("Checking filesystem version"));

  todo_in_loop_update_firmware_partition = fs_mounted ? !(check_filesystem_version()) : true;

  readFromEEPROM();

#ifdef METER_HAN_DIRECT_ENABLED
#define HAN_P1_SERIAL_SIZE_RX 1024 // Big enough for HAN P1 message
  if (s.energy_meter_type == ENERGYM_HAN_DIRECT)
  {
    Serial.printf("Initializing HAN P1 Serial for HAN P1 read. GPIO: %d\n", (int)s.energy_meter_gpio);

    HAN_P1_SERIAL.setRxBufferSize(HAN_P1_SERIAL_SIZE_RX);
    HAN_P1_SERIAL.begin(115200, SERIAL_8N1, s.energy_meter_gpio, -1); // Hardware Serial of ESP32
    HAN_P1_SERIAL.flush();
    HAN_P1_SERIAL.onReceive(receive_energy_meter_han_direct, false); // sets a RX callback function for Serial 2
  }
#endif

#ifdef RESET_BUTTON_ENABLED
  if (hw_templates[hw_template_idx].hw_io.reset_button_gpio != ID_NA)
  {
    pinMode(hw_templates[hw_template_idx].hw_io.reset_button_gpio, hw_templates[hw_template_idx].hw_io.reset_button_normal_state == HIGH ? INPUT_PULLUP : INPUT_PULLDOWN);
    reset_button_push_started_ms = 0;
    // check when change in gpio state - could be polling in the loop/iotasks instead
    attachInterrupt(hw_templates[hw_template_idx].hw_io.reset_button_gpio, check_reset_button, CHANGE);
    Serial.printf("DEBUG: Activating reset button interrupt for gpio %d\n", hw_templates[hw_template_idx].hw_io.reset_button_gpio);
  }
#endif

  io_tasks();

  if (s.check_value != EEPROM_CHECK_VALUE) // setup not initiated
  {
    Serial.printf(PSTR("Memory structure changed. Resetting settings. Current check value %d, new %d\n "), s.check_value, (int)EEPROM_CHECK_VALUE);
    reset_config(); // assume check value -1 on first run after eeprom init
    config_resetted = true;
    read_shadow_settings(); // try to get save settings from shadow settings file
  }
  else
    Serial.printf(PSTR("Current check value %d match with firmware check value.\n "), s.check_value);

  // Channel init with state DOWN/failsafe
  Serial.println(F("Setting relays default/failsafe."));
  for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
  {
    if (s.ch[channel_idx].type == CH_TYPE_GPIO_FIXED) // deprecate CH_TYPE_GPIO_FIXED type
      s.ch[channel_idx].type = CH_TYPE_GPIO_USER_DEF;

    //  set channels to default states before calculated values
    // Serial.printf("DEBUG ch %d default state %s\n",channel_idx,s.ch[channel_idx].default_state ?"up":"down" );
    s.ch[channel_idx].wanna_be_up = s.ch[channel_idx].default_state;
    s.ch[channel_idx].is_up = s.ch[channel_idx].default_state;
    chstate_transit[channel_idx] = CH_STATE_BYDEFAULT;

    apply_relay_state(channel_idx, true);
    relay_state_reapply_required[channel_idx] = false;
  }

// HW EXTENSIONS setup()
#ifdef HW_EXTENSIONS_ENABLED

  if (hw_template_idx != -1)
  {
    io_tasks();

    // Set all the pins of 74HC595 as OUTPUT
    if (hw_templates[hw_template_idx].hw_io.output_register)
    {
      pinMode(hw_templates[hw_template_idx].hw_io.rclk_gpio, OUTPUT);
      pinMode(hw_templates[hw_template_idx].hw_io.ser_gpio, OUTPUT);
      pinMode(hw_templates[hw_template_idx].hw_io.srclk_gpio, OUTPUT);
      register_out = 0; // all down

      //   Serial.printf("register_out %d\n", (int)register_out);
      updateShiftRegister();
    }
  }
#else // no extensions (Shelly/SN74HC595) but led for HomeWizard, Olimex. todo: other devices, move SN74HC595-based devices under the same logic
  if (!hw_templates[hw_template_idx].hw_io.output_register && hw_templates[hw_template_idx].hw_io.status_led_type != STATUS_LED_TYPE_NONE)
  {
    for (int i = 0; i < 3; i++)
    {
      if (hw_templates[hw_template_idx].hw_io.status_led_ids[i] != ID_NA)
        pinMode(hw_templates[hw_template_idx].hw_io.status_led_ids[i], OUTPUT);
    }

    // setup() led
    set_led(255, 0, 0,30,LED_PATTERN_SHORT_LONG_SHORT);

    led_timer = timerBegin(0, 80, true);
    timerAttachInterrupt(led_timer, &on_led_timer, true);
    timerAlarmWrite(led_timer, LED_TIMER_INTERVAL_US, true);
    timerAlarmEnable(led_timer);

  }

#endif

  Serial.println("Starting wifi");
  scan_and_store_wifis(true, false); // testing this in the beginning

  connect_wifi();

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
    settimeofday_cb(ntp_time_is_set); // register callback if time was sent
    if (time(nullptr) < ACCEPTED_TIMESTAMP_MINIMUM)
      getRTC(); // Fallback to RTC on startup if we are before 2020-09-13
  }
#endif

  io_tasks(); // starting leds

#ifdef OTA_UPDATE_ENABLED
              //  update form
  server_web.on("/update", HTTP_GET, [](AsyncWebServerRequest *request)
                { onWebUpdateGet(request); });

  //
  server_web.on("/update", HTTP_POST, [](AsyncWebServerRequest *request)
                { onWebUpdatePost(request); });

  server_web.on(
      "/releases", HTTP_GET, [](AsyncWebServerRequest *request)
      { request->send(200,"application/json",update_releases.c_str()); 
        todo_in_loop_get_releases= true; });

  server_web.on(
      "/doUpdate", HTTP_POST,
      [](AsyncWebServerRequest *request) {},
      [](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data,
         size_t len, bool final)
      { handleFirmwareUpdate(request, filename, index, data, len, final); });
#endif

  server_web.on("/status", HTTP_GET, onWebStatusGet);

  server_web.on("/prices", HTTP_GET, onWebPricesGet);
  server_web.on("/series", HTTP_GET, onWebSeriesGet);

  server_web.on("/application", HTTP_GET, onWebApplicationGet);

  server_web.on("/settings", HTTP_GET, onWebSettingsGet);
  server_web.on("/wifis", HTTP_GET, onWebWifisGet);

  server_web.on(
      "/settings", HTTP_POST,
      [](AsyncWebServerRequest *request) {},
      [](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data,
         size_t len, bool final) {},
      onWebSettingsPost);

  server_web.addHandler(ActionsPostHandler); // for url "/actions"

  // full json file, multi part upload
  server_web.on(
      "/settings-restore",
      HTTP_POST,
      [](AsyncWebServerRequest *request) {},
      [](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data,
         size_t len, bool final) {},
      onWebUploadConfigPost);

  // server_web.on("/", HTTP_GET, onWebUIGet);
  server_web.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
                { 
                  if (!request->authenticate(s.http_username, s.http_password))
    return request->requestAuthentication(); 
    if (!first_loop_ended && WiFi.getMode() == WIFI_STA )  {
      request->send(200, "text/html", PSTR("<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"10\"></head><body><p>Arska is waking up, wait a while...</p></body></html>"));
    }
    else {
      request->send(FILESYSTEM, "/ui3.html", "text/html");
    } });

  server_web.on(
      "/update.schedule", HTTP_POST,
      [](AsyncWebServerRequest *request) {},
      [](AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data,
         size_t len, bool final) {},
      onScheduleUpdatePost);

  server_web.serveStatic("/js/", FILESYSTEM, "/js/").setCacheControl("max-age=84600, public");
  server_web.serveStatic("/css/", FILESYSTEM, "/css/").setCacheControl("max-age=84600, public");
  // TODO: check authentication for files in /cache
  server_web.serveStatic("/data/", FILESYSTEM, "/data/").setCacheControl("max-age=84600, public");
  server_web.serveStatic("/cache/", FILESYSTEM, "/cache/");

  server_web.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request)
                { request->header("Cache-Control: max-age=86400, public"); request->send(FILESYSTEM, F("/data/favicon.ico"), F("image/x-icon")); });
  server_web.on("/favicon-32x32.png", HTTP_GET, [](AsyncWebServerRequest *request)
                { request->header("Cache-Control: max-age=86400, public"); request->send(FILESYSTEM, F("/data/favicon-32x32.png"), F("image/png")); });
  server_web.on("/favicon-16x16.png", HTTP_GET, [](AsyncWebServerRequest *request)
                {  request->header("Cache-Control: max-age=86400, public"); request->send(FILESYSTEM, F("/data/favicon-16x16.png"), F("image/png")); });

  // templates
  server_web.on(template_filename, HTTP_GET, [](AsyncWebServerRequest *request)
                { request->send(FILESYSTEM, template_filename, "text/json"); });
  //

  // TODO: remove function notfound
  server_web.onNotFound([](AsyncWebServerRequest *request)
                        { request->send(404, "text/plain", "Not found"); });

  // TODO: remove force create
  // generate_ui_constants(true); // generate ui constant json if needed
  server_web.begin();

  if (wifi_sta_connected)
  {
    Serial.printf("\nArska dashboard url: http://%s/ in WiFi: %s\n", WiFi.localIP().toString().c_str(), WiFi.SSID().c_str());
  }
  else
  {
    Serial.printf("\nArska dashboard url: http://%s/ in Arska private WiFi: %s\n", WiFi.softAPIP().toString().c_str(), WiFi.softAPSSID().c_str());
    Serial.println("Select wifi from the list above, if you want to connect existing wifi.");
  }

  Serial.printf(PSTR("Web admin: [%s], password: [%s]\n\n"), s.http_username, s.http_password);

  Serial.println(F("setup() ended."));

#ifdef LOOP_WATCHDOG_ENABLED
  // watchdog loop
  xTaskCreatePinnedToCore(loop_watchdog, "loop_watchdog", 1000, NULL, 0, NULL, 1); //  0 - wifi,ble, 1 - user
#endif
} // end of setup()

//************** L O O P *****************/
/**
 * @brief Arduino framwork function. This function is executed repeatedly after setup().  Make calls to scheduled functions
 *
 */
void loop()
{
#ifdef LOOP_WATCHDOG_ENABLED
  last_loop_started = esp_timer_get_time(); // store start time for a watchdog process
#endif

  bool period_changed = false;

  bool got_forecast_ok = false;
  bool got_price_ok = false;

  io_tasks();

  //  handle initial wifi setting from the serial console command line, first 2 minutes only
  if (!wifi_sta_connected && Serial.available() && millis() < 1000 * 120)
  {
    serial_command = Serial.readStringUntil('\n');
    if (serial_command_state == 0)
    {
      if (serial_command.c_str()[0] == 's')
      {
        scan_and_store_wifis(true, false);
        delay(10);
        return;
      }
      if (isdigit(serial_command[0]))
      {
        // Serial.print("Debug serial:");

        int wifi_idx = serial_command.toInt() - WIFI_OPTION_NOWIFI_SERIAL;
        //  if (wifi_idx < network_count + WIFI_OPTION_NOWIFI_SERIAL && wifi_idx >= WIFI_OPTION_NOWIFI_SERIAL)
        if (wifi_idx < network_count && wifi_idx >= 0)
        {
          strncpy(s.wifi_ssid, WiFi.SSID(wifi_idx).c_str(), 30);
          Serial.printf(PSTR("Enter password for network %s\n"), WiFi.SSID(wifi_idx).c_str());
          Serial.println();
          Serial.flush();
          serial_command_state = 1;
        }
        else if (wifi_idx == -1) // no wifi selected, WIFI_OPTION_NOWIFI_SERIAL must be 1
        {
          s.wifi_ssid[0] = 0;
          writeToEEPROM();
          // log_msg(MSG_TYPE_FATAL, PSTR("Restarting with disabled WiFI."), true);
          // delay(2000);
          // ESP.restart();
          log_msg(MSG_TYPE_FATAL, PSTR("Continue with disabled WiFI."), true);
          serial_command_state = 99;
        }
        else
        {
          Serial.println("SERIAL");
          Serial.println(wifi_idx);
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

  if (todo_in_loop_restart)
  {
    led_set_color_rgb(0, 0, 0);
    WiFi.disconnect();
    log_msg(MSG_TYPE_FATAL, PSTR("Restarting due to user activity (settings/cmd)."), true);
    writeToEEPROM();
    delay(2000);
    ESP.restart();
  }

  if (cooling_down_state) // the cpu is cooling down,  keep calm and wait
    delay(10000);

  if (todo_in_loop_scan_wifis)
  {
    todo_in_loop_scan_wifis = false;
    scan_and_store_wifis(true, true);
  }

#ifdef OTA_DOWNLOAD_ENABLED
  if (todo_in_loop_get_releases)
  {
    todo_in_loop_get_releases = false;
    get_releases();
  }
#endif

// started from admin UI
#ifdef SENSOR_DS18B20_ENABLED
  if (todo_in_loop_scan_sensors)
  {
    todo_in_loop_scan_sensors = false;
    if (scan_sensors())
      writeToEEPROM();
  }
#endif

#ifdef OTA_UPDATE_ENABLED
  // Note:  was earlier after time setup
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

  // uint32_t wifi_sta_disconnected_ms = 0    //!< restart if enough time since first try
  //     uint32_t wifi_connect_tried_last_ms = 0 //!< reconnect if enough time since first try

  if (!wifi_sta_connected && wifi_sta_connection_required && millis() - wifi_connect_tried_last_ms > WIFI_FAILED_RECONNECT_INTERVAL_SEC * 1000)
  {
    Serial.println(PSTR("Trying to reconnect wifi"));
    connect_wifi();
  }

  if (!wifi_sta_connected && wifi_sta_connection_required && ((millis() - wifi_sta_disconnected_ms) > (WIFI_FAILED_RESTART_RECONNECT_INTERVAL_SEC * 1000)))
  {
    WiFi.disconnect();
    log_msg(MSG_TYPE_FATAL, PSTR("Restarting due to missing wifi connection."), true);
    delay(2000);
    ESP.restart();
  }

  if (time(nullptr) < ACCEPTED_TIMESTAMP_MINIMUM)
  {
    delay(1000);
    return;
    //  <<--******* no other operations allowed before the clock is set
  }
  else if (processing_started_ts == 0) // we have clock set, run once
  {
    // There was a extra waiting here (for ntp time settle ?), removed because most probably not needed

    processing_started_ts = time(nullptr);
    calculate_time_based_variables(); // no external info needed for these

    if (config_resetted)
      log_msg(MSG_TYPE_WARN, PSTR("Version upgrade caused configuration reset. Started processing."), true);
    else if (wifi_sta_connected)
      log_msg(MSG_TYPE_INFO, PSTR("Started processing."), true);
    else if (wifi_sta_connection_required)
      log_msg(MSG_TYPE_WARN, PSTR("Started processing in configuration only mode."), true);
    else if (!wifi_sta_connected)
      log_msg(MSG_TYPE_INFO, PSTR("Started processing in standalone mode."), true);

    bool give_wifi_relay_warning = false;
    for (int channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++)
    {
      if (is_wifi_relay(s.ch[channel_idx].type) && !wifi_sta_connected && !wifi_sta_connection_required)
        give_wifi_relay_warning = true;
    }
    if (give_wifi_relay_warning)
      log_msg(MSG_TYPE_WARN, PSTR("Wifi relays cannot be switched in standalone mode."), true);

    set_timezone_ntp_settings(true, false); // need to set tz

    ch_counters.init();
    next_query_price_data_ts = time(nullptr);
    next_query_fcst_data_ts = time(nullptr);
    recorded_period_start_ts = processing_started_ts;
    current_period_start_ts = get_netting_period_start_time(processing_started_ts);
    period_changed = true;
    Serial.println("DEBUG: processing initiated.");
    yield();
  }

  // Below business logic actions that require mcu clock in time -->
  // process measurements
  if (todo_in_loop_process_energy_meter_readings)
  {
    todo_in_loop_process_energy_meter_readings = false;
    process_energy_meter_readings();
  }

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
    calculate_channel_states();
    set_relays(false);
  }

  /*
    // just in case check the wifi and reconnect/restart if needed
    if ((WiFi.waitForConnectResult(10000) != WL_CONNECTED) && wifi_sta_connected)
    {
      // Wait for the wifi to come up again
      for (int wait_loop = 0; wait_loop < 20; wait_loop++)
      {
        delay(1000);
        Serial.print('w');
        if (WiFi.waitForConnectResult(10000) == WL_CONNECTED)
          break;
      }
      if (WiFi.waitForConnectResult(10000) != WL_CONNECTED)
      {
        log_msg(MSG_TYPE_FATAL, PSTR("Restarting due to wifi error."), true);
        delay(2000);
        ESP.restart(); // boot if cannot recover wifi in time
      }
    }
    */

  if ((next_query_price_data_ts <= time(nullptr)) && (prices_expires_ts <= time(nullptr)) && wifi_sta_connected)
  {
    io_tasks(STATE_PROCESSING);

    if (strncmp(s.entsoe_area_code, "#", 1) == 0)
    {
      got_price_ok = false; // no area code, not price query
    }
    else
    {
#ifdef PRICE_ELERING_ENABLED
      if (strncmp(s.entsoe_area_code, "elering:", 8) == 0)
        got_price_ok = get_price_data_elering();
      else
        got_price_ok = get_price_data_entsoe();
#else
      got_price_ok = get_price_data_entsoe();
#endif
      delay(DELAY_AFTER_EXTERNAL_DATA_UPDATE_MS);
    }
    io_tasks(STATE_PROCESSING);
    // todo_in_loop_update_price_rank_variables = got_price_ok;
    if (got_price_ok)
    {
      todo_calculate_ranks_period_variables = true;
    }
    next_query_price_data_ts = (got_price_ok ? (max(prices_expires_ts, time(nullptr)) + random(0, 300)) : (time(nullptr) + 600 + random(0, 60))); // random, to prevent query peak
    Serial.printf("next_query_price_data_ts: %ld %s\n", next_query_price_data_ts, got_price_ok ? "ok" : "failed");
  }

  if (next_query_fcst_data_ts <= time(nullptr) && wifi_sta_connected) // got solar & wind fcsts
  {
    io_tasks(STATE_PROCESSING);
    got_forecast_ok = get_renewable_forecast(FORECAST_TYPE_FI_LOCAL_SOLAR, &solar_forecast);
    delay(DELAY_AFTER_EXTERNAL_DATA_UPDATE_MS);
    get_renewable_forecast(FORECAST_TYPE_FI_WIND, &wind_forecast);
    delay(DELAY_AFTER_EXTERNAL_DATA_UPDATE_MS);

    todo_calculate_ranks_period_variables = true;
    next_query_fcst_data_ts = time(nullptr) + (got_forecast_ok ? (3 * SECONDS_IN_HOUR + random(0, 200)) : 600 + random(0, 100));
  }

// new period coming, record last minute, launch only once in period end, EXPERIMENTAL
#define ESTIMATED_LOOP_PROCESSING_TIME_A_SEC 10 // estimated time for (optional) meter polling + variable processing
  if (get_netting_period_start_time(time(nullptr)) < get_netting_period_start_time(time(nullptr) + ESTIMATED_LOOP_PROCESSING_TIME_A_SEC) && (millis() - energy_meter_last_read_started_ms > (ESTIMATED_LOOP_PROCESSING_TIME_A_SEC * 1000)))
  {
    Serial.printf("\nForcing processing before period change %ld\n", time(nullptr));
    next_energy_meter_read_ts = time(nullptr);
    next_process_ts = time(nullptr);
  }

  // new period
  if (previous_period_start_ts != get_netting_period_start_time(time(nullptr)))
  {
    period_changed = true;
    next_energy_meter_read_ts = time(nullptr); // tämä lisätty 28.10.2023 tai jotain..., debug info,
    next_process_ts = time(nullptr);           // process now if new period

    // update period info
    current_period_start_ts = get_netting_period_start_time(time(nullptr));
    recorded_period_start_ts = current_period_start_ts;

    // rotates history array and sets selected variables to the history array
    vars.rotate_period();
    calculate_time_based_variables();
    calculate_price_rank_variables();
    calculate_forecast_variables();

    Serial.printf("\nPeriod changed %ld -> %ld, grid protection delay %d secs\n", previous_period_start_ts, current_period_start_ts, grid_protection_delay_interval);
  }

#ifdef INFLUX_REPORT_ENABLED
  if (todo_in_loop_influx_write && wifi_sta_connected) // TODO: maybe we could combine this with buffer update
  {
    todo_in_loop_influx_write = false;
    write_buffer_to_influx();
  }
#endif

  // Todo tasks (time must be set) -->
  if (todo_calculate_ranks_period_variables) // set after price query
  {
    io_tasks(STATE_PROCESSING);
    todo_calculate_ranks_period_variables = false;
    calculate_price_rank_variables();
    delay(1000);
    calculate_forecast_variables();
  }
  // <-- Todo tasks (time must be set) -

  // Scheduled tasks -->
  // Meter polling could be shorter than normal processing frequency
#define ESTIMATED_METER_READ_TIME_MAX_SEC (CONNECT_TIMEOUT_INTERNAL + 1)
  if (next_energy_meter_read_ts <= time(nullptr) && wifi_sta_connected)
  {
    if (s.energy_meter_type == ENERGYM_SHELLY3EM || s.energy_meter_type == ENERGYM_SHELLY_GEN2)
    {
      io_tasks(STATE_PROCESSING);
      read_energy_meter();
      next_energy_meter_read_ts = max((time_t)(next_energy_meter_read_ts + s.energy_meter_pollingfreq), time(nullptr) + (s.energy_meter_pollingfreq - ESTIMATED_METER_READ_TIME_MAX_SEC)); // max is just in case to allow skipping reading, if reading takes too long
    }
  }

  // TODO: all sensor /meter reads could be here?, do we need diffrent frequencies?
  if (next_process_ts <= time(nullptr)) // time to process
  {
    if (s.production_meter_type != PRODUCTIONM_NONE && wifi_sta_connected)
    {
      io_tasks(STATE_PROCESSING);
      read_production_meter();
    }

#ifdef SENSOR_DS18B20_ENABLED
    read_ds18b20_sensors();
#endif
    calculate_time_based_variables();                                                                                      // call here for minute level changes
    calculate_meter_based_variables();                                                                                     // TODO: if period change we could set write influx buffer after this?
    next_process_ts = max((time_t)(next_process_ts + PROCESS_INTERVAL_SECS), time(nullptr) + (PROCESS_INTERVAL_SECS / 2)); // max is just in case to allow skipping processing, if processing takes too long
    calculate_channel_states();
    set_relays(true); // grid protection delay active
  }

  // finalize period change
  if (period_changed)
  {
    Serial.printf("Finishing period_changed previous_period_start_ts %ld, current_period_start_ts %ld \n", previous_period_start_ts, current_period_start_ts);
#ifdef INFLUX_REPORT_ENABLED
    if (previous_period_start_ts != 0)
    {
      // add values from the last period to the influx buffer and schedule writing to the influx db
      // we still must have old period variable values set
      add_period_variables_to_influx_buffer(previous_period_start_ts);
      ch_counters.new_log_period(previous_period_start_ts);
      todo_in_loop_influx_write = true;
    }
#endif

    previous_period_start_ts = current_period_start_ts;
    Serial.printf("Finishing period_changed B previous_period_start_ts %ld, current_period_start_ts %ld \n", previous_period_start_ts, current_period_start_ts);
    period_changed = false;
  }
  // <-- Scheduled tasks

  // Tasks that should be run
#ifdef INVERTER_SMA_MODBUS_ENABLED
  mb.task(); // process modbus event queue
#endif
  yield();
  delay(50); // short break
  io_tasks(STATE_NONE);
  first_loop_ended = true;
}
