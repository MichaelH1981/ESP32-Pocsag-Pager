#pragma once
#include <stdint.h>
#include <stddef.h>

// Trim the two highest and two lowest readings, then average the middle 12.
// Input is calibrated ADC millivolts, not raw ADC codes.
inline bool batteryVolts(const uint32_t* readings, float divider, float gain, float offset, float& out) {
  if (!readings || divider <= 0 || gain <= 0) return false;
  uint32_t sorted[16];
  for (size_t i=0;i<16;++i) {
    sorted[i]=readings[i];
    for(size_t j=i;j>0 && sorted[j]<sorted[j-1];--j) {
      uint32_t t=sorted[j];sorted[j]=sorted[j-1];sorted[j-1]=t;
    }
  }
  uint32_t sum=0;
  for(size_t i=2;i<14;++i) sum+=sorted[i];
  const float volts=(sum/12.0f)/1000.0f*divider*gain+offset;
  if (!(volts >= 2.5f && volts <= 4.5f)) return false;
  out=volts;
  return true;
}
