// Cheeko Gotchi side of the iPhone Agent firmware: power latch, the middle button and a status screen.
// Pins and panel init follow SKILL.md and firmware/hardware_test.
#pragma once
#include <Arduino.h>

void board_begin();                      // first thing in setup(): power latch, button, display
void board_ui_start();                   // status screen task; call after BLE, Wi-Fi and AirPlay are up
void board_note(const String &action);   // last HID command, shown on the screen
bool board_stop_pressed();               // edge: the middle button went down since the last call

const char *usbState();                  // defined in iphone_agent.ino
