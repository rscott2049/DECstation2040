/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021, Ha Thach (tinyusb.org)
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
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <pico/stdlib.h>
#include "pico/sync.h"
#include "pico/multicore.h"

#include "bsp/board.h"
#include "tusb.h"

#include "dz11.h"
#include "usbHID.h"

//--------------------------------------------------------------------+
// MACRO TYPEDEF CONSTANT ENUM DECLARATION
//--------------------------------------------------------------------+

#define MAX_REPORT  4

static uint8_t const keycode2ascii[128][2] =  { HID_KEYCODE_TO_ASCII };

// Each HID instance can has multiple reports
static struct
{
  uint8_t report_count;
  tuh_hid_report_info_t report_info[MAX_REPORT];
}hid_info[CFG_TUH_HID];

// Save report count from initial enumeration
uint32_t init_report_count = 0;

static void process_kbd_report(hid_keyboard_report_t const *report);
static void process_mouse_report(hid_mouse_report_t const * report);
static void process_generic_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len);

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

// Invoked when device with hid interface is mounted
// Report descriptor is also available for use. tuh_hid_parse_report_descriptor()
// can be used to parse common/simple enough descriptor.
// Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE, it will be skipped
// therefore report_desc = NULL, desc_len = 0
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len)
{
  printf("HID device address = %d, instance = %d is mounted\r\n", dev_addr, instance);

  // Interface protocol (hid_interface_protocol_enum_t)
  const char* protocol_str[] = { "None", "Keyboard", "Mouse" };
  uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);

  printf("HID Interface Protocol = %s\r\n", protocol_str[itf_protocol]);

  // By default host stack will use activate boot protocol on supported interface.
  // Therefore for this simple example, we only need to parse generic report descriptor (with built-in parser)
  if ( itf_protocol == HID_ITF_PROTOCOL_NONE )
  {
    hid_info[instance].report_count = tuh_hid_parse_report_descriptor(hid_info[instance].report_info, MAX_REPORT, desc_report, desc_len);
    init_report_count = hid_info[instance].report_count;
  }

  // request to receive report
  // tuh_hid_report_received_cb() will be invoked when report is available
  if ( !tuh_hid_receive_report(dev_addr, instance) )
  {
    printf("Error: cannot request to receive report\r\n");
  }
}

// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
  printf("HID device address = %d, instance = %d is unmounted\r\n", dev_addr, instance);
}

// Invoked when received report from device via interrupt endpoint
void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  uint8_t itf_protocol = HID_ITF_PROTOCOL_NONE;
  uint8_t const* report_offset = report;

  if (hid_info[instance].report_count > 1) {
    // we know this is a composite report, so extract the id
    // and start the reading the data one byte over
    report_offset = report + 1;
    uint8_t id = report[0];

    // find the tuh_hid_report_info_t that matches the id of the
    // report we just received 
    for (size_t i = 0; i < hid_info[instance].report_count; i++) {
      tuh_hid_report_info_t info = hid_info[instance].report_info[i];
      if (info.report_id == id) {
        if (info.usage_page == HID_USAGE_PAGE_DESKTOP) {
          if (info.usage == HID_USAGE_DESKTOP_MOUSE) {
            itf_protocol = HID_ITF_PROTOCOL_MOUSE;
          } else if (info.usage == HID_USAGE_DESKTOP_KEYBOARD) {
            itf_protocol = HID_ITF_PROTOCOL_KEYBOARD;
          }
        }
        // TODO handle other usage pages, Consume Control, etc?
        break;
      }
    }
  } else {
    itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
  }

  switch (itf_protocol)
  {
    case HID_ITF_PROTOCOL_KEYBOARD:
      //TU_LOG2("HID receive boot keyboard report\r\n");
      process_kbd_report( (hid_keyboard_report_t const*) report_offset );
    break;

    case HID_ITF_PROTOCOL_MOUSE:
      //TU_LOG2("HID receive boot mouse report\r\n");
      // Fix minikeyboard report count
      if (init_report_count == 0) {
	report_offset = report + 1;
      }
      process_mouse_report( (hid_mouse_report_t const*) report_offset );
    break;

    default:
      // Generic report requires matching ReportID and contents with previous parsed report info
      process_generic_report(dev_addr, instance, report, len);
    break;
  }

  // continue to request to receive report
  if ( !tuh_hid_receive_report(dev_addr, instance) )
  {
    printf("Error: cannot request to receive report\r\n");
  }
}

//--------------------------------------------------------------------+
// Keyboard
//--------------------------------------------------------------------+

// look up new key in previous keys
static inline bool find_key_in_report(hid_keyboard_report_t const *report, uint8_t keycode)
{
  for(uint8_t i=0; i<6; i++)
  {
    if (report->keycode[i] == keycode)  return true;
  }

  return false;
}

#if 0
static void process_kbd_report(hid_keyboard_report_t const *report)
{
  static hid_keyboard_report_t prev_report = { 0, 0, {0} }; // previous report to check key released

  //------------- example code ignore control (non-printable) key affects -------------//
  for(uint8_t i=0; i<6; i++)
  {
    if ( report->keycode[i] )
    {
      if ( find_key_in_report(&prev_report, report->keycode[i]) )
      {
        // exist in previous report means the current key is holding
      }else
      {
        // not existed in previous report means the current key is pressed
        bool const is_shift = report->modifier & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT);
        uint8_t ch = keycode2ascii[report->keycode[i]][is_shift ? 1 : 0];
        putchar(ch);
        if ( ch == '\r' ) putchar('\n'); // added new line for enter key

        fflush(stdout); // flush right away, else nanolib will wait for newline
      }
    }
    // TODO example skips key released
  }

  prev_report = *report;
}
#endif

#if 1 //kb
// PC 105-key keyboard (USB HID Boot mode codes) to LK201 mapping.
//
// Entries in the main lookup table are keyed by the scan code.  The
// value is the LK201 keycode.  Entries in the "divs" table are keyed
// by LK201 keycode, and give the "keyboard division" number which
// selects the mode (up/down, autorepeat, down only) and, if
// applicable, autorepeat parameter buffer.
//
// For the most part these mappings are obvious.  The 6 editing keys
// (above the cursor keys) are mapped according to their location, not
// their labels, so "home" is coded as LK201 "insert here" (rather than
// using the "insert" key for this code).
//
// Similarly, num lock, scroll lock and pause become F14, F15 (Help)
// and F16 (Do).  And the numeric keypad mappings are by layout.  Note
// that there is no PF4 because the PC keypad has one key less than the
// LK201, so the - key is used for - (Cut) instead of PF4 as its
// placement might suggest.
//
// The left Windows key is the < > key.
//
// For convenience, Esc is mapped to F11 which is its conventional
// interpretation.  Both shifts are shift, but the right
// Alt/Win/App/Ctrl keys are F17 to F20 instead.

const uint8_t divs[] =
{
  10, 10, 10, 10, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  11, 11, 11, 11, 11, 0, 0, 0, 0, 0, 0, 0, 0,
  12, 12, 12, 12, 0, 0, 0, 0, 0, 0, 0, 13, 13, 0, 0,
  14, 14, 14, 14, 0, 0, 0, 0, 0, 0, 9, 9, 9, 9, 9, 9,
  0, 0, 2, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
  0, 0, 0, 7, 7, 8, 8, 0, 0, 0, 6, 6, 5, 5, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0, 3, 4, 4, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1,
  0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0,
  1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0,
  1, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1
};
// First LK201 code in divs is 86
#define DIVBASE 86

// Main lookup table, indexed by report scan code
const uint8_t usb_lk201[] =
{
  0,               // No keys
  0,               // Rollover overflow
  0,               // error
  0,               // reserved
  194,             // A
  217,             // B
  206,             // C
  205,             // D
  204,             // E
  210,             // F
  216,             // G
  221,             // H
  230,             // I
  226,             // J
  231,             // K
  236,             // L
  227,             // M
  222,             // N
  235,             // O
  240,             // P
  193,             // Q
  209,             // R
  199,             // S
  215,             // T
  225,             // U
  211,             // V
  198,             // W
  200,             // X
  220,             // Y
  195,             // Z
  192,             // 1
  197,             // 2
  203,             // 3
  208,             // 4
  214,             // 5
  219,             // 6
  224,             // 7
  229,             // 8
  234,             // 9
  239,             // 0
  189,             // Enter
  113,             // Esc (F11)
  188,             // Backspace
  190,             // Tab
  212,             // Space
  249,             // - / _
  245,             // = / +
  250,             // [ / {
  246,             // ] / }
  247,             // \ / |
  0,               // unused code
  242,             // ; / :
  251,             // ' / "
  191,             // ` / ~
  232,             // , / <
  237,             // . / >
  243,             // / / ?
  176,             // Caps Lock
  86,              // F1
  87,              // F2
  88,              // F3
  89,              // F4
  90,              // F5
  100,             // F6
  101,             // F7
  102,             // F8
  103,             // F9
  104,             // F10
  113,             // F11
  114,             // F12
  116,             // PrtScr (F14)
  124,             // Scroll Lock (Help)
  125,             // Pause (Do)
  138,             // Insert (Find)
  139,             // Home (Insert here)
  140,             // PgUp (Remove)
  141,             // Delete (Select)
  142,             // End (Prev Screen)
  143,             // PgDn (Next Screen)
  168,             // Right
  167,             // Left
  169,             // Down
  170,             // Up
  161,             // Num Lock (PF1)
  162,             // KP / (PF2)
  163,             // KP * (PF3)
  160,             // KP -
  156,             // KP + (num comma)
  149,             // KP Enter
  150,             // KP 1 / End
  151,             // KP 2 / Down
  152,             // KP 3 / PgDn
  153,             // KP 4 / Left
  154,             // KP 5
  155,             // KP 6 / Right
  157,             // KP 7 / Home
  158,             // KP 8 / Up
  159,             // KP 9 / PgUp
  146,             // KP 0 / Ins
  148,             // KP . / Del
  0,               // unused code
  130              // Applic (F19)
};

#define MAXSCAN sizeof (usb_lk201)

// Modifier lookup table, indexed by bit number
const uint8_t usbmod_lk201[] =
{
  175,             // Left control
  174,             // Shift
  177,             // Left Alt (Compose)
  201,             // Left Windows  (< >)
  131,             // Right control (F20)
  0,               // Unused (right shift)
  128,             // Right Alt (F17)
  129              // Right Windows (F18)
};

// Modifier bit values
#define LCTRL  0x01
#define LSHIFT 0x02
#define LALT   0x04
#define LWIN   0x08
#define RCTRL  0x10
#define RSHIFT 0x20
#define RALT   0x40
#define RWIN   0x80

// Modes
#define D 0         // Down only
#define A 1         // Autorepeat
#define U 3         // Up/down

// Default mode settings for the divisions.  Note that divisions are
// numbered starting at 1.
// Defaults are: main, keypad, delete, cursor - autorepeat; return,
// tab, lock, compose: down only; shift, control, editing: down/up.
// Documentation doesn't say what we do for function keys: make those
// down only.
// Division 0 doesn't exist, we use it internally for special keys
// like ALL UP.  Set it to up/down so those are always sent.
const uint8_t defmodes[] =
{ U, A, A, A, D, D, U, A, A, U, D, D, D, D, D };

// Active modes, loaded from defmode at reset but can be modified by
// the host.
uint8_t modes[sizeof (defmodes)];

// Auto-repeat buffers
typedef struct
{
  int timeout;      // Delay before first repeat
  int interval;     // Delay between subsequent repeats
}  arparams_t;

// Timer interrupt interval is 0.25 ms, to give 2 kHz square wave
// (which is the resonant frequency of the piezo beeper used).
#define INTRATE 4000

#define MS2DELAY(x)   ((x) * INTRATE / 1000)
#define WPM2DELAY(x)  MS2DELAY (1200 / (x))
#define RATE2DELAY(x) (INTRATE / (x))
#define ARBUF(tmo,rate) { MS2DELAY (tmo), RATE2DELAY (rate) }

const arparams_t defarbufs[4] =
{
  ARBUF (500, 30),
  ARBUF (300, 30),
  ARBUF (500, 40),
  ARBUF (300, 40)
};

// These are the active params, loaded at reset but changeable.
arparams_t arbufs[4];

// Auto-repeat buffer assignments for divisions.  Documentation
// doesn't state what the default assignment is for divisions that
// don't default to autorepeat; use zero for those.
const uint8_t defar[] =
{ 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0 };

// Active auto-repeat buffer assignments, loaded from defar at reset
// but can be modified by the host.
uint8_t ar[sizeof (defar)];

// Current auto-repeat values
int arDelay;
const arparams_t *arBuf;
int nextArKey;

// Ident stuff
#define IDUNIT WPM2DELAY (30)   // 30 wpm
#define T 1
#define H (T * 3)
#define S (-T)
#define LS (-H)

const int8_t *idp;

const int8_t errA[] =
{ LS, T, S, H, LS, 0 };

const int8_t errB[] =
{ LS, H, S, T, S, T, S, T, LS, 0 };

#if DOIDENT
const int8_t ident[] =
{ H, S, T, S, T, LS, T, LS, LS, S,
  H, S, T, LS, T, S, T, LS,
  T, S, H, S, H, S, H, S, H, LS,
  H, S, T, S, T, 0 
};
#endif

// USB scan state
uint8_t prevmod;
uint8_t prevscans[7];
uint8_t curmod;
uint8_t curscans[7];

// LK201 key down stack.  This tracks all the LK201 keys that are
// down, except for Up/Down mode keys (modifiers like shift).  The
// purpose is to handle autorepeat when keys are pressed and released;
// the rule is that the most recent key that is still down is
// autorepeated if applicable to that key.
uint8_t lkdown[14];
int downcnt;

// LK201 command buffer
static uint8_t cmd[4];
static int cmdcnt;

void send (int k) {
  if (dz11numBytesFreeInRxBuffer(0) > 0) {
    dz11charRx(0, k);
  }
}

void resetkb ()
{
  // Keyboard reset code
  memcpy (modes, defmodes, sizeof (modes));
  memcpy (ar, defar, sizeof (ar));
  memcpy (arbufs, defarbufs, sizeof (arbufs));

  // Reset current state
  prevmod = 0;
  memset (prevscans, 0, sizeof (prevscans));
  downcnt = cmdcnt = 0;

  //clickOff = ctrlClick = bellOff = false;
  //clickVol = bellVol = DEFVOL;
  arDelay = 0;
  
  // Send reply
  send (1);
  send (0);
  send (0);
  send (0);
  // Flash the LEDs
  //changeLeds (0x0f, true);
  //delay (100);
  //changeLeds (0x0f, false);
}

void setar (int lk, bool sendlk)
{
  const int div = divs[lk - DIVBASE];
  if (modes[div] == A)
  {
    arBuf = &arbufs[ar[div]];
    arDelay = arBuf->timeout;
    if (sendlk)
      nextArKey = lk;        // First time, send the actual key
    else
      nextArKey = 180;       // Metronome code
  }
}

void key(int lk, bool down)
{
  const int div = divs[lk - DIVBASE];

  // First of all, cancel any current auto-repeat on every new
  // keystroke.
  arDelay = 0;

  // If this isn't an up/down key, adjust the list of currently down
  // keys -- this is what allows auto-repeat to work properly when
  // several repeating keys are pressed and released.
  if (modes[div] != U)
  {
    if (down)
    {
      // Key down, push it onto the "down" stack
      if (downcnt >= sizeof (lkdown))
      {
        // No room, something is wrong
        //panic ('A', errA);
        return;
      }
      if (downcnt)
        memmove (lkdown + 1, lkdown, downcnt);
      downcnt++;
      lkdown[0] = lk;
    }
    else
    {
      // Key up, remove it from the list
      int i;
      for (i = 0; i < downcnt; i++)
        if (lkdown[i] == lk)
          break;
      if (i == downcnt)
      {
        // Not there, bug, just exit
        //panic ('B', errB);
        return;
      }
      if (i != downcnt - 1)
        memmove (lkdown + i, lkdown + i + 1, downcnt - i - 1);
      downcnt--;

      // If anything is left, and the last entry (most recent key
      // that's still down) is up/down, start it autorepeating.
      if (downcnt)
        setar (lkdown[0], true);
      return;
    }
  }
  
  send (lk);
#if 0
  if (down && lk != 174 && (lk != 175 || ctrlClick))
  {
    doClick ();
  }
#endif
  // If this key is an auto-repeating one, set up the initial delay
  setar (lk, false);
}

static void process_kbd_report(hid_keyboard_report_t const *report)
{
  bool down;
  int i, j;
  uint8_t p, c;

  if (report->keycode[0] == 1)
    return;   // Error case

  curmod = report->modifier;
  memcpy (curscans, &(report->keycode[0]), 6);
  if (curmod == 0 && curscans[0] == 0)
  {
    // No keys down, so send "all up"
    send (179);
    // No more autorepeat, nothing down
    arDelay = downcnt = 0;
  }
  else
  {
    if (curmod & RSHIFT)
    {
      curmod = (curmod | LSHIFT) & (~RSHIFT);
    }
    int cmod = curmod ^ prevmod;
    for (int i = 0; i < 8; i++)
    {
      if (cmod & (1 << i))
      {
        down = ((curmod & (1 << i)) != 0);
        key (usbmod_lk201[i], down);
      }
    }
    i = j = 0;
    while (i < 7 && j < 7)
    {
      p = prevscans[j];
      c = curscans[i];
      if (p != c)
      {
        if (p == 0)
        {
          // New key, so send "down"
          if (c < MAXSCAN)
          {
            // New key down, so send "down"
            key (usb_lk201[c], true);
          }
          i++;
        }
        else
        {
          if (p < MAXSCAN)
          {
            // Previous key down, so send "up"
            key (usb_lk201[p], false);
          }
          j++;
        }
      }
      else
      {
        // Matching keys, advance both
        i++;
        j++;
      }
    }
  }
  // Save current into previous
  prevmod = curmod;
  memcpy (prevscans, curscans, 6);
}

// Process LK201 commands from host
void decKeyboardTx(uint8_t inChar) {

    if (inChar == 0xfd)
    {
      // Power up reset is recognized immediately
      resetkb ();
      return;
    }
    
    if (cmdcnt < sizeof (cmd))
    {
      cmd[cmdcnt++] = inChar;
    }
    // If not end of parameters, keep collecting
    if ((inChar & 0x80) == 0) return;
      //continue;

    const uint8_t op = cmd[0];
    
    if ((op & 1) == 0)
    {
      // Mode or auto-repeat parameters
      const int div = (op >> 3) & 0x0f;
      const int modnum = (op >> 1) & 0x03;
      
      if (div == 0x0f)
      {
        // Auto-repeat params.  "modnum" is the buffer number
        if (cmdcnt > 2)
        {
          arbufs[modnum].timeout = MS2DELAY ((cmd[1] & 0x7f) * 5);
          int rate = cmd[2] & 0x7f;
          if (rate < 12)
            rate = 12;
          arbufs[modnum].interval = RATE2DELAY (rate);
        }
      }
      else
      {
        // Set mode
        if (div > 0 && div < sizeof (modes) && modnum != 2)
        {
          modes[div] = modnum;
          // Do AR buffer change if requested
          if (cmdcnt > 1)
          {
            ar[div] = cmd[1] & 0x03;
          }
        }
        // Ack the mode change
        send (186);
      }
    }
    else
    {
      switch (op)
      {
      case 0xab:
        // Request keyboard ID
        send (0);
        send (0);
        break;
      case 0x11:
        // LEDs off
        //changeLeds (cmd[1], false);
        break;
      case 0x13:
        // LEDs on
        //changeLeds (cmd[1], true);
        break;
      case 0x99:
        // Disable key click
        //clickOff = true;
        break;
      case 0x1b:
        // Enable key click, set volume
        //clickOff = false;
        //clickVol = cmd[1] & 7;
        break;
      case 0xb9:
        // Disable click for CTRL
        //ctrlClick = false;
        break;
      case 0xbb:
        // Enable click for CTRL
        //ctrlClick = true;
        break;
      case 0x9f:
        // sound click
        //doClick ();
        break;
      case 0xA1:
        // Disable bell
        //bellOff = true;
        break;
      case 0x23:
        // Enable bell, set volume
        //bellOff = false;
        //bellVol = cmd[1] & 7;
        break;
      case 0xa7:
        // Sound bell
        //doBell ();
        break;
      case 0xc1:
        // Inhibit autorepeat, current key only
        arDelay = 0;
        break;
      }
    }
    
    // Reset command buffer
    cmdcnt = 0;
}

#else
void decKeyboardTx(uint8_t chr) {
}
#endif //kb


//--------------------------------------------------------------------+
// Mouse
//--------------------------------------------------------------------+

static uint8_t dec_report[3];
// Power up default - prompt mode
static uint32_t dec_mode = 'D';

static void process_mouse_report(hid_mouse_report_t const * report)
{

#if 0
  printf("core number: %d\n", get_core_num());
  printf("usb: b: %02x x: %02x y: %02x w: %02x\n",
	 report->buttons, report->x, report->y, report->wheel);
#endif
  

  // Initialize DEC mouse report byte 0
  dec_report[0] = 0x80;

  // Translate USB button state to DEC mouse format
  dec_report[0] |= report->buttons & MOUSE_BUTTON_LEFT   ? 0x04 : 0x00;
  dec_report[0] |= report->buttons & MOUSE_BUTTON_MIDDLE ? 0x02 : 0x00;
  dec_report[0] |= report->buttons & MOUSE_BUTTON_RIGHT  ? 0x01 : 0x00;
  
  if (dec_report[0] & 0x01) printf("report 0: %02x\n", dec_report[0]);
  
  // Make movement sign/magnitude
  if (report->x >= 0) {
    dec_report[0] |= 0x10;
    dec_report[1] = (report->x) & 0x7f;
  } else {
    dec_report[1] = (-report->x) & 0x7f;
  }

  if (report->y >= 0) {
    dec_report[2] = (report->y) & 0x7f;
  } else {
    dec_report[0] |= 0x08;
    dec_report[2] = (-report->y) & 0x7f;
  }

  
#if 0
  for (uint32_t i = 0; i < 3; i++) {
    printf("%d mouse: %02x\n", i, dec_report[i]);
  }

  printf("mouse: b: %02x x: %02x y: %02x\n",
	 dec_report[0], dec_report[1], dec_report[2]);
#endif

  // If mode is streaming, then send data now
  if (dec_mode == 'R') {
    // Transmit only if there's space in the buffer
    if (dz11numBytesFreeInRxBuffer(1) > 3) {
      for (uint32_t i = 0; i < 3; i++) {
	dz11charRx(1, dec_report[i]);
      }
    }
  }
}

void decMouseTx(uint8_t chr) {

  // Process mode switch byte
  switch(chr) {
  case 'R': // Streaming mode - send data upon mouse movement/button change
    dec_mode = 'R';
    break;
  case 'P': // Prompt mode - wait for request
    dec_mode = 'P';
    break;
  case 'D': // Request mode - send current info, then go to prompt mode
    dec_mode = 'P';
    // Transmit only if there's space in the buffer
    if (dz11numBytesFreeInRxBuffer(1) > 3) {
      for (uint32_t i = 0; i < 3; i++) {
	dz11charRx(1, dec_report[i]);
      }
    }
    break;
  case 'T':
    // Self test - send self test report, then go to prompt mode
    dec_mode = 'P';
    // Transmit only if there's space in the buffer
    if (dz11numBytesFreeInRxBuffer(1) > 4) {
      dz11charRx(1, 0xA0);  // Frame sync <7:5>, HW revision <4:0>
      dz11charRx(1, 0x02);  // Manufacturer ID <6:4>, device code <4:0>
      dz11charRx(1, 0x00);  // Error code <6:0> 
      dz11charRx(1, 0x00);  // Button code <2:0> 
    }
    break;
  }
}

//--------------------------------------------------------------------+
// Generic Report
//--------------------------------------------------------------------+
static void process_generic_report(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  (void) dev_addr;

  uint8_t const rpt_count = hid_info[instance].report_count;
  tuh_hid_report_info_t* rpt_info_arr = hid_info[instance].report_info;
  tuh_hid_report_info_t* rpt_info = NULL;

  if ( rpt_count == 1 && rpt_info_arr[0].report_id == 0)
  {
    // Simple report without report ID as 1st byte
    rpt_info = &rpt_info_arr[0];
  }else
  {
    // Composite report, 1st byte is report ID, data starts from 2nd byte
    uint8_t const rpt_id = report[0];

    // Find report id in the array
    for(uint8_t i=0; i<rpt_count; i++)
    {
      if (rpt_id == rpt_info_arr[i].report_id )
      {
        rpt_info = &rpt_info_arr[i];
        break;
      }
    }

    report++;
    len--;
  }

  if (!rpt_info)
  {
    printf("Couldn't find the report info for this report !\r\n");
    return;
  }

  // For complete list of Usage Page & Usage checkout src/class/hid/hid.h. For examples:
  // - Keyboard                     : Desktop, Keyboard
  // - Mouse                        : Desktop, Mouse
  // - Gamepad                      : Desktop, Gamepad
  // - Consumer Control (Media Key) : Consumer, Consumer Control
  // - System Control (Power key)   : Desktop, System Control
  // - Generic (vendor)             : 0xFFxx, xx
  if ( rpt_info->usage_page == HID_USAGE_PAGE_DESKTOP )
  {
    switch (rpt_info->usage)
    {
      case HID_USAGE_DESKTOP_KEYBOARD:
        TU_LOG1("HID receive keyboard report\r\n");
        // Assume keyboard follow boot report layout
        process_kbd_report( (hid_keyboard_report_t const*) report );
      break;

      case HID_USAGE_DESKTOP_MOUSE:
        TU_LOG1("HID receive mouse report\r\n");
        // Assume mouse follow boot report layout
        process_mouse_report( (hid_mouse_report_t const*) report );
      break;

      default: break;
    }
  }
}


// Run USB stack on second core
void usb_start_core_1() {

  // init host stack on configured roothub port
  tuh_init(BOARD_TUH_RHPORT);

  while (1) {
    // Run host usb 
    tuh_task();  
    //sleep_ms(100);
    //sleep_ms(75);
    // Working:
    //sleep_ms(85);
    // Exp:
    sleep_ms(75);
  }
}

uint32_t usbhid_init() {

  //board_init();

  printf("TinyUSB Host CDC MSC HID Example\n");

  //usb_start_core_1();
  
  // Start USB host on second core
  multicore_reset_core1();
  multicore_fifo_drain();
  multicore_launch_core1(usb_start_core_1);

  return 0;
}

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+

void tuh_mount_cb(uint8_t dev_addr)
{
  // application set-up
  printf("A device with address %d is mounted\r\n", dev_addr);
}

void tuh_umount_cb(uint8_t dev_addr)
{
  // application tear-down
  printf("A device with address %d is unmounted \r\n", dev_addr);
}


