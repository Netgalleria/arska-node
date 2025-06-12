***Documentation and tutorials in [Arska wiki](/wiki).*** 
***Lue Arskasta taustatieotja suomeksi [Arska.info 🇫🇮](https://arska.info)***

# Arska

**Make your energy purchases greener and cut costs with Arska!**  
This cloud-free application optimizes energy consumption and battery charging through **demand-side flexibility**—maximizing the use of **self-produced solar energy** and shifting purchases to **cheaper, lower-carbon-intensive hours**.

## Key Features

Arska helps you manage your energy efficiently by controlling loads based on **real-time consumption and production data, electricity prices, and solar forecasts**. It runs on various **ESP32 microcontroller devices** and supports:

- On-board **GPIO relays**
- **Shelly (Gen1 & Gen2)** and **Tasmota Wi-Fi relays**
- **Fronius GEN24 hybrid inverter charging states**
- **15-minute Market Time Unit (MTU) support** (Version 1.3)

### Intelligent Device Control

Arska can manage multiple electric switches, including those connected to **water heaters, heat pumps**, and other appliances. It controls devices with channel rules using various data sources:

- **Day-ahead electricity price per period** (1-hour/15-minute intervals) from [EntsoE](https://transparency.entsoe.eu/) (covering 25 European countries 🇦🇹 🇧🇪 🇧🇬 🇭🇷 🇨🇿 🇩🇪 🇩🇰 🇪🇪 🇫🇮 🇫🇷 🇬🇷 🇭🇺 🇮🇪 🇮🇹 🇱🇻 🇱🇹 🇳🇱 🇳🇴 🇵🇱 🇵🇹 🇷🇴 🇸🇪 🇷🇸 🇸🇰 🇸🇮 🇪🇸 🇨🇭).  
  [Elering](https://dashboard.elering.ee/assets/api-doc.html) provides backup price data for Estonia, Finland, Lithuania, and Latvia.
- **Grid energy metering** (Supports HAN P1 port meters and Shelly 3 EM) – [Read more](https://github.com/Netgalleria/arska-node/wiki/Energy-Meter-configuration).
- **Energy production metering** (Compatible with selected Fronius and SMA inverters).
- **Local solar forecasts** using Open Meteo and Finnish Meteorological Institute data.
- **Finnish wind power forecasts** from [Finnish Meteorological Institute (FMI)](https://www.ilmatieteenlaitos.fi/aurinko-ja-tuulivoimaennuste).
- **Current date, time, and temperature sensor values** for enhanced automation.


## Intro Videos 
- [Arska power manager - installation and basic configuration](https://www.youtube.com/watch?v=MvDFJclwr6A)  
    - Old video (old UI), but explains the basics
    - Basic hardware, relays
    - Software installation
    - English with Finnish subtitles.
- [Introducing the new version of Arska; basic settings and creating rules using rule templates)](https://www.youtube.com/watch?v=BFsiXRxTFBo)
    - Rules and rule templates
    - Renevable energy forecasts
    - Finnish, English subtitles
- [Arska version 1.1 introduction, LilyGo relay card and HAN P1 adapter introduction](https://www.youtube.com/watch?v=tg8wLuIKFNg)
    - HAN P1 adapter setup
    - Load management
    - Finnish, English subtitles. 

## More information:
- [Arska Wiki](https://github.com/Netgalleria/arska-node/wiki) 
- [Discussions, English or Finnish](https://github.com/Netgalleria/arska-node/discussions) 
- [Arska on X](https://twitter.com/ArskaEnergy)


![Arska Data flow diagra](https://github.com/user-attachments/assets/081bad3f-6193-498b-bff7-2257e07019e9)

*Arska Data flows*


# Current status
The software is under development. Use beta version for newest features. Stable/release candidate versions are recommeded for production environments.   
# License 
The software is licenced under GPL v.3 license. For other licencing options contact olli@netgalleria.fi .


