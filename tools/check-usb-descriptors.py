"""Inspect actual linked FS/HS descriptors. No USB, UART or network access."""
from pathlib import Path
import struct
import sys
from elftools.elf.elffile import ELFFile

path = Path(sys.argv[1] if len(sys.argv) > 1 else 'build/ouse_core_desktop.elf')
with path.open('rb') as source:
    elf = ELFFile(source)
    symbols = elf.get_section_by_name('.symtab')
    for name, interval, bulk_size in [('fs_config', 1, 64), ('hs_config', 1, 512)]:
        matches = symbols.get_symbol_by_name(name)
        assert matches and len(matches) == 1, (name, 'missing or ambiguous descriptor symbol')
        symbol = matches[0]
        section = elf.get_section(symbol['st_shndx'])
        offset = symbol['st_value'] - section['sh_addr']
        data = section.data()[offset:offset + symbol['st_size']]
        assert data[:2] == bytes([9, 2]) and data[4] == 5
        assert struct.unpack_from('<H', data, 2)[0] == len(data)
        entries = []
        cursor = 0
        while cursor < len(data):
            length = data[cursor]
            assert length >= 2 and cursor + length <= len(data), (name, cursor, length)
            entries.append(data[cursor:cursor + length])
            cursor += length
        interfaces = [d for d in entries if d[1] == 4]
        assert {(d[2], d[3]) for d in interfaces} == {(0, 0), (1, 0), (1, 1), (2, 0), (2, 1), (3, 0), (4, 0), (4, 1)}
        assert [(d[2], d[3]) for d in entries if d[1] == 11] == [(0, 3), (3, 2)]
        assert all(d[8] < 8 for d in interfaces)  # pinned esp_tinyusb string capacity
        ncm_eth = next(d for d in entries if d[:3] == bytes([13, 0x24, 0x0f]))
        assert ncm_eth[3] == 7  # MAC string, within the eight-string table
        endpoints = {d[2]: d for d in entries if d[1] == 5}
        assert len(endpoints) == 6 and set(endpoints) == {1, 0x81, 0x82, 0x83, 4, 0x84}
        for address in [1, 0x81, 0x82]:
            assert endpoints[address][6] == interval
        assert endpoints[1][3] == 5 and endpoints[0x81][3] == 5  # asynchronous PCM
        assert endpoints[0x82][3] == 0x11  # explicit feedback
        for address in [4, 0x84]:
            assert struct.unpack_from('<H', endpoints[address], 4)[0] == bulk_size
        # Audio class length and AC class-specific total must stop before NCM.
        iad_offset = next(i for i in range(len(data)) if data[i:i+4] == bytes([8, 11, 3, 2]))
        assert iad_offset - 9 == 209
        ac = next(d for d in entries if d[:3] == bytes([9, 0x24, 1]))
        assert struct.unpack_from('<H', ac, 6)[0] == 75
        print(f'PASS {name}: {len(data)} bytes, UAC2 duplex + feedback + NCM, five interfaces, six endpoints')
