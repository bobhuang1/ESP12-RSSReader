#include <DHT.h>
#include <DHT_U.h>
#include <ESP8266WiFi.h>
#include <ESPHTTPClient.h>
#include <JsonListener.h>
#include <stdio.h>
#include <time.h>                   // struct timeval
#include <coredecls.h>                  // settimeofday_cb()
#include <Timezone.h>
#include <Arduino.h>
#include <U8g2lib.h>
#include <SPI.h>
#include <WiFiManager.h>
#include "FS.h"
#include "HeWeatherCurrent.h"
#include "GarfieldCommon.h"

//#define DEBUG
#define DISPLAY_TYPE 2   // 1-BIG 12864, 2-MINI 12864
//#define USE_WIFI_MANAGER     // disable to NOT use WiFi manager, enable to use
//#define SHOW_US_CITIES  // disable to NOT to show Fremont and NY, enable to show - do NOT use, causes heap to overflow
#define USE_HIGH_ALARM       // disable - LOW alarm sounds, enable - HIGH alarm sounds
#define LANGUAGE_CN  // LANGUAGE_CN or LANGUAGE_EN
#define BACKLIGHT_OFF_MODE // turn off backlight between 0:00AM and 7:00AM

#define DHTTYPE  DHT11       // Sensor type DHT11/21/22/AM2301/AM2302
#define BUTTONPIN   4
#define DHTPIN   2 // 2, -1
#define ALARMPIN 5
#define BACKLIGHTPIN 0 // 2, 0

#ifdef LANGUAGE_CN
const String HEWEATHER_LANGUAGE = "zh"; // zh for Chinese, en for English
#else ifdef LANGUAGE_EN
const String HEWEATHER_LANGUAGE = "en"; // zh for Chinese, en for English
#endif

#ifdef USE_WIFI_MANAGER
const String HEWEATHER_LOCATION = "auto_ip"; // Get location from IP address
#else
const String HEWEATHER_LOCATION = "CN101210202"; // Changxing
#endif

#ifdef SHOW_US_CITIES
const String HEWEATHER_LOCATION1 = "US3290117";
const String HEWEATHER_LOCATION2 = "US5392171";
#endif

#ifdef LANGUAGE_CN
const String WDAY_NAMES[] = { "星期天", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六" };
#else ifdef LANGUAGE_EN
const String WDAY_NAMES[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };
#endif

#ifdef SHOW_US_CITIES
// Japan, Tokyo
TimeChangeRule japanRule = { "Japan", Last, Sun, Mar, 1, 540 };     // Japan
Timezone Japan(japanRule);
// Central European Time (Frankfurt, Paris)
TimeChangeRule CEST = { "CEST", Last, Sun, Mar, 2, 120 };     // Central European Summer Time
TimeChangeRule CET = { "CET ", Last, Sun, Oct, 3, 60 };       // Central European Standard Time
Timezone CE(CEST, CET);
// United Kingdom (London, Belfast)
TimeChangeRule BST = { "BST", Last, Sun, Mar, 1, 60 };        // British Summer Time
TimeChangeRule GMT = { "GMT", Last, Sun, Oct, 2, 0 };         // Standard Time
Timezone UK(BST, GMT);
// UTC
TimeChangeRule utcRule = { "UTC", Last, Sun, Mar, 1, 0 };     // UTC
Timezone UTC(utcRule);
// US Eastern Time Zone (New York, Detroit)
TimeChangeRule usEDT = { "EDT", Second, Sun, Mar, 2, -240 };  // Eastern Daylight Time = UTC - 4 hours
TimeChangeRule usEST = { "EST", First, Sun, Nov, 2, -300 };   // Eastern Standard Time = UTC - 5 hours
Timezone usET(usEDT, usEST);
// US Central Time Zone (Chicago, Houston)
TimeChangeRule usCDT = { "CDT", Second, Sun, Mar, 2, -300 };
TimeChangeRule usCST = { "CST", First, Sun, Nov, 2, -360 };
Timezone usCT(usCDT, usCST);
// US Mountain Time Zone (Denver, Salt Lake City)
TimeChangeRule usMDT = { "MDT", Second, Sun, Mar, 2, -360 };
TimeChangeRule usMST = { "MST", First, Sun, Nov, 2, -420 };
Timezone usMT(usMDT, usMST);
// Arizona is US Mountain Time Zone but does not use DST
Timezone usAZ(usMST);
// US Pacific Time Zone (Las Vegas, Los Angeles)
TimeChangeRule usPDT = { "PDT", Second, Sun, Mar, 2, -420 };
TimeChangeRule usPST = { "PST", First, Sun, Nov, 2, -480 };
Timezone usPT(usPDT, usPST);
#endif

#if (DHTPIN >= 0)
DHT dht(DHTPIN, DHTTYPE);
#endif

HeWeatherCurrentData currentWeather;
HeWeatherCurrent currentWeatherClient;

#ifdef SHOW_US_CITIES
HeWeatherCurrentData currentWeather1;
HeWeatherCurrentData currentWeather2;
HeWeatherCurrent currentWeatherClient1;
HeWeatherCurrent currentWeatherClient2;
#endif

#if DISPLAY_TYPE == 1
U8G2_ST7565_LM6059_F_4W_SW_SPI display(U8G2_R2, /* clock=*/ 14, /* data=*/ 12, /* cs=*/ 13, /* dc=*/ 15, /* reset=*/ 16); // U8G2_ST7565_LM6059_F_4W_SW_SPI
#else if DISPLAY_TYPE == 2
U8G2_ST7565_64128N_F_4W_SW_SPI display(U8G2_R0, /* clock=*/ 14, /* data=*/ 12, /* cs=*/ 13, /* dc=*/ 15, /* reset=*/ 16); // U8G2_ST7565_64128N_F_4W_SW_SPI
#endif

time_t nowTime;
const String degree = String((char)176);
bool readyForWeatherUpdate = false;
long timeSinceLastWUpdate = 0;
float previousTemp = 0;
float previousHumidity = 0;
int lightLevel[10];
int draw_state = 1; // 0 - Garfield,  1 - RSS page, 2 - Local clock 3 - Fremont clock, 4 - New York clock

long timeSinceLastPageUpdate = 0;
#define PAGE_UPDATE_INTERVAL 10*1000
int buttonState;             // the current reading from the input pin
int lastButtonState = LOW;   // the previous reading from the input pin
// the following variables are unsigned longs because the time, measured in
// milliseconds, will quickly become a bigger number than can be stored in an int.
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
const unsigned long debounceDelay = 30;    // the debounce time; increase if the output flickers

#if DISPLAY_TYPE == 1
#define LONGBUTTONPUSH 30
#else if DISPLAY_TYPE == 2
#define LONGBUTTONPUSH 80
#endif

int buttonPushCounter = 0;
int majorMode = 0; // 0-Clock/News Mode, 1-Math Mode, 2-80 Poems, 3-300Poems-not possible out of memory

int questionCount = 0;
const int questionTotal = 100;
int currentMode = 0; // 0 - show question, 1 - show answer
String currentQuestion = "";
String currentAnswer = "";

int lineCount = 0;
int lineTotal = 1;
int poemCount = 1;
const int poemTotal = 10;
#define TOTAL_POEMS  96 // Total number of poems
#define MAXIMUM_POEM_SIZE 11 //11 for 80 poems, 121 for 300 poems

String poemText[MAXIMUM_POEM_SIZE];
int currentPoem = 1;
bool readPoem = false;

#define NEWS_POLITICS_SIZE 10
#define NEWS_WORLD_SIZE 20
#define NEWS_ENGLISH_SIZE 10
String newsText[NEWS_POLITICS_SIZE + NEWS_WORLD_SIZE + NEWS_ENGLISH_SIZE];


void setup() {
  delay(100);
  Serial.begin(115200);
#ifdef DEBUG
  Serial.println("Begin");
#endif
  initializeBackLightArray(lightLevel, BACKLIGHTPIN);

#if (DHTPIN >= 0)
  dht.begin();
#endif

  pinMode(BUTTONPIN, INPUT);
  pinMode(ALARMPIN, OUTPUT);
  noBeep(ALARMPIN,
#ifdef USE_HIGH_ALARM
         true
#else
         false
#endif
        );
  listSPIFFSFiles(); // Lists the files so you can see what is in the SPIFFS

  display.begin();
  display.setFontPosTop();
#if DISPLAY_TYPE == 1
  display.setContrast(135);
#endif
  display.clearBuffer();
  display.drawXBM(31, 0, 66, 64, garfield);
  display.sendBuffer();
  shortBeep(ALARMPIN,
#ifdef USE_HIGH_ALARM
            true
#else
            false
#endif
           );
  delay(1000);

  connectWIFI(
#ifdef USE_WIFI_MANAGER
    true
#else
    false
#endif
  );

  if (WiFi.status() != WL_CONNECTED) ESP.restart();

  // Get time from network time service
#ifdef DEBUG
  Serial.println("WIFI Connected");
#endif
  drawProgress("连接WIFI成功,", "正在同步时间...");
  configTime(TZ_SEC, DST_SEC, "pool.ntp.org");
  drawProgress("同步时间成功,", "正在更新天气数据...");
  updateData(true);
  timeSinceLastWUpdate = millis();
  timeSinceLastPageUpdate = millis();
}

void detectButtonPush() {
  int reading;
  reading = digitalRead(BUTTONPIN);
  if (reading == HIGH)
  {
    buttonPushCounter++;
  }
  else
  {
    buttonPushCounter = 0;
  }
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
        shortBeep(ALARMPIN,
#ifdef USE_HIGH_ALARM
                  true
#else
                  false
#endif
                 );
        if (majorMode == 0)
        {
          draw_state++;
          timeSinceLastPageUpdate = millis();
        }
        else if (majorMode == 1)
        {
          if (currentMode == 0)
          {
            currentMode = 1;
          }
          else
          {
            currentMode = 0;
            currentQuestion = generateMathQuestion(currentAnswer);
            questionCount++;
            if (questionCount >= questionTotal + 1)
            {
              questionCount = 1;
            }
          }
        }
        else if (majorMode == 2)
        {
          lineCount++;
          if (lineCount >= lineTotal)
          {
            lineCount = 0;
            currentPoem = random(1, TOTAL_POEMS + 1);
            readPoem = true;
            poemCount++;
            if (poemCount >= poemTotal + 1)
            {
              poemCount = 0;
            }
          }
        }
      }
      else
      {
        buttonPushCounter = 0;
      }
    }
  }
  lastButtonState = reading;
}

void loop() {
  if (buttonPushCounter >= LONGBUTTONPUSH)
  {
    buttonPushCounter = 0;
    draw_state = 0;
    if (majorMode == 0)
    {
      majorMode = 1; // we are in Math mode, need to initialize all variables
      questionCount = 0;
      currentMode = 0; // 0 - show question, 1 - show answer
      currentQuestion = generateMathQuestion(currentAnswer);
    }
    else if (majorMode == 1)
    {
      majorMode = 2; // we are in 80 Poem mode
      currentPoem = random(1, TOTAL_POEMS + 1);
      lineCount = 0;
      poemCount = 1;
      readPoem = true;
    }
    else
    {
      majorMode = 0; // we are in Clock mode
    }
    longBeep(ALARMPIN,
#ifdef USE_HIGH_ALARM
             true
#else
             false
#endif
            );
  }

  if (majorMode == 2 && readPoem)
  {
    String strFileName = convertPoemNumberToFileName(currentPoem, TOTAL_POEMS);
    readPoemFromSPIFFS(strFileName, poemText, lineTotal);
    readPoem = false;
  }

#ifdef  BACKLIGHT_OFF_MODE
  nowTime = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&nowTime);
  if (timeInfo->tm_hour >= 0 && timeInfo->tm_hour < 7)
  {
    tunOffBacklight(BACKLIGHTPIN);
  }
  else
  {
    adjustBacklight(lightLevel, BACKLIGHTPIN);
  }
#else
  adjustBacklight(lightLevel, BACKLIGHTPIN);
#endif
  
  detectButtonPush();

  display.firstPage();
  do {
    draw();

    if (majorMode == 2)
    {
      draw_state++;
    }
  } while ( display.nextPage() );

  if (millis() - timeSinceLastWUpdate > (1000 * UPDATE_INTERVAL_SECS)) {
    readyForWeatherUpdate = true;
    timeSinceLastWUpdate = millis();
  }

  if (majorMode == 0)
  {
    if (millis() - timeSinceLastPageUpdate > PAGE_UPDATE_INTERVAL)
    {
      timeSinceLastPageUpdate = millis();
      draw_state++;
    }
  }
  else if (majorMode == 1)
  {
    if (draw_state >= 10)
    {
      draw_state = 0;
    }
  }
  else if (majorMode == 2)
  {
    if (draw_state >= 121)
    {
      draw_state = 0;
    }
  }

#if (DHTPIN >= 0)
  if (dht.read())
  {
    float fltHumidity = dht.readHumidity();
    float fltCTemp = dht.readTemperature() - 1;
#ifdef DEBUG
/*
    Serial.print("Humidity: ");
    Serial.println(fltHumidity);
    Serial.print("CTemp: ");
    Serial.println(fltCTemp);
*/
#endif
    if (isnan(fltCTemp) || isnan(fltHumidity))
    {
    }
    else
    {
      previousTemp = fltCTemp;
      previousHumidity = fltHumidity;
    }
  }
#endif

  if (readyForWeatherUpdate) {
    updateData(false);
  }
}

void draw(void) {
  detectButtonPush();
  if (majorMode == 0) // Clock mode
  {
    drawClock();
    detectButtonPush();
  }
  else if (majorMode == 1) // Math mode
  {
    drawMath();
  }
  else if (majorMode == 2) // Poem modes
  {
    drawPoem();
    detectButtonPush();
  }
}

void drawClock(void) {
  detectButtonPush();

  if (draw_state == 0) // 0 - Garfield,  1 to 20 - RSS page, 21 - Local clock 22 - Fremont clock, 23 - New York clock
  {
    display.drawXBM(31, 0, 66, 64, garfield);
  }
  else if (draw_state > 0 && draw_state < NEWS_POLITICS_SIZE + NEWS_WORLD_SIZE + NEWS_ENGLISH_SIZE + 1 )
  {
    if (draw_state < NEWS_ENGLISH_SIZE + 1)
    {
      drawEnglishNews(draw_state);
    }
    else
    {
      drawChineseNews(draw_state);
    }
  }
  else if (draw_state == NEWS_POLITICS_SIZE + NEWS_WORLD_SIZE + NEWS_ENGLISH_SIZE + 1)
  {
    drawLocal();
  }
  else if (draw_state == NEWS_POLITICS_SIZE + NEWS_WORLD_SIZE + NEWS_ENGLISH_SIZE + 2)
  {
#ifdef SHOW_US_CITIES
    drawWorldLocation("弗利蒙", usPT, currentWeather2);
#else
    draw_state = 1;
#endif
  }
  else if (draw_state == 23)
  {
#ifdef SHOW_US_CITIES
    drawWorldLocation("纽约", usET, currentWeather1);
#else
    draw_state = 1;
#endif
  }
  else
  {
    draw_state = 1;
  }
  detectButtonPush();
}

void updateData(bool isInitialBoot) {
  nowTime = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&nowTime);
  if (isInitialBoot)
  {
    drawProgress("正在更新...", "本地天气实况...");
  }
  currentWeatherClient.updateCurrent(&currentWeather, HEWEATHER_APP_ID, HEWEATHER_LOCATION, HEWEATHER_LANGUAGE);
  if (isInitialBoot || (timeInfo->tm_hour >= weatherBeginHour && timeInfo->tm_hour < weatherEndHour))
  {
#ifdef SHOW_US_CITIES
    delay(300);
    if (isInitialBoot)
    {
      drawProgress("正在更新...", "纽约天气实况...");
    }
    currentWeatherClient1.updateCurrent(&currentWeather1, HEWEATHER_APP_ID, HEWEATHER_LOCATION1, HEWEATHER_LANGUAGE);
    delay(300);
    if (isInitialBoot)
    {
      drawProgress("正在更新...", "弗利蒙天气实况...");
    }
    currentWeatherClient2.updateCurrent(&currentWeather2, HEWEATHER_APP_ID, HEWEATHER_LOCATION2, HEWEATHER_LANGUAGE);
#endif
  }

  delay(300);
  if (isInitialBoot)
  {
    drawProgress("正在更新...", "英语新闻...");
  }
  getEnglishNewsData();
  delay(300);
  if (isInitialBoot)
  {
    drawProgress("正在更新...", "中文新闻...");
  }
  getChineseNewsData();
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

void drawChineseNews(int currentNewsLine) {
  display.enableUTF8Print();
  display.setFont(u8g2_font_wqy12_t_gb2312); // u8g2_font_wqy12_t_gb2312, u8g2_font_helvB08_tf

  int charsPerLine = 30;
  String strTemp = newsText[currentNewsLine - 1];

  strTemp.trim();
  int stringLength = strTemp.length();

  if (stringLength == 0)
  {
    draw_state++;
    return;
  }
  if (stringLength > charsPerLine * 5 - 5)
  {
    strTemp = strTemp.substring(0, charsPerLine * 5 - 5);
    strTemp.trim();
  }
  int numOfLines = strTemp.length() / charsPerLine + 1;

  for (int i = 0; i < numOfLines; ++i)
  {
    int beginPostion = i * charsPerLine;
    int endPosition = (i + 1) * charsPerLine;
    if (beginPostion >= stringLength)
    {
      exit;
    }
    if (endPosition >= stringLength)
    {
      endPosition = stringLength;
    }
    String strTempLine = strTemp.substring(beginPostion, endPosition);
    strTempLine.trim();
    display.setCursor(0, i * 13 + 1);
    display.print(strTempLine);
    detectButtonPush();
  }

  String stringText = String(currentNewsLine) + "/" + String(NEWS_POLITICS_SIZE + NEWS_WORLD_SIZE + NEWS_ENGLISH_SIZE);
  int stringWidth = display.getUTF8Width(string2char(stringText));
  display.setCursor(128 - stringWidth, 53);
  display.print(stringText);

  display.disableUTF8Print();
}

void drawEnglishNews(int currentNewsLine) {
  display.setFont(u8g2_font_t0_12b_mf); // width 8, height 11 u8g2_font_t0_12b_mf, 6X11
  int charsPerLine = 128 / 6;
  String strTemp = newsText[currentNewsLine - 1];

  strTemp.trim();
  int stringLength = strTemp.length();

  if (stringLength == 0)
  {
    draw_state++;
    return;
  }
  if (stringLength > charsPerLine * 5 - 5)
  {
    strTemp = strTemp.substring(0, charsPerLine * 5 - 5);
    strTemp.trim();
  }
  int numOfLines = strTemp.length() / charsPerLine + 1;

  for (int i = 0; i < numOfLines; ++i)
  {
    int beginPostion = i * charsPerLine;
    int endPosition = (i + 1) * charsPerLine;
    if (beginPostion >= stringLength)
    {
      exit;
    }
    if (endPosition >= stringLength)
    {
      endPosition = stringLength;
    }
    String strTempLine = strTemp.substring(beginPostion, endPosition);
    strTempLine.trim();
    display.setCursor(0, i * 13 + 1);
    display.print(strTempLine);
    detectButtonPush();
  }

  String stringText = String(currentNewsLine) + "/" + String(NEWS_POLITICS_SIZE + NEWS_WORLD_SIZE + NEWS_ENGLISH_SIZE);
  int stringWidth = display.getStrWidth(string2char(stringText));
  display.setCursor(128 - stringWidth, 53);
  display.print(stringText);

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
  stringWidth = display.getUTF8Width(string2char(String(currentWeather.cond_txt)));
  display.setCursor((128 - stringWidth) / 2, 40);
  display.print(String(currentWeather.cond_txt));
  String WindDirectionAndSpeed = windDirectionTranslate(currentWeather.wind_dir) + String(currentWeather.wind_sc) + "级";
  stringWidth = display.getUTF8Width(string2char(WindDirectionAndSpeed));
  display.setCursor((128 - stringWidth) / 2, 54);
  display.print(WindDirectionAndSpeed);
  display.disableUTF8Print();
  display.setFont(u8g2_font_helvR24_tn); // u8g2_font_inb21_ mf, u8g2_font_helvR24_tn
  //  sprintf_P(buff, PSTR("%02d:%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min, timeInfo->tm_sec);
  sprintf_P(buff, PSTR("%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min);
  stringWidth = display.getStrWidth(buff);
  display.drawStr((128 - 30 - stringWidth) / 2, 11, buff);

  display.setFont(Meteocon21);
  display.drawStr(98, 17, string2char(chooseMeteocon(currentWeather.iconMeteoCon)));

  display.setFont(u8g2_font_helvR08_tf);
  String temp = String(currentWeather.tmp) + degree + "C";
  display.drawStr(0, 53, string2char(temp));

  display.setFont(u8g2_font_helvR08_tf);
  stringWidth = display.getStrWidth(string2char((String(currentWeather.hum) + "%")));
  display.drawStr(127 - stringWidth, 53, string2char((String(currentWeather.hum) + "%")));

  display.setFont(u8g2_font_helvB08_tf);
  if (previousTemp != 0 && previousHumidity != 0)
  {
    display.drawStr(0, 39, string2char(String(previousTemp, 0) + degree + "C"));
  }
  else
  {
    //    display.drawStr(0, 39, string2char("..."));
  }

  if (previousTemp != 0 && previousHumidity != 0)
  {
    int stringWidth = display.getStrWidth(string2char(String(previousHumidity, 0) + "%"));
    display.drawStr(128 - stringWidth, 39, string2char(String(previousHumidity, 0) + "%"));
  }
  else
  {
    //    int stringWidth = display.getStrWidth(string2char("..."));
    //    display.drawStr(128 - stringWidth, 39, string2char("..."));
  }
  display.drawHLine(0, 51, 128);
}

#ifdef SHOW_US_CITIES
void drawWorldLocation(String stringText, Timezone tztTimeZone, HeWeatherCurrentData currentWeather) {
  time_t utc = time(nullptr) - TZ_SEC;
  TimeChangeRule *tcr;        // pointer to the time change rule, use to get the TZ abbrev
  time_t t = tztTimeZone.toLocal(utc, &tcr);
  char buff[5];
  sprintf(buff, "%02d:%02d", hour(t), minute(t));
  display.enableUTF8Print();
  display.setFont(u8g2_font_wqy12_t_gb2312); // u8g2_font_wqy12_t_gb2312, u8g2_font_helvB08_tf
  String stringTemp = stringText + String(month(t)) + "月" + String(day(t)) + "日" + " " + WDAY_NAMES[weekday(t) - 1].c_str();
  int stringWidth = display.getUTF8Width(string2char(stringTemp));
  display.setCursor((128 - stringWidth) / 2, 1);
  display.print(stringTemp);
  stringWidth = display.getUTF8Width(string2char(String(currentWeather.cond_txt)));
  display.setCursor((128 - stringWidth) / 2, 40);
  display.print(String(currentWeather.cond_txt));
  String WindDirectionAndSpeed = windDirectionTranslate(currentWeather.wind_dir) + String(currentWeather.wind_sc) + "级";
  stringWidth = display.getUTF8Width(string2char(WindDirectionAndSpeed));
  display.setCursor((128 - stringWidth) / 2, 54);
  display.print(WindDirectionAndSpeed);
  display.disableUTF8Print();

  display.setFont(u8g2_font_helvR24_tn);
  //  stringTemp = String(hour(t)) + ":" + String(minute(t));
  stringWidth = display.getStrWidth(buff);
  display.drawStr((128 - 30 - stringWidth) / 2, 11, buff);
  String temp = String(currentWeather.tmp) + degree + "C";
  display.setFont(u8g2_font_helvR08_tf);
  display.drawStr(0, 53, string2char(temp));
  String tempHumidity = String(currentWeather.hum) + "%";
  stringWidth = display.getStrWidth(string2char(tempHumidity));
  display.setFont(u8g2_font_helvR08_tf);
  display.drawStr(128 - stringWidth, 53, string2char(tempHumidity));
  display.drawHLine(0, 51, 128);

  display.setFont(Meteocon21);
  display.drawStr(98, 17, string2char(String(currentWeather.iconMeteoCon).substring(0, 1)));
}
#endif

void drawMath(void) {
  nowTime = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&nowTime);
  char buff[20];
  sprintf_P(buff, PSTR("%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min);

  display.setFont(u8g2_font_helvB10_tf); // u8g2_font_helvB08_tf, u8g2_font_6x13_tn
  display.setCursor(1, 1);
  display.print(questionCount);
  display.print("/");
  display.print(questionTotal);

  display.setCursor(90, 1);
  display.print(buff);

  display.setFont(u8g2_font_helvB12_tf); // u8g2_font_helvB08_tf, u8g2_font_10x20_tf
  int stringWidth = display.getStrWidth(string2char(currentAnswer));
  display.setCursor((128 - stringWidth) / 2, 28);
  if (currentMode == 0)
  {
    display.print(currentQuestion);
  }
  else
  {
    display.print(currentAnswer);
  }
}

void drawPoem(void) {
  //    display.drawXBM(31, 0, 66, 64, garfield);
  nowTime = time(nullptr);
  struct tm* timeInfo;
  timeInfo = localtime(&nowTime);
  char buff[20];
  sprintf_P(buff, PSTR("%02d:%02d"), timeInfo->tm_hour, timeInfo->tm_min);

  int stringWidth = 0;

  display.enableUTF8Print();
  display.setFont(u8g2_font_wqy12_t_gb2312); // u8g2_font_wqy12_t_gb2312, u8g2_font_helvB08_tf

  int tempLineBegin = 0;
  int tempLineMultiply = 1;
  int tempLineEnd = 5;

  tempLineMultiply = lineCount / 5;
  tempLineBegin = tempLineMultiply * 5;
  tempLineEnd = tempLineBegin + 5;

  if (tempLineEnd > lineCount + 1)
  {
    tempLineEnd = lineCount + 1;
  }

  int intMaxY = 0;
  for (int i = tempLineBegin; i < tempLineEnd; ++i)
  {
    String strTemp = poemText[i];
    if (strTemp.length() > 30)
    {
      // each Chinese character's length is 3 in UTF-8
      strTemp = strTemp.substring(0, 30);
      strTemp.trim();
    }
    stringWidth = display.getUTF8Width(string2char(strTemp));
    intMaxY = (i % 5) * 13 + 1;
    display.setCursor((128 - stringWidth) / 2, intMaxY);
    display.print(strTemp);
    detectButtonPush();
  }
  display.disableUTF8Print();

  display.setFont(u8g2_font_helvB08_tf); // u8g2_font_helvB08_tf, u8g2_font_6x13_tn
  if (intMaxY < 52)
  {
    display.setCursor(28, 53);
    display.print(TOTAL_POEMS);
    display.setCursor(70, 53);
    display.print(buff);
  }

  display.setCursor(0, 41);
  display.print(lineCount);

  stringWidth = display.getStrWidth(string2char(String(lineTotal - 1)));
  display.setCursor(128 - stringWidth, 41);
  display.print(lineTotal - 1);

  display.setCursor(0, 53);
  display.print(poemCount);

  stringWidth = display.getStrWidth(string2char(String(poemTotal)));
  display.setCursor(128 - stringWidth, 53);
  display.print(poemTotal);
}

void getChineseNewsDataDetails(char NewsServer[], char NewsURL[], int beginLine, int lineSizeLimit) {
  int tempBeginLine = beginLine;
  /*
    WiFiClientSecure client;
    int httpport = 443;
  */

  WiFiClient client;
  int httpport = 80;

  String line = "";
#ifdef DEBUG
  Serial.print(">> Connecting to ");
  Serial.println(NewsServer);
#endif
  int retryCounter = 0;
  while (!client.connect(NewsServer, httpport))
  {
#ifdef DEBUG
    Serial.println(".");
#endif    delay(1000);
    retryCounter++;
    if (retryCounter > 10)
    {
      client.stop();
      return;
    }
  }

  String url = NewsURL;
#ifdef DEBUG
  Serial.print(">> Requesting URL: ");
  Serial.println(NewsURL);
  Serial.println("");
#endif
  client.print(String("GET ") + url + " HTTP/1.1\r\n" + "Host: " + NewsServer + "\r\nUser-Agent: IBEDevices-ESP8266\r\n" +  "Connection: close\r\n\r\n");

  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 30000) {
#ifdef DEBUG
      Serial.println(">> Client Timeout !");
#endif
      client.stop();
      return;
    }
  }

  int lineCount = 0;
  while (client.available())
  {
    line = client.readStringUntil('!');
    const String titleBeginMark = "[CDATA[";
    const String titleEndMark = "</title>";
    int titleBeginPos = 0;
    int titleEndPos = line.indexOf(titleEndMark);
    if (titleEndPos > -1)
    {
      line.replace("&#x2019;", "\'");                        //replace special characters
      line.replace("&#39;", "\'");
      line.replace("&apos;", "\'");
      line.replace("&amp;", "&");
      line.replace("&quot;", "\"");
      line.replace("&gt;", ">");
      line.replace("&lt;", "<");
      line.replace(titleBeginMark, "");
      line.replace(titleEndMark, "");
      line.trim();
      line = line.substring(titleBeginPos, titleEndPos - titleBeginPos);
      line.replace("]]>", "");
      if (line.indexOf("时政频道") < 0 && line.indexOf("时政新闻") < 0 && line.indexOf("Copyright") < 0 && line.indexOf("国际频道") < 0 && line.indexOf("国际新闻") < 0)
      {
        line.trim();
#ifdef DEBUG
        Serial.print("Title ");
        Serial.print(lineCount);
        Serial.println(": " + line);
#endif
        newsText[tempBeginLine] = line;
        tempBeginLine++;
        lineCount++;
      }
    }
    if (lineCount >= lineSizeLimit)
    {
      client.stop();
      return;
    }
    line = "";
  }
#ifdef DEBUG
  Serial.println();
  Serial.println("closing connection");
#endif
  client.stop();
}

void getEnglishNewsDataDetails(char NewsServer[], char NewsURL[], int beginLine, int lineSizeLimit) {
  int tempBeginLine = beginLine;
  /*
    WiFiClientSecure client;
    int httpport = 443;
  */
  WiFiClient client;
  int httpport = 80;

  String line = "";
#ifdef DEBUG
  Serial.print(">> Connecting to ");
  Serial.println(NewsServer);
#endif
  int retryCounter = 0;
  while (!client.connect(NewsServer, httpport))
  {
#ifdef DEBUG
    Serial.println(".");
#endif    delay(1000);
    retryCounter++;
    if (retryCounter > 10)
    {
      client.stop();
      return;
    }
  }
  String url = NewsURL;
#ifdef DEBUG
  Serial.print(">> Requesting URL: ");
  Serial.println(NewsURL);
  Serial.println("");
#endif
  client.print(String("GET ") + url + " HTTP/1.1\r\n" + "Host: " + NewsServer + "\r\nUser-Agent: Mozilla/5.0\r\n" +  "Connection: close\r\n\r\n");

  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 30000) {
#ifdef DEBUG
      Serial.println(">> Client Timeout !");
#endif
      client.stop();
      return;
    }
  }
  client.setTimeout(30000);
  int lineCount = 0;
  while (client.available())
  {
    line = client.readStringUntil('\n');
    const String titleBeginMark = "<title>";
    const String titleEndMark = "</title>";
    if (line.indexOf(titleBeginMark) > -1 && line.indexOf(titleEndMark) > -1)
    {
      line.replace("&#x2019;", "\'");                        //replace special characters
      line.replace("&#39;", "\'");
      line.replace("&apos;", "\'");
      line.replace("’", "\'");
      line.replace("‘", "\'");
      line.replace("&amp;", "&");
      line.replace("&quot;", "\"");
      line.replace("&gt;", ">");
      line.replace("&lt;", "<");
      line.replace(titleBeginMark, "");
      line.replace(titleEndMark, "");
      line.trim();
      if (line.indexOf("USATODAY - News Top") < 0 && line.indexOf("GANNETT Syndication") < 0)
      {
        line.replace("&lsquo;", "\'");
        line.replace("&rsquo;", "\'");
        line.replace("&ldquo;", "\"");
        line.replace("&rdquo;", "\"");
#ifdef DEBUG
        Serial.print("Title ");
        Serial.print(lineCount);
        Serial.print(": ");
        Serial.println(line);
#endif
        newsText[tempBeginLine] = line;
        tempBeginLine++;
        lineCount++;
      }
    }
    if (line.substring(0, 6) == titleBeginMark)
    {
      line.replace(titleBeginMark, "");
      line.trim();
      if (line.indexOf("Yahoo News - Latest") < 0)
      {
#ifdef DEBUG
        Serial.print("Title ");
        Serial.print(lineCount);
        Serial.print(": ");
        Serial.println(line);
#endif
        newsText[tempBeginLine] = line;
        tempBeginLine++;
        lineCount++;
      }
    }
    if (lineCount >= lineSizeLimit)
    {
      client.stop();
      return;
    }
    line = "";
  }
#ifdef DEBUG
  Serial.println();
  Serial.println("closing connection");
#endif
  client.stop();
}

void getEnglishNewsData() {
  // http://rssfeeds.usatoday.com/usatoday-newstopstories&x=1
  char newsDataServer[] = "rssfeeds.usatoday.com";

  getEnglishNewsDataDetails(newsDataServer, "/usatoday-newstopstories&x=1", 0, NEWS_ENGLISH_SIZE); // NEWS_POLITICS_SIZE + NEWS_WORLD_SIZE, NEWS_ENGLISH_SIZE
}

void getChineseNewsData() {
  // http://www.people.com.cn/rss/politics.xml world.xml
  char newsDataServer[] = "www.people.com.cn";

  getChineseNewsDataDetails(newsDataServer, "/rss/world.xml", NEWS_ENGLISH_SIZE, NEWS_WORLD_SIZE);
  getChineseNewsDataDetails(newsDataServer, "/rss/politics.xml", NEWS_ENGLISH_SIZE + NEWS_WORLD_SIZE, NEWS_POLITICS_SIZE);
}

