#!/usr/bin/env bash
# Verify every comparator binary built by comparators.sh against the baseline harness on a case file.
#
#   bench/suite/verify-comparators.sh <outdir> [suite=bench/suite/frozen.txt] [threads="1 4"]
#
# 1. harness equivalence: bin/fftchode (fftsuite.cpp + this repo's fft.o) and bin/fftchode-harness (adapters/harness.h + adapters/fftw.cpp on the
#    same fft.o) must print identical --mode hash lines (same inputs, geometry, in/out-of-place and c2r input construction).
# 2. accuracy: every bin/*-err (relative rms and max error against long-double upstream FFTW, fftsuite -DFFTSUITE_REF semantics) at each thread
#    count; thresholds 1e-12 (double) and 1e-5 (single) relative rms.  Unsupported cases ("FAIL <reason>") are listed, not counted as errors.
# Writes <outdir>/verify/<id>-t<N>.err and <outdir>/verify/summary.txt (also printed).  Exit status 1 if any case exceeds a threshold.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(cd "$1" && pwd); SUITE=$(realpath "${2:-$HERE/frozen.txt}"); THREADS=${3:-"1 4"}
V=$OUT/verify; mkdir -p "$V"
"$OUT/bin/fftchode" --suite "$SUITE" --threads 1 --mode hash > "$V/hash-fftsuite.txt"
"$OUT/bin/fftchode-harness" --suite "$SUITE" --threads 1 --mode hash > "$V/hash-harness.txt"
if diff -q "$V/hash-fftsuite.txt" "$V/hash-harness.txt" >/dev/null; then echo "harness equivalence: OK ($(wc -l < "$V/hash-harness.txt") hash lines identical)"
else echo "harness equivalence: MISMATCH"; diff "$V/hash-fftsuite.txt" "$V/hash-harness.txt" | head; exit 1; fi
for b in "$OUT"/bin/*-err; do
  id=$(basename "$b" -err)
  for t in $THREADS; do
    "$b" --suite "$SUITE" --threads "$t" --mode err > "$V/$id-t$t.err" 2> "$V/$id-t$t.stderr" || echo "WARNING: $id threads=$t exited non-zero (see $V/$id-t$t.stderr)"
  done
done
python3 -I - "$V" "$SUITE" <<'EOF' | tee "$V/summary.txt"
import sys, glob, os, collections
V, suite = sys.argv[1], sys.argv[2]
cases = [l.split()[0] for l in open(suite) if l.strip() and not l.startswith("#")]
TH = {"d": 1e-12, "s": 1e-5}
rows = []; bad = 0
base = {}
for f in sorted(glob.glob(os.path.join(V, "*.err"))):
    name = os.path.basename(f)[:-4]; id_, t = name.rsplit("-t", 1)
    ok = {}; fail = collections.Counter(); over = []; missing = set(cases)
    for line in open(f):
        p = line.split()
        if len(p) < 2: continue
        c = p[0]; missing.discard(c)
        if p[1] == "FAIL": fail[" ".join(p[2:])] += 1; continue
        ok[c] = (float(p[1]), float(p[2]))
        if not (float(p[1]) <= TH[c[0]]): over.append((c, float(p[1])))
    if id_ == "fftchode": base[t] = ok
    rows.append((id_, t, ok, fail, over, missing))
print("%-18s %3s %5s %5s | worst rel_rms d (case) | worst rel_rms s (case) | over threshold" % ("binary", "thr", "ok", "n/a"))
for id_, t, ok, fail, over, missing in rows:
    def worst(pre):
        xs = [(v[0], c) for c, v in ok.items() if c.startswith(pre)]
        return max(xs) if xs else (float("nan"), "-")
    wd, ws = worst("d_"), worst("s_")
    print("%-18s %3s %5d %5d | %.2e (%s) | %.2e (%s) | %d%s" % (id_, t, len(ok), sum(fail.values()), wd[0], wd[1], ws[0], ws[1], len(over), (" MISSING %d" % len(missing)) if missing else ""))
    if over:
        bad += len(over)
        for c, v in over[:12]: print("    OVER  %s %.3e" % (c, v))
    br = base.get(t, {})
    worse = [(c, ok[c][0] / max(br[c][0], 1e-30)) for c in ok if c in br and br[c][0] > 0 and ok[c][0] > 30 * br[c][0] and ok[c][0] > (1e-14 if c[0] == "d" else 1e-6)]
    if id_ != "fftchode" and worse:
        print("    >30x worse than baseline fft.o: " + ", ".join("%s(%.0fx)" % w for w in worse[:8]))
    for r, n in fail.most_common(): print("    n/a x%d: %s" % (n, r))
print("cases over threshold: %d" % bad)
sys.exit(1 if bad else 0)
EOF
