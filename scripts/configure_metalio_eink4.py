"""Compile the verified ordinary-board panel backends, preserving other SDK sources."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
Import("env")
if env["PIOENV"] in ("rickyos_metalio_eink4", "simulator_rickyos_metalio_eink4"):
    root = Path(env.subst("$PROJECT_DIR"))
    report = root / "generated/fonts/packing-report.json"
    records = json.loads(report.read_text()) if report.exists() else []
    if len(records) != 7 or not any(r["name"] == "metalio_body_10_regular" for r in records) or any(
        hashlib.sha256((root / "lib/EpdFont/builtinFonts" / (r["name"] + ".h")).read_bytes()).hexdigest()
        != r["source_sha256"] or not (root / "generated/fonts" / (r["name"] + ".packed.h")).exists()
        for r in records
    ):
        subprocess.run([sys.executable, str(root / "scripts/compress_metalio_fonts.py"), "--native-small"], check=True)
    env.Append(CPPPATH=[str(root / "generated")])
    if env["PIOENV"] == "simulator_rickyos_metalio_eink4":
        env.Prepend(CPPPATH=[str(root / '.pio/libdeps' / env['PIOENV'] / 'simulator/src'), str(root / 'lib/hal')])
        env.BuildSources("$BUILD_DIR/preview-hal", "$PROJECT_DIR/lib/hal", src_filter=[
            "+<HalNetwork.cpp>", "+<HalCamera.cpp>", "+<HalCellular.cpp>", "+<HalAudioOutput.cpp>"])
    allowed = {"CrossMuxSsd1677Driver.cpp", "PaperMonoDriver.cpp"}
    def panel_source(build_env, node):
        if Path(str(node)).name not in allowed:
            return None
        return node
    env.AddBuildMiddleware(panel_source, "*/FreeInkDisplay/src/driver/*.cpp")
