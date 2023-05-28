/*
 * Copyright 2023 Andrew Brining
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * @file metriClock.cpp
 * @author Andrew Brining (andrew@thebrinings.com)
 * @brief An Arduino clock that displays the time in delineations of 10.
 * @version 0.1
 * @date 2023-02-07
 * 
 * @copyright Copyright (c) 2023
 * 
 */

/*
 * The following is a conversion for each of the delineations of metric time:
 *
 * cycle  10:00:00 = 24   hr  = 86400 sec
 * decic   1:00:00 = 2.4  hr  = 8640  sec
 *         0:10:00 = 14.4 min = 864   sec
 * millic  0:01:00 = 1.44 min = 86.4  sec
 *         0:00:10 = ........ = 8.64  sec
 * lakh    0:00:01 = ........ = 864   ms
 * 
 * Smallest whole-number conversion:
 *  0:01:25 (125 lakhs) = 108 sec
 */

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>
#include <Timezone.h>  // https://youtu.be/-5wpm-gesOY

const byte TX_PIN = 2;
const byte RX_PIN = 3;

const byte LCD_WIDTH = 16;
const byte LCD_HEIGHT = 2;

// position of digits on the display from most to least significant
const byte DIGITS[6] = {8, 9, 11, 12, 14, 15};
// set character to use for the delimeter and where it should go
const char DELIMETER = ':';
const byte DELIMS[2] = {10, 13};

LiquidCrystal_I2C lcd(0x27, LCD_WIDTH, LCD_HEIGHT);
SoftwareSerial gpsComms(RX_PIN, TX_PIN);
TinyGPSPlus gps;

// define daylight saving time rules and set up our local timezone
TimeChangeRule EDT = {"EDT", Second, Sun, Mar, 2, -240};
TimeChangeRule EST = {"EST ", First, Sun, Nov, 2, -300};
Timezone ET(EDT, EST);

volatile long metricTime = 0;
volatile long realTime = 0;
volatile unsigned long nextMetricTick = 864;
volatile unsigned long nextRealTick = 1000;
volatile unsigned long lastSync = 0;

void updateLCD() {
    // update the time by iterating digit-by-digit
    // starts with the lsd (least significant digit)
    long metricTimeCopy = metricTime;
    for (byte i = 5; i != 0; i--) {
        lcd.setCursor(DIGITS[i], 0); // select the digit we are manipulating
        lcd.print(metricTimeCopy % 10);    // print the lsd of timeCopy
        metricTimeCopy /= 10;              // drop the lsd of timeCopy
    }

    // update real time
    // hours
    long realTimeCopy = realTime;
    int hours = realTimeCopy / 3600;
    lcd.setCursor(DIGITS[0], 1);
    if (hours < 10) lcd.print("0");
    lcd.print(hours);

    // minutes
    realTimeCopy %= 3600;
    int minutes = realTimeCopy / 60;
    lcd.setCursor(DIGITS[2], 1);
    if (minutes < 10) lcd.print("0");
    lcd.print(minutes);

    // seconds
    realTimeCopy %= 60;
    int seconds = realTimeCopy;
    lcd.setCursor(DIGITS[4], 1);
    if (seconds < 10) lcd.print("0");
    lcd.print(seconds);
}

void getGPSTime() {
    // skip if the GPS isn't working
    if (gps.time.isValid() == false) return;

    // set the time for the timezone library
    setTime(gps.time.hour(),
            gps.time.minute(),
            gps.time.second(),
            gps.date.day(),
            gps.date.month(),
            gps.date.year());
}

void setRealTime() {
    // convert current time to the eastern time zone
    time_t local = ET.toLocal(now());
    realTime = 0;
    realTime += long(hour(local)) * 3600;
    realTime += int(minute(local)) * 60;
    realTime += second(local);
    nextRealTick = millis() + 1000; // keep time in sync
}

void setMetricTime() {
    metricTime = (realTime * 1000) / 864;
    metricTime %= 100000;
    nextMetricTick = millis() + 864; // keep time in sync
}

void updateRealTime(const bool doUpdate = true) {
    realTime++;
    realTime %= 86400;  // rollover at midnight

    if (doUpdate) updateLCD();
}

void updateMetricTime(const bool doUpdate = true) {
    metricTime++;
    metricTime %= 100000;  // rollover at midnight

    if (doUpdate) updateLCD();

}

void syncGPSTime() {
    getGPSTime();
    setRealTime();
    setMetricTime();
    updateLCD();
}

void setup() {
    // start communiction with the GPS module ASAP
    gpsComms.begin(9600);

    // set up the LCD
    lcd.init();
    lcd.clear();
    lcd.backlight();

    // wait for the GPS unit to "warm up"
    lcd.setCursor(1,0);
    lcd.print("Acquiring Time");
    lcd.setCursor(2,0);
    lcd.print("Please Wait");
    // sometimes TinyGPS++ will report the GPS time is valid prematurely
    // when this happens, it will falsely report the time as 00:00:00
    // this will wait until it is no longer midnight before continuing
    bool isMidnight = true;
    while (gps.time.isValid() == false || isMidnight) {
        while (gpsComms.available()) gps.encode(gpsComms.read());

        isMidnight = bool(gps.time.hour() == 0 && gps.time.minute() == 0);
    }

    // finish display setup
    lcd.clear();
    // metric time
    lcd.setCursor(0, 0);
    lcd.print("Metric:");
    for (byte i = 1; i != 255; i--) {
        lcd.setCursor(DELIMS[i], 0);
        lcd.print(DELIMETER);
    }
    // real time
    lcd.setCursor(0, 1);
    lcd.print("Actual:");
    for (byte i = 1; i != 255; i--) {
        lcd.setCursor(DELIMS[i], 1);
        lcd.print(DELIMETER);
    }

    syncGPSTime();
}

void loop() {
    // read all GPS data in the buffer
    while (gpsComms.available()) gps.encode(gpsComms.read());

    // avoid conversion issues by only syncing on whole-second covertable ticks
    long syncDelta = millis() - lastSync;
    if (metricTime % 125 == 0 && syncDelta > 5000) {
        lastSync = millis();
        syncGPSTime();
    }
    
    // check if it is time to tick up metricTime
    int metricTickDelta = millis() - nextMetricTick;
    if (metricTickDelta >= 0) {
        nextMetricTick += 864;
        updateMetricTime();
    }

    int realTickDelta = millis() - nextRealTick;
    // check if it is time to tick up realTime
    if (realTickDelta >= 0) {
        nextRealTick += 1000;
        updateRealTime();
    }
}