"""Measure the real target compiler's CFF frames; desktop stacks hide overflows."""
from pathlib import Path
import json
import shlex
import subprocess
import tempfile

PORT = Path(__file__).resolve().parents[1]
commands_path = PORT / '.local/build-rickyos-plus-1.2/compile_commands.json'
if not commands_path.exists():
    print('SKIP CFF target stack budget: configure/build the Plus firmware first')
    raise SystemExit(0)
commands = json.loads(commands_path.read_text())
with tempfile.TemporaryDirectory(prefix='plus-cff-stack-') as folder:
    for name, function, ceiling in [('ft_psaux.c', 'cf2_interpT2CharString', 2048),
                                    ('ft_cff.c', 'cff_slot_load', 2048)]:
        entry = next((c for c in commands if c['file'].endswith('/' + name)), None)
        assert entry, 'CFF is not included in the configured firmware'
        args = shlex.split(entry['command'])
        obj = Path(folder) / (name + '.o')
        args[args.index('-o') + 1] = str(obj)
        # GCC's LTO objects do not emit .su here. All other target/optimization
        # settings match the configured production compile command.
        subprocess.run([*args, '-fno-lto', '-fstack-usage'], cwd=entry['directory'], check=True)
        rows = obj.with_suffix('.su').read_text().splitlines()
        row = next(line.split('\t') for line in rows if line.split('\t')[0].endswith(':' + function))
        size = int(row[1])
        assert size <= ceiling, (function, size, ceiling)
        print(f'CFF target stack budget: {function}={size} B <= {ceiling} B')
