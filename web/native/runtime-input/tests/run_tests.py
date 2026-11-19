#!/usr/bin/env python3
"""Compile original runtime/input code and execute it with real browser input.

The supplied checkout must have the browser-memory and input patches applied.
The real host input adapter is supplied separately through --host. No copied
upstream files, ROMs, generated game C or test binaries are retained here.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

HERE=Path(__file__).resolve().parent
parser=argparse.ArgumentParser()
parser.add_argument("--upstream",type=Path,required=True)
parser.add_argument("--host",type=Path,required=True,help="source folder containing the real input_emscripten.c adapter")
parser.add_argument("--emcc",required=True)
parser.add_argument("--node",default="node")
parser.add_argument("--cc",default="cc")
args=parser.parse_args()
up=args.upstream.resolve();host=args.host.resolve()
if not (up/"port/runtime/core/memory_browser.inc").is_file():
    parser.error("--upstream needs the real browser-memory patch before the integration test")
if not (host/"input_emscripten.c").is_file():
    parser.error("--host must identify the real browser host input adapter")

names=["input.c","controls_hooks.c","analog.c","sequencers.c","walk.c"]
with tempfile.TemporaryDirectory(prefix="days-runtime-input-") as directory:
    work=Path(directory)
    (work/"khdays_data_area.h").write_text(
        "#define KHDAYS_IO_VIEW 0x0b100000u\n#define KHDAYS_IO2_VIEW 0x0b110000u\n"
        "#define KHDAYS_DATA_AREA 0x0b200000u\n#define KHDAYS_DATA_AREA_END 0x0b200100u\n")
    includes=[up/"port",up/"port/runtime/input",up/"port/host/app",up/"port/runtime/core",host,work]
    include_args=[item for folder in includes for item in ("-I",str(folder))]
    common=[args.emcc,"-std=c17","-O2","-fms-extensions","-DKHDAYS_BROWSER=1","-DKHDAYS_BROWSER_MEMORY=1",*include_args]
    for name in names:
        subprocess.run([*common,"-Wall","-Wextra","-Werror","-c",str(up/"port/runtime/input"/name),"-o",str(work/(name+".o"))],check=True)
    print("All five original runtime/input units compile as Wasm with strict warnings",flush=True)

    # The native/default code path must still preprocess to its original
    # token stream. The baseline comes from Git, never the patched file itself.
    baseline=work/"native_input.c"
    baseline.write_text(subprocess.check_output([
        "git","-C",str(up),"show","2037f79a171524eafcf40a1d641b16da14667a64:port/runtime/input/input.c"],text=True))
    outputs=[subprocess.check_output([args.cc,"-E","-P",*include_args,str(source)],text=True)
             for source in (baseline,up/"port/runtime/input/input.c")]
    if re.sub(r"\s+","",outputs[0])!=re.sub(r"\s+","",outputs[1]):
        raise AssertionError("The native/default input path changed")
    print("Native/default input path preprocesses identically to the pinned source",flush=True)

    executable=work/"input-smoke.cjs"
    subprocess.run([
        *common,str(HERE/"input_smoke.c"),*[str(up/"port/runtime/input"/name) for name in names],
        str(up/"port/runtime/core/memory.c"),str(host/"input_emscripten.c"),
        "-sENVIRONMENT=node","-sSINGLE_FILE=1","-sWASM_ASYNC_COMPILATION=0","-sEXIT_RUNTIME=1",
        "-sASSERTIONS=2","-sALLOW_MEMORY_GROWTH=1","-o",str(executable)],check=True)
    for mode in ("taps","repress","ownership","scripts","scripts-ui","anchors","analog"):
        subprocess.run([args.node,str(executable),mode],check=True)
    rejected=subprocess.run([args.node,str(executable),"bad-script"],capture_output=True,text=True)
    if rejected.returncode!=2 or "KHDAYS_INPUT: cannot read" not in rejected.stderr:
        raise AssertionError(f"Malformed script did not follow the native error path: {rejected.returncode}")
    print("Malformed numeric script rejected with the original error status",flush=True)
