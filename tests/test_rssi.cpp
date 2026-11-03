#include "../Arduino Sketch/src/pager_rssi.h"
#include <cassert>
#include <cstdio>
#include <limits>
int main() {
  PagerRssi rssi;
  rssi.received(0); assert(!rssi.valid());
  rssi.sample(-105, 1000); rssi.sample(-40.5f, 1020); rssi.sample(-101, 1040);
  rssi.received(1050); assert(rssi.valid() && rssi.last() == -40.5f);
  rssi.sample(-75, 1700); rssi.received(1720); assert(rssi.last() == -75);
  rssi.sample(20, 1740); rssi.sample(-200, 1740);
  rssi.sample(std::numeric_limits<float>::quiet_NaN(), 1740);
  rssi.received(1750); assert(rssi.last() == -75);
  // Idle polling never replaces the last received level or keeps an old peak.
  for(unsigned i=0;i<50;++i)rssi.sample(-110,2000+i*20);
  assert(rssi.last()==-75);rssi.received(3000);assert(rssi.last()==-110);
  PagerRssi wrapped;wrapped.sample(-42,0xfffffff0u);wrapped.received(10);
  assert(wrapped.valid() && wrapped.last()==-42);
  puts("PASS: RSSI cache, last-reception peak, stale samples, invalid values and timer wrap");
}
