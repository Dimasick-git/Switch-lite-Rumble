#!/usr/bin/env python3
"""Combine a full companion kit with this checkout's module/test builds."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import zipfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--base', type=Path, required=True, help='Full SD kit with Ryzhand and RyazhTune')
parser.add_argument('--output', type=Path, default=root / 'dist')
parser.add_argument('--version', default='beta-0.21.0')
parser.add_argument('--notes', type=Path)
args = parser.parse_args()
base = args.base.resolve()
assert base.is_dir(), base
args.output.mkdir(parents=True, exist_ok=True)
files = {}
for file in sorted(base.rglob('*')):
    if file.is_file() and file.relative_to(base).parts[0] in {'atmosphere', 'config', 'switch'}:
        name = file.relative_to(base).as_posix()
        assert file.suffix.lower() not in {'.log', '.nca', '.keys'}, name
        assert file.name not in {'volume-level.ini', 'volume-session.ini'}, name
        files[name] = file.read_bytes()
tid = 'atmosphere/contents/4200000000525652/'
files[tid + 'exefs.nsp'] = (root / 'out/nintendo_nx_arm64_armv8a/release/ryz-lite-rumble-mitm.nsp').read_bytes()
files[tid + 'flags/boot2.flag'] = b''
files[tid + 'mitm.lst'] = b'audctl\novln:rcv\n'
files['switch/ryz-rumble-test/ryz-rumble-test.nro'] = (root / 'test-app/ryz-rumble-test.nro').read_bytes()
files['config/ryz-lite-rumble/config.ini'] = (root / 'config/config.ini').read_bytes()
files['config/ryz-lite-rumble/LICENSE'] = (root / 'LICENSE').read_bytes()
files['README-RU.txt'] = (args.notes or root / 'docs/releases' / (args.version + '.md')).read_bytes()
for name in ('switch/.overlays/ovlmenu.ovl', 'switch/.overlays/RyazhTune-Overlay.ovl',
             'atmosphere/contents/0100000000000ED1/exefs.nsp',
             'atmosphere/contents/420000000000000E/exefs.nsp',
             'switch/Ryazhahand-Reload/Ryazhahand-Reload.nro',
             'config/ryazhahand/.loaded_sounds/tick.wav'):
    assert files.get(name), f'Missing companion file: {name}'
subprocess.run(['git', '-C', str(root), 'diff', '--quiet', 'HEAD'], check=True)
commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
metadata = {'release': args.version, 'source_commit': commit,
            'ryzhand_reference_commit': subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD:Ryzhand'], text=True).strip(),
            'companions_reused_from_base_kit': True,
            'ryzhand_sha256': hashlib.sha256(files['switch/.overlays/ovlmenu.ovl']).hexdigest(),
            'module_sha256': hashlib.sha256(files[tid + 'exefs.nsp']).hexdigest(),
            'test_sha256': hashlib.sha256(files['switch/ryz-rumble-test/ryz-rumble-test.nro']).hexdigest()}
files['BUILD-INFO.json'] = (json.dumps(metadata, ensure_ascii=False, indent=2) + '\n').encode()
archive = args.output / 'sdout.zip'
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as output:
    for name, data in sorted(files.items()):
        output.writestr(name, data)
with zipfile.ZipFile(archive) as output:
    assert output.testzip() is None
    assert set(output.namelist()) == files.keys()
    for name, data in files.items():
        assert output.read(name) == data, name
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
(args.output / 'SHA256SUMS.txt').write_text(f'{digest}  sdout.zip\n', encoding='ascii')
update = args.output / 'module-update.zip'
update_files = {name: data for name, data in files.items() if name.startswith(tid) or
                name.startswith('switch/ryz-rumble-test/') or name in {'README-RU.txt', 'BUILD-INFO.json'}}
with zipfile.ZipFile(update, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as output:
    for name, data in sorted(update_files.items()):
        output.writestr(name, data)
with zipfile.ZipFile(update) as output:
    assert output.testzip() is None
    assert set(output.namelist()) == update_files.keys()
    for name, data in update_files.items():
        assert output.read(name) == data, name
update_digest = hashlib.sha256(update.read_bytes()).hexdigest()
(args.output / 'SHA256SUMS.txt').write_text(f'{digest}  sdout.zip\n{update_digest}  module-update.zip\n', encoding='ascii')
print(json.dumps({'archive': str(archive), 'files': len(files), 'bytes': archive.stat().st_size,
                  'sha256': digest, 'update_archive': str(update), 'update_sha256': update_digest, **metadata}, indent=2))
