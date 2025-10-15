/**
 * NeoPixel fire strip
 *
 * Autor: Paul J Chase
 *
 *
 *
 *
 * Copyright (c) 2025 Paul Chase. All rights reserved.
 * Publication, distribution and modification of this script are under the
 * Condition of attribution permitted and royalty-free.
 */
#include <ESP8266WiFi.h>
#include "WiFiManager.h"        //https://github.com/tzapu/WiFiManager
#include <WiFiUdp.h>
#define FASTLED_INTERNAL        // Suppress build banner
#include <FastLED.h>
#include "fire.h"


#define LED_PIN D7
#define LED_BUILTIN D2
#define NUM_LEDS 20


#define DEBUG 1    //comment this out to remove serial port chatter

//global fastled settings
int g_Brightness = 255;         // 0-255 LED brightness scale
int g_PowerLimit = 3000;         // 900mW Power Limit


void setup() {
  #ifdef DEBUG
    Serial.begin(500000);  // be sure to set your serial monitor to 500000!
    Serial.println("Serial is working");
  #endif
  
  //set up pins
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  
  //set up FastLED
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(g_LEDs, NUM_LEDS);               // Add our LED strip to the FastLED library
  FastLED.setBrightness(g_Brightness);
  set_max_power_indicator_LED(LED_BUILTIN);                               // Light the builtin LED if we power throttle
  FastLED.setMaxPowerInMilliWatts(g_PowerLimit);                          // Set the power limit, above which brightness will be throttled
  
  //set up the wifi
  WiFiManager wifiManager;
  wifiManager.autoConnect("BackFire");
  
  //start the fire effect
  //ClassicFireEffect(int size, int cooling = 80, int sparking = 50, int sparks = 3, int sparkHeight = 4,
  //                  bool breversed = true, bool bmirrored = true)
  ClassicFireEffect fire(NUM_LEDS, 30, 100, 3, 2, true, true);    // Inwards toward Middle
}

void loop() {
    FastLED.clear();
    fire.DrawFire();
    FastLED.show(g_Brightness);                          //  Show and delay

    delay(33);
}

/* int clamp(int num)
 *
 * simple function added to ensure we never ask for brightness that's too bright
 * also a good reason to use ints and not bytes everywhere, so overflow can be handled easily
*/
int clamp(int num) {
  return (num>255) ? 255 : num;
}
