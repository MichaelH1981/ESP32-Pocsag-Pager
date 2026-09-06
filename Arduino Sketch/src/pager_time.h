#pragma once
#include <stdint.h>
#include <string.h>

struct PagerTime {
  int year, month, day, hour, minute, second;
  bool valid;
};

inline int pagerDaysInMonth(int year, int month) {
  static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  if (month < 1 || month > 12) return 0;
  return days[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
}

inline void pagerAddMinutes(PagerTime& t, int delta) {
  int minutes = t.hour * 60 + t.minute + delta;
  while (minutes < 0) {
    minutes += 1440;
    if (--t.day == 0) {
      if (--t.month == 0) { t.month = 12; --t.year; }
      t.day = pagerDaysInMonth(t.year, t.month);
    }
  }
  while (minutes >= 1440) {
    minutes -= 1440;
    if (++t.day > pagerDaysInMonth(t.year, t.month)) {
      t.day = 1;
      if (++t.month == 13) { t.month = 1; ++t.year; }
    }
  }
  t.hour = minutes / 60;
  t.minute = minutes % 60;
}

// Gregorian weekday: Sunday=0. Called only for validated dates.
inline int pagerWeekday(int year, int month, int day) {
  static const int offsets[] = {0,3,2,5,0,3,5,1,4,6,2,4};
  if (month < 3) --year;
  return (year + year/4 - year/100 + year/400 + offsets[month-1] + day) % 7;
}

// EU summer time starts/ends on the last Sunday in March/October at 01:00 UTC.
inline bool pagerEuSummerTime(const PagerTime& utc) {
  if (utc.month < 3 || utc.month > 10) return false;
  if (utc.month > 3 && utc.month < 10) return true;
  const int sunday = 31 - pagerWeekday(utc.year, utc.month, 31);
  const bool afterBoundary = utc.day > sunday || (utc.day == sunday && utc.hour >= 1);
  return utc.month == 3 ? afterBoundary : !afterBoundary;
}

inline bool pagerTimeRic(uint32_t ric) {
  return ric == 200 || ric == 208 || ric == 216 || ric == 224;
}

// DAPNET SkyperProtocol: XTIME=HHmmddMMyy (often repeated), or
// YYYYMMDDHHMMSS followed by yyMMddHHmmss. 208/224 are already local time.
// On invalid input the caller's existing clock is never modified.
inline bool parsePagerTime(uint32_t ric, const char* text, int standardOffset,
                           bool euDst, PagerTime& result) {
  if (!pagerTimeRic(ric) || !text || standardOffset < -720 || standardOffset > 840)
    return false;
  const bool xtime = ric == 200 || ric == 208;
  const char* marker = xtime ? "XTIME=" : "YYYYMMDDHHMMSS";
  const char* p = strstr(text, marker);
  if (!p) return false;
  p += strlen(marker);
  const int length = xtime ? 10 : 12;
  if (strlen(p) < static_cast<unsigned>(length)) return false;
  for (int i = 0; i < length; ++i) if (p[i] < '0' || p[i] > '9') return false;
  if (p[length] >= '0' && p[length] <= '9') return false;
  int n[6] = {};
  for (int i = 0; i < length/2; ++i) n[i] = (p[2*i]-'0')*10 + p[2*i+1]-'0';
  PagerTime t = xtime ? PagerTime{2000+n[4],n[3],n[2],n[0],n[1],0,true}
                      : PagerTime{2000+n[0],n[1],n[2],n[3],n[4],n[5],true};
  if (t.month < 1 || t.month > 12 || t.day < 1 ||
      t.day > pagerDaysInMonth(t.year,t.month) || t.hour > 23 ||
      t.minute > 59 || t.second > 59) return false;
  if (ric == 200 || ric == 216)
    pagerAddMinutes(t, standardOffset + (euDst && pagerEuSummerTime(t) ? 60 : 0));
  result = t;
  return true;
}
