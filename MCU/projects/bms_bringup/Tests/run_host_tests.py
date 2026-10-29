"""运行PC端逻辑回归；只在仓库build/host下生成文件，不访问开发板。"""
import argparse
from pathlib import Path
import subprocess

PROJECT = Path(__file__).resolve().parents[1]
ROOT = PROJECT.parents[2]
PURE = sorted((PROJECT / "Components/Bms/Src").glob("*.c"))
INCLUDES = [
    "Tests/host/stubs", "Tests/host", "App/Inc", "Bsp/Inc", "Devices/Inc",
    "Components/Bms/Inc", "Components/Sensing/Inc", "Components/Protocol/Inc",
    "Core/Inc",
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="gcc")
    args = parser.parse_args()
    output = ROOT / "build/host"
    output.mkdir(parents=True, exist_ok=True)
    groups = [(test, PURE) for test in sorted((PROJECT / "Core/tests").glob("test_*.c"))]
    extra = {
        "test_sensing": ["Components/Sensing/Src/ntc.c", "Components/Sensing/Src/sample_filter.c"],
        "test_protocol": ["Components/Protocol/Src/bms_protocol.c"],
        "test_ipc_comm": ["App/Src/bms_ipc.c", "App/Src/bms_comm.c", "App/Src/bms_config.c",
                          "Components/Protocol/Src/bms_protocol.c", "Tests/host/mock_queue.c"],
        "test_sampling": ["App/Src/bms_sampling.c", "App/Src/bms_config.c", "Devices/Src/ina226.c",
                          "Components/Sensing/Src/ntc.c", "Components/Sensing/Src/sample_filter.c"],
        "test_board": ["Bsp/Src/bsp_analog.c", "Bsp/Src/bsp_output.c", "Bsp/Src/bsp_transport.c"],
    }
    groups += [(PROJECT / f"Tests/host/{name}.c", [PROJECT / f for f in files])
               for name, files in extra.items()]
    for test, sources in groups:
        exe = output / (test.stem + ".exe")
        command = [args.compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-g"]
        command += ["-I" + str(PROJECT / path) for path in INCLUDES]
        command += [str(test), *map(str, sources), "-lm", "-o", str(exe)]
        subprocess.run(command, check=True)
        print(test.stem + ": ", end="", flush=True)
        subprocess.run([str(exe)], check=True)
    print(f"PASS: {len(groups)} host test groups. Hardware/real RTOS scheduling are not simulated.")


if __name__ == "__main__":
    main()
