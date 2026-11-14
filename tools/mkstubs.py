#!/usr/bin/env python3
"""Generate link-time stub shared libraries for the aarch64 cross build.

There is no aarch64 sysroot in the build environment, so we link against tiny
stub .so files that carry the correct SONAMEs and (for glibc) the GLIBC_2.17
symbol version that every symbol in the aarch64 glibc baseline has. At run time
the real system libraries on the R36S are used. Symbols are discovered from the
undefined symbols of the compiled objects, so the stubs always match the code.
usage: mkstubs.py OUTDIR CLANG nm-output-file
"""
import os, subprocess, sys
outdir, clang, undef_file = sys.argv[1], sys.argv[2], sys.argv[3]
syms = sorted(set(l.strip() for l in open(undef_file) if l.strip()))
LIBM = set("""sinf cosf tanf expf logf log10f powf tanhf sqrtf floorf ceilf fabsf fmodf exp2f log2f roundf
sin cos exp log log10 pow sqrt floor ceil fabs fmod tan atan2f atan2 asinf acosf""".split())
groups = {"libc": [], "libm": [], "libSDL2": []}
for s in syms:
    if s.startswith("SDL_"): groups["libSDL2"].append(s)
    elif s in LIBM: groups["libm"].append(s)
    elif s in ("main", "_start") or s.startswith("_GLOBAL_OFFSET_TABLE_"): continue
    else: groups["libc"].append(s)
os.makedirs(outdir, exist_ok=True)
spec = {"libc": ("libc.so.6", "GLIBC_2.17"), "libm": ("libm.so.6", "GLIBC_2.17"),
        "libSDL2": ("libSDL2-2.0.so.0", None)}
for name, (soname, ver) in spec.items():
    src = os.path.join(outdir, name + "_stub.c")
    with open(src, "w") as f:
        for s in groups[name]:
            f.write("void %s(void){}\n" % s)
        if name == "libc":  # needed by start code
            if "__libc_start_main" not in groups[name]:
                f.write("void __libc_start_main(void){}\n")
    cmd = [clang, "--target=aarch64-linux-gnu", "-nostdlib", "-fno-builtin", "-w", "-shared", "-fPIC", "-fuse-ld=lld",
           "-Wl,-soname," + soname, "-o", os.path.join(outdir, name + ".so"), src]
    if ver:
        vs = os.path.join(outdir, name + ".map")
        with open(vs, "w") as f:
            f.write("%s { global: *; };\n" % ver)
        cmd.append("-Wl,--version-script=" + vs)
    subprocess.check_call(cmd)
    print("stub %-10s %3d symbols -> %s" % (name, len(groups[name]) + (1 if name=='libc' else 0), soname))
unknown = [s for s in groups["libc"] if s.startswith("_") and s not in ("__libc_start_main", "__errno_location")]
if unknown:
    print("WARNING: unexpected implicit symbols:", unknown)
