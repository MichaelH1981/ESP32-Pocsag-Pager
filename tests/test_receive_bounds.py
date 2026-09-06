# Compile the actual installed RadioLib readData() against a deterministic radio.
# Use --expect-overflow against the original 5.6.0 source to reproduce the bug.
from pathlib import Path
import subprocess, sys, tempfile
root=Path(__file__).resolve().parents[1]
lib=root/'Arduino Sketch/.pio/libdeps/ttgo-lora32-v21/RadioLib/src/protocols/Pager'
if '--source-dir' in sys.argv:
    lib = Path(sys.argv[sys.argv.index('--source-dir') + 1])
source=(lib/'Pager.cpp').read_text()
start=source.index('int16_t PagerClient::readData(uint8_t* data, size_t* len, uint32_t* addr) {')
end=source.index('\nvoid PagerClient::write(',start)
function=source[start:end]
defs='\n'.join(line for line in (lib/'Pager.h').read_text().splitlines() if line.startswith('#define RADIOLIB_PAGER_'))
harness=r'''
#include <cstdint>
#include <cstddef>
#include <vector>
#include <cassert>
#include <cstdio>
#define RADIOLIB_ERR_PACKET_TOO_LONG -4
#define RADIOLIB_ERR_ADDRESS_NOT_FOUND -100
#define RADIOLIB_ERR_NONE 0
struct Module { static uint8_t flipBits(uint8_t b) { uint8_t out=0;for(int i=0;i<8;++i){out=(out<<1)|(b&1);b>>=1;}return out;} };
struct Radio {std::vector<uint32_t> words;size_t at=0;int partial=0;int available(){return int((words.size()-at)*4)+partial;}};
class PagerClient {public: Radio* _phy;uint32_t _filterMask=0,_filterAddr=0;
 PagerClient(Radio* r):_phy(r){};
 uint32_t read(){assert(_phy->at<_phy->words.size());return _phy->words[_phy->at++];}
 char decodeBCD(uint8_t b){const char* chars="0123456789*U -)(";return chars[b&15];}
 int16_t readData(uint8_t*,size_t*,uint32_t*);
};
'''
main=r'''
int main(){
 Radio radio;radio.words.push_back(0x1800); // alphanumeric address
 for(int i=0;i<100;++i)radio.words.push_back(0xffffffff); // oversized received message
 PagerClient pager(&radio);std::vector<uint8_t> out(8);size_t length=8;uint32_t addr=0;
 int state=pager.readData(out.data(),&length,&addr);
 assert(state==RADIOLIB_ERR_PACKET_TOO_LONG && length==8);
 // Partial words must not be read; short valid message still succeeds.
 Radio shortRadio;shortRadio.words={0x1800,0x80000000,0x7a89c197};shortRadio.partial=3;
 PagerClient shortPager(&shortRadio);length=8;
 assert(shortPager.readData(out.data(),&length,&addr)==0 && length==2);
 Radio partial;partial.partial=3;PagerClient partialPager(&partial);length=8;
 assert(partialPager.readData(out.data(),&length,&addr)==RADIOLIB_ERR_ADDRESS_NOT_FOUND);
 length=0;assert(pager.readData(out.data(),&length,&addr)==RADIOLIB_ERR_PACKET_TOO_LONG);
 puts("PASS: actual RadioLib readData bounds, partial words and valid short message");
}
'''
with tempfile.TemporaryDirectory() as d:
 cpp=Path(d)/'test.cpp';exe=Path(d)/'test';cpp.write_text(defs+harness+function+main)
 subprocess.run(['c++','-std=c++14','-fsanitize=address,undefined','-g',str(cpp),'-o',str(exe)],check=True)
 result=subprocess.run([str(exe)],capture_output=True,text=True)
 if '--expect-overflow' in sys.argv:
  assert result.returncode!=0 and 'heap-buffer-overflow' in result.stderr, result.stdout+result.stderr
  print('REPRODUCED: original RadioLib 5.6.0 writes beyond caller buffer (AddressSanitizer)')
 else:
  print(result.stdout,end='');print(result.stderr,end='');assert result.returncode==0
