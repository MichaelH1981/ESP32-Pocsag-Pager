#pragma once
#include <stddef.h>
#include <stdint.h>

struct SkyperHeader {
  bool valid;
  size_t headerLength;
  unsigned rubric;
  unsigned item;
};

// DAPNET Core SkyperProtocol: 4520 = news (2 header bytes),
// 4512 = rubric label ('1' plus 2 header bytes). Headers are NOT ROT-1.
// Decode the payload in place without changing its length or allocating memory.
inline SkyperHeader decodeSkyper(uint32_t ric, char* text, size_t length) {
  const size_t header = ric == 4520 ? 2 : ric == 4512 ? 3 : 0;
  if (!header || !text || length <= header) return {false, 0, 0, 0};
  const size_t base = header - 2;
  const uint8_t rubric = static_cast<uint8_t>(text[base]);
  const uint8_t item = static_cast<uint8_t>(text[base + 1]);
  if ((ric == 4512 && text[0] != '1') || rubric < 0x20 || rubric > 0x7f ||
      item < 0x21 || item > 0x2a) return {false, 0, 0, 0};
  // Reject unexpected control bytes before changing anything. Allow terminal
  // ETX/EOT padding from the decoder, but never rotate these into printable text.
  for (size_t i = header; i < length; ++i) {
    const uint8_t c = static_cast<uint8_t>(text[i]);
    if (c != 0x03 && c != 0x04 && (c < 0x21 || c > 0x7f))
      return {false, 0, 0, 0};
  }
  for (size_t i = header; i < length; ++i) {
    const uint8_t c = static_cast<uint8_t>(text[i]);
    if (c >= 0x21) text[i] = static_cast<char>(c - 1);
  }
  return {true, header, static_cast<unsigned>(rubric - 0x1f),
          static_cast<unsigned>(item - 0x20)};
}
