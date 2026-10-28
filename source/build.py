#!/usr/bin/env python3

import glob, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.abspath(os.path.join(HERE, "..", "tools"))
SDK = os.path.join(TOOLS, "GoldHEN_Plugins_SDK")
REPO = os.path.join(TOOLS, "GoldHEN_Plugins_Repository")
ORBIS = os.path.join(TOOLS, "orbis-sdk")
CC = os.path.join(ORBIS, "host_tools", "bin", "orbis-clang.exe")
sys.path.insert(0, os.path.join(TOOLS, "ps4sdk"))
import ps4sdk, sce_stub

COMPAT = os.path.join(TOOLS, "ps4sdk", "sony_compat")
EXTRA_STUBS = {
    "libkernel_stub_weak.a": ["sceKernelGetFsSandboxRandomWord"],
    "libScePad_stub_weak.a": ["scePadReadExt", "scePadReadStateExt"],
}
STUBS = ["-lScePad_stub_weak"]

def run(cmd, cwd=None):
    env = dict(os.environ, SCE_ORBIS_SDK_DIR=ORBIS)
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd, env=env)
    if r.returncode or r.stderr.strip():
        print(" ".join(f'"{c}"' if " " in c else c for c in cmd[:3]), "...")
        print(r.stdout + r.stderr)
    if r.returncode: sys.exit(f"FAILED ({r.returncode})")

def plugin(d):
    d = os.path.abspath(d); name = os.path.basename(d.rstrip("\\/"))
    out = os.path.join(d, "build"); os.makedirs(out, exist_ok=True)
    for o in glob.glob(os.path.join(out, "*.o")): os.remove(o)
    gv = os.path.join(REPO, "common", "git_ver.h")
    if not os.path.exists(gv):
        open(gv, "w").write('#pragma once\n#define GIT_COMMIT "local"\n#define GIT_VER "local"\n#define GIT_NUM 0\n#define BUILD_DATE __DATE__\n')
    inc = ["-I", COMPAT, "-I", os.path.join(SDK, "include"), "-I", os.path.join(REPO, "common"), "-I", os.path.join(d, "include")]
    flags = ["-O2", "-fvisibility=hidden", "-Wno-c99-designator", "-D__FINAL__=1", *inc]
    objs = []
    srcs = glob.glob(os.path.join(SDK, "source", "*.c")) + glob.glob(os.path.join(REPO, "common", "*.c")) + glob.glob(os.path.join(d, "source", "*.c"))
    for src in srcs:
        o = os.path.basename(src) + ".o"
        run([CC, *flags, f"-fmacro-prefix-map={os.path.dirname(src)}{os.sep}=", "-c", src, "-o", o], cwd=out); objs.append(o)
    extra = sce_stub.build_extras(os.path.join(ORBIS, "target", "lib"), os.path.join(out, "stubs"), EXTRA_STUBS)
    sony = os.path.join(out, "sony"); os.makedirs(sony, exist_ok=True)
    run([CC, *[os.path.join("..", o) for o in objs], "-o", name + ".prx", "-Wl,--oformat=prx", "-L", extra, *STUBS], cwd=sony)
    prx = os.path.join(out, name + ".prx")
    elf = ps4sdk.scrub_original_filename(open(os.path.join(sony, name + ".prx"), "rb").read())
    open(os.path.join(out, name + ".oelf"), "wb").write(elf)
    open(prx, "wb").write(ps4sdk.make_fself(elf))
    print("built", prx, os.path.getsize(prx), "bytes")

if __name__ == "__main__":
    {"plugin": lambda: plugin(sys.argv[2])}[sys.argv[1]]()
