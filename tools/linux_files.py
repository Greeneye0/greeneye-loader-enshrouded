"""The Linux files every Greeneye mod zip ships next to the loader, in greeneye/linux/:
LINUX.txt (the player guide: rename dbgcore.dll to dinput8.dll, WINEDLLOVERRIDES, chain loading)
and dinput8-chain.ini.example. They load nothing by themselves.

From a package script (as a module):
    sys.path.insert(0, <GreeneyeLoader>/tools); import linux_files
    files += linux_files.entries()          # [(source path, path in the zip), ...]

For a zip packed by hand or repacked from the previous release:
    python -I GreeneyeLoader/tools/linux_files.py <release.zip>
adds (or replaces) them in that zip.
"""
import os
import sys
import zipfile

LINUX_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'dist', 'linux')
NAMES = ['LINUX.txt', 'dinput8-chain.ini.example']
ARC = 'greeneye/linux/'


def entries():
    out = []
    for name in NAMES:
        path = os.path.join(LINUX_DIR, name)
        if not os.path.isfile(path):
            sys.exit('missing ' + path)
        out.append((path, ARC + name))
    return out


def add_to_zip(zip_path):
    keep = []
    with zipfile.ZipFile(zip_path) as z:
        for info in z.infolist():
            if not info.filename.startswith(ARC):
                keep.append((info, z.read(info.filename)))
    tmp = zip_path + '.tmp'
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as z:
        for info, data in keep:
            z.writestr(info, data)
        for src, arc in entries():
            z.write(src, arc)
    os.replace(tmp, zip_path)
    print(zip_path, os.path.getsize(zip_path), 'bytes; added', ', '.join(arc for _, arc in entries()))


if __name__ == '__main__':
    if len(sys.argv) != 2 or not sys.argv[1].lower().endswith('.zip'):
        sys.exit(__doc__)
    add_to_zip(sys.argv[1])
