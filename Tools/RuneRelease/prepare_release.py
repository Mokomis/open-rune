"""Validate a release identity and write a manifest beside staged release assets.
Does not commit, publish, upload or use GitHub credentials.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def identity(root=ROOT):
    header = (root / 'SurrealEngine/OpenRuneVersion.h').read_text()
    def field(name):
        match = re.search(r'\b' + name + r'\s*=\s*(?:"([^"]+)"|(\d+))', header)
        if not match:
            raise ValueError('Missing identity field: ' + name)
        return match.group(1) or int(match.group(2))
    protocol = int(re.search(r'ProtocolVersion\s*=\s*(\d+)',
        (root / 'Network/include/openrune/net/Protocol.h').read_text()).group(1))
    discovery = int(re.search(r'GAME_PROTOCOL\s*=\s*(\d+)',
        (root / 'Tools/RuneDiscovery/protocol.cjs').read_text()).group(1))
    if protocol != discovery:
        raise ValueError('Discovery/native network protocols differ; update and rebuild both.')
    return dict(version=field('Version'), build=field('Build'), repository=field('Repository'),
                compatibility=dict(network_protocol=protocol, gameplay_rules=field('GameplayRulesRevision')))


def prepare(tag, assets, development=False, root=ROOT):
    manifest = identity(root)
    if tag != 'v' + manifest['version']:
        raise ValueError('Release tag must match the compiled client/server version.')
    if not development and not re.fullmatch(r'v(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)', tag):
        raise ValueError('Stable releases need a stable version; use --development for local staging only.')
    if not 1 <= manifest['build'] <= 2100000000:
        raise ValueError('Android build number is outside its supported range.')
    manifest.update(schema=1, tag=tag, channel='development' if development else 'stable', assets=[])
    names = set()
    for path in assets:
        path = Path(path)
        if path.name in names:
            raise ValueError('Duplicate asset filename: ' + path.name)
        names.add(path.name)
        digest = hashlib.sha256()
        with path.open('rb') as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b''):
                digest.update(chunk)
        manifest['assets'].append(dict(name=path.name, bytes=path.stat().st_size, sha256=digest.hexdigest()))
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--asset', action='append', type=Path, default=[])
    parser.add_argument('--development', action='store_true')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    value = prepare(args.tag, args.asset, args.development)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
    print(args.output)
