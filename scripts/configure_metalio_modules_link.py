"""Only a compiled camera module may force its custom USB enumerator into ELF."""
from pathlib import Path
import sys

Import('env')
sys.path.insert(0, str(Path(env.subst('$PROJECT_DIR')) / 'scripts'))
from metalio_module_config import modules_from_defines

modules = modules_from_defines(env.get('CPPDEFINES', []))
if modules['camera']:
    env.AppendUnique(LINKFLAGS=['-Wl,--undefined=enum_install'])

if env["PIOENV"] == "rickyos_metalio_eink4":
    def check_ota_size(target, source, env):
        image_bytes = Path(str(target[0])).stat().st_size
        limit = 0x640000 - 512 * 1024
        aligned = (image_bytes + 4095) & ~4095
        if aligned > limit:
            raise RuntimeError(f"Metalio OTA image ({image_bytes} bytes) exceeds {limit} bytes with reserve")
        print(f"Metalio OTA image: {image_bytes} bytes; {limit - aligned} bytes below aligned OTA limit")

    env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", check_ota_size)
