"""Check BCH against RadioLib's independent GF encoder, and reproduce legacy bugs.

Compiles extracted, unmodified method bodies from the installed pinned source.
The legacy receive tests document failures; they do not exercise the new path.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
lib = root / 'Arduino Sketch/.pio/libdeps/ttgo-lora32-v21/RadioLib/src/protocols/Pager'
source = (lib / 'Pager.cpp').read_text()
defines = '\n'.join(line for line in (lib / 'Pager.h').read_text().splitlines()
                    if line.startswith('#define RADIOLIB_PAGER_'))

def method(signature, source=source):
    start = source.index(signature)
    body = source.index('{', start)
    depth = 1
    end = body + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

harness = r'''
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>
#include <cassert>
#include <cstdio>
#include "pocsag_receiver.h"
#define RADIOLIB_ERR_PACKET_TOO_LONG -4
#define RADIOLIB_ERR_ADDRESS_NOT_FOUND -100
#define RADIOLIB_ERR_NONE 0
struct Module { static uint8_t flipBits(uint8_t b) {
 uint8_t out=0;for(int i=0;i<8;++i){out=(out<<1)|(b&1);b>>=1;}return out;
}};
struct Radio {std::vector<uint32_t> words;size_t at=0;
 int available(){return int((words.size()-at)*4);}};
class PagerClient {public:
 Radio* _phy;uint32_t _filterMask=0,_filterAddr=0;
 int32_t _bchAlphaTo[32],_bchIndexOf[32],_bchG[11];
 PagerClient(Radio* r):_phy(r){encoderInit();}
 uint32_t read(){assert(_phy->at<_phy->words.size());return _phy->words[_phy->at++];}
 char decodeBCD(uint8_t b){return "0123456789*U -)("[b&15];}
 int16_t readData(uint8_t*,size_t*,uint32_t*);
 void encoderInit();uint32_t encodeBCH(uint32_t);
};
#define RADIOLIB_VERBOSE_PRINT(...)
#define RADIOLIB_VERBOSE_PRINTLN(...)
class PhysicalLayer { public:
 uint8_t _bufferBitPos=0,_bufferWritePos=0,_bufferReadPos=0;
 uint8_t _buffer[256]={};
 uint32_t _syncBuffer=0,_directSyncWord=0x7cd215d8,_directSyncWordMask=0xffffffff;
 uint8_t _directSyncWordLen=0;
 bool _gotSync=true;
 int16_t available();void dropSync();uint8_t read(bool drop=true);
 void updateDirectBuffer(uint8_t);
 void byte(uint8_t value){for(int i=7;i>=0;--i)updateDirectBuffer((value>>i)&1);}
};
'''
main = r'''
int main(){
 Radio radio;PagerClient reference(&radio);PocsagReceiver receiver(nullptr);
 assert(reference.encodeBCH(0x7a89c000)==PocsagReceiver::Idle);
 uint32_t random=17;
 for(unsigned i=0;i<50000;++i){
  random=random*1664525u+1013904223u;
  const uint32_t expected=reference.encodeBCH(random&0xfffff800u);
  assert(PocsagReceiver::syndrome(expected)==0);
  uint32_t bad=expected^(1u<<(i%32))^(1u<<((i+7)%32));
  assert(receiver.correct(bad)==2 && bad==expected);
 }
 puts("PASS: 50,000 independent RadioLib GF-encoded words and double-bit repairs");
 uint8_t out[512];size_t n=512;uint32_t addr=0;
 Radio partial;partial.words={0x1800,0x80000000};PagerClient p(&partial);
 assert(p.readData(out,&n,&addr)==0 && n==2);
 puts("REPRODUCED legacy bug: incomplete message reported as success");
 Radio adjacent;adjacent.words={0x1800,0x80000000,0x20001800,0xffffffff,0x7a89c197};
 PagerClient a(&adjacent);n=512;
 assert(a.readData(out,&n,&addr)==0 && n>2 && adjacent.at==5);
 puts("REPRODUCED legacy bug: next address decoded as payload, merging two messages");
 Radio odd;odd.words={0x7a89c197,0x1800,0x7a89c197};PagerClient o(&odd);n=512;
 assert(o.readData(out,&n,&addr)==0 && addr==1);
 puts("REPRODUCED legacy bug: second word of frame 0 incorrectly assigned RIC 1");
 PhysicalLayer buffer;
 for(uint8_t value:{10,20,30,40})buffer.byte(value);
 assert(buffer.read(false)==10);buffer.byte(50);
 assert(buffer.read(false)==20 && buffer.read(false)==30 && buffer.read(false)==50);
 puts("REPRODUCED legacy buffer bug: reading changes producer index, overwriting unread byte 40");
 PhysicalLayer sync;sync._directSyncWordLen=32;sync.byte(0xab);
 assert(sync.read()==0xab);sync.byte(0xcd);
 assert(!sync._gotSync && sync.available()==0);
 puts("REPRODUCED legacy buffer bug: ordinary read drops sync, ignoring subsequent data");
 PhysicalLayer wrapped;for(int i=0;i<256;++i)wrapped.byte(0xaa);
 assert(wrapped.available()==0);
 puts("REPRODUCED legacy buffer bug: count wraps at 256 bytes, reporting empty");
}
'''
physical = (lib.parent / 'PhysicalLayer/PhysicalLayer.cpp').read_text()
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    cpp = path / 'reference.cpp'
    cpp.write_text(defines + harness +
                   method('void PagerClient::encoderInit()') +
                   method('uint32_t PagerClient::encodeBCH(uint32_t dat)') +
                   method('int16_t PagerClient::readData(uint8_t* data, size_t* len, uint32_t* addr)') +
                   method('int16_t PhysicalLayer::available()', physical) +
                   method('void PhysicalLayer::dropSync()', physical) +
                   method('uint8_t PhysicalLayer::read(bool drop)', physical) +
                   method('void PhysicalLayer::updateDirectBuffer(uint8_t bit)', physical) + main)
    exe = path / 'reference'
    subprocess.run(['c++', '-std=c++11', '-fsanitize=address,undefined',
                    '-I', str(root / 'Arduino Sketch/src'), str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
