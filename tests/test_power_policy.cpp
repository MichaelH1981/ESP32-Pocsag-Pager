#include "../Arduino Sketch/src/pager_power_policy.h"
#include <cassert>
#include <iostream>

int main() {
  PagerPowerPolicy p;
  p.begin(0);
  assert(!p.maySleep(7999, true, false));
  assert(p.maySleep(8000, true, false));
  assert(!p.maySleep(8000, false, false)); // Display, button, ADC or alarm active.
  assert(!p.maySleep(8000, true, true)); // Complete batch must be drained first.
  p.signal(7900);
  assert(!p.maySleep(10899, true, false));
  assert(p.maySleep(10900, true, false));
  p.activity(11000);
  assert(!p.maySleep(18999, true, false));
  assert(p.maySleep(19000, true, false));
  p.begin(UINT32_MAX - 4000);
  assert(!p.maySleep(3998, true, false));
  assert(p.maySleep(3999, true, false));
  std::cout << "PASS: receive hold, carrier hold, UI and batch guards, timer wrap\n";
}
