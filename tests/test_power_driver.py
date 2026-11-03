"""Compile the actual driver with fake radio/GPIO/sleep to test sequencing."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
stubs = r'''
#pragma once
#include <cstdint>
#include <cassert>
#define LORA_DIO0 26
#define BTN_UP 12
#define BTN_ENTER 13
#define BTN_DOWN 15
#define ESP_OK 0
#define ESP_ERR_SLEEP_REJECT 263
#define PAGER_SLEEP_BUSY_RSSI_DBM -90.0f
#define GPIO_INTR_HIGH_LEVEL 1
#define GPIO_INTR_LOW_LEVEL 0
#define ESP_SLEEP_WAKEUP_GPIO 1
#define ESP_SLEEP_WAKEUP_TIMER 2
#define RADIOLIB_SX127X_REG_PREAMBLE_DETECT 0x1f
#define RADIOLIB_SX127X_PREAMBLE_DETECTOR_ON 0x80
#define RADIOLIB_SX127X_PREAMBLE_DETECTOR_3_BYTE 0x40
#define RADIOLIB_SX127X_PREAMBLE_DETECTOR_TOL 10
#define RADIOLIB_SX127X_DIO0_CONT_RSSI_PREAMBLE_DETECT 0x40
#define RADIOLIB_SX127X_REG_DIO_MAPPING_1 0x40
#define RADIOLIB_SX127X_REG_DIO_MAPPING_2 0x41
#define RADIOLIB_SX127X_REG_IRQ_FLAGS_1 0x3e
#define RADIOLIB_SX127X_FLAG_PREAMBLE_DETECT 2
using gpio_num_t = int;
using esp_err_t = int;
extern uint32_t clockMs;
extern int pins[40], sleepCalls, sleepError, setupError, resetCalls;
extern bool capture, wakeRf;
uint32_t millis();
int digitalRead(int);
int gpio_wakeup_enable(int,int);
int gpio_wakeup_disable(int);
int esp_sleep_enable_gpio_wakeup();
int esp_sleep_enable_timer_wakeup(uint64_t);
int esp_sleep_disable_wakeup_source(int);
int64_t esp_timer_get_time();
int esp_light_sleep_start();
struct SerialStub {
 template<class... Args> void printf(const char*, Args...) {}
 void flush() {}
};
extern SerialStub Serial;
struct EspStub { unsigned getFreeHeap() { return 200000; } };
extern EspStub ESP;
struct Module {
 uint8_t regs[256] = {};
 int SPIsetRegValue(uint8_t r, uint8_t v) {regs[r]=v;return setupError;}
 uint8_t SPIreadRegister(uint8_t r) {return regs[r];}
 void SPIwriteRegister(uint8_t r, uint8_t v) {
  assert(r==0x3e && v==2); pins[26]=0;
 }
};
struct SX1278 {
 Module mod;
 float rssi=-125;
 int queued=0;
 int available() {return queued;}
 Module* getMod() {return &mod;}
 int setDIOMapping(int pin, uint8_t value) {assert(pin==0 && value==0x40);mod.regs[0x40]=value;return 0;}
 int setDIOPreambleDetect(bool value) {assert(value);mod.regs[0x41]=1;return 0;}
 float getRSSI(bool skipReceive=false) {assert(skipReceive);return rssi;}
 void clearDio1Action() {capture=false;}
 void setDirectAction(void(*)()) {capture=true;}
 void resetDirectBuffer() {assert(!capture);++resetCalls;}
};
'''
main = r'''
#include "pager_power.h"
#include <cstdio>
uint32_t clockMs=0;
int pins[40]={}, sleepCalls=0, sleepError=0, setupError=0, resetCalls=0;
bool capture=true, wakeRf=false;
SerialStub Serial;
EspStub ESP;
uint32_t millis(){return clockMs;}
int digitalRead(int pin){return pins[pin];}
int gpio_wakeup_enable(int,int){return 0;}
int gpio_wakeup_disable(int){return 0;}
int esp_sleep_enable_gpio_wakeup(){return 0;}
int esp_sleep_enable_timer_wakeup(uint64_t n){assert(n==250000);return 0;}
int esp_sleep_disable_wakeup_source(int){return 0;}
int64_t esp_timer_get_time(){return int64_t(clockMs)*1000;}
int esp_light_sleep_start(){
 assert(!capture); ++sleepCalls; clockMs+=250;
 if(wakeRf)pins[26]=1;
 return sleepError;
}
void bit(){}
int main(){
 pins[12]=pins[13]=pins[15]=1;
 SX1278 radio; PagerPower power(radio,bit); power.begin(2);
 assert(radio.mod.regs[0x1f]==0xca);
 clockMs=8000;
  assert(!power.shouldDrainTail());
  radio.queued=4; assert(power.shouldDrainTail());
  radio.queued=3; assert(!power.shouldDrainTail());radio.queued=0;
 power.idle(false,false);assert(!sleepCalls && capture);
 power.idle(true,true);assert(!sleepCalls && capture);
 pins[12]=0;power.idle(true,false);assert(!sleepCalls);
 pins[12]=1;
 power.idle(true,false);assert(sleepCalls==1 && !capture); // timer wake
 wakeRf=true; power.idle(true,false);
 assert(sleepCalls==2 && capture); // RF restores ISR before returning
 power.poll();
 assert(pins[26]==0);
 power.idle(true,false);assert(sleepCalls==2 && capture); // receive hold
  radio.queued=100;assert(!power.shouldDrainTail());radio.queued=0;
 power.received();clockMs+=7999;power.idle(true,false);assert(sleepCalls==2);
 clockMs+=1;
  wakeRf=false;sleepError=ESP_ERR_SLEEP_REJECT;
  power.idle(true,false);assert(sleepCalls==3 && capture);
  clockMs+=8000;
 wakeRf=false;sleepError=1;
  power.idle(true,false);assert(sleepCalls==4 && capture); // fallback
  clockMs+=10000;power.idle(true,false);assert(sleepCalls==4);
 PagerPower observer(radio,bit);observer.begin(1);
  clockMs+=10000;observer.idle(true,false);assert(sleepCalls==4 && capture);
 setupError=1;PagerPower broken(radio,bit);broken.begin(2);
  broken.idle(true,false);assert(sleepCalls==4 && capture);
 assert(resetCalls>=2);
 puts("PASS: actual driver GPIO mapping, preamble W1C, ISR suspension/restoration, timer/RF wake, hold and error fallback");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'driver').mkdir()
    (path / 'common.h').write_text(stubs)
    for header in ['Arduino.h', 'RadioLib.h', 'esp_sleep.h', 'esp_timer.h', 'driver/gpio.h']:
        (path / header).write_text('#include "common.h"\n')
    cpp = path / 'driver_test.cpp'
    cpp.write_text(main)
    exe = path / 'driver_test'
    subprocess.run(['c++', '-std=c++11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(path),
                    '-I', str(root / 'Arduino Sketch/src'), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
