# Arska Node
Arska Node saves on your energy bill by demand-side flexibility, i.e. maximising usage of self produced (solar) energy and shifting energy purchase to cheapest hours.

Arska Node is microcontroller (ESP8266) based application for controlline reading energy meters, sensors and controlling switches based on price, energy forecast and grid (consumption and production) information. Arska Node gets processes market and energy forecast data from [Arska Server](https://github.com/Netgalleria/arska-server) .


# Interfaces

![Data flow diagram](https://github.com/Netgalleria/arska-node/blob/main/docs/img/Arska%20Node%20and%20Server%20all-in-one%20diagram.png?raw=true)

## Energy and production metering

### Shelly 3EM energy meter
Reads consumed and sold energy information from the meter (http query) and calculates net net consumption for current period. [More info](https://github.com/Netgalleria/arska-node/wiki/Configure-Shelly-3EM-for-Arska-Node)

### Inverter with Fronius Solar API
Solar power production values energy (cumulated) and current power can be read from a Fronius inverter (Solar API). Tested with FRONIUS Eco 27.0-3-S . [More info](https://github.com/Netgalleria/arska-node/wiki/Configure-Fronius-Solar-API-inverter-connection)


### Inverter with SMA Modbus TCP
Solar power production values energy (cumulated) and current power can be read from a SMA Inverter via Modbus TCP interface. [More info](https://github.com/Netgalleria/arska-node/wiki/Configure-SMA-inverter-Modbus-connection)

## DS18B20 temperature sensor 
Currently one sensor is supported per a Arska Node device. The sensoer is optional, but needed if you want to have multiple target temperature levels depending on conditions. For example a water heater temperature target could be 90 °C, when there is extra solar power, 60 °C when cheap electricity is available and 45 °C otherwise. The sensor must be in contact with the boiler, hot water pipeline or another object you are measuring. [Read more](https://github.com/Netgalleria/arska-node/wiki/Adding-DS18B20-temperature-sensor)

## Arska Server
All day-ahead (spot) electricity price data and energy production forecasts are preprocessed by a Arska Server instance. Arska Node gets state info (e.g "now spot price is low") from a Arska Server service (http query). Arska server is Python-based service you can run  locally or use a shared service. [Arska Server in GitHub](https://github.com/Netgalleria/arska-server)


## Switches
Low voltage switches connected to the microcontroller can control grid voltage relays. Additionally Shelly 3EM energy meter has one relay interface which can be used. Leave grid voltage installation to a certified professinal. 

## How to configure channels

[Read more about configuration](https://github.com/Netgalleria/arska-node/wiki/Configuring-Arska-Node)
[Channel configuration example](https://github.com/Netgalleria/arska-node/wiki/Example-channel-configuration)

### Channel 1
Channel 1 is connected to a pre-heater boiler (syöttövaraaja). Type GPIO ON/OFF means that channel is on if one or more states listed in states field (comma separated, no-spaces) is enabled. GPIO channels are wired to the microcontroller. State 1007 indicates that there is excess afternoon (solar) power production. Minimum uotime 120 seconds means that the channel stays up at least 120 seconds even if conditions are changed (in this case even if the is more consumption than production).

### Channel 2 
Channel 2 is connected to a super-heater boiler (tulistusvaraaja). Type GPIO ON/OFF means that channel is on is one or more states listed in states field (comma separated, no-spaces) is enabled and the temperature measured by the sensor is below target. Target levels are checked in order and first matching is used. In this case target is 90C if there is more (solar) production than consumption(state 1005). Target is 50C is spot price is below 2c/kWh (state 11010). Otherwise target is 40C (state 1 is always on)



# Current status
The software is under development (beta testing)

# Hardware Configuration
Example 2+1 channel configuration with ESP8266  based 2 channel relay module ja temperature sensor. With this configuration you can control for example 12VDC (or 24VDC) controlled AC relays or a water-based underfloor heating system (select between pre-programmed temperature levels). Waterproof temperature sensor can be attached to hot water pipeline or a water boiler, to sensor water temperature.

## Required modules/parts:
### ESP8266 microcontroller 
- ESP8266 (ESP-12F) Relay Module [Aliexpress](https://www.aliexpress.com/item/1005001908708140.html)
- USB to TTL converter for initial setup (this board does not have an USB connector), [e.g.](https://www.aliexpress.com/item/32529737466.html?), 
- female-female Dupont Dupont jumper lines for TTL connector, 4 pcs, [e.g.](https://www.aliexpress.com/item/1005003007413890.html)
- Optional DS3231 real time clock (RTC). Without RTC clock sync from internet or manual clock sync is needed after restart (including power breaks).

### Temperature sensor DS18B20
- DS18B20 waterproof temperature sensor, [Aliexpress](https://www.aliexpress.com/item/4000550061662.html)
- ≅ 5kΩ resistor for 1-wire pull-up resistor, between data and voltage connector
- Electric cables (3 wire) for connecting the sensor

### 12V wiring:
- 12V power supply, supplying power to ESP8266 module and pulling external relays
- Electric wires for 12V connection (two color, e.g. red and black recommended)
- female and male connecters for 12V, optional [Aliexpress](https://www.aliexpress.com/item/4000085878441.html)
- Screw terminal blocks, optional [Aliexpress](https://www.aliexpress.com/item/32939185688.html)





- 
 
 Tools:
 - Soldering iron, soldering tin
 - Tin suction gun (optional) for desoldering



# Thanks
    - https://github.com/me-no-dev/ESPAsyncWebServer  - Control UI web server
    - https://github.com/ayushsharma82/ElegantOTA  - OTA update
    - https://arduinojson.org/ - Processing web API results 