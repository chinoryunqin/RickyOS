"""Only a compiled camera module may force its custom USB enumerator into ELF."""
from pathlib import Path
import sys

Import('env')
sys.path.insert(0, str(Path(env.subst('$PROJECT_DIR')) / 'scripts'))
from metalio_module_config import modules_from_defines

modules = modules_from_defines(env.get('CPPDEFINES', []))
if modules['camera']:
    env.AppendUnique(LINKFLAGS=['-Wl,--undefined=enum_install'])
