"""Replace LGPL shared libraries in an APK; align/sign the result separately."""
import argparse
from pathlib import Path
import zipfile


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('apk', type=Path)
    p.add_argument('output', type=Path)
    p.add_argument('--library', action='append', type=Path, required=True)
    args = p.parse_args()
    if args.output.exists() or args.apk.resolve() == args.output.resolve():
        p.error('Choose a new output path; existing files will not be overwritten')
    replacements = {}
    for library in args.library:
        if library.name not in ('libopenal.so', 'libSurrealVideo.so'):
            p.error('Only libopenal.so and libSurrealVideo.so may be replaced')
        data = library.read_bytes()
        # ELF64, little endian, AArch64. Reject a desktop library by mistake.
        if len(data) < 20 or data[:6] != b'\x7fELF\x02\x01' or data[18:20] != b'\xb7\x00':
            p.error(f'Not an ARM64 ELF library: {library}')
        replacements['lib/arm64-v8a/' + library.name] = data
    with zipfile.ZipFile(args.apk) as source:
        if not replacements.keys() <= set(source.namelist()):
            p.error('Input APK does not contain the requested library')
        with zipfile.ZipFile(args.output, 'x') as dest:
            for entry in source.infolist():
                name = entry.filename
                if name.upper().startswith('META-INF/') and name.upper().endswith(('.SF', '.RSA', '.DSA', '.EC', 'MANIFEST.MF')):
                    continue
                dest.writestr(entry, replacements.get(name, source.read(name)))
    print(f'Created {args.output}; align and sign it before installing.')


if __name__ == '__main__':
    main()
