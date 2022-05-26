# Arska
Arska makes your energy purchases greener and saves on your energy bill by demand-side flexibility, i.e. maximising usage of self produced (solar) energy and shifting energy purchase to cheapest and lower carbon intensive hours.
![Arska diagram)(https://github.com/Netgalleria/arska-node/blob/main/docs/img/Arska%20Node%20ESP32%20diagram.png)
Arska is a microcontroller (ESP32, limited functionality with ESP8266) based application for managing energy consumption. It can switch on and of loads  based on measured consumption and production information as well as price and renevable energy forecast. Arska gets preprocessed market and energy forecast data from Arska Server (optional). 
![Arska Diagram](https://github.com/Netgalleria/arska-node/blob/main/docs/img/Arska%20Node%20ESP32%20diagram.png)

Arska can control various electric switches connected to e.g. water heater and car chargers. It can also privide potential-free signal for temperature control for example to heat-pumps. Arska controls devices based on following data:
- Day-ahead electricity price per hour from [EntsoE](https://transparency.entsoe.eu/) . Price data is availabe from 25 European countries (.
- Real time energy export/import of the property (measured by [Shelly 3EM](https://shelly.cloud/products/shelly-3em-smart-home-automation-energy-meter/) ) or solar production (selected Fronius and SMA inverters supported).
- Current date and time
- Solar forecast from BCDC Energia, available in Finland

More information in [Arska Wiki](https://github.com/Netgalleria/arska-node/wiki) .

[Discussions, English or Finnish](https://github.com/Netgalleria/arska-node/discussions)

# Current status
The software is under development (beta testing). For earlier versions see: [Arska Node](https://github.com/Netgalleria/arska-node) 
# License 
The software is licenced under GPL v.3 license. For other licencing options contact olli@netgalleria.fi .

# Thanks
    - https://github.com/me-no-dev/ESPAsyncWebServer  - Control UI web server
    - https://github.com/ayushsharma82/ElegantOTA  - OTA update
    - https://arduinojson.org/ - Processing web API results 
    to be updated...
