/*
User-modifiable configuration
In the near future, bring this to a menu, and load-edit them in SPIFFS
*/

#pragma once
#include <stddef.h>
#include <stdint.h>
#if __has_include("config_local.h")
#include "config_local.h"
#endif
#ifndef PERSONAL_RIC
#define PERSONAL_RIC 0 // Disabled until configured in config_local.h
#endif
#ifndef PERSONAL_CALLSIGN
#define PERSONAL_CALLSIGN "UNCONFIGURED"
#endif
#ifndef BATTERY_ADC_PIN
#define BATTERY_ADC_PIN -1 // Local T3 config enables 35 after its LoRa bridge is removed.
#endif
#ifndef BATTERY_DIVIDER_RATIO
#define BATTERY_DIVIDER_RATIO 2.0f
#endif
#ifndef BATTERY_CALIBRATION_GAIN
#define BATTERY_CALIBRATION_GAIN 1.0f
#endif
#ifndef BATTERY_CALIBRATION_OFFSET
#define BATTERY_CALIBRATION_OFFSET 0.0f
#endif
#ifndef SKYPER_NEWS_INBOX
#define SKYPER_NEWS_INBOX 0 // Set to 1 to store/alert on all Skyper news (RIC 4520).
#endif
#ifndef SKYPER_DECODE
#define SKYPER_DECODE 1
#endif
// Folder routes inferred from the received local feed; override in config_local.h.
#ifndef WEATHER_SKYPER_RUBRICS
#define WEATHER_SKYPER_RUBRICS 61, 63, 80
#endif
#ifndef WARNING_SKYPER_RUBRICS
#define WARNING_SKYPER_RUBRICS 39
#endif
#ifndef WARNING_RIC
#define WARNING_RIC 1040
#endif
#ifndef WARNING_AUDIBLE
#define WARNING_AUDIBLE 0 // Enable only after verifying the configured warning feed.
#endif
#ifndef WARNING_RINGTONE
#define WARNING_RINGTONE 0
#endif
// Standard UTC offset; EU DST adds 60 minutes in summer (Germany defaults).
// Already-local RIC 208/224 never receive another offset.
#ifndef TIME_UTC_OFFSET_MINUTES
#define TIME_UTC_OFFSET_MINUTES 60
#endif
#ifndef TIME_EU_DST
#define TIME_EU_DST 1
#endif
//Default settings

const float offset = 0.0000;  // device specific, in MHz. VHF: 0.0014 UHF: 0.0044
const float frequency = 439.98750;


#define RINGTONE 4 //Number of ringtones available
#define NOTENUMBER 8 //Number of tones per ringtone

#define STARTUPTONE 3 //Which tone to play when booting

#define ATONE 2730 //Frequencies in Hz of specific tones
#define BTONE 1005
#define CTONE 3201
// Display Timeout in Sekunden
// 0 = immer an, 5 = 5s, 15 = 15s, 30 = 30s
#define DISPLAY_TIMEOUT_SECONDS 30

struct ric{
    uint32_t ricvalue; //RIC adress itself
    const char* name; //"Nickname"
    int ringtype; //TBD: ring "melody"
    bool placeholder1;
    bool placeholder2;
}

/*RICs the pager will respond to. As described by the struct above:
{RIC,"NAME",ringtone(see below),TBD,TBD}
*/
ric[]={
        {PERSONAL_RIC, PERSONAL_CALLSIGN,2,0,0},
        {65009, "IND",2,0,0},
        {1040, "EMERGENCY",0,0,0},
        {1080, "APRSWX",1,0,0},
#if SKYPER_NEWS_INBOX
        {4520, "SKYPER",2,0,0},
#endif
};

constexpr size_t RICNUMBER = sizeof(ric) / sizeof(ric[0]);

//"melodies", 130ms tones. Individual frequencies, 8 slots. 0 equals to a 130ms silence.
int beepTones[RINGTONE][NOTENUMBER]={
        {ATONE,CTONE,ATONE,CTONE,ATONE,0,0,0},
        {CTONE,0,ATONE,0,BTONE,BTONE,0,ATONE},
        {ATONE,CTONE,CTONE,0,ATONE,CTONE,CTONE,0},
        {ATONE,0,0,CTONE,0,0,0,0}, //Startup ringtone
};
