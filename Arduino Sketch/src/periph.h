#pragma once

#ifndef LORA_SCK
#define LORA_SCK        5
#endif
#ifndef LORA_MISO
#define LORA_MISO       19
#endif
#ifndef LORA_MOSI
#define LORA_MOSI       27
#endif
#define LORA_SS         18
#define LORA_DIO0       26
#define LORA_DIO1       33 // T3 V1.6.1 internal DIO1; remove external bridge to GPIO35.
#define LORA_DIO2       34
#ifndef LORA_RST
#define LORA_RST        23
#endif

// Keep the proven OLED wiring. Board package OLED_RST=16 is not reliable
// across TTGO revisions; never drive it without verifying the schematic.
#ifndef OLED_SDA
#define OLED_SDA 21
#endif
#ifndef OLED_SCL
#define OLED_SCL 22
#endif
#define PAGER_OLED_RESET -1
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define BUZZER 14
#define LED 25 // Internal green LED
#define BTN_UP     12
#define BTN_ENTER  13
#define BTN_DOWN   15
