"""Run one sleep/wake through the pinned Days alarm path under WebAssembly."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--upstream", type=Path, required=True)
parser.add_argument("--emcc", required=True)
parser.add_argument("--node", default="node")
args = parser.parse_args()
upstream = args.upstream.resolve()
sys.path.insert(0, str(upstream / "port/tools"))

import prepare
from references import rewrite_text
from references.emit import Index
from references.preprocess import Flags, preprocess

decomp = upstream / "external/khdays-decomp"
sources = ["auto/OS_CreateAlarm", "calls/OS_Sleep", "calls/OS_SetAlarm",
           "calls/OSi_InsertAlarm", "calls/OSi_ArrangeTimer",
           "calls/OSi_SleepAlarmCallback", "calls/OS_WakeupThreadDirect"]
index = Index(ds_objects={name: int(name[5:], 16) for name in
                          ("data_0204430c", "data_02044674", "data_027e0000")},
              functions={"OSi_SleepAlarmCallback": 0x02002178},
              discarded_returns={"OS_RestoreInterrupts": "i32"})

with tempfile.TemporaryDirectory(prefix="days-sleep-alarm-") as work:
    directory = Path(work)
    outputs = []
    for relative in sources:
        source = decomp / "libs/nitro/os" / (relative + ".c")
        if source.stem == "OSi_SleepAlarmCallback":
            text = source.read_text()
            for before, after in prepare.HOOKS["main@02002178"]:
                if text.count(before) != 1:
                    raise ValueError(f"Sleep callback hook no longer matches {source}")
                text = text.replace(before, after)
            source = directory / "callback-input.c"
            source.write_text(text)
        cooked = preprocess(source, Flags(includes=[decomp / "include"]), dict(os.environ),
                            args.emcc, backend="clang")
        output = directory / (Path(relative).name + ".c")
        output.write_text(rewrite_text(cooked, str(source), index))
        outputs.append(output)
    (directory / "khdays_data_area.h").write_text("/* No numbered data in this slice. */\n")
    program = directory / "alarm.cjs"
    subprocess.run([args.emcc, "-std=c17", "-O2", "-fms-extensions", "-fpack-struct=4",
                    "-ffunction-sections", "-Werror", "-Wno-unknown-pragmas",
                    "-Wno-missing-declarations", "-Wno-parentheses-equality",
                    "-DKHDAYS_BROWSER_MEMORY", "-I", upstream / "port/compat", "-I", directory,
                    *outputs, upstream / "port/hal/sdk_os.c", Path(__file__).with_name("sleep_alarm.c"),
                    "-Wl,--fatal-warnings", "-sSINGLE_FILE=1", "-sENVIRONMENT=node",
                    "-sWASM_ASYNC_COMPILATION=0", "-sEXIT_RUNTIME=1", "-o", program], check=True)
    subprocess.run([args.node, program], check=True)
print("Days sleep alarm registered, dispatched once and woke the original thread.")
