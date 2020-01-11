/*
 * Copyright (c) 2014-2018 Cesanta Software Limited
 * All rights reserved
 *
 * Licensed under the Apache License, Version 2.0 (the ""License"");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an ""AS IS"" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mgos.h"
#include <Arduino.h>
#include <MD_MAX72xx.h>
#include <SPI.h>

// #define LED_PIN 2

#define USE_POT_CONTROL 0
#define PRINT_CALLBACK 0

#define PRINT(s, v)     \
  {                     \
    Serial.print(F(s)); \
    Serial.print(v);    \
  }

// Define the number of devices we have in the chain and the hardware interface
// NOTE: These pin numbers will probably not work with your hardware and may
// need to be adapted
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 2

#define CLK_PIN 18  // or SCK
#define DATA_PIN 23 // or MOSI
#define CS_PIN 5    // or SS

// SPI hardware interface
MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
// Arbitrary pins
//MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, DATA_PIN, CLK_PIN, CS_PIN, MAX_DEVICES);

// Scrolling parameters
#define SCROLL_DELAY 75 // in milliseconds

// Global message buffers shared by Serial and Scrolling functions
#define BUF_SIZE 75
char curMessage[BUF_SIZE];
char newMessage[BUF_SIZE];
bool newMessageAvailable = false;
bool msgShown = false;

uint16_t scrollDelay; // in milliseconds

// Text parameters
#define CHAR_SPACING 1 // pixels between characters

void scrollDataSink(uint8_t dev, MD_MAX72XX::transformType_t t, uint8_t col)
// Callback function for data that is being scrolled off the display
{
#if PRINT_CALLBACK
  Serial.print("\n cb ");
  Serial.print(dev);
  Serial.print(' ');
  Serial.print(t);
  Serial.print(' ');
  Serial.println(col);
#endif
}

uint8_t scrollDataSource(uint8_t dev, MD_MAX72XX::transformType_t t)
// Callback function for data that is required for scrolling into the display
{
  static char *p = curMessage;
  static uint8_t state = 0;
  static uint8_t curLen = 0, showLen = 0;
  static uint8_t cBuf[8];
  uint8_t colData = 0;
  static bool lastChar = false;

  if (msgShown)
    return 0; // dont scroll after shown

  // finite state machine to control what we do on the callback
  switch (state)
  {
  case 0: // Load the next character from the font table
    showLen = mx.getChar(*p++, sizeof(cBuf) / sizeof(cBuf[0]), cBuf);
    curLen = 0;
    state++;

    // if we reached end of message, reset the message pointer
    if (*p == '\0')
    {
      lastChar = true;

      p = curMessage;          // reset the pointer to start of message
      if (newMessageAvailable) // there is a new message waiting
      {
        strcpy(curMessage, newMessage); // copy it in
        newMessageAvailable = false;
      }
    }
    else
    {
      lastChar = false;
    }
    // !! deliberately fall through to next state to start displaying

  case 1: // display the next part of the character
    colData = cBuf[curLen++];
    if (curLen == showLen)
    {
      showLen = CHAR_SPACING;
      curLen = 0;
      state = 2;
    }
    break;

  case 2: // display inter-character spacing (blank column)
    colData = 0;
    curLen++;
    if (curLen == showLen)
    {
      state = 0;
      if (lastChar)
      {
        msgShown = true;
      }
    }

    else
    {
      break;
    }

  default:
    state = 0;
  }

  return (colData);
}

void scrollText(void)
{
  static uint32_t prevTime = 0;

  mx.transform(MD_MAX72XX::TSL); // scroll along - the callback will load all the data
}

uint16_t getScrollDelay(void)
{
  return (SCROLL_DELAY);
}

static void timer_cb(void *arg)
{
  /*   static bool s_tick_tock = false;
  LOG(LL_INFO,
      ("%s uptime: %.2lf, RAM: %lu, %lu free", (s_tick_tock ? "Tick" : "Tock"),
       mgos_uptime(), (unsigned long)mgos_get_heap_size(),
       (unsigned long)mgos_get_free_heap_size()));
  s_tick_tock = !s_tick_tock; */

#ifdef LED_PIN
  mgos_gpio_toggle(LED_PIN);
#endif

  if (msgShown)
    return;

  scrollText();

  (void)arg;
}

extern "C" enum mgos_app_init_result mgos_app_init(void)
{

  mx.begin();
  mx.control(0, MAX_DEVICES - 1, MD_MAX72XX::INTENSITY, 0);
  mx.setShiftDataInCallback(scrollDataSource);
  mx.setShiftDataOutCallback(scrollDataSink);

  scrollDelay = SCROLL_DELAY;
  strcpy(curMessage, "HSBC 58.6");
  newMessage[0] = '\0';

#ifdef LED_PIN
  mgos_gpio_setup_output(LED_PIN, 0);
#endif
  mgos_set_timer(SCROLL_DELAY /* ms */, MGOS_TIMER_REPEAT, timer_cb, NULL);
  return MGOS_APP_INIT_SUCCESS;
}
