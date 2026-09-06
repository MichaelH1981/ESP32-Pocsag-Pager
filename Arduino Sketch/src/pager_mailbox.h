#pragma once
#include <stddef.h>
#include <stdint.h>

// Fixed storage owned by the caller; each folder has its own eviction boundary.
template<class Message> struct PagerMailbox {
  Message* messages;
  int capacity;
  int count = 0, writeIndex = 0, current = 0;
  bool dirty = false;
  PagerMailbox(Message* storage, int size) : messages(storage), capacity(size) {}
  void clear() {
    for (int i = 0; i < capacity; ++i) messages[i] = Message{};
    count = writeIndex = current = 0;
    dirty = true;
  }
  void push(const Message& message) {
    // Reuse holes before evicting an existing message.
    for (int i = 0; i < capacity; ++i) {
      int slot = (writeIndex + i) % capacity;
      if (!messages[slot].valid) { writeIndex = slot; break; }
    }
    if (!messages[writeIndex].valid) ++count;
    messages[writeIndex] = message;
    messages[writeIndex].valid = true;
    current = writeIndex;
    writeIndex = (writeIndex + 1) % capacity;
    dirty = true;
  }
  // Weather key is RIC + rubric/item (stored in ricName).
  bool update(const Message& message) {
    for (int i = 0; i < capacity; ++i) {
      if (messages[i].valid && messages[i].addr == message.addr &&
          messages[i].ricName == message.ricName) {
        messages[i] = message;
        dirty = true;
        return true;
      }
    }
    return false;
  }
  bool containsText(const Message& message) const {
    for (int i = 0; i < capacity; ++i)
      if (messages[i].valid && messages[i].addr == message.addr &&
          messages[i].text == message.text) return true;
    return false;
  }
};

enum InboxFolder { PersonalInbox = 0, WeatherInbox = 1, WarningInbox = 2 };
// The configured numbers are local routing choices, not a protocol priority bit.
inline int skyperFolder(unsigned rubric, const unsigned* weather, size_t weatherCount,
                        const unsigned* warnings, size_t warningCount) {
  for (size_t i = 0; i < warningCount; ++i) if (warnings[i] == rubric) return WarningInbox;
  for (size_t i = 0; i < weatherCount; ++i) if (weather[i] == rubric) return WeatherInbox;
  return -1;
}
