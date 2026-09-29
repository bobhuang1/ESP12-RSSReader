#include <DHT.h>
#include <DHT_U.h>
#include <ESP8266WiFi.h>
#include <ESPHTTPClient.h>
#include <JsonListener.h>
#include <stdio.h>
#include <time.h>                   // struct timeval
#include <coredecls.h>                  // settimeofday_cb()
#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>
#include <WiFiManager.h>
#include "FS.h"
#include "WeatherApiWeather.h"
#include "StringHelpers.h"
#include "AlarmBeeper.h"
#include "BacklightController.h"
#include "WiFiMultiConnect.h"
#include "WeatherDisplayHelpers.h"
#include "BootSplashBitmap.h"

// HISTORY: this sketch originally used the HeWeather (和风天气) client
// (HeWeatherCurrent.h / GarfieldCommon.h, never committed to this repo) plus a
// set of pre-library helper functions. HeWeather's free API has since changed
// and the missing headers made the repo unbuildable, so the sketch was
// migrated to the WeatherApiWeather client
// (https://github.com/bobhuang1/esp8266-weather-WeatherApi) and the shared
// modules from ESP8266-Functions-Common. The RSS news feed feature was
// dropped at the same time (it was tied to the old helper generation and to
// two brittle third-party feed layouts); the device now cycles local
// conditions and the 3-day forecast, like the WeatherStation sketch.
#define CURRENT_VERSION 6
#define DEBUG
//#define USE_WIFI_MANAGER     // disable to NOT use WiFi manager, enable to use
#define LANGUAGE_CN  // LANGUAGE_CN or LANGUAGE_EN
#define USE_HIGH_ALARM       // disable - LOW alarm sounds, enable - HIGH alarm sounds. Enable for all serials.

// Use 1 for serial 400-406 (400.bin), 2 for serial 407 (407.bin)!!!
#define DISPLAY_TYPE 1   // 1-BIG 12864, 2-MINI 12864, 3-New Big BLUE 12864, to use 3, you must change u8x8_d_st7565.c as well!!!, 4- New BLUE 12864-ST7920

// Serial 400 to 407
bool dummyMode = false;
bool backlightOffMode = false;
int displayContrast = 135;
int displayMultiplier = 200;
int displayBias = 25;
int displayMinimumLevel = 1;
int displayMaximumLevel = 1023;
int temperatureMultiplier = 100;
int temperatureBias = 0;
int humidityMultiplier = 76;
int humidityBias = 0;

// Fill in your own SSID/password pairs (or better, use USE_WIFI_MANAGER above
// instead of hardcoding any of this). Never commit real WiFi credentials.
const char* const WIFI_SSIDS[] = {"YOUR_SSID_1", "YOUR_SSID_2", "YOUR_SSID_3"};
const char* const WIFI_PASSWORDS[] = {"YOUR_PASSWORD_1", "YOUR_PASSWORD_2", "YOUR_PASSWORD_3"};

const String WEATHERAPI_APP_ID = "YOUR_WEATHERAPI_COM_KEY"; // https://www.weatherapi.com/
#define MAX_FORECASTS 5

#define DHTTYPE  DHT11       // Sensor type DHT11/21/22/AM2301/AM2302
#define BUTTONPIN   4
#define DHTPIN   2 // 2, -1
#define ALARMPIN 5
#define BACKLIGHTPIN 0 // 2, 0

#if DISPLAY_TYPE == 3
#define BIGBLUE12864
#endif

#ifdef LANGUAGE_CN
const String WEATHERAPI_LANGUAGE = "zh"; // zh for Chinese, en for English
#else
const String WEATHERAPI_LANGUAGE = "en"; // zh for Chinese, en for English
#endif

#ifdef USE_WIFI_MANAGER
const String WEATHERAPI_LOCATION = "auto:ip"; // WeatherAPI.com: resolve location from the request's IP address
#else
const String WEATHERAPI_LOCATION = "YOUR_CITY"; // e.g. "London", "New York", or "lat,lon" - see WeatherAPI.com docs
#endif

#ifdef LANGUAGE_CN
const String WDAY_NAMES[] = { "星期天", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六" };
#else
const String WDAY_NAMES[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
#endif

#if (DHTPIN >= 0)
DHT dht(DHTPIN, DHTTYPE);
#endif

WeatherApiCurrentData currentWeather;
WeatherApiForecastData forecasts[MAX_FORECASTS];
WeatherApiWeather weatherClient;

BacklightController backlight;

#if DISPLAY_TYPE == 1
U8G2_ST7565_LM6059_F_4W_SW_SPI display(U8G2_R2, /* clock=*/ 14, /* data=*/ 12, /* cs=*/ 13, /* dc=*/ 15, /* reset=*/ 16); // U8G2_ST7565_LM6059_F_4W_SW_SPI
#endif

#if DISPLAY_TYPE == 2
U8G2_ST7565_64128N_F_4W_SW_SPI display(U8G2_R0, /* clock=*/ 14, /* data=*/ 12, /* cs=*/ 13, /* dc=*/ 15, /* reset=*/ 16); // U8G2_ST7565_64128N_F_4W_SW_SPI
#endif

#if DISPLAY_TYPE == 3
U8G2_ST7565_64128N_F_4W_SW_SPI display(U8G2_R2, /* clock=*/ 14, /* data=*/ 12, /* cs=*/ 13, /* dc=*/ 15, /* reset=*/ 16); // U8G2_ST7565_64128N_F_4W_SW_SPI
#endif

#if DISPLAY_TYPE == 4
U8G2_ST7920_128X64_F_SW_SPI display(U8G2_R2, /* clo  ck=*/ 14 /* A4 */ , /* data=*/ 12 /* A2 */, /* CS=*/ 16 /* A3 */, /* reset=*/ U8X8_PIN_NONE); // 16, U8X8_PIN_NONE
#endif

time_t nowTime;
const String degree = String((char)176);
bool readyForWeatherUpdate = false;
long timeSinceLastWUpdate = 0;
float previousTemp = 0;
float previousHumidity = 0;

// Page cycling: 0-1 local conditions, 2-3 forecast day 1, 4-5 day 2, 6-7 day 3
long timeSinceLastPageUpdate = 0;
#define PAGE_UPDATE_INTERVAL 10*1000
#define UPDATE_INTERVAL_SECS 1500
int draw_state = 1;

int buttonState;             // the current reading from the input pin
int lastButtonState = LOW;   // the previous reading from the input pin
// the following variables are unsigned longs because the time, measured in
// milliseconds, will quickly become a bigger number than can be stored in an int.
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
const unsigned long debounceDelay = 30;    // the debounce time; increase if the output flickers

void setup() {
  delay(100);
  Serial.begin(115200);
#ifdef DEBUG
  Serial.println("Begin");
#endif
  backlight.begin(BACKLIGHTPIN);
  adjustBacklightSub();

#if (DHTPIN >= 0)
  dht.begin();
#endif

  pinMode(BUTTONPIN, INPUT);
  pinMode(ALARMPIN, OUTPUT);
  beepOff(ALARMPIN,
#ifdef USE_HIGH_ALARM
         true
#else
         false
#endif
        );

  display.begin();
  display.setFontPosTop();
  setContrastSub();

  display.clearBuffer();
  display.drawXBM(31, 0, 66, 64, garfield);
  display.sendBuffer();
  beepShort(ALARMPIN,
#ifdef USE_HIGH_ALARM
            true
#else
            false
#endif
           );
  delay(1000);

  drawProgress(String(CompileDate), String(CURRENT_VERSION));
  delay(1000);

  drawProgress("Backlight Level", "Test");
  backlight.selfTest();

#ifdef USE_WIFI_MANAGER
  drawProgress("连接WIFI:", "ESP8266-Setup");
  connectWiFiWithManager("ESP8266-Setup");
#else
  drawProgress("连接WIFI中,", "请稍等...");
  connectWiFi(WIFI_SSIDS, WIFI_PASSWORDS, 3);
#endif

  if (WiFi.status() != WL_CONNECTED) ESP.restart();

  // Get time from network time service
#ifdef DEBUG
  Serial.println("WIFI Connected");
#endif
  drawProgress("连接WIFI成功,", "正在同步时间...");
  configTime(TZ_SEC_FOR(8), DST_SEC_FOR(0), DefaultNtpServer);
  setContrastSub();
  drawProgress("同步时间成功,", "正在更新天气数据...");
  updateData(true);
  timeSinceLastWUpdate = millis();
  timeSinceLastPageUpdate = millis();
}

void detectButtonPush() {
  int reading;
  reading = digitalRead(BUTTONPIN);
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > debounceDelay)
  {
    if (reading != buttonState)
    {
      buttonState = reading;
      if (buttonState == HIGH)
      {
        beepShort(ALARMPIN,
#ifdef USE_HIGH_ALARM
                  true
#else
                  false
#endif
                 );
        draw_state++;
        timeSinceLastPageUpdate = millis();
      }
    }
  }
  lastButtonState = reading;
}

void setContrastSub() {
  if (displayContrast > 0)
  {
    display.setContrast(displayContrast);
    Serial.print("Set displayContrast to: ");
    Serial.println(displayContrast);
    Serial.println();
  }
}

void adjustBacklightSub() {
  backlight.update(displayBias, displayMultiplier);
}

void loop() {
  if (backlightOffMode)
  {
    nowTime = time(nullptr);
    struct tm* timeInfo;
    timeInfo = localtime(&nowTime);
    if (timeInfo->tm_hour >= 0 && timeInfo->tm_hour < 7)
    {
      backlight.turnOff(displayMinimumLevel);
    }
    else
    {
      adjustBacklightSub();
    }
  }
  else
  {
    adjustBacklightSub();
  }
  detectButtonPush();

  display.firstPage();
  do {
    draw();
  } while ( display.nextPage() );

  if (millis() - timeSinceLastWUpdate > (1000 * UPDATE_INTERVAL_SECS)) {
    readyForWeatherUpdate = true;
    timeSinceLastWUpdate = millis();
  }

  if (millis() - timeSinceLastPageUpdate > PAGE_UPDATE_INTERVAL)
  {
    timeSinceLastPageUpdate = millis();
    draw_state++;
    if (draw_state >= 8) draw_state = 0;
  }

#if (DHTPIN >= 0)
  if (dht.read())
  {
    float fltHumidity = dht.readHumidity() * humidityMultiplier / 100 + humidityBias;
    float fltCTemp = dht.readTemperature() * temperatureMultiplier / 100 + temperatureBias;
#ifdef DEBUG
    Serial.print("Humidity: ");
    Serial.println(fltHumidity);
    Serial.print("CTemp: ");
    Serial.println(fltCTemp);
#endif
    if (isnan(fltCTemp) || isnan(fltHumidity))
    {
    }
    else
    {
      previousTemp = fltCTemp;
      if (fltHumidity <= 100)
      {
        previousHumidity = fltHumidity;
      }
      else
      {
        previousHumidity = 100;
      }
    }
  }
#endif

  if (readyForWeatherUpdate) {
    updateData(false);
  }
}

void draw(void) {
  // Skip to tomorrow's forecast once today's date no longer matches, or after
  // 8 PM on the matching day (same rule as the WeatherStation sketch).
  int forecastBase = 0;
  nowTime = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&nowTime);
  String strTempDate = String(forecasts[0].date);
  strTempDate.trim(); // 2018-08-09
  int day = (strTempDate.substring(8)).toInt();
  if (timeInfo->tm_mday != day)
  {
    forecastBase = 1;
  }
  else if (timeInfo->tm_mday == day && timeInfo->tm_hour > 19)
  {
    forecastBase = 1;
  }

  if (dummyMode)
  {
    draw_state = 1;
  }
  if (draw_state < 2)
  {
    drawLocal();
  }
  else if (draw_state < 4)
  {
    drawForecastDetails(0 + forecastBase);
  }
  else if (draw_state < 6)
  {
    drawForecastDetails(1 + forecastBase);
  }
  else if (draw_state < 8)
  {
    drawForecastDetails(2 + forecastBase);
  }
  else
  {
    draw_state = 0;
  }
}

void updateData(bool isInitialBoot) {
  if (isInitialBoot)
  {
    drawProgress("正在更新...", "本地天气实况...");
  }
  // WeatherAPI.com's forecast.json returns current conditions + forecast in one
  // request, so both are refreshed together on every update.
  weatherClient.updateWeather(&currentWeather, forecasts, WEATHERAPI_APP_ID, WEATHERAPI_LOCATION, WEATHERAPI_LANGUAGE, MAX_FORECASTS);
  readyForWeatherUpdate = false;
}

void drawProgress(String labelLine1, String labelLine2) {
  display.clearBuffer();
  display.enableUTF8Print();
  display.setFont(u8g2_font_wqy12_t_gb2312); // u8g2_font_wqy12_t_gb2312, u8g2_font_helvB08_tf
  int stringWidth = 1;
  if (labelLine1 != "")
  {
    stringWidth = display.getUTF8Width(string2char(labelLine1));
    display.setCursor((128 - stringWidth) / 2, 13);
    display.print(labelLine1);
  }
  if (labelLine2 != "")
  {
    stringWidth = display.getUTF8Width(string2char(labelLine2));
    display.setCursor((128 - stringWidth) / 2, 36);
    display.print(labelLine2);
  }
  display.disableUTF8Print();
  display.sendBuffer();
}

void drawLocal() {
  nowTime = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&nowTime);
  char buff[20];

  display.enableUTF8Print();
  display.setFont(u8g2_font_wqy12_t_gb2312); // u8g2_font_wqy12_t_gb2312, u8g2_font_helvB08_tf
  String stringText = String(timeInfo->tm_year + 1900) + "年" + String(timeInfo->tm_mon + 1) + "月" + String(timeInfo->tm_mday) + "日 " + WDAY_NAMES[timeInfo->tm_wday].c_str();
  int stringWidth = display.getUTF8Width(string2char(stringText));
  display.setCursor((128 - stringWidth) / 2, 1);
  display.print(stringText);
  stringWidth = display.getUTF8Width(string2char(String(currentWeather.text)));
  display.setCursor((128 - stringWidth) / 2, 40);
  display.print(String(currentWeather.text));
  String WindDirectionAndSpeed = translateWindDirectionToChinese(currentWeather.wind_dir) + String(currentWeather.wind_kph) + "km/h";
  stringWidth = display.getUTF8Width(string2char(WindDirectionAndSpeed));
  display.setCursor((128 - stringWidth) / 2, 54);
  display.print(WindDirectionAndSpeed);
  display.disableUTF8Print();
  display.setFont(u8g2_font_helvR24_tn); // u8g2_font_inb21_ mf, u8g2_font_helvR24_tn
  sprintf_P(buff, PSTR("%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min);
  stringWidth = display.getStrWidth(buff);
  display.drawStr((128 - 30 - stringWidth) / 2, 11, buff);

  display.setFont(Meteocon21);
  display.drawStr(98, 17, string2char(chooseMeteoconChar(currentWeather.iconMeteoCon)));

  display.setFont(u8g2_font_helvR08_tf);
  String temp = String(currentWeather.temp_c, 0) + degree + "C";
  display.drawStr(0, 53, string2char(temp));

  display.setFont(u8g2_font_helvR08_tf);
  stringWidth = display.getStrWidth(string2char((String(currentWeather.humidity) + "%")));
  display.drawStr(127 - stringWidth, 53, string2char((String(currentWeather.humidity) + "%")));

  display.setFont(u8g2_font_helvB08_tf);
  if (previousTemp != 0 && previousHumidity != 0)
  {
    display.drawStr(0, 39, string2char(String(previousTemp, 0) + degree + "C"));
  }

  if (previousTemp != 0 && previousHumidity != 0)
  {
    int stringWidth = display.getStrWidth(string2char(String(previousHumidity, 0) + "%"));
    display.drawStr(128 - stringWidth, 39, string2char(String(previousHumidity, 0) + "%"));
  }
  display.drawHLine(0, 51, 128);
}

void drawForecastDetails(int dayIndex) {
  if (dayIndex < 0 || dayIndex >= MAX_FORECASTS) dayIndex = 0;
  String strTempDate = String(forecasts[dayIndex].date);
  strTempDate.trim(); // 2018-08-09
  int year = (strTempDate.substring(0, 4)).toInt();
  int month = (strTempDate.substring(5, 7)).toInt();
  int day = (strTempDate.substring(8)).toInt();
  struct tm *idotmpstruct, timetmps32;
  time_t observationTimestamp;
  observationTimestamp = 0;
  idotmpstruct = &timetmps32;
  idotmpstruct->tm_year = year - 1900;
  idotmpstruct->tm_mon = month - 1;
  idotmpstruct->tm_mday = day;
  idotmpstruct->tm_hour = 1;
  idotmpstruct->tm_min = 0;
  idotmpstruct->tm_sec = 0;
  idotmpstruct->tm_wday = 0;  /*/dummy */
  observationTimestamp = mktime(idotmpstruct);   /*/elllit timestamp */
  struct tm* timeInfo;
  timeInfo = localtime(&observationTimestamp);

  display.enableUTF8Print();
  display.setFont(u8g2_font_wqy12_t_gb2312); // u8g2_font_wqy12_t_gb2312, u8g2_font_helvB08_tf
  String stringText = " " + String(timeInfo->tm_mon + 1) + "月" + String(timeInfo->tm_mday) + "日 " + String(WDAY_NAMES[timeInfo->tm_wday].c_str());
  int stringWidth = display.getUTF8Width(string2char(stringText));
  display.setCursor(0, 1);
  display.print(stringText);

  // WeatherAPI.com's daily forecast has one overall condition, not a separate
  // day/night pair.
  stringText = String("天气:" + forecasts[dayIndex].text);
  stringText.replace("\"", "");
  stringText.trim();
  if (stringText.length() > 21)
  {
    stringText = stringText.substring(0, 21);
    stringText.trim();
  }
  display.setCursor(26, 24);
  display.print(stringText);

  // WeatherAPI.com's daily forecast gives a peak wind speed only, no
  // direction (direction is only available at hourly granularity).
  stringText = String(forecasts[dayIndex].maxwind_kph, 0) + "km/h";
  stringWidth = display.getUTF8Width(string2char(stringText));
  display.setCursor(0, 54);
  display.print(stringText);
  display.disableUTF8Print();

  display.setFont(u8g2_font_helvR08_tf);
  stringText = String(forecasts[dayIndex].avghumidity) + "%";
  stringWidth = display.getStrWidth(string2char(stringText));
  display.drawStr(128 - stringWidth, 1, string2char(stringText));

  stringText = String(forecasts[dayIndex].maxtemp_c, 0) + degree + "C";
  stringWidth = display.getStrWidth(string2char(stringText));
  display.drawStr(128 - stringWidth, 18, string2char(stringText));

  stringText = String(forecasts[dayIndex].mintemp_c, 0) + degree + "C";
  stringWidth = display.getStrWidth(string2char(stringText));
  display.drawStr(128 - stringWidth, 35, string2char(stringText));

  stringText = String(String(forecasts[dayIndex].totalprecip_mm, 1) + "mm") + "  " + String(forecasts[dayIndex].chanceOfRain) + "%";
  stringWidth = display.getStrWidth(string2char(stringText));
  display.drawStr(128 - stringWidth, 53, string2char(stringText));

  display.setFont(Meteocon21);
  display.drawStr(0, 19, string2char(String(forecasts[dayIndex].iconMeteoCon).substring(0, 1)));
}
