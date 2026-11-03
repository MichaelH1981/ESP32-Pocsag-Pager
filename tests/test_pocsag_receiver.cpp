#include "../Arduino Sketch/src/pocsag_receiver.h"
#include "../Arduino Sketch/src/pager_text.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

static uint32_t encode(uint32_t data) {
  uint32_t remainder = data >> 1;
  for (int i = 30; i >= 10; --i)
    if (remainder & (1u << i)) remainder ^= 0x769u << (i - 10);
  uint32_t word = (data & 0xfffff800u) | (remainder << 1);
  return word | __builtin_parity(word);
}
struct Page { uint32_t address; std::string text; };
static std::vector<Page> received;
static void page(const PocsagReceiver::Message& m, void*) {
  received.push_back({m.address, std::string(m.text, m.length)});
}
struct Sender {
  std::vector<uint32_t> words{PocsagReceiver::Sync};
  unsigned slot = 0;
  void word(uint32_t w) {
    if (slot == 16) { words.push_back(PocsagReceiver::Sync); slot = 0; }
    words.push_back(w); ++slot;
  }
  void address(uint32_t ric, bool odd = false, unsigned function = 3) {
    const unsigned target = (ric & 7) * 2 + unsigned(odd);
    while ((slot % 16) != target) word(PocsagReceiver::Idle);
    word(encode(((ric >> 3) << 13) | (function << 11)));
  }
  void text(const std::string& s, unsigned width = 7) {
    // Pack characters LSB first, words MSB first, independently of receiver.
    std::vector<bool> bits;
    for (unsigned char c : s)
      for (unsigned i = 0; i < width; ++i) bits.push_back((c >> i) & 1);
    while (bits.size() % 20) bits.push_back(false);
    for (size_t i = 0; i < bits.size(); i += 20) {
      uint32_t w = 0x80000000;
      for (unsigned j = 0; j < 20; ++j) w |= uint32_t(bits[i+j]) << (30-j);
      word(encode(w));
    }
  }
  void end() { word(PocsagReceiver::Idle); }
};
static void feed(PocsagReceiver& rx, const std::vector<uint32_t>& words) {
  for (uint32_t w : words) rx.chunk(w, 100);
}
int main() {
  assert(encode(0x7a89c000) == PocsagReceiver::Idle); // ITU golden codeword
  PocsagReceiver rx(page);
  for (uint32_t source : {0u, 0x1800u, 0x7a89c000u, 0x80000000u, 0xfffff800u}) {
    const uint32_t valid = encode(source);
    assert(PocsagReceiver::syndrome(valid) == 0);
    for (unsigned i = 0; i < 32; ++i) {
      uint32_t bad = valid ^ (1u << i);
      assert(rx.correct(bad) == 1 && bad == valid);
      for (unsigned j = i+1; j < 32; ++j) {
        bad = valid ^ (1u << i) ^ (1u << j);
        assert(rx.correct(bad) == 2 && bad == valid);
        for (unsigned k = j+1; k < 32; ++k) {
          bad = valid ^ (1u << i) ^ (1u << j) ^ (1u << k);
          assert(rx.correct(bad) == -1); // extended BCH detects every 3-bit error
        }
      }
    }
  }
  // Correct frame numbering in both halves, also across multiple batches.
  for (unsigned frame = 0; frame < 8; ++frame) {
    for (bool odd : {false, true}) {
      received.clear(); rx.reset(); Sender tx;
      const uint32_t ric = 632960 + frame;
      const std::string text = "RIC " + std::to_string(ric) + " " + std::string(200, 'a' + frame);
      tx.address(ric, odd); tx.text(text); tx.end();
      // Begin at an arbitrary bit offset, not a queue word boundary.
      PocsagBitQueue queue;
      for (unsigned i=0;i<13;++i) queue.pushBit(i&1);
      for (uint32_t w:tx.words) for (int i=31;i>=0;--i) {
        queue.pushBit((w>>i)&1);
        uint32_t out;
        while(queue.pop(out)) rx.chunk(out,100);
      }
      for (unsigned i=0;i<32;++i) queue.pushBit(false);
      uint32_t out;while(queue.pop(out))rx.chunk(out,100);
      assert(received.size()==1 && received[0].address==ric && received[0].text==text);
    }
  }
  received.clear();rx.reset();Sender adjacent;
  adjacent.address(4520);adjacent.text("first message");
  // Slot 6 is frame 3: a new address terminates the previous message.
  const uint32_t second = 2080 + (adjacent.slot % 16)/2;
  adjacent.address(second, adjacent.slot%2);adjacent.text("second message");adjacent.end();
  feed(rx,adjacent.words);
  assert(received.size()==2 && received[0].text=="first message" && received[1].address==second && received[1].text=="second message");
  received.clear();rx.reset();Sender partial;
  partial.address(4520);partial.text("never report this fragment");
  feed(rx,partial.words);assert(received.empty() && rx.active());
  rx.expire(1700);assert(received.empty() && !rx.active());
  // A bad message word invalidates the whole page, not merely one character.
  received.clear();rx.reset();Sender damaged;
  damaged.address(4520);damaged.text(std::string(90,'x'));damaged.end();
  damaged.words[4]^=7;feed(rx,damaged.words);assert(received.empty());
  // Single/double errors in address, message, idle and continuation sync.
  received.clear();rx.reset();Sender repaired;
  repaired.address(4520);repaired.text(std::string(180,'R'));repaired.end();
  for(size_t i=1;i<repaired.words.size();++i)repaired.words[i]^=(1u<<5)|(1u<<23);
  feed(rx,repaired.words);assert(received.size()==1 && received[0].address==4520 && received[0].text==std::string(180,'R'));
  for(unsigned length : {512u,513u,1000u}) {
    received.clear();rx.reset();Sender longPage;
    longPage.address(4520);longPage.text(std::string(length,'L'));longPage.end();feed(rx,longPage.words);
    assert(received.size()==(length==512?1u:0u));
  }
  received.clear();rx.reset();Sender padded;
  padded.address(4520);padded.text(std::string(512,'P')+"\x03\x04");padded.end();feed(rx,padded.words);
  assert(received.size()==1 && received[0].text==std::string(512,'P'));
  // Real Pi-Star APRS weather text, including a shifted Unicode control in
  // the middle: it must not be mistaken for a premature message terminator.
  received.clear();rx.reset();Sender skyper;
  const std::string weather="1635z FW0327/Kaiserslautern: 6.7C w: no h: 65% hPa: 1028.2 r: 0mm/h";
  std::string raw;raw+=char(80+31);raw+=char(6+32);
  for(char c:weather)raw+=char(c+1);
  raw.insert(15,1,char(3));
  skyper.address(4520);skyper.text(raw);skyper.end();feed(rx,skyper.words);
  assert(received.size()==1 && received[0].text==raw);
  auto header=decodeSkyper(4520,&received[0].text[0],received[0].text.size());
  assert(header.valid && header.rubric==80 && header.item==6);
  std::string expected=weather;expected.insert(13,1,' ');
  assert(received[0].text.substr(2)==expected);
  // Numeric symbols, and a tone-only address followed by idle.
  received.clear();rx.reset();Sender numeric;
  numeric.address(2000,false,0);numeric.text(std::string("\1\2\3\4\5"),4);numeric.end();feed(rx,numeric.words);
  assert(received.size()==1 && received[0].text=="12345");
  received.clear();rx.reset();Sender tone;tone.address(208);tone.end();feed(rx,tone.words);
  assert(received.size()==1 && received[0].text.empty());
  // Broken batch sync must discard rather than deliver a partial message.
  received.clear();rx.reset();Sender syncLoss;
  syncLoss.address(4520);syncLoss.text(std::string(150,'s'));syncLoss.end();
  syncLoss.words[17]^=7;feed(rx,syncLoss.words);assert(received.empty());
  // Queue interleaving/wrap, no overwrite of unread chunks, explicit overrun.
  PocsagBitQueue q;uint32_t out;
  for(uint32_t start=0;start<100000;start+=7) {
    for(uint32_t w=start;w<start+7;++w)for(int i=31;i>=0;--i)q.pushBit((w>>i)&1);
    for(uint32_t w=start;w<start+7;++w){assert(q.pop(out));assert(out==w);}
    assert(!q.pop(out));
  }
  for(uint32_t w=0;w<257;++w)for(int i=31;i>=0;--i)q.pushBit((w>>i)&1);
  assert(q.overruns()==1 && q.pending()==256);
  for(uint32_t w=0;w<256;++w){assert(q.pop(out));assert(out==w);}
  q.reset();assert(q.pending()==0 && q.overruns()==1);
  std::puts("PASS: BCH correction/rejection, all RIC frames, streaming boundaries, long pages, loss/timeout, queue wrap/overflow");
}
