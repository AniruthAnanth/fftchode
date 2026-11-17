#!/usr/bin/env python3
"""Run a fftsuite-style binary one case at a time with a wall-clock limit per case.

  case_timeout.py BIN [fftsuite options...]        env: CASE_TIMEOUT seconds per case (default 120), CASE_CWD directory to run in

Same command line and output lines as BIN itself; a case that exceeds the limit (or crashes) prints
  <name> <threads> FAIL timeout>120s      (time/mem/hash modes)      <name> FAIL timeout>120s   (err mode)
and the run continues.  Used for the aocl-app-* wrappers: AOCL-FFTW built with --enable-amd-app-opt searches for its plan in
every FFTW planner mode and needs seconds (small sizes) to hours (>= 64k points) for the first plan of a problem class.
That layer also imports and exports FFTW wisdom through the file "wis.dat" in the current directory (apiplan.c), so a leftover wis.dat makes later plans
near-instant and changes the plans used.  Default: every case runs in a fresh empty temporary directory, i.e. plan_s is the cold first-plan cost and no wisdom
carries over.  CASE_CWD=DIR runs all cases in DIR instead (keep the wisdom: generate it once with a long CASE_TIMEOUT, then time with the file in place).
"""
import os, subprocess, sys, tempfile

def main():
    if len(sys.argv) < 2: sys.exit("usage: case_timeout.py BIN [options]")
    binary, args = sys.argv[1], sys.argv[2:]
    limit = float(os.environ.get("CASE_TIMEOUT", "120"))
    suite = os.path.abspath(args[args.index("--suite") + 1]); args[args.index("--suite") + 1] = suite
    binary = os.path.abspath(binary)
    fixed = os.environ.get("CASE_CWD")
    threads = args[args.index("--threads") + 1] if "--threads" in args else "1"
    err = "--mode" in args and args[args.index("--mode") + 1] == "err"
    for line in open(suite):
        f = line.split()
        if not f or f[0].startswith("#"): continue
        name = f[0]
        try:
            with tempfile.TemporaryDirectory() as tmp:
                p = subprocess.run([binary] + args + ["--only", name], capture_output=True, text=True, timeout=limit, cwd=fixed or tmp)
            out = p.stdout
            if p.returncode != 0 and not out.strip():
                out = ("%s FAIL crash(exit %d)\n" % (name, p.returncode)) if err else ("%s %s FAIL crash(exit %d)\n" % (name, threads, p.returncode))
        except subprocess.TimeoutExpired:
            out = ("%s FAIL timeout>%gs\n" % (name, limit)) if err else ("%s %s FAIL timeout>%gs\n" % (name, threads, limit))
        sys.stdout.write(out); sys.stdout.flush()

if __name__ == "__main__":
    main()
