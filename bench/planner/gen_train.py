"""gen_train.py FROZEN EXISTING OUT: draw random training cases (excluding every frozen-suite and
EXISTING shape/batch/layout) for planner calibration; the frozen suite is never a training target."""
import random, math, sys
random.seed(20261117)
suite = set()
for l in open(sys.argv[1]):
    if l.startswith('#') or not l.strip(): continue
    f = l.split(); suite.add((f[2], f[3], f[4], f[5]))
train = set()
for l in open(sys.argv[2]):
    f = l.split(); train.add((f[2], f[3], f[4], f[5]))
import bisect
CANDS = sorted(set(2**a * 3**b * 5**c * 7**d for a in range(23) for b in range(14) for c in range(10) for d in range(8)))
def smooth(lo, hi):
    c = [n for n in CANDS if lo <= n <= hi]
    t = math.exp(random.uniform(math.log(lo), math.log(hi)))
    return c[min(bisect.bisect_left(c, t), len(c) - 1)]
def pow2(lo, hi): return 2 ** random.randint(int(math.log2(lo)), int(math.log2(hi)))
out = []
def add(prec, kind, shape, h, layout, place='oop'):
    key = (kind, shape, str(h), layout)
    if key in suite or key in train: return
    name = '%s_%s%s%s' % (prec, kind, shape, ('h%d' % h if layout == 'contig' and h > 1 else ('c%d' % h if layout == 'col' else '')))
    if place == 'ip': name += 'ip'
    if any(o.split()[0] == name for o in out): return
    out.append('%s %s %s %s %d %s %s' % (name, prec, kind, shape, h, layout, place))
for i in range(14): add(random.choice('ds'), 'cf', str(smooth(20, 3e6)), 1, 'contig')
for i in range(6): add(random.choice('ds'), 'cf', str(pow2(16, 4e6)), 1, 'contig')
for i in range(4): add('d', 'cb', str(smooth(20, 2e6)), 1, 'contig')
for i in range(3): add('d', 'cf', str(smooth(1000, 2e6)), 1, 'contig', 'ip')
for i in range(8): add(random.choice('ds'), 'rf', str(2 * smooth(10, 1e6)), 1, 'contig')
for i in range(4): add(random.choice('ds'), 'rb', str(2 * smooth(10, 1e6)), 1, 'contig')
for i in range(5):
    n = smooth(8, 2048); h = max(2, int(2e5 / n * random.uniform(0.3, 3))); add('d', random.choice(['cf', 'rf']), str(n if True else n), h, 'contig')
for i in range(5):
    n = smooth(64, 4096); h = max(2, int(5e5 / n * random.uniform(0.3, 2))); add(random.choice('ds'), random.choice(['cf', 'cf', 'rf']), str(n), h, 'col')
for i in range(6):
    a = smooth(16, 2048); b = smooth(16, 2048)
    if a * b > 4e6 or a * b < 1000: continue
    add(random.choice('ds'), random.choice(['cf', 'cf', 'rf', 'rb']), '%dx%d' % (a, b), 1, 'contig')
for i in range(4):
    a, b, c = smooth(8, 160), smooth(8, 160), smooth(8, 160)
    if a * b * c > 4e6: continue
    add(random.choice('ds'), random.choice(['cf', 'rf']), '%dx%dx%d' % (a, b, c), 1, 'contig')
for i in range(3):
    p = random.choice([10009, 30011, 65599, 250007, 524309, 3001, 7919]); add('d', random.choice(['cf', 'rf']), str(p), 1, 'contig')
open(sys.argv[3], 'w').write('\n'.join(out) + '\n')
print(len(out)); print('\n'.join(out))
