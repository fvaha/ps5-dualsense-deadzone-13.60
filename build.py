from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent
for name in ['PS5_PAYLOAD_SDK', 'LLVM_CONFIG']:
    if not os.environ.get(name):
        sys.exit(f'{name} is not set (see docs/BUILD.md)')
sdk = Path(os.environ['PS5_PAYLOAD_SDK']).resolve()
env = dict(os.environ)
src = root / 'src'
(root / 'build').mkdir(exist_ok=True)
stage = Path(tempfile.mkdtemp(prefix='payload-', dir=root / 'build'))
inputs = sorted([root / 'build.py', root / 'ui/index.html'] + [p for p in src.iterdir() if p.suffix in {'.c', '.h', '.S', '.py'} and not p.name.startswith(('home-blob-', 'game-blob-', 'web-'))])
snapshot = {str(p.relative_to(root)): p.read_bytes() for p in inputs}
source_hashes = {name: hashlib.sha256(data).hexdigest() for name, data in snapshot.items()}
frozen_src = stage / 'src'
frozen_src.mkdir()
for name, data in snapshot.items():
    if name.startswith('src/'):
        with (frozen_src / Path(name).name).open('xb') as stream:
            stream.write(data)
llvm = Path(subprocess.check_output([env['LLVM_CONFIG'], '--bindir'], text=True).strip())
dependencies = {str(p.relative_to(sdk)): hashlib.sha256(p.read_bytes()).hexdigest()
                for folder in ['bin', 'target/include', 'target/lib', 'target/user/homebrew/lib']
                for p in sorted((sdk / folder).rglob('*')) if p.is_file()}
tools = {name: hashlib.sha256((llvm / name).read_bytes()).hexdigest()
         for name in ['clang', 'ld.lld', 'llvm-nm', 'llvm-readobj', 'llvm-objcopy']}
subprocess.run([sys.executable, '-B', str(frozen_src / 'make_filter.py'), str(stage)], env=env, check=True, stdout=subprocess.DEVNULL)
header = stage / 'web.h'
with header.open('x') as stream:
    stream.write('static const unsigned char web[] = {' + ','.join(map(str, snapshot['ui/index.html'])) + '};\n')
identity = {'sources': source_hashes, 'sdk_files': dependencies, 'llvm_tools': tools}
build_id = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()
with (stage / 'build-id.h').open('x') as stream:
    stream.write('#define DZ_BUILD_ID "' + build_id + '"\n')
for name, source in [('ps5-dualsense-deadzone', 'server.c')]:
    built = stage / f'{name}.elf'
    subprocess.run([str(sdk / 'bin/prospero-clang'), '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(stage), '-DWEB_HEADER="web.h"', source, 'game.c', 'patch.c', '-o', str(built)], cwd=frozen_src, env=env, check=True)
    data = built.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    output = root / f'{name}.elf'
    output.write_bytes(data)
    record = {'output': output.name, 'sha256': digest, 'sources': source_hashes, 'build_id': build_id,
              'sdk_files': dependencies, 'llvm_tools': tools,
              'sdk': str(sdk), 'crt1_sha256': hashlib.sha256((sdk / 'target/lib/crt1.o').read_bytes()).hexdigest(),
              'llvm_version': subprocess.check_output([env['LLVM_CONFIG'], '--version'], text=True).strip()}
    with (stage / f'{name}.json').open('x') as stream:
        json.dump(record, stream, indent=2)
        stream.write('\n')
    print(output)
print(f'Build records and generated headers: {stage}')
