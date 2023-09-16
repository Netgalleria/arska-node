***Install ready firmware you can directly install from [the installation page](https://iot.netgalleria.fi/arska-install/).*** 

# Arska
Arska makes your energy purchases greener and saves on your energy bill by demand-side flexibility, i.e. maximising usage of self produced (solar) energy and shifting energy purchase to cheapest and lower carbon intensive hours.

Arska is a ESP32 microcontroller based application for managing energy consumption. It can switch on and of loads based on measured consumption and production information as well as price and solar energy forecast. 
 
## Intro Videos 

[![Arska power manager - installation and basic configuration](https://github.com/Netgalleria/arska-node/blob/main/docs/img/Arska-youtube-thumbnail.png)](https://www.youtube.com/watch?v=MvDFJclwr6A)

[![Introducing the new version of Arska; basic settings and creating rules using rule templates (Finnish, English subtitles)](https://github.com/Netgalleria/arska-node/blob/devel-ui/docs/img/youtube2_tn.png)](https://www.youtube.com/watch?v=BFsiXRxTFBo)

![Arska Diagram](https://github.com/Netgalleria/arska-node/blob/devel-ui/docs/img/Arska%20Node%20ESP32%20diagram%20202309.drawio.png)


Arska can control various electric switches connected to e.g. water heater and car chargers. It can also privide potential-free signal for temperature control for example to heat-pumps. Arska controls devices based on following data:
- Day-ahead electricity price per hour from [EntsoE](https://transparency.entsoe.eu/) . Price data is availabe from 25 European countries 🇦🇹 🇧🇪 🇧🇬 🇭🇷 🇨🇿 🇩🇪 🇩🇰 🇪🇪 🇫🇮 🇫🇷 🇬🇷 🇭🇺 🇮🇪 🇮🇹 🇱🇻 🇱🇹 🇳🇱 🇳🇴 🇵🇱 🇵🇹 🇷🇴 🇸🇪 🇷🇸 🇸🇰 🇸🇮 🇪🇸 🇨🇭. Optional price data source [Elering](https://dashboard.elering.ee/assets/api-doc.html)  provides prices for Estonia, Finland, Lithuania and Latvia. 
- Grid Energy Metering, supports HAN P1-port (with [Homewizard Wi-Fi P1 Meter]([url](https://www.homewizard.com/shop/wi-fi-p1-meter/)) tested in Finland), Shelly 3 EM and Shelly Pro 3 EM meters
- Energy Production Metering, supports selected Fronius and SMA inverters
- Current date and time, temperature sensor values
- Local solar forecast and Finnish wind power forecast from [Finnish Meteorological Institute (FMI)](https://www.ilmatieteenlaitos.fi/aurinko-ja-tuulivoimaennuste), currently available in Finland 🇫🇮

## More information:
- [Arska Wiki](https://github.com/Netgalleria/arska-node/wiki) 
- [Discussions, English or Finnish](https://github.com/Netgalleria/arska-node/discussions) 
- [Arska on X](https://twitter.com/ArskaEnergy)

# Current status
The software is under development (beta testing). 
# License 
The software is licenced under GPL v.3 license. For other licencing options contact olli@netgalleria.fi .


