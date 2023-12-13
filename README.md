***More detailed documentation and tutorials in [Arska wiki](/wiki).*** 
***Lue Arskasta taustatieotja suomeksi [Arska.info 🇫🇮](https://arska.info)***


# Arska
Arska makes your energy purchases greener and saves on your energy bill by demand-side flexibility, i.e. maximising usage of self produced (solar) energy and shifting energy purchase to cheapest and lower carbon intensive hours.

Arska is a ESP32 microcontroller based application for managing energy consumption. It can switch on and of loads based on measured consumption and production information as well as price and solar energy forecast. 

Arska can control various electric switches connected to e.g. water heater and car chargers. It can also privide potential-free signal for temperature control for example to heat-pumps. Arska controls devices based on following data:
- Day-ahead electricity price per hour from [EntsoE](https://transparency.entsoe.eu/) . Price data is availabe from 25 European countries 🇦🇹 🇧🇪 🇧🇬 🇭🇷 🇨🇿 🇩🇪 🇩🇰 🇪🇪 🇫🇮 🇫🇷 🇬🇷 🇭🇺 🇮🇪 🇮🇹 🇱🇻 🇱🇹 🇳🇱 🇳🇴 🇵🇱 🇵🇹 🇷🇴 🇸🇪 🇷🇸 🇸🇰 🇸🇮 🇪🇸 🇨🇭. Optional price data source [Elering](https://dashboard.elering.ee/assets/api-doc.html)  provides prices for Estonia, Finland, Lithuania and Latvia. 
- Grid energy metering, supports meters with HAN P1 port and Shelly 3 EM, [read more](/wiki/Energy-Meter-configuration) 
- Energy production metering, supports selected Fronius and SMA inverters
- Current date and time, temperature sensor values
- Local solar forecast and Finnish wind power forecast from [Finnish Meteorological Institute (FMI)](https://www.ilmatieteenlaitos.fi/aurinko-ja-tuulivoimaennuste), currently available in Finland 🇫🇮

 
## Intro Videos 

[![Arska power manager - installation and basic configuration](https://github.com/Netgalleria/arska-node/assets/1752838/3d59ae0b-1a02-47c2-97b4-c399720c3787)](https://www.youtube.com/watch?v=MvDFJclwr6A)]

[![Introducing the new version of Arska; basic settings and creating rules using rule templates (Finnish, English subtitles)](https://github.com/Netgalleria/arska-node/assets/1752838/39256ebc-ec60-4826-a3b2-3e6431a00345)](https://www.youtube.com/watch?v=BFsiXRxTFBo)

![Arska Data flow diagram](https://github.com/Netgalleria/arska-node/assets/1752838/55de61ea-fe19-416d-bdfe-feb6becbf8b7)


## More information:
- [Arska Wiki](https://github.com/Netgalleria/arska-node/wiki) 
- [Discussions, English or Finnish](https://github.com/Netgalleria/arska-node/discussions) 
- [Arska on X](https://twitter.com/ArskaEnergy)

# Current status
The software is under development. Use beta version for newest features. Stable/release candidate versions are recommeded for production environments.   
# License 
The software is licenced under GPL v.3 license. For other licencing options contact olli@netgalleria.fi .


