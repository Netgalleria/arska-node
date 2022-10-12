# Arska release notes
## Arska 0.92 (updated 2022-10-12)

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


