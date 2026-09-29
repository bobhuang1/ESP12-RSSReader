# ESP12-RSSReader
ESP12-RSSReader<br />
ESP8266 project (ESP-12 module) with WIFI.<br />
It can display current date/time, live temperature and humidity using a DHT module, plus local weather conditions and a 3-day forecast from WeatherAPI.com.<br />
See wiring.txt for interface details between EPS8266 and all peripherals.<br />
<img src="https://github.com/bobhuang1/ESP12-RSSReader/blob/master/12864Panel5Sample.jpg" alt="Picture"><br />

## Build notes (2026 update)
The original sketch used the HeWeather (和风天气) client (`HeWeatherCurrent.h` /
`GarfieldCommon.h`) and an RSS news feed. Those headers were never committed, so
the repo had stopped compiling, and HeWeather's free API has changed. The sketch
was migrated to the [WeatherApiWeather](https://github.com/bobhuang1/esp8266-weather-WeatherApi)
client and the shared modules from
[ESP8266-Functions-Common](https://github.com/bobhuang1/ESP8266-Functions-Common)
(copies of which are bundled here); the news-feed feature was dropped. If you
need the old HeWeather version, check out a commit before 2026-09.

### Setup
1. Set your WiFi credentials (or enable `USE_WIFI_MANAGER`) and
   `WEATHERAPI_LOCATION` at the top of the sketch.
2. Get a free API key at https://www.weatherapi.com/ and put it in
   `WEATHERAPI_APP_ID`.
3. `DISPLAY_TYPE` selects the panel variant (see wiring.txt).
4. The `data/` folder holds SPIFFS image assets; upload with the ESP8266FS tool
   if your build uses them.
