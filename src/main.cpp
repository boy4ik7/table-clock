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
#include <GyverLCD.h>
//#include <GyverLCD_UTF8.h>
#include <GTimer.h>

GyverLCD lcd(0x27, 16, 2);
//GyverLCD_UTF8 lcd(0x27, 16, 2);

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

bool timeout, alarm, pik, beeper, flag = false, start_one = true, localip = false, softip = false;
float temperature, windspeed;
int hum, temp_in, temp_out, wind_s, i;
String days[] = {"Err", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
int8_t count = 0;
//String updated;

DB_KEYS(
    kk,
    wifi_ssid,
    wifi_pass,
    dynamic_flag,
    ssids,
    ssids_index,
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
    do_not_turn_off_screen,
    new_hour_screen_bool,
    new_hour_sound_bool,
    screen_switch,
    reminder_bool,
    reminder_1_bool,
    reminder_1_repeat_bool,
    reminder_1_text,
    reminder_1_hour,
    reminder_1_min,
    reminder_2_bool,
    reminder_2_repeat_bool,
    reminder_2_text,
    reminder_2_hour,
    reminder_2_min,
    reminder_3_bool,
    reminder_3_repeat_bool,
    reminder_3_text,
    reminder_3_hour,
    reminder_3_min,
    reminder_4_bool,
    reminder_4_repeat_bool,
    reminder_4_text,
    reminder_4_hour,
    reminder_4_min,
    reminder_5_bool,
    reminder_5_repeat_bool,
    reminder_5_text,
    reminder_5_hour,
    reminder_5_min,
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
        //Serial.println(result);
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
    if (resp.code() == 200) result = resp.body().readString();/*{
      result = resp.body().readString();
      Serial.println("Cities received!");
      //Serial.println(result);
      Serial.println("");
    } else {
      Serial.print("Error - ");
      Serial.print(resp.code());
      Serial.println("");
    }*/
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
  if ((db[kk::latitude] != 0) && (db[kk::longitude] != 0)) {
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
  } else {
    Serial.println("Error! Latitude and Longitude are 0");
    Serial.println("");
  }
}

void scan_wifi() {
  int scans_networks = WiFi.scanNetworks();
  db_ram[kk::ssids] = "";
  db_ram[kk::ssids_index] = 0;
  for (int i = 0; i < scans_networks; i++) {
    db_ram[kk::ssids] += WiFi.SSID(i) + ";";
  }
}

void build(sets::Builder& b) {
  if (b.beginGroup("Wi-Fi")) {
    String SSIDS = db_ram[kk::ssids];
    if (db[kk::dynamic_flag]) b.Select(kk::ssids_index, "Found SSIDS:", SSIDS);
    else b.Input(kk::wifi_ssid, "Manual input SSID:");
    b.Pass(kk::wifi_pass, "Password:");
    if (db[kk::dynamic_flag]) {
      if (b.Button("Cancel", sets::Colors::Red)) {
        db[kk::dynamic_flag] = false;
        db_ram[kk::wifi_pass] = "";
        sett.attachDB(&db);
        b.reload();
      }
    } else {
      if (b.Button("Find networks", sets::Colors::Blue)) {
        db[kk::dynamic_flag] = true;
        scan_wifi();
        sett.attachDB(&db_ram);
        b.reload();
      }
    }
    if (b.Button("Connect")) {
      if(db[kk::dynamic_flag]) {
        db_ram[kk::wifi_ssid] = WiFi.SSID(db_ram[kk::ssids_index]);
        db[kk::wifi_ssid] = String(db_ram[kk::wifi_ssid]);
        db[kk::wifi_pass] = String(db_ram[kk::wifi_pass]);
        db[kk::dynamic_flag] = false;
        db_ram[kk::wifi_pass] = "";
      }
      Serial.println("Connect to: \n" + String(db[kk::wifi_ssid]) + "\n"+ String(db[kk::wifi_pass]) + "\n");
      WiFiConnector.connect(db[kk::wifi_ssid], db[kk::wifi_pass]);
      db.update();
      sett.attachDB(&db);
      b.reload();
    }
    b.endGroup();
  }
  
  if (b.beginGroup("Screen and Time")) {
    b.Switch(kk::do_not_turn_off_screen, "Do not turn off the screen");
    b.reload();
    if (db[kk::do_not_turn_off_screen] == false) {
      b.Switch(kk::new_hour_screen_bool, "Screen lights up at the start of a new hour");
    }
    b.Switch(kk::new_hour_sound_bool, "Emit a sound at the start of each new hour");
    b.Switch(kk::screen_switch, "Automatic screen switching");
    db.update();
    b.Select(kk::time_zone, "Time zone:", " UTC+0; UTC+1; UTC+2; UTC+3; UTC+4; UTC+5; UTC+6;");
    if (b.Button("Update time")) {
      db.update();
      NTP.begin(db[kk::time_zone]);
    }
    b.endGroup();
  }

  if (b.beginRow("Alarm")) {
    //b.Input(kk::alarm_hour, "Hour:");
    b.Spinner(kk::alarm_hour, "Hour:", 0, 23);
    //b.Input(kk::alarm_min, "Minute:");
    b.Spinner(kk::alarm_min, "Minute:", 0, 59);
    b.Label("", "");
    b.Switch(kk::alarm_bool, "");
    db.update();
    b.endRow();
  }

  if (b.beginGroup("Reminders")) {
    b.Switch(kk::reminder_bool, "Enable");
    db.update();
    b.reload();
    if (db[kk::reminder_bool] == true) {
      b.Switch(kk::reminder_1_bool, "Reminder 1");
      b.Input(kk::reminder_1_text, "Text:");
      b.Switch(kk::reminder_1_repeat_bool, "Repeat every day");
      db.update();
      if (b.beginRow(" ")) {
        b.Spinner(kk::reminder_1_hour, "Hour:", 0, 23);
        b.Spinner(kk::reminder_1_min, "Minute:", 0, 59);
        db.update();
        b.endRow();
      }
      b.Switch(kk::reminder_2_bool, "Reminder 2");
      b.Input(kk::reminder_2_text, "Text:");
      b.Switch(kk::reminder_2_repeat_bool, "Repeat every day");
      db.update();
      if (b.beginRow(" ")) {
        b.Spinner(kk::reminder_2_hour, "Hour:", 0, 23);
        b.Spinner(kk::reminder_2_min, "Minute:", 0, 59);
        db.update();
        b.endRow();
      }
      b.Switch(kk::reminder_3_bool, "Reminder 3");
      b.Input(kk::reminder_3_text, "Text:");
      b.Switch(kk::reminder_3_repeat_bool, "Repeat every day");
      db.update();
      if (b.beginRow(" ")) {
        b.Spinner(kk::reminder_3_hour, "Hour:", 0, 23);
        b.Spinner(kk::reminder_3_min, "Minute:", 0, 59);
        db.update();
        b.endRow();
      }
      b.Switch(kk::reminder_4_bool, "Reminder 4");
      b.Input(kk::reminder_4_text, "Text:");
      b.Switch(kk::reminder_4_repeat_bool, "Repeat every day");
      db.update();
      if (b.beginRow(" ")) {
        b.Spinner(kk::reminder_4_hour, "Hour:", 0, 23);
        b.Spinner(kk::reminder_4_min, "Minute:", 0, 59);
        db.update();
        b.endRow();
      }
      b.Switch(kk::reminder_5_bool, "Reminder 5");
      b.Input(kk::reminder_5_text, "Text:");
      b.Switch(kk::reminder_5_repeat_bool, "Repeat every day");
      db.update();
      if (b.beginRow(" ")) {
        b.Spinner(kk::reminder_5_hour, "Hour:", 0, 23);
        b.Spinner(kk::reminder_5_min, "Minute:", 0, 59);
        db.update();
        b.endRow();
      }
    }
    b.endGroup();
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
      String CITY_LIST = db_ram[kk::city_list];
      b.Select(kk::city_index, "City", CITY_LIST);
      if (b.Button("Confirm")) {
        if (db_ram[kk::city_list].toString() != "Not Found;") {
          float lati, longi;
          lati = json["results"][db_ram[kk::city_index]]["latitude"];
          longi = json["results"][db_ram[kk::city_index]]["longitude"];
          db[kk::latitude] = lati;
          db[kk::longitude] = longi;
          db.update();
          //weather_check();
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
      //b.Label("updated"_h, "Updated:");
    }
    b.endGroup();
  }
  b.Link("GitHub", "https://github.com/boy4ik7/table-clock");
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
    //upd.update("updated"_h, updated);
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

void beeper_active() {
  if (b.press()) {
    lcd.clear();
    beeper = false;
    count = 0;
    one_sec.stop();
    backlight_off.start();
    digitalWrite(BUZZER, LOW);
    delay(10);
  }
  if (db[kk::alarm_bool] == false) {
    lcd.clear();
    beeper = false;
    count = 0;
    one_sec.stop();
    backlight_off.start();
    digitalWrite(BUZZER, LOW);
    delay(10);
  }
  lcd.setCursor(5, 0);
  if (db[kk::alarm_hour] < 10) {
    lcd.print("0");
  }
  lcd.print(db[kk::alarm_hour]);
  lcd.print(":");
  if (db[kk::alarm_min] < 10) {
    lcd.print("0");
  }
  lcd.print(db[kk::alarm_min]);
  if (one_sec == true) {
    pik = !pik;
    count += 1;
  }
  if (pik == true) {
    lcd.setBacklight(true);
    //lcd.setDisplay(true);
    digitalWrite(BUZZER, HIGH);
    lcd.setCursor(6, 1);
    lcd.print("0w0");
  } else {
    if (db[kk::do_not_turn_off_screen] == false) lcd.setBacklight(false);
    //lcd.setDisplay(false);
    digitalWrite(BUZZER, LOW);
    lcd.setCursor(6, 1);
    lcd.print("-w-");
  }
  if (count > 60) {
    lcd.clear();
    count = 0;
    beeper = false;
    one_sec.stop();
    backlight_off.start();
    digitalWrite(BUZZER, LOW);
    delay(10);
  }
}

void lcd_print() {
  if (backlight_off == true) {
    //lcd.setDisplay(false);
    if (db[kk::do_not_turn_off_screen] == false) lcd.setBacklight(false);
  } else {
    //lcd.setDisplay(true);
    lcd.setBacklight(true);
  }
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
  // экран 1
  if (screen_show == true && db[kk::screen_switch] == true) {
    flag = !flag;
    lcd.clear();
  }
  if (flag == true) {
    // первая строка
    lcd.home();
    // Значек статуса будильника вкл/выкл
    if (db[kk::alarm_bool] == true) lcd.write(1);
    else lcd.print(" ");
    // Время
    if (NTP.hour() < 10) {
      lcd.print("0");
    }
    lcd.print(NTP.hour());
    lcd.print(":");
    if (NTP.minute() < 10) {
      lcd.print("0");
    }
    lcd.print(NTP.minute());
    lcd.print("  ");
    // Температура внутри
    lcd.setCursor(9, 0);
    lcd.write(3);
    lcd.print(temp_in);
    lcd.write(223);
    lcd.print("C  ");
    // вторая строка
    // Дата
    lcd.setCursor(0, 1);
    lcd.print(NTP.day());
    lcd.print(".");
    lcd.print(NTP.month());
    lcd.print(".");
    lcd.print(NTP.year() - 2000);
    //lcd.setCursor(11, 1);
    lcd.print("  ");
    // Влажность внутри
    lcd.setCursor(9, 1);
    lcd.write(2);
    lcd.print(hum);
    lcd.print("%");
    lcd.print("   ");
  } 
  // Экран 2
  if (flag == false) {
    // первая строка
    // Будильник (Время)
    lcd.home();
    lcd.write(5);
    if (db[kk::alarm_hour] < 10) {
      lcd.print("0");
    }
    lcd.print(db[kk::alarm_hour]);
    lcd.print(":");
    if (db[kk::alarm_min] < 10) {
      lcd.print("0");
    }
    lcd.print(db[kk::alarm_min]);
    lcd.print("  ");
    // Температура снаружи
    lcd.setCursor(9, 0);
    lcd.write(4);
    lcd.print(temp_out);
    lcd.write(223);
    lcd.print("C  ");
    // вторая строка
    // День недели
    lcd.setCursor(0, 1);
    lcd.print(" ");
    lcd.print(days[NTP.weekDay()]);
    lcd.print("    ");
    // Ветер
    lcd.setCursor(9, 1);
    lcd.write(6);
    lcd.print(wind_s);
    lcd.print("Km/h  ");
  }
}
//без иконок
/* 
void lcd_print() {
  if (backlight_off == true) {
    //lcd.setDisplay(false);
    lcd.setBacklight(false);
  } else {
    //lcd.setDisplay(true);
    lcd.setBacklight(true);
  }
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
  // экран 1
  if (screen_show == true && db[kk::screen_switch] == true) {
    flag = !flag;
    lcd.clear();
  }
  if (flag == true) {
    // первая строка
    lcd.home();
    // Значек статуса будильника вкл/выкл
    if (db[kk::alarm_bool] == true) lcd.print("A");
    else lcd.print(" ");
    // Время
    if (NTP.hour() < 10) {
      lcd.print("0");
    }
    lcd.print(NTP.hour());
    lcd.print(":");
    if (NTP.minute() < 10) {
      lcd.print("0");
    }
    lcd.print(NTP.minute());
    lcd.print("  ");
    // Температура внутри
    lcd.setCursor(9, 0);
    lcd.print(" ");
    lcd.print(temp_in);
    lcd.print("°C  ");
    // вторая строка
    // Дата
    lcd.setCursor(0, 1);
    lcd.print(NTP.day());
    lcd.print(".");
    lcd.print(NTP.month());
    lcd.print(".");
    lcd.print(NTP.year() - 2000);
    //lcd.setCursor(11, 1);
    lcd.print("  ");
    // Влажность внутри
    lcd.setCursor(9, 1);
    lcd.print(" ");
    lcd.print(hum);
    lcd.print("%");
    lcd.print("   ");
  } 
  // Экран 2
  if (flag == false) {
    // первая строка
    // Будильник (Время)
    lcd.home();
    lcd.print("A");
    if (db[kk::alarm_hour] < 10) {
      lcd.print("0");
    }
    lcd.print(db[kk::alarm_hour]);
    lcd.print(":");
    if (db[kk::alarm_min] < 10) {
      lcd.print("0");
    }
    lcd.print(db[kk::alarm_min]);
    lcd.print("  ");
    // Температура снаружи
    lcd.setCursor(9, 0);
    lcd.print(" ");
    lcd.print(temp_out);
    lcd.print("°C  ");
    // вторая строка
    // День недели
    lcd.setCursor(0, 1);
    lcd.print(" ");
    lcd.print(days[NTP.weekDay()]);
    lcd.print("    ");
    // Ветер
    lcd.setCursor(9, 1);
    lcd.print(" ");
    lcd.print(wind_s);
    lcd.print("Km/h  ");
  }
}
*/
void alarm_tick() {
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

void reminder_tick() {
  if (db[kk::reminder_bool] == true) {
    if ((db[kk::reminder_1_bool] == true) && (db[kk::reminder_1_hour] == NTP.hour()) && (db[kk::reminder_1_min] == NTP.minute()) && (NTP.second() < 5)) {
      lcd.setBacklight(true);
      //lcd.setDisplay(true);
      lcd.clear();
      lcd.home();
      lcd.setTextLoop(true);
      lcd.print(db[kk::reminder_1_text]);
      digitalWrite(BUZZER, HIGH);
      delay(100);
      digitalWrite(BUZZER, LOW);
      delay(100);
      if (db[kk::reminder_1_repeat_bool] == false) {
        db[kk::reminder_1_bool] = false;
        db.update();
      }
      delay(1000);
      lcd.setTextLoop(false);
      lcd.clear();
      backlight_off.start();
    }

    if ((db[kk::reminder_2_bool] == true) && (db[kk::reminder_2_hour] == NTP.hour()) && (db[kk::reminder_2_min] == NTP.minute()) && (NTP.second() < 5)) {
      lcd.setBacklight(true);
      //lcd.setDisplay(true);
      lcd.clear();
      lcd.home();
      lcd.setTextLoop(true);
      lcd.print(db[kk::reminder_2_text]);
      digitalWrite(BUZZER, HIGH);
      delay(100);
      digitalWrite(BUZZER, LOW);
      delay(100);
      if (db[kk::reminder_2_repeat_bool] == false) {
        db[kk::reminder_2_bool] = false;
        db.update();
      }
      delay(1000);
      lcd.setTextLoop(false);
      lcd.clear();
      backlight_off.start();
    }

    if ((db[kk::reminder_3_bool] == true) && (db[kk::reminder_3_hour] == NTP.hour()) && (db[kk::reminder_3_min] == NTP.minute()) && (NTP.second() < 5)) {
      lcd.setBacklight(true);
      //lcd.setDisplay(true);
      lcd.clear();
      lcd.home();
      lcd.setTextLoop(true);
      lcd.print(db[kk::reminder_3_text]);
      digitalWrite(BUZZER, HIGH);
      delay(100);
      digitalWrite(BUZZER, LOW);
      delay(100);
      if (db[kk::reminder_3_repeat_bool] == false) {
        db[kk::reminder_3_bool] = false;
        db.update();
      }
      delay(1000);
      lcd.setTextLoop(false);
      lcd.clear();
      backlight_off.start();
    }

    if ((db[kk::reminder_4_bool] == true) && (db[kk::reminder_4_hour] == NTP.hour()) && (db[kk::reminder_4_min] == NTP.minute()) && (NTP.second() < 5)) {
      lcd.setBacklight(true);
      //lcd.setDisplay(true);
      lcd.clear();
      lcd.home();
      lcd.setTextLoop(true);
      lcd.print(db[kk::reminder_4_text]);
      digitalWrite(BUZZER, HIGH);
      delay(100);
      digitalWrite(BUZZER, LOW);
      delay(100);
      if (db[kk::reminder_4_repeat_bool] == false) {
        db[kk::reminder_4_bool] = false;
        db.update();
      }
      delay(1000);
      lcd.setTextLoop(false);
      lcd.clear();
      backlight_off.start();
    }

    if ((db[kk::reminder_5_bool] == true) && (db[kk::reminder_5_hour] == NTP.hour()) && (db[kk::reminder_5_min] == NTP.minute()) && (NTP.second() < 5)) {
      lcd.setBacklight(true);
      //lcd.setDisplay(true);
      lcd.clear();
      lcd.home();
      lcd.setTextLoop(true);
      lcd.print(db[kk::reminder_5_text]);
      digitalWrite(BUZZER, HIGH);
      delay(100);
      digitalWrite(BUZZER, LOW);
      delay(100);
      if (db[kk::reminder_5_repeat_bool] == false) {
        db[kk::reminder_5_bool] = false;
        db.update();
      }
      delay(1000);
      lcd.setTextLoop(false);
      lcd.clear();
      backlight_off.start();
    }
  }
}

void new_hour() {
  if (db[kk::new_hour_screen_bool] == true || db[kk::new_hour_sound_bool] == true) {
    if (db[kk::new_hour_screen_bool] == true) {
      lcd.setBacklight(true);
      //lcd.setDisplay(true);
      backlight_off.start();
    }
    if (db[kk::new_hour_sound_bool] == true) {
      digitalWrite(BUZZER, HIGH);
      delay(50);
      digitalWrite(BUZZER, LOW);
      delay(100);
    }
  }
}

void weather_tick() {
  if ((NTP.minute() == 0) && (NTP.online() == true)) {
  	if (timeout == false) {
      weather_check();
      delay(100);
      new_hour();
      //updated = NTP.timeToString();
    	timeout = !timeout;
    }
  } else {
    if (timeout == true) timeout = !timeout;
  }
}

void one_start() {
  if (localip == true) {
    lcd.clear();
    lcd.home(); 
    lcd.print("Conected!");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    NTP.begin(db[kk::time_zone]);
    delay(1500);
    //updated = NTP.timeToString();
    weather_check();
    delay(1500);
    lcd.clear();
    start_one = false;
  }
  if (softip == true) {
    lcd.clear();
    lcd.home(); 
    lcd.print("Error!");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.softAPIP());
    delay(3000);
    lcd.clear();
    start_one = false;
  }
}

void lcd_show() {
  b.tick();
  // Будильник
  if (beeper == true) beeper_active();
  else lcd_print();
  alarm_tick();
  reminder_tick();
  weather_tick();
}

void setup() {
  Serial.begin(9600);
  #ifdef ESP32
    LittleFS.begin(true);
  #else
    LittleFS.begin();
  #endif
  db.begin();
  //ПЗУ
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
  db.init(kk::do_not_turn_off_screen, true);
  db.init(kk::new_hour_screen_bool, false);
  db.init(kk::new_hour_sound_bool, false);
  db.init(kk::screen_switch, false);
  db.init(kk::reminder_bool, false);
  db.init(kk::reminder_1_bool, false);
  db.init(kk::reminder_1_repeat_bool, false);
  db.init(kk::reminder_1_text, "");
  db.init(kk::reminder_1_hour, 0);
  db.init(kk::reminder_1_min, 0);
  db.init(kk::reminder_2_bool, false);
  db.init(kk::reminder_2_repeat_bool, false);
  db.init(kk::reminder_2_text, "");
  db.init(kk::reminder_2_hour, 0);
  db.init(kk::reminder_2_min, 0);
  db.init(kk::reminder_3_bool, false);
  db.init(kk::reminder_3_repeat_bool, false);
  db.init(kk::reminder_3_text, "");
  db.init(kk::reminder_3_hour, 0);
  db.init(kk::reminder_3_min, 0);
  db.init(kk::reminder_4_bool, false);
  db.init(kk::reminder_4_repeat_bool, false);
  db.init(kk::reminder_4_text, "");
  db.init(kk::reminder_4_hour, 0);
  db.init(kk::reminder_4_min, 0);
  db.init(kk::reminder_5_bool, false);
  db.init(kk::reminder_5_repeat_bool, false);
  db.init(kk::reminder_5_text, "");
  db.init(kk::reminder_5_hour, 0);
  db.init(kk::reminder_5_min, 0);
  //ОЗУ
  db_ram.init(kk::wifi_ssid, "");
  db_ram.init(kk::wifi_pass, "");
  db_ram.init(kk::dynamic_flag, false);
  db_ram.init(kk::ssids, "");
  db_ram.init(kk::ssids_index, 0);
  db_ram.init(kk::search_status, false);
  db_ram.init(kk::city_index, 0);
  db_ram.init(kk::city_list, "");
  WiFiConnector.connect(db[kk::wifi_ssid], db[kk::wifi_pass]);
  WiFiConnector.onConnect([]() {
      Serial.print("Connected! ");
      Serial.println(WiFi.localIP());
      localip = true;
  });
  WiFiConnector.onError([]() {
      Serial.print("Error! Start AP ");
      Serial.println(WiFi.softAPIP());
      softip = true;
  });
  sett.begin();
  sett.onBuild(build);
  sett.onUpdate(update);
  delay(100);
  AHT.begin();
  pinMode(BUZZER, OUTPUT);
  Wire.begin();
  lcd.begin();

  lcd.createChar(0, drop);
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
  //Serial.println("Start loop");
  WiFiConnector.tick();
  if (start_one == true) {
    one_start();
  } else {
    sett.tick();
    NTP.tick();
    lcd_show();
  }
  delay(10);
}
