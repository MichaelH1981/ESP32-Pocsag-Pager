#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef ESP32
#include <esp_attr.h>
#define PAGER_ISR_ATTR IRAM_ATTR
#else
#define PAGER_ISR_ATTR
#endif

// One ISR producer, one loop consumer. Neither side changes the other's index.
// 256 words hold 6.8 seconds at 1200 bit/s. No allocation or flash calls in ISR.
class PocsagBitQueue {
 public:
  void PAGER_ISR_ATTR pushBit(bool value) {
    partial_ = (partial_ << 1) | uint32_t(value);
    if (++bits_ != 32) return;
    bits_ = 0;
    const uint32_t head = __atomic_load_n(&head_, __ATOMIC_RELAXED);
    const uint32_t tail = __atomic_load_n(&tail_, __ATOMIC_ACQUIRE);
    if (head - tail == Capacity) {
      __atomic_fetch_add(&overruns_, 1u, __ATOMIC_RELEASE);
      return;
    }
    words_[head & (Capacity - 1)] = partial_;
    __atomic_store_n(&head_, head + 1, __ATOMIC_RELEASE);
  }
  bool pop(uint32_t& word) {
    const uint32_t tail = __atomic_load_n(&tail_, __ATOMIC_RELAXED);
    if (tail == __atomic_load_n(&head_, __ATOMIC_ACQUIRE)) return false;
    word = words_[tail & (Capacity - 1)];
    __atomic_store_n(&tail_, tail + 1, __ATOMIC_RELEASE);
    return true;
  }
  uint32_t pending() const {
    return __atomic_load_n(&head_, __ATOMIC_ACQUIRE) -
           __atomic_load_n(&tail_, __ATOMIC_ACQUIRE);
  }
  uint32_t overruns() const { return __atomic_load_n(&overruns_, __ATOMIC_ACQUIRE); }
  void discardQueued() {
    __atomic_store_n(&tail_, __atomic_load_n(&head_, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
  }
  // Only with producer interrupt detached. Keep lifetime overrun statistics.
  void reset() { head_ = tail_ = partial_ = 0; bits_ = 0; }
 private:
  static constexpr uint32_t Capacity = 256;
  uint32_t words_[Capacity] = {};
  uint32_t head_ = 0, tail_ = 0, overruns_ = 0, partial_ = 0;
  uint8_t bits_ = 0;
};

// ITU-R M.584-2 Annex 1: 16 words per batch, BCH(31,21) plus even parity.
// Decode continuously: a short queue never means end-of-message. Only a checked
// idle/address word completes a message; loss, timeout and overflow discard it.
class PocsagReceiver {
 public:
  enum : uint32_t { Sync = 0x7cd215d8, Idle = 0x7a89c197 };
  static constexpr size_t MaxLength = 512;
  struct Message {
    uint32_t address;
    uint8_t function;
    size_t length;
    char text[MaxLength + 3]; // Up to two complete 7-bit padding symbols.
  };
  struct Stats {
    uint32_t syncs = 0, words = 0, correctedBits = 0, rejectedWords = 0;
    uint32_t lostSync = 0, discardedMessages = 0, messages = 0;
  } stats;
  using Handler = void (*)(const Message&, void*);
  PocsagReceiver(Handler handler, void* context = nullptr) : handler_(handler), context_(context) {
    // Extended 11-bit syndrome includes overall parity. All 0/1/2-bit masks
    // are unique for this code (minimum distance 6). 8 KiB, built once in RAM.
    for (unsigned i = 0; i < 2048; ++i) corrections_[i] = 0;
    for (unsigned i = 0; i < 32; ++i) {
      const uint32_t one = uint32_t(1) << i;
      corrections_[syndrome(one)] = one;
      for (unsigned j = i + 1; j < 32; ++j) {
        const uint32_t two = one | (uint32_t(1) << j);
        corrections_[syndrome(two)] = two;
      }
    }
  }
  static unsigned syndrome(uint32_t word) {
    uint32_t remainder = word >> 1;
    for (int bit = 30; bit >= 10; --bit)
      if (remainder & (uint32_t(1) << bit)) remainder ^= 0x769u << (bit - 10);
    return unsigned(remainder) | (unsigned(__builtin_parity(word)) << 10);
  }
  int correct(uint32_t& word) const {
    const unsigned s = syndrome(word);
    if (!s) return 0;
    const uint32_t mask = corrections_[s];
    if (!mask) return -1;
    word ^= mask;
    return __builtin_popcount(mask);
  }
  void reset() {
    discard(); locked_ = false; shift_ = 0; bits_ = 0; slot_ = 0;
  }
  bool active() const { return messageActive_; }
  void expire(uint32_t now) {
    if (locked_ && uint32_t(now - lastWordAt_) > 1500) {
      if (messageActive_) ++stats.lostSync;
      reset();
    }
  }
  void chunk(uint32_t bits, uint32_t now) {
    for (int i = 31; i >= 0; --i) acceptBit((bits >> i) & 1u, now);
  }
  void acceptBit(bool value, uint32_t now) {
    shift_ = (shift_ << 1) | uint32_t(value);
    if (!locked_) {
      // Exact match for acquisition; tolerate two wrong sync bits once framed.
      if (shift_ == Sync) { locked_ = true; bits_ = slot_ = 0; ++stats.syncs; lastWordAt_ = now; }
      return;
    }
    if (++bits_ != 32) return;
    bits_ = 0;
    if (slot_ == 16) {
      if (__builtin_popcount(shift_ ^ Sync) > 2) {
        // No following sync after a checked idle is an ordinary carrier end.
        if (messageActive_) ++stats.lostSync;
        discard(); locked_ = false;
      } else { ++stats.syncs; slot_ = 0; lastWordAt_ = now; }
      return;
    }
    uint32_t word = shift_;
    const uint8_t frame = slot_++ / 2;
    const int corrected = correct(word);
    if (corrected < 0) {
      ++stats.rejectedWords; discard();
      return;
    }
    ++stats.words; stats.correctedBits += unsigned(corrected); lastWordAt_ = now;
    if (word == Idle) { complete(); return; }
    if (!(word & 0x80000000u)) {
      complete();
      message_.address = ((word >> 13) << 3) | frame;
      message_.function = (word >> 11) & 3;
      message_.length = 0;
      messageActive_ = true;
      symbol_ = symbolBits_ = 0;
      ended_ = false;
      return;
    }
    if (!messageActive_) return;
    const unsigned width = message_.function == 0 ? 4 : 7;
    for (int i = 30; i >= 11; --i) {
      symbol_ |= ((word >> i) & 1u) << symbolBits_;
      if (++symbolBits_ != width) continue;
      if (!ended_) {
        // Skyper's shifted payload can contain 0x03/0x04 inside real text.
        // Only NUL terminates early; trim ETX/EOT padding at the checked end.
        if (width == 7 && symbol_ == 0) ended_ = true;
        else {
          if (message_.length == MaxLength + 2) { discard(); return; }
          const char numeric[] = "0123456789*U -)(";
          message_.text[message_.length++] = width == 4 ? numeric[symbol_] : char(symbol_);
        }
      }
      symbol_ = symbolBits_ = 0;
    }
  }
 private:
  Handler handler_;
  void* context_;
  uint32_t corrections_[2048] = {};
  Message message_{};
  uint32_t shift_ = 0, lastWordAt_ = 0;
  uint8_t bits_ = 0, slot_ = 0, symbol_ = 0, symbolBits_ = 0;
  bool locked_ = false, messageActive_ = false, ended_ = false;
  void discard() {
    if (messageActive_) ++stats.discardedMessages;
    messageActive_ = false;
  }
  void complete() {
    if (!messageActive_) return;
    if (message_.function != 0) {
      while (message_.length && (message_.text[message_.length - 1] == 3 ||
                                message_.text[message_.length - 1] == 4)) --message_.length;
    }
    if (message_.length > MaxLength) { discard(); return; }
    messageActive_ = false;
    message_.text[message_.length] = 0;
    ++stats.messages;
    if (handler_) handler_(message_, context_);
  }
};
