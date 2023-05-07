# Arska release notes

## Arska 0.99
* Major user interface (UI) upgrade. The **Dashboard** contains latest time series for prices, import, production and channel utilization as well as manual scheduling. Time series are not stored in the non-volatile memory so restart of the device will clear them. The **Channels** sections contains relay settings and channel rules. The **Settings** sections is for system parameters and administration tasks.
* Can read both grid energy meter (Shelly 3EM, HAN P1 telegram) and production inverters (SMA, Fronius). 
* Rule templates
  * New UI for template variables. Min, max, step for numeric fields. Copying variable to several conditions, with optonal multiply. 
  * New rule templates
* Additional channel variables 	
  * `152 ch up mins in 8 h` and `153 ch up mins in 24 h` store how long time (minutes) each channel has been up during last 8 hours or 24 hours. These uptime counters are initiated with 0 when the device is restarted.
  * `160 Consumption estim.` is a summary of total consumption of channels during this period. Based on uptime and load (channel attribute). 
  * `401 virtual solar count` A solar forecast based counter counting from 0 to max 1440 each period. Can be used to distrubute channel uptime to sunny hours. For example rule `(401) virtual solar count < 300` will keep the channel up 5 hours (except dark days) and uptime per period is proportional to forecasted solar energy during that period.
  * `402 solar prod. estim.`  Estimated solar production during this period so far, based on local solar forecast, Wh.
  * `411 FI wind d+1, MW` and `412 FI wind d+2, MW` express forecasted average wind power in Finland tomorrow/the day after tomorrow in MW. Data from FMI wind power forecast.
  * `421 FI wind d+1 bl, MW` and `422 FI wind d+2 bl, MW` express forecasted average wind power in Finland  in the same 8 h tomorrow/the day after tomorrow in MW. Data from FMI wind power forecast.
  * `430 solar rank fix 24h` Solar volume rank of current period within day (24 h) based on solar forecast.
  * `700 Netting source` Express data source used to get netting/overproduction data, variable 100.
* New channel attibutes
  * A priority value (0-255) added the channels. Channel with the lowest priority value is switched up first and switched down last when there are two or more channels to switch simultanously.
  * Load (Watts) is used to estimate channel energy usage in consumption estimates for optimazed netting.
  * Channel colour, user defined with default values, is used in graphs and channels lists
* Many internal optimizations. Unnecessary/redundant filesystem caches and calculations removed/optimized.


## Arska 0.93 

### New features (updated 2023-02-04)
* New variables for fixed 8 hours blocks (23-06,07-14,15-22): VARIABLE_PRICERANK_FIXED_8, VARIABLE_PRICERANK_FIXED_8_BLOCKID
* HAN P1 port reading from HomeWizard Wi-Fi P1 Meter (HWE-P1-G1) or device with similar telegram output, tested with Aidon 7534, HWE-P1-G1 firmware version 4.14

### Bug Fixes
* Special character including Scandinavian characters did not work in channel id or forecast location. Fixed. Stores now characters in ISO-8859-1 .

### Other Changes
* Code cleanup: Removed outdated ESP8266 code, documentation, removed unused variables and functions

## Arska 0.92 (updated 2022-11-02)
* 0.92.0-stable released 2022-11-02


### Upgrade Steps
* Prepare to rewrite device configuration when upgrading from version 0.91. Configuration backup in older version does not necessarily create complete backup file. If you are using an earliar build of 0.92, backup the configuration and restore it after upgrade.
* Upgrade from [the installation page](https://iot.netgalleria.fi/arska-install/) with a cable connection or upload separately both files firmware.bin and littlefs.bin at http:://[Arska ip address]/update. Both files must be upgraded.

### New Features
* Initial support for Shelly Gen1, Gen2 and Tasmota relays. Select relay type and relay configuration at channel configuration page.
* New variables for channel rules: current price ratio %, compared with 9/24 hours average, variable id:s 13,14 and 15
* Variable operators/functions "defined" and  "undefined" in rule definitions.
* Price graph of last know 48 hours added to the dashboard.
* New Relay type "GPIO inversed" can be used e.g. for failsafe dry contacs. If channel is DOWN gpio pin is HIGH and  vice versa.


### Bug Fixes
* Fixed memory allocation error in channel rule configuration. Caused sproratic errors in saving channel rules.
* Configuration backup and restore now functional.
* Fixed sporatic error in saving settings in non-volatile memory. 
* Price query has better "garbage" detection should prevent all known price errors caused by extra characters.

### Other Changes
* Multiple binary (firmware.bin) builds (esp32doit-devkit-v1, esp32lilygo-4ch, esp32wroom-4ch-a) replaced with one generic version (build: esp32-generic-6ch). Relay GPIO numbers are now defined in hardware templates (select at admin page) and channel configuration. In earlier versions GPIO numbers were harcoded in binary versions.
* New internal storage format-> causes configuration reset when updagrading from earlier versions. Prepare to rewrite device configuration when upgrading from version 0.91.
* Relay device network discovery (mDNS) evaluated, but disabled due to stability concerns.
* Price query timing has now a random element to prevent all instances to make a simultanous query .
* User interface (UI) tranformed to one html-document. No page loading when moving from section to another
* UI/Dashboard, updating channel schedule now from update button (earlier save and reload of whole dashbord). The save button removed from the dashboard.
* Dashboard status responds faster to channes status changes. 
* GPIO relays are initiated with channel DOWN (-> LOW for normal gpio, -> HIGH for inversed GPIO) before wifi connect. pinMode is set only when needed (earlier always when switched)
* New versioning system
* More documentation


