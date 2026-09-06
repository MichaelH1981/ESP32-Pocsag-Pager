#include "../Arduino Sketch/src/pager_time.h"
#include <cassert>
#include <iostream>
int main() {
  PagerTime t{};
  assert(parsePagerTime(208,"XTIME=1326060926XTIME=1326060926",60,true,t));
  assert(t.year==2026 && t.month==9 && t.day==6 && t.hour==13 && t.minute==26 && t.second==0);
  assert(parsePagerTime(200,"XTIME=1126060926XTIME=1126060926",60,true,t));
  assert(t.hour==13 && t.minute==26);
  assert(parsePagerTime(224,"YYYYMMDDHHMMSS260906132645",60,true,t));
  assert(t.hour==13 && t.second==45);
  assert(parsePagerTime(216,"YYYYMMDDHHMMSS260906112645",60,true,t));
  assert(t.hour==13 && t.second==45);
  assert(parsePagerTime(216,"YYYYMMDDHHMMSS261231233000",60,true,t));
  assert(t.year==2027 && t.month==1 && t.day==1 && t.hour==0 && t.minute==30);
  assert(parsePagerTime(216,"YYYYMMDDHHMMSS240228233000",60,true,t));
  assert(t.month==2 && t.day==29);
  assert(parsePagerTime(216,"YYYYMMDDHHMMSS260101003000",-60,false,t));
  assert(t.year==2025 && t.month==12 && t.day==31 && t.hour==23);
  assert(parsePagerTime(216,"YYYYMMDDHHMMSS260329005900",60,true,t));
  assert(t.hour==1 && t.minute==59);
  assert(parsePagerTime(216,"YYYYMMDDHHMMSS260329010000",60,true,t));
  assert(t.hour==3);
  assert(parsePagerTime(216,"YYYYMMDDHHMMSS261025005900",60,true,t));
  assert(t.hour==2 && t.minute==59);
  assert(parsePagerTime(216,"YYYYMMDDHHMMSS261025010000",60,true,t));
  assert(t.hour==2 && t.minute==0);
  assert(parsePagerTime(224,"YYYYMMDDHHMMSS260906132645\x03",60,true,t));
  const PagerTime previous=t;
  for (const char* invalid : {"", "YYYYMMDDHHMMSS2609", "YYYYMMDDHHMMSS26xx06132645",
      "YYYYMMDDHHMMSS260229132645", "YYYYMMDDHHMMSS260431132645", "YYYYMMDDHHMMSS260906242645",
      "YYYYMMDDHHMMSS260906136045", "YYYYMMDDHHMMSS260906132660", "YYYYMMDDHHMMSS2609061326450"}) {
    assert(!parsePagerTime(216,invalid,60,true,t));
    assert(t.year==previous.year && t.hour==previous.hour && t.minute==previous.minute);
  }
  assert(!parsePagerTime(208,"XTIME=1326310426",60,true,t));
  assert(!parsePagerTime(632967,"XTIME=1326060926",60,true,t));
  assert(!parsePagerTime(200,nullptr,60,true,t));
  assert(!parsePagerTime(200,"YYYYMMDDHHMMSS260906132645",60,true,t));
  assert(pagerDaysInMonth(2000,2)==29 && pagerDaysInMonth(2100,2)==28);
  std::cout << "PASS: all four RICs, XTIME repetition, calendar validation, UTC/local, EU DST boundaries, year/leap-day rollover\n";
}
