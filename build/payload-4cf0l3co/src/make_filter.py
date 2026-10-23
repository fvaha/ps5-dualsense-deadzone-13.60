from pathlib import Path
import json
import os
import subprocess
import sys

root = Path(__file__).resolve().parent
out = Path(sys.argv[1]).resolve()
out.mkdir(parents=True, exist_ok=True)
llvm_config = os.environ['LLVM_CONFIG']
llvm = Path(subprocess.check_output([llvm_config, '--bindir'], text=True).strip())
result = {}
for kind, count in [('home', 2), ('game', 5)]:
    source = root / f'filter-{kind}.S'
    obj = out / f'filter-{kind}.o'
    binary = out / f'filter-{kind}.bin'
    if any(p.exists() for p in [obj, binary, out / f'{kind}-blob.h']):
        raise FileExistsError('Use a new output directory for each generation')
    subprocess.run([str(llvm / 'clang'), '-target', 'x86_64-unknown-freebsd', '-c', str(source), '-o', str(obj)], check=True)
    reloc = subprocess.check_output([str(llvm / 'llvm-readobj'), '--relocations', str(obj)], text=True)
    if '0x' in reloc:
        raise ValueError(reloc)
    subprocess.run([str(llvm / 'llvm-objcopy'), '-O', 'binary', '--only-section=.text', str(obj), str(binary)], check=True)
    symbols = {line.split()[2]: int(line.split()[0], 16) for line in subprocess.check_output([str(llvm / 'llvm-nm'), str(obj)], text=True).splitlines() if len(line.split()) == 3}
    code = binary.read_bytes()
    if not (0 < len(code) <= 512 and symbols[kind + '_blob'] == 0 and len(code) == symbols[kind + '_blob_end']):
        raise ValueError('Invalid filter size or boundaries')
    name = 'blob' if kind == 'home' else 'game_blob'
    prefix = 'DZ' if kind == 'home' else 'GAME'
    fields = [side + '_' + field for side in 'lr' for field in ['threshold', 'n1x', 'n1y', 'n2x', 'n2y']] + ['config']
    header = 'static const unsigned char ' + name + '[] = {' + ','.join(map(str, code)) + '};\n'
    header += 'static const unsigned ' + kind + '_entries[] = {' + ','.join(str(symbols[kind + '_entry' + str(i)]) for i in range(count)) + '};\n'
    header += '#define ' + prefix + '_CONFIG_OFFSET ' + str(symbols[kind + '_config']) + '\n'
    header += 'static const unsigned ' + kind + '_fields[] = {' + ','.join(str(symbols[kind + '_' + field]) for field in fields) + '};\n'
    path = out / f'{kind}-blob.h'
    with path.open('x') as stream:
        stream.write(header)
    result[kind] = str(path)
print(json.dumps(result))
