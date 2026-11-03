"""Read-only validator for the supplied Newton manifest and eight PCM files.

The device uses this exact fixed basename allowlist; manifest paths never become
arbitrary SD filesystem paths. No artwork authoring, network access or card writes.
"""
import argparse
import hashlib
import json
import pathlib

ACTIONS = ('look_up', 'think', 'walk_out', 'walk_back', 'demonstrate_prism', 'inspect_then_delight')
EXPECTED = {f'newton_{i}': f'audio/newton_{i}.pcm' for i in range(1, 7)}
EXPECTED.update(music='audio/music_loop.pcm', wind='audio/wind_loop.pcm')

def validate(manifest, root):
    if manifest.get('version') != 1 or manifest.get('format') != {
        'encoding': 'signed16_little_endian_raw', 'sample_rate': 24000, 'channels': 1
    }:
        raise ValueError('Unsupported manifest version or PCM format')
    assets = manifest.get('assets', [])
    if len(assets) != 8 or {a.get('asset_id') for a in assets} != set(EXPECTED):
        raise ValueError('Expected exactly eight unique known audio assets')
    receipt = []
    root = pathlib.Path(root).resolve(strict=True)
    for asset in assets:
        identity = asset['asset_id']
        if asset.get('path') != EXPECTED[identity]:
            raise ValueError('Asset path is not the exact fixed allowlisted path')
        name = pathlib.PurePosixPath(EXPECTED[identity]).name
        path = root / name
        if path.is_symlink() or path.resolve(strict=True).parent != root:
            raise ValueError('Audio file must remain within the selected directory')
        data = path.read_bytes()
        if not data or len(data) % 2 or len(data) > 24000 * 2 * 30:
            raise ValueError(f'{name}: invalid/oversized 16-bit PCM')
        digest = hashlib.sha256(data).hexdigest()
        if digest != asset.get('sha256'):
            raise ValueError(f'{name}: SHA256 mismatch')
        seconds = len(data) / 48000
        if abs(seconds - asset.get('duration_seconds', -1)) > 1 / 24000:
            raise ValueError(f'{name}: duration mismatch')
        if identity.startswith('newton_'):
            number = int(identity[-1])
            if asset.get('action_id') != ACTIONS[number - 1] or asset.get('loop') is not False:
                raise ValueError(f'{name}: voice/action mismatch')
        elif asset.get('loop') is not True:
            raise ValueError(f'{name}: ambience must loop')
        receipt.append({'file': name, 'bytes': len(data), 'sha256': digest, 'seconds': seconds})
    return receipt

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('manifest', type=pathlib.Path)
    parser.add_argument('audio_root', type=pathlib.Path)
    args = parser.parse_args()
    print(json.dumps({'verified': validate(json.loads(args.manifest.read_text(encoding='utf-8-sig')), args.audio_root)}, indent=2))
