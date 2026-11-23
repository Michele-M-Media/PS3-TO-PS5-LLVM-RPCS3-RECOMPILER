"""Check title artwork and package metadata with synthetic local inputs."""

from pathlib import Path
import json
import struct
import subprocess
import sys
import tempfile
from types import SimpleNamespace


sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from pack_update import pack
from prepare_dump import dds_background, prepare_icon, stage


def write_sfo(path, fields):
    keys, values, entries = bytearray(), bytearray(), bytearray()
    for key, value in fields.items():
        encoded = value.encode() + b'\x00'
        entries.extend(struct.pack('<HHIII', len(keys), 0x0204, len(encoded), len(encoded), len(values)))
        keys.extend(key.encode() + b'\x00')
        values.extend(encoded)
    index = 20 + len(entries)
    path.write_bytes(struct.pack('<4s4I', b'\x00PSF', 0x101, index, index + len(keys), len(fields))
                     + entries + keys + values)


def check_update(folder):
    disc = folder / 'disc'
    game = disc / 'PS3_GAME'
    (game / 'USRDIR').mkdir(parents=True)
    (disc / 'PS3_DISC.SFB').touch()
    original = game / 'USRDIR/EBOOT.BIN'
    original.write_bytes(b'disc payload')
    title = {'TITLE_ID': 'TEST12345', 'TITLE': 'Test'}
    write_sfo(game / 'PARAM.SFO', title | {'APP_VER': '01.00'})
    update = folder / 'update'
    (update / 'USRDIR').mkdir(parents=True)
    (update / 'USRDIR/EBOOT.BIN').write_bytes(b'update payload')
    write_sfo(update / 'PARAM.SFO', title | {'APP_VER': '01.02', 'CATEGORY': 'GD'})
    bundle = folder / 'bundle'
    pack(disc, update, bundle)
    assert '01.02' in (bundle / 'recompiler_title_update_manifest.h').read_text()
    core = folder / 'eboot.bin'
    core.write_bytes(b'native payload')
    param = folder / 'param.json'
    param.write_text(json.dumps({'titleId': 'PPSA12345'}))
    firmware = folder / 'firmware'
    (firmware / 'sys/external').mkdir(parents=True)
    built = folder / 'built'
    stage(SimpleNamespace(disc=disc, core=core, param=param, firmware=firmware, output=built,
                          display_mode='default', icon0=None, pic0=None, pic1=None, spu_hybrid=False,
                          spu_async=False, update_bin=bundle / 'UPDATE.BIN'))
    assert (built / 'UPDATE.BIN').read_bytes() == (bundle / 'UPDATE.BIN').read_bytes()
    assert (built / 'PS3_GAME/USRDIR/EBOOT.BIN').read_bytes() == original.read_bytes() == b'disc payload'


def main():
    with tempfile.TemporaryDirectory() as scratch:
        folder = Path(scratch)
        source = folder / 'wide.png'
        target = folder / 'icon0.png'
        subprocess.run(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'color=blue:size=320x180',
                        '-frames:v', '1', '-update', '1', str(source)], check=True)
        original = source.read_bytes()
        prepare_icon(source, target)
        header = target.read_bytes()[:26]
        assert struct.unpack_from('>II', header, 16) == (512, 512)
        assert header[25] == 2 and source.read_bytes() == original

        dds = folder / 'background.dds'
        header = bytearray(148)
        header[:4], header[84:88] = b'DDS ', b'DX10'
        struct.pack_into('<I', header, 4, 124)
        struct.pack_into('<II', header, 12, 2160, 3840)
        struct.pack_into('<IIII', header, 128, 98, 3, 0, 1)
        with dds.open('wb') as file:
            file.write(header)
            file.truncate(len(header) + 3840 * 2160)
        assert dds_background(dds) == dds
        with dds.open('r+b') as file:
            file.seek(128)
            file.write(struct.pack('<I', 99))
        try:
            dds_background(dds)
        except ValueError:
            pass
        else:
            raise AssertionError('Invalid BC7 format was accepted')
        check_update(folder)
    print('Artwork fit, DDS validation and UPDATE.BIN staging passed')


if __name__ == '__main__':
    main()
