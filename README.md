# Arska Node
Arska Node saves on your energy bill by demand-side flexibility, i.e. maximising usage of self produced (solar) energy and shifting energy purchase to cheapest hours.

Arska Node is microcontroller (ESP8266) based application for reading energy meters, sensors and controlling switches based on price, energy forecast and grid (consumption and production) information. Arska Node gets processes market and energy forecast data from [Arska Server](https://github.com/Netgalleria/arska-server) .


# Interfaces

![Data flow diagram](https://github.com/Netgalleria/arska-node/blob/main/docs/img/Arska%20Node%20and%20Server%20all-in-one%20diagram.png?raw=true)


# Features

# Techical
* can run without internet connection (no market or forecast data, RTC required)


## Arska Server
All day-ahead (spot) electricity price data and energy production forecasts are preprocessed by a Arska Server instance. Arska Node gets state info (e.g "now spot price is low") from a Arska Server service (http query). Arska server is Python-based service you can run  locally or use a shared service. [Arska Server in GitHub](https://github.com/Netgalleria/arska-server)


## Switches
Low voltage switches connected to the microcontroller can control grid voltage relays. Additionally Shelly 3EM energy meter has one relay interface which can be used. Leave grid voltage installation to a certified professinal. 

## How to configure channels

* [Read more about configuration](https://github.com/Netgalleria/arska-node/wiki/Configuring-Arska-Node)
* [Channel configuration example](https://github.com/Netgalleria/arska-node/wiki/Example-channel-configuration)

# Current status
The software is under development (beta testing)



# Thanks
    - https://github.com/me-no-dev/ESPAsyncWebServer  - Control UI web server
    - https://github.com/ayushsharma82/ElegantOTA  - OTA update
    - https://arduinojson.org/ - Processing web API results 