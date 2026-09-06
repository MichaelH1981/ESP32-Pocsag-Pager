# Project-local, reproducible bounds hardening for the pinned RadioLib 5.6.0.
from pathlib import Path

def patch_pager(path):
    path = Path(path)
    text = path.read_text()
    marker = '// Pager v0.4: enforce caller buffer capacity'
    if marker in text:
        return
    start = text.index('int16_t PagerClient::readData(uint8_t* data, size_t* len, uint32_t* addr) {')
    end = text.index('\nvoid PagerClient::write(', start)
    body = text[start:end]
    if body.count('data[decodedBytes++] = symbol;') != 2:
        raise RuntimeError('Unexpected RadioLib Pager implementation; inspect before patching')
    body = body.replace('  // find the correct address',
        marker + '\n  if (!data || !len || *len == 0) return(RADIOLIB_ERR_PACKET_TOO_LONG);\n  const size_t capacity = *len;\n  // find the correct address', 1)
    body = body.replace('_phy->available())', '_phy->available() >= 4)')
    body = body.replace('      data[decodedBytes++] = symbol;',
        '      if (decodedBytes >= capacity) {\n'
        '        *len = decodedBytes;\n'
        '        return(RADIOLIB_ERR_PACKET_TOO_LONG);\n'
        '      }\n'
        '      data[decodedBytes++] = symbol;')
    path.write_text(text[:start] + body + text[end:])

if __name__ == '__main__':
    import sys
    patch_pager(sys.argv[1])
else:
    Import('env')
    dependency = Path(env.subst('$PROJECT_LIBDEPS_DIR')) / env.subst('$PIOENV') / 'RadioLib/src/protocols/Pager/Pager.cpp'
    patch_pager(dependency)
