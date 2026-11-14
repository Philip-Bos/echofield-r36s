#!/usr/bin/env python3
"""Static checks on the R36S binary: architecture, interpreter, needed libraries and
the newest glibc symbol version it requires (ArkOS on the R36S ships glibc 2.30)."""
import re, subprocess, sys
p = sys.argv[1]
def run(*a): return subprocess.run(a, capture_output=True, text=True).stdout
hdr = run("llvm-readelf", "-h", "-l", "-d", p)
ok = True
def check(cond, msg):
    global ok
    print(("  ok   " if cond else "  FAIL ") + msg); ok &= cond
check("AArch64" in hdr, "machine AArch64")
check("ELF64" in hdr, "64-bit ELF")
check("/lib/ld-linux-aarch64.so.1" in hdr, "interpreter /lib/ld-linux-aarch64.so.1")
needed = re.findall(r"Shared library: \[(.+?)\]", hdr)
print("  needed:", ", ".join(needed))
check(set(needed) <= {"libc.so.6", "libm.so.6", "libSDL2-2.0.so.0"}, "only libc, libm and SDL2 are required (libasound is optional, dlopen'ed)")
vers = run("llvm-readelf", "-V", p)
gl = sorted(set(re.findall(r"GLIBC_([0-9.]+)", vers)), key=lambda v: [int(x) for x in v.split(".")])
print("  glibc versions referenced:", gl)
check(all([int(x) for x in v.split(".")] <= [2, 30] for v in gl), "no glibc symbol newer than 2.30")
dyn = run("llvm-nm", "-D", "--undefined-only", p)
print("  undefined dynamic symbols:", len(dyn.split("\n")) - 1)
sys.exit(0 if ok else 1)
