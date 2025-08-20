#define BUTTON D7
#define BUZZER D5
//GPIO13 (D7), GPIO14 (D5)

#include <Arduino.h>
#include <GyverDBFile.h>
#include <LittleFS.h>
#include <SettingsESP.h>
#include <WiFiConnector.h>
#include <GyverNTP.h>
#include <AHT10.h>
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiClientSecureBearSSL.h>
#include <GyverHTTP.h>
#include <ArduinoJson.h>
#include <EncButton.h>
#include <LiquidCrystal_I2C.h>
#include <GTimer.h>

#define COLUMS 16
#define ROWS 2
LiquidCrystal_I2C lcd(PCF8574_ADDR_A21_A11_A01, 4, 5, 6, 16, 11, 12, 13, 14, POSITIVE);

GyverDBFile db(&LittleFS, "/data.db");
GyverDB db_ram;
SettingsESP sett("Table Clock", &db);
AHT10 AHT(AHT10_ADDRESS_0X38);
BearSSL::WiFiClientSecure client;
JsonDocument json;
Button b(BUTTON, INPUT, HIGH);
GTimer<millis> backlight_off;
GTimer<millis> screen_show;
GTimer<millis> one_sec;

bool timeout, alarm, pik, beeper, flag = false;
float temperature, windspeed;
int hum, temp_in, temp_out, wind_s, i;
String days[] = {"Err", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
int8_t count = 0;
String updated;

DB_KEYS(
    kk,
    wifi_ssid,
    wifi_pass,
    time_zone,
    latitude,
    longitude,
    alarm_bool,
    alarm_hour,
    alarm_min,
    info_show,
    location,
    search_status,
    city_index,
    city_list,
    apply);

String get_weather(float latitude, float longitude) {
  String result = "";
  if (latitude != 0 && longitude != 0) {
    client.setInsecure();
    String url = "/v1/forecast?latitude=" + String(latitude) + "&longitude=" + String(longitude) + "&current_weather=true";
    ghttp::Client http(client, "api.open-meteo.com", 443);
    if (http.request(url)) {
      ghttp::Client::Response resp = http.getResponse();
      if (resp.code() == 200) {
        result = resp.body().readString();
        Serial.println("Weather received!");
        Serial.println(result);
        Serial.println("");
      } else {
        Serial.print("Error - ");
        Serial.print(resp.code());
        Serial.println("");
      }
    }
  }
  return result;
}

String get_city(String city) {
  String result = "";
  client.setInsecure();
  String url = "/v1/search?name=" + city;
  ghttp::Client http(client, "geocoding-api.open-meteo.com", 443);
  if (http.request(url)) {
    ghttp::Client::Response resp = http.getResponse();
    if (resp.code() == 200) {
      result = resp.body().readString();
      Serial.println("Cities received!");
      Serial.println(result);
      Serial.println("");
    } else {
      Serial.print("Error - ");
      Serial.print(resp.code());
      Serial.println("");
    }
  }
  return result;
}

String index_city() {
  String result = "";
  if (!json["results"].isNull()) {
    JsonArray results = json["results"];
    for (JsonObject obj : results) {
      result += obj["name"].as<String>() + "/";
      result += obj["country"].as<String>() + "/";
      if (obj["admin2"].as<String>() != "null") {
        result += obj["admin1"].as<String>() + "/";
        result += obj["admin2"].as<String>() + "; ";
      } else result += obj["admin1"].as<String>() + "; ";
    }
  }
  else result = "Not Found;";
  return result;
}

void weather_check() {
  db_ram[kk::search_status] = false;
  deserializeJson(json, get_weather(db[kk::latitude], db[kk::longitude]));
  temperature = json["current_weather"]["temperature"];
  windspeed = json["current_weather"]["windspeed"];
  hum = round(AHT.readHumidity());
  temp_in = round(AHT.readTemperature());
  temp_out = round(temperature);
  wind_s = round(windspeed);
  Serial.println("Updated weather!");
  Serial.println("");
}

void build(sets::Builder& b) {
  if (b.beginGroup("Wi-Fi")) {
    b.Input(kk::wifi_ssid, "SSID:");
    b.Pass(kk::wifi_pass, "Password:");
    if (b.Button(kk::apply, "Connect")) {
      db.update();
      WiFiConnector.connect(db[kk::wifi_ssid], db[kk::wifi_pass]);
    }
    b.endGroup();
  }
  
  if (b.beginRow("Time")) {
    b.Select(kk::time_zone, "Time zone:", " UTC+0; UTC+1; UTC+2; UTC+3; UTC+4; UTC+5; UTC+6;");
    if (b.Button("Confirm")) {
      db.update();
      NTP.begin(db[kk::time_zone]);
    }
    b.endRow();
  }

  if (b.beginRow("Alarm")) {
    b.Input(kk::alarm_hour, "Hour:");
    b.Input(kk::alarm_min, "Minute:");
    b.Label("", "");
    b.Switch(kk::alarm_bool, "OFF/ON");
    db.update();
    b.endRow();
  }
  
  if (b.beginGroup("Location")) {
    b.Input(kk::latitude, "Latitude:");
    b.Input(kk::longitude, "Longitude:");
    b.Input(kk::location, "Location:");
    sett.attachDB(&db_ram);
    if (b.Button("Search", sets::Colors::Blue)) {
      db_ram[kk::search_status] = true;
      deserializeJson(json, get_city(db[kk::location]));
      db_ram[kk::city_index] = 0;
      db_ram[kk::city_list] = index_city();
      b.reload();
    }
    if (db_ram[kk::search_status] == true) {
      b.Select(kk::city_index, "City", db_ram[kk::city_list]);
      if (b.Button("Confirm")) {
        if (db_ram[kk::city_list].toString() != "Not Found;") {
          float lati, longi;
          lati = json["results"][db_ram[kk::city_index]]["latitude"];
          longi = json["results"][db_ram[kk::city_index]]["longitude"];
          db[kk::latitude] = lati;
          db[kk::longitude] = longi;
          db.update();
          weather_check();
        }
        db_ram[kk::search_status] = false;
        db_ram[kk::city_index] = 0;
        db_ram[kk::city_list] = "";
        b.reload();
      }
    }
    if (b.Button("Update weather")) {
      weather_check();
    }
    b.endGroup();
  }
  
  sett.attachDB(&db);
  if (b.beginGroup("Info")) {
    b.Switch(kk::info_show, "Show info");
    db.update();
    b.reload();
    if (db[kk::info_show] == true) {
      b.Label("time"_h, "Time:");
      b.Label("date"_h, "Date:");
      b.Label("day"_h, "Day:");
      b.Label("temperature_inside"_h, "Temperature inside (°C):");
      b.Label("humidity_inside"_h, "Humidity inside (%):");
      b.Label("temperature_outside"_h, "Temperature outside (°C):");
      b.Label("windspeed"_h, "Windspeed (km/s):");
      b.Label("updated"_h, "Updated:");
    }
    b.endGroup();
  }
  b.Link("GitHub by BOY4ik", "https://github.com/boy4ik7/table-clock");
}

void update(sets::Updater& upd) {
  if (db[kk::info_show] == true) {
    upd.update("time"_h, NTP.timeToString());
    upd.update("date"_h, NTP.dateToString());
    upd.update("day"_h, days[NTP.weekDay()]);
    upd.update("temperature_inside"_h, round(AHT.readTemperature() * 10) / 10);
    upd.update("humidity_inside"_h, round(AHT.readHumidity() * 10) / 10);
    upd.update("temperature_outside"_h, temperature);
    upd.update("windspeed"_h, windspeed);
    upd.update("updated"_h, updated);
  }
}

byte drop[] = {
  B00100,
  B01110,
  B11111,
  B11111,
  B01110,
  B00000,
  B00000,
  B00000
};

byte bell[] = {
  B00100,
  B01110,
  B01110,
  B11111,
  B00100,
  B00000,
  B00000,
  B00000
};

byte home[] = {
  B00000,
  B00100,
  B01110,
  B11111,
  B11111,
  B11111,
  B11111,
  B00000
};

byte window[] = {
  B00000,
  B11111,
  B10101,
  B11111,
  B10101,
  B11111,
  B00000,
  B00000
};

byte wind[] = {
  B10000,
  B01111,
  B00000,
  B11111,
  B00000,
  B01111,
  B10000,
  B00000
};

byte watch[] = {
  B00000,
  B01110,
  B10011,
  B10101,
  B10001,
  B01110,
  B00000,
  B00000
};

void lcd_show() {
  b.tick();
  if (beeper == true) {
    if (b.press()) {
      digitalWrite(BUZZER, LOW);
      lcd.clear();
      beeper = false;
      count = 0;
      one_sec.stop();
      backlight_off.start();
    }
    lcd.setCursor(5, 0);
    if (db[kk::alarm_hour] < 10) {
      lcd.print(F("0"));
    }
    lcd.print(db[kk::alarm_hour]);
    lcd.print(F(":"));
    if (db[kk::alarm_min] < 10) {
      lcd.print(F("0"));
    }
    lcd.print(db[kk::alarm_min]);
    if (one_sec == true) {
      pik = !pik;
      count += 1;
    }
    if (pik == true) {
      lcd.backlight();
      digitalWrite(BUZZER, HIGH);
      lcd.setCursor(6, 1);
      lcd.print(F("0w0"));
    } else {
      lcd.noBacklight();
      digitalWrite(BUZZER, LOW);
      lcd.setCursor(6, 1);
      lcd.print(F("-w-"));
    }
    if (count > 60) {
      digitalWrite(BUZZER, LOW);
      lcd.clear();
      count = 0;
      beeper = false;
      one_sec.stop();
      backlight_off.start();
    }
  } else {
    if (backlight_off == true) lcd.noBacklight();
    else lcd.backlight();
    if (b.click()) {
      if (backlight_off == false) {
        flag = !flag;
      }
      backlight_off.start();
      screen_show.start();
    }
    if (b.hold()) {
      db[kk::alarm_bool] = !db[kk::alarm_bool];
      db.update();
      backlight_off.start();
      screen_show.start();
    }
    if (screen_show == true) flag = !flag;
    if (flag == true) {
      // первая строка
      lcd.setCursor(0, 0);
      if (db[kk::alarm_bool] == true) lcd.write(1);
      else lcd.print(F(" "));
      if (NTP.hour() < 10) {
        lcd.print(F("0"));
      }
      lcd.print(NTP.hour());
      lcd.print(F(":"));
      if (NTP.minute() < 10) {
        lcd.print(F("0"));
      }
      lcd.print(NTP.minute());
      lcd.print(F("  "));
      lcd.setCursor(9, 0);
      lcd.write(3);
      lcd.print(temp_in);
      lcd.write(223);
      lcd.print(F("C  "));
      // вторая строка
      lcd.setCursor(0, 1);
      lcd.print(NTP.day());
      lcd.print(F("."));
      lcd.print(NTP.month());
      lcd.print(F("."));
      lcd.print(NTP.year() / 100);
      //lcd.setCursor(11, 1);
      lcd.print(F("  "));
      lcd.setCursor(9, 1);
      lcd.write(2);
      lcd.print(hum);
      lcd.print(F("%"));
      lcd.print(F("   "));
    } 
    if (flag == false) {
      // первая строка
      lcd.setCursor(0, 0);
      lcd.write(5);
      if (db[kk::alarm_hour] < 10) {
        lcd.print(F("0"));
      }
      lcd.print(db[kk::alarm_hour]);
      lcd.print(F(":"));
      if (db[kk::alarm_min] < 10) {
        lcd.print(F("0"));
      }
      lcd.print(db[kk::alarm_min]);
      lcd.print(F("  "));
      lcd.setCursor(9, 0);
      lcd.write(4);
      lcd.print(temp_out);
      lcd.write(223);
      lcd.print(F("C  "));
      // вторая строка
      lcd.setCursor(0, 1);
      lcd.print(F(" "));
      lcd.print(days[NTP.weekDay()]);
      lcd.print(F("   "));
      lcd.setCursor(9, 1);
      lcd.write(6);
      lcd.print(wind_s);
      lcd.print(F("Km/h  "));
    }
  }
  if ((db[kk::alarm_bool] == true) && (db[kk::alarm_hour] == NTP.hour()) && (db[kk::alarm_min] == NTP.minute())) {
  	if (alarm == false) {
      beeper = true;
      lcd.clear();
      one_sec.start();
    	alarm = !alarm;
    }
  } else {
    if (alarm == true) alarm = !alarm;
  }
}

void weather_tick() {
  if ((NTP.minute() == 0) && (NTP.online() == true)) {
  	if (timeout == false) {
      weather_check();
      updated = NTP.timeToString();
    	timeout = !timeout;
    }
  } else {
    if (timeout == true) timeout = !timeout;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  #ifdef ESP32
    LittleFS.begin(true);
  #else
    LittleFS.begin();
  #endif
  db.begin();
  db.init(kk::wifi_ssid, "");
  db.init(kk::wifi_pass, "");
  db.init(kk::time_zone, 3);
  db.init(kk::latitude, 0.0);
  db.init(kk::longitude, 0.0);
  db.init(kk::alarm_bool, false);
  db.init(kk::alarm_hour, 9);
  db.init(kk::alarm_min, 0);
  db.init(kk::info_show, false);
  db.init(kk::location, "");
  //db_ram.init(kk::search_location, false);
  //db_ram.init(kk::search_location, false);
  db_ram.init(kk::search_status, false);
  db_ram.init(kk::city_index, 0);
  db_ram.init(kk::city_list, "");
  WiFiConnector.onConnect([]() {
      Serial.print("Connected! ");
      Serial.println(WiFi.localIP());
      lcd.clear();
      lcd.setCursor(0, 0); 
      lcd.print(F("Conected!"));
      lcd.setCursor(0, 1);
      lcd.print(WiFi.localIP());
      NTP.begin(db[kk::time_zone]);
      delay(3000);
      weather_check();
      updated = NTP.timeToString();
      lcd.clear();
  });
  WiFiConnector.onError([]() {
      Serial.print("Error! Start AP ");
      Serial.println(WiFi.softAPIP());
      lcd.clear();
      lcd.setCursor(0, 0); 
      lcd.print(F("Error!"));
      lcd.setCursor(0, 1);
      lcd.print(WiFi.softAPIP());
      delay(3000);
      lcd.clear();
  });
  WiFiConnector.connect(db[kk::wifi_ssid], db[kk::wifi_pass]);
  sett.begin();
  sett.onBuild(build);
  sett.onUpdate(update);
  //NTP.begin(db[kk::time_zone]);
  delay(100);
  AHT.begin();
  pinMode(BUZZER, OUTPUT);
  lcd.begin(COLUMS, ROWS, LCD_5x8DOTS, 4, 5, 400000, 250);
  //lcd.createChar(0, drop);
  lcd.createChar(1, bell);
  lcd.createChar(2, drop);
  lcd.createChar(3, home);
  lcd.createChar(4, window);
  lcd.createChar(5, watch);
  lcd.createChar(6, wind);
  backlight_off.setMode(GTMode::Overflow);
  backlight_off.setTime(15000);
  backlight_off.start();
  screen_show.setMode(GTMode::Interval);
  screen_show.setTime(5000);
  screen_show.start();
  one_sec.setMode(GTMode::Interval);
  one_sec.setTime(1000);
}

void loop() {  
  WiFiConnector.tick();
  sett.tick();
  NTP.tick();
  weather_tick();
  lcd_show();
}
