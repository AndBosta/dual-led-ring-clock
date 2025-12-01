/**
 * NeoPixel Clock with NTP Synchronization
 * Sunrise/Sunset dimming, smooth fading,
 * top-of-hour animation, 11→12 hour-hand chase, and startup sweep.
 * Modifications by Andrew Albosta with loads of help from ChatGPT

 * NeoPixel Uhr mit NTP-Synchronisierung
 * Dieses Skript steuert einen NeoPixel LED-Ring, um die aktuelle Zeit anzuzeigen,
 * synchronisiert über NTP (Network Time Protocol).
 *
 * Erstellt am: 04.11.2023
 * Letzte Aktualisierung: 04.11.2023 V1.0
 *
 * Autor: Alf Müller / purecrea gmbh Schweiz / www.bastelgarage.ch
 * Updated by Paul J Chase
 *
 *
 * I've added English comments where I thought necessary - most of the setup is the same
 * I modified the main loop to update every LED every time - I prefer constant-time loops
 * on micros - but for the most part the logic is Alf's.
 *
 *
 * Copyright (c) 2023 Alf Müller. Alle Rechte vorbehalten.
 * Veröffentlichung, Verbreitung und Modifikation dieses Skripts sind unter der
 * Bedingung der Namensnennung erlaubt und lizenzfrei.
 *
 * Modifications Copyright (c) 2023 Paul Chase. All rights reserved.
 * Publication, distribution and modification of this script are under the
 * Condition of attribution permitted and royalty-free.
 *
 * Modifications Copyright (c) 2025 Andrew Albosta. All rights reserved.
 * Publication, distribution and modification of this script are under the
 * Condition of attribution permitted and royalty-free.
 */

#include <ESP8266WiFi.h>
#include "WiFiManager.h"
#include <Timezone.h>
#include <TimeLib.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Adafruit_NeoPixel.h>
#include <Dusk2Dawn.h>

#define PIN D7               // GPIO13, verified working
#define NUMPIXELS 120

#define DEBUG 1

// ---------- NeoPixel ----------
Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

// ---------- NTP ----------
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);  // No offset; Timezone handles conversion

// ---------- Timezone (US Eastern) ----------
TimeChangeRule usEDT = {"EDT", Second, Sun, Mar, 2, -240};  // UTC-4
TimeChangeRule usEST = {"EST", First, Sun, Nov, 2, -300};   // UTC-5
Timezone usEastern(usEDT, usEST);

// ---------- Dusk2Dawn (Washington DC) ----------
const double LAT = 38.9072;
const double LON = -77.0369;
const float TZ_OFFSET = -5.0;

Dusk2Dawn dc(LAT, LON, TZ_OFFSET);

// Cached sunrise/sunset
int sunriseMinutes = -1;
int sunsetMinutes  = -1;
int lastYear  = -1;
int lastMonth = -1;
int lastDay   = -1;

// ---------- Brightness Control ----------
const uint8_t BRIGHT_LEVEL = 190;   // about 75%
const uint8_t DIM_LEVEL    = 30;

const int DIM_AFTER_SUNSET      = 30;  // minutes after sunset = dim
const int BRIGHT_BEFORE_SUNRISE = 30;  // minutes before sunrise = bright

// Smooth fade duration: 5 minutes
const unsigned long FADE_DURATION_MS = 5UL * 60UL * 1000UL;

// Fade state
uint8_t currentBrightness = BRIGHT_LEVEL;
uint8_t targetBrightness  = BRIGHT_LEVEL;
uint8_t startBrightness   = BRIGHT_LEVEL;
unsigned long fadeStart   = 0;
bool fading = false;
bool lastDimState = false;

// Animation tracking
int lastMinute = -1;
int lastHour   = -1;

// --------------------------------------------------
// Helpers
// --------------------------------------------------
int clampInt(int v) {
  if (v < 0) return 0;
  if (v > 255) return 255;
  return v;
}

void updateSunTimesIfNeeded(time_t utc, time_t local) {
  int y = year(local);
  int m = month(local);
  int d = day(local);

  if (y == lastYear && m == lastMonth && d == lastDay) return;

  TimeChangeRule *rule;
  usEastern.toLocal(utc, &rule);
  bool isDST = (rule == &usEDT);

  sunriseMinutes = dc.sunrise(y, m, d, isDST);
  sunsetMinutes  = dc.sunset(y, m, d, isDST);

  lastYear = y;
  lastMonth = m;
  lastDay = d;

  #ifdef DEBUG
    Serial.print("Updated sunrise/sunset for ");
    Serial.print(y); Serial.print("-");
    Serial.print(m); Serial.print("-");
    Serial.println(d);

    char sr[6], ss[6];
    Dusk2Dawn::min2str(sr, sunriseMinutes);
    Dusk2Dawn::min2str(ss, sunsetMinutes);

    Serial.print("  Sunrise: "); Serial.println(sr);
    Serial.print("  Sunset : "); Serial.println(ss);
  #endif
}

bool shouldBeDim(int currentMinutes) {
  if (sunriseMinutes < 0 || sunsetMinutes < 0) return false;

  int dimStart   = sunsetMinutes + DIM_AFTER_SUNSET;
  int brightStart = sunriseMinutes - BRIGHT_BEFORE_SUNRISE;

  if (brightStart < 0) brightStart = 0;
  if (dimStart > 1440) dimStart = 1440;

  return (currentMinutes < brightStart || currentMinutes >= dimStart);
}

// Smooth brightness transition over FADE_DURATION_MS
void updateFade(bool dimNow) {
  unsigned long now = millis();

  // Start new fade when dim/bright state changes
  if (dimNow != lastDimState) {
    lastDimState   = dimNow;
    startBrightness = currentBrightness;
    targetBrightness = dimNow ? DIM_LEVEL : BRIGHT_LEVEL;
    fadeStart = now;
    fading = true;
  }

  if (!fading) {
    currentBrightness = targetBrightness;
  } else {
    unsigned long elapsed = now - fadeStart;

    if (elapsed >= FADE_DURATION_MS) {
      currentBrightness = targetBrightness;
      fading = false;
    } else {
      float t = (float)elapsed / (float)FADE_DURATION_MS;
      float val = startBrightness + t * (targetBrightness - startBrightness);
      currentBrightness = clampInt((int)val);
    }
  }

  pixels.setBrightness(currentBrightness);
}

// Draw the time onto the LEDs
void drawTimeOnLEDs(
  int hours, int minutes, int seconds,
  byte rMin, byte gMin, byte bMin,
  byte rHour, byte gHour, byte bHour,
  byte rSec, byte gSec, byte bSec
) {
  hours = hours % 12;
  int hourPixels = hours * 5 + minutes / 12;

  for (int i = 0; i < 60; i++) {
    byte rm=0, gm=0, bm=0;
    byte rh=0, gh=0, bh=0;

    // Seconds
    if (i == seconds) {
      rm += rSec; gm += gSec; bm += bSec;
      rh += rSec; gh += gSec; bh += bSec;
    }

    // Minutes
    if (i < minutes) {
      rm += rMin; gm += gMin; bm += bMin;
    }

    // Hours
    if (i < hourPixels) {
      rh += rHour; gh += gHour; bh += bHour;
    }

    // Map indexes
    int mIdx = (i + 29) % 60;
    int hIdx = ((90 - i) % 60) + 60;

    pixels.setPixelColor(mIdx, pixels.Color(clampInt(rm), clampInt(gm), clampInt(bm)));
    pixels.setPixelColor(hIdx, pixels.Color(clampInt(rh), clampInt(gh), clampInt(bh)));
  }

  pixels.show();
}

// Hour-change animation: minutes always chase off;
// on 11→12, hours also chase off
void runHourAnimation(int prevHour, int newHour) {
  int prevH = prevHour % 12;
  int newH  = newHour % 12;
  bool wrap11to12 = (prevH == 11 && newH == 0);

  int prevHourPixels = prevH * 5;
  int newHourPixels  = newH * 5;

  const byte rMin  = 0, gMin  = 0,   bMin  = 255; // blue
  const byte rHour = 0, gHour = 127, bHour = 0;   // greenish

  // 60 steps × 50 ms ≈ 3 seconds
  for (int step = 0; step < 60; step++) {
    // Clear all
    for (int i = 0; i < NUMPIXELS; i++) pixels.setPixelColor(i, 0);

    // Minutes: from 'step' to 59 remain lit
    for (int m = step; m < 60; m++) {
      int idx = (m + 29) % 60;
      pixels.setPixelColor(idx, pixels.Color(rMin, gMin, bMin));
    }

    // Hours:
    if (wrap11to12) {
      // Chase off the previous hour segments
      for (int i = step; i < prevHourPixels; i++) {
        int idx = ((90 - i) % 60) + 60;
        pixels.setPixelColor(idx, pixels.Color(rHour, gHour, bHour));
      }
    } else {
      // Normal hour change: show new hour statically
      for (int i = 0; i < newHourPixels; i++) {
        int idx = ((90 - i) % 60) + 60;
        pixels.setPixelColor(idx, pixels.Color(rHour, gHour, bHour));
      }
    }

    pixels.show();
    delay(50);
  }
}

// Startup sweep: light each pixel around both rings
void runStartupSweep() {
  // Minute ring sweep
  for (int i = 0; i < 60; i++) {
    pixels.clear();
    int mIdx = (i + 29) % 60;
    pixels.setPixelColor(mIdx, pixels.Color(0, 0, 255)); // blue
    pixels.show();
    delay(20);
  }

  // Hour ring sweep
  for (int i = 0; i < 60; i++) {
    pixels.clear();
    int hIdx = ((90 - i) % 60) + 60;
    pixels.setPixelColor(hIdx, pixels.Color(0, 127, 0)); // greenish
    pixels.show();
    delay(20);
  }

  // Clear at end
  pixels.clear();
  pixels.show();
}

// --------------------------------------------------
// Setup / Loop
// --------------------------------------------------
void setup() {
  #ifdef DEBUG
    Serial.begin(500000);
    Serial.println("Booting...");
  #endif

  pixels.begin();
  pixels.setBrightness(currentBrightness);
  pixels.show();

  // Nice startup effect
  runStartupSweep();

  WiFiManager wifi;
  wifi.autoConnect("NeoPixelClock");

  timeClient.begin();
}

void loop() {
  timeClient.update();

  time_t utc   = timeClient.getEpochTime();
  time_t local = usEastern.toLocal(utc);

  updateSunTimesIfNeeded(utc, local);

  int hours   = hour(local);
  int minutes = minute(local);
  int seconds = second(local);
  int minsSinceMidnight = hours * 60 + minutes;

  // Bright/dim fade logic
  bool dimNow = shouldBeDim(minsSinceMidnight);
  updateFade(dimNow);

  // Hour-change animation (detect 59→0)
  if (lastMinute == 59 && minutes == 0) {
    runHourAnimation(lastHour, hours);
  }

  lastMinute = minutes;
  lastHour   = hours;

  // Draw the time
  drawTimeOnLEDs(
    hours, minutes, seconds,
    0, 0, 255,     // minutes = blue
    0, 127, 0,     // hours = greenish
    255, 0, 0      // seconds = red
  );

  delay(100);
}
