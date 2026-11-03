"""Test the installed reset method on stale and partly consumed capture data."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
header = root / 'Arduino Sketch/.pio/libdeps/ttgo-lora32-v21/RadioLib/src/protocols/PhysicalLayer/PhysicalLayer.h'
text = header.read_text()
start = text.index('    void resetDirectBuffer() {')
end = text.index('\n    }', start) + len('\n    }')
method = text[start:end]
source = r'''
#include <cstdint>
#include <cassert>
struct Receiver {
 uint8_t _bufferBitPos=7, _bufferWritePos=200, _bufferReadPos=199;
 uint32_t _syncBuffer=0xdeadbeef, _directSyncWord=0x832dea27;
 uint8_t _directSyncWordLen=32;
 bool _gotSync=true;
METHOD
};
int main() {
 Receiver r;
 r.resetDirectBuffer();
 assert(!r._bufferBitPos && !r._bufferWritePos && !r._bufferReadPos);
 assert(!r._syncBuffer && !r._gotSync);
 assert(r._directSyncWord==0x832dea27 && r._directSyncWordLen==32);
 r.resetDirectBuffer(); assert(!r._gotSync);
 r._directSyncWordLen=0; r.resetDirectBuffer(); assert(r._gotSync);
}
'''.replace('METHOD', method)
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / 'reset.cpp'
    exe = Path(directory) / 'reset'
    cpp.write_text(source)
    subprocess.run(['c++', '-std=c++11', '-fsanitize=address,undefined', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('PASS: installed direct-reset clears partial byte, stale indices and sync while retaining configuration')
