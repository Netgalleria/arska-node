# Arska release notes
## Arska [0.92] (2022-09-09)

### Upgrade Steps
* Prepare to rewrite device configuration when upgrading from version 0.91. Configuration backup in older version does not necessarily create complete backup file.
* Updagrade from [the installation page](https://iot.netgalleria.fi/arska-install/) or upload separately both files firmware.bin and littlefs.bin at http:://[Arska ip address]/update.

### New Features
* Initial support for Shelly Gen1, Gen2 and Tasmota relays. Select relay type and relay configuration at channel configuration page.
* New variables for channel rules: current price ratio %, compared with 9/24 hours average, variable id:s 13,14 and 15
* Variable operators/functions "defined" and  "undefined" in rule definitions.
* Multiple binary (firmware.bin) builds (esp32doit-devkit-v1, esp32lilygo-4ch, esp32wroom-4ch-a) replaced with one generic version (build: esp32-generic-6ch). Relay GPIO numbers are now defined in hardware templates (select at admin page) and channel configuration. In earlier versions GPIO numbers were harcoded in binary versions.

### Bug Fixes
* Configuration backup and restore now functional.

### Other Changes
* More documentation


