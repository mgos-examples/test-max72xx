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
#define MODE_CYCLE_PIN 33

// for use with: void mgos_clear_timer(mgos_timer_id id);
static mgos_timer_id scroll_timer_id = 0;

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

void printText(uint8_t modStart, uint8_t modEnd, char *pMsg)
// Print the text string to the LED matrix modules specified.
// Message area is padded with blank columns after printing.
{
  uint8_t state = 0;
  uint8_t curLen = 0;
  uint16_t showLen = 0;
  uint8_t cBuf[8];
  int16_t col = ((modEnd + 1) * COL_SIZE) - 1;

  mx.control(modStart, modEnd, MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);

  do // finite state machine to print the characters in the space available
  {
    switch (state)
    {
    case 0: // Load the next character from the font table
      // if we reached end of message, reset the message pointer
      if (*pMsg == '\0')
      {
        showLen = col - (modEnd * COL_SIZE); // padding characters
        state = 2;
        break;
      }

      // retrieve the next character form the font file
      showLen = mx.getChar(*pMsg++, sizeof(cBuf) / sizeof(cBuf[0]), cBuf);
      curLen = 0;
      state++;
      // !! deliberately fall through to next state to start displaying

    case 1: // display the next part of the character
      mx.setColumn(col--, cBuf[curLen++]);

      // done with font character, now display the space between chars
      if (curLen == showLen)
      {
        showLen = CHAR_SPACING;
        state = 2;
      }
      break;

    case 2: // initialize state for displaying empty columns
      curLen = 0;
      state++;
      // fall through

    case 3: // display inter-character spacing or end of message padding (blank columns)
      mx.setColumn(col--, 0);
      curLen++;
      if (curLen == showLen)
        state = 0;
      break;

    default:
      col = -1; // this definitely ends the do loop
    }
  } while (col >= (modStart * COL_SIZE));

  mx.control(modStart, modEnd, MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
}

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
    break;

  default:
    state = 0;
  }

  return (colData);
}

static void scroll_left_cb(void *arg)
{
  mx.transform(MD_MAX72XX::TSL); // scroll left
  (void)arg;
}

static void scroll_up_cb(void *arg)
{
  static int count = 0;
  if ( count++ < 8 ) {;
    mx.transform(MD_MAX72XX::TSU); // scroll up
  } else {
    count = 0;
    mgos_clear_timer(scroll_timer_id); // stop timer
    strcpy(curMessage, "30%");
    printText(0, MAX_DEVICES -1, curMessage);    
  }
  (void)arg;
}

/* --- list of tests --- */

static void testPrintText(const char *msg)
{
  strcpy(curMessage, msg);
  printText(0, MAX_DEVICES - 1, curMessage);
}

static void testScrollTextLeft(const char *msg)
{
  strcpy(curMessage, msg);
  scroll_timer_id = mgos_set_timer(SCROLL_DELAY /* ms */, MGOS_TIMER_REPEAT, scroll_left_cb, NULL);
}

static void testScrollTextUp(const char *msg)
{
  strcpy(curMessage, msg);
  printText(0, MAX_DEVICES - 1, curMessage);
  scroll_timer_id = mgos_set_timer(SCROLL_DELAY + 100 /* ms */, MGOS_TIMER_REPEAT, scroll_up_cb, NULL);
}

// show '28' using 2 device
static void testSetChar() {
  mx.update(MD_MAX72XX::OFF);

  int leftmost_col = (MAX_DEVICES * 8) - 1;
  int second_device_col = leftmost_col - 8;
  mx.setChar(leftmost_col, 50); // '2'
  mx.setChar(second_device_col, 56); // '8'
  mx.update();
}

// rotate font
static void testRotate() {
  testSetChar();
  mx.transform(MD_MAX72XX::TRC);
  mx.update();
}

static int test_mode = -1;
static void cycle_test_mode_cb(int pin, void *arg)
{
  test_mode = (test_mode + 1) % 5;
  mgos_clear_timer(scroll_timer_id);
  mx.clear();

  switch (test_mode)
  {
  case 0: // testPrintText
    testPrintText("RDY");
    break;

  case 1:
    testScrollTextLeft("Scroll Left Test");
    break;
  
  case 2:
    testScrollTextUp("\x18" "UP");
    break;

  case 3:
    testSetChar();
    break;

  case 4:
    testRotate();
    break;

  default:
    test_mode = 0;
    testPrintText("Rst");
  }

  (void)arg;
}

/* --- finally, main entry --- */
extern "C" enum mgos_app_init_result mgos_app_init(void)
{
  mx.begin();
  mx.control(0, MAX_DEVICES - 1, MD_MAX72XX::INTENSITY, 0);
  scrollDelay = SCROLL_DELAY;
  mx.setShiftDataInCallback(scrollDataSource);
  mx.setShiftDataOutCallback(scrollDataSink);

  // default is testPrintText:
  testPrintText("RDY");

  mgos_gpio_set_button_handler(MODE_CYCLE_PIN, MGOS_GPIO_PULL_UP, MGOS_GPIO_INT_EDGE_NEG, 100, cycle_test_mode_cb, NULL);

  return MGOS_APP_INIT_SUCCESS;
}
