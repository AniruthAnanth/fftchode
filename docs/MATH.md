# The mathematics in src/fft.cpp

src/fft.cpp is FFTW 3.3.10 with its scalar codelets. This note describes what the code computes and how the planner, in \`FFTW_ESTIMATE\` mode, chooses an algorithm. Section names refer to the FFTW source directories that the file is built from.

## 1. Transforms

For a length-$n$ complex input $x$, the forward transform (sign $-1$) and the backward transform (sign $+1$) are

$$X_k = \sum_{j=0}^{n-1} x_j\,\omega_n^{\pm jk}, \qquad \omega_n = e^{-2\pi i/n}.$$

Neither direction is normalized, so a forward transform followed by a backward one multiplies the data by $n$. Multi-dimensional transforms apply the 1-D transform along every axis. A "vector" (batched) transform applies the same transform to many arrays that sit at fixed strides from one another.

FFTW describes a problem as two tensors of $(n, \text{is}, \text{os})$ triples. One tensor gives the transform dimensions and the other gives the loop (vector) dimensions. Problem kinds:

- **dft**: complex to complex.
- **rdft2**: real to complex (r2c) and complex to real (c2r). The complex side is stored as the $\lfloor n/2\rfloor+1$ non-redundant outputs.
- **rdft**: real to real. This includes the halfcomplex R2HC/HC2R transforms, the DHT, and the DCT/DST (REDFT/RODFT) types I–IV.

## 2. Codelets (dft/scalar/codelets, rdft/scalar/r2c*)

Codelets are straight-line functions that FFTW's generator (genfft) produced for small $n$. The generator simplifies the DFT expression algebraically: it removes multiplications by $\pm1$ and $\pm i$, shares common subexpressions, and uses the network transposition principle to cut operations. The constants are literal decimal expansions of $\cos$ and $\sin$ at rational multiples of $\pi$, so codelet results do not depend on the platform's libm.

- **n1_k**: a complete DFT of size $k$. FFTW 3.3.10 has $k \in \{2..16, 20, 25, 32, 64\}$.
- **t1_r / t2_r / q1_r**: a radix-$r$ Cooley–Tukey butterfly with twiddle factors. t1 reads a full table of $r-1$ twiddles per column. t2 reads a few twiddles and builds the rest by complex multiplication. q1 handles the square in-place case.
- **r2cf_k / r2cb_k, r2cfII_k / r2cbIII_k**: real-input and real-output DFTs. The II/III forms carry the half-sample shift that appears in the real Cooley–Tukey step.
- **hf / hb / hc2cf / hc2cb (+ dft variants)**: twiddle codelets for the real-data Cooley–Tukey steps.
- **e01_8 / e10_8**: size-8 DCT-II/III kernels.

## 3. Cooley–Tukey (dft/ct.c, dftw-*.c)

For $n = r m$, write $j = j_1 + m j_2$ and $k = k_2 + r k_1$. Then

$$X_{k_2 + r k_1} = \sum_{j_1=0}^{m-1} \Big[\omega_n^{j_1 k_2} \sum_{j_2=0}^{r-1} x_{j_1+m j_2}\,\omega_r^{j_2 k_2}\Big]\,\omega_m^{j_1 k_1}.$$

In this factorization, $r$ size-$m$ sub-transforms are combined by $m$ radix-$r$ butterflies with twiddles $\omega_n^{j_1 k_2}$.

- **Decimation in time (DIT)** runs the child transforms first and then the twiddle codelet.
- **Decimation in frequency (DIF)** runs the twiddle codelet first.

The radix $r$ is always the size of a twiddle codelet, and the child problem is planned recursively. Variants:

- **dftw-direct**: applies the codelet in place over the $m$ columns.
- **dftw-directsq**: the in-place square case.
- **dftw-generic**: an explicit twiddle multiply for a radix with no codelet.
- **buffered**: copies through a contiguous buffer.

### Twiddle factors (kernel/trig.c, kernel/twiddle.c)

Twiddles are computed at plan time. The exponent $m/n$ is first reduced to the octant $[0, 1/8]$ using the symmetries of $\sin$ and $\cos$. Then $\theta = 2\pi m/n$ and $\cos\theta$, $\sin\theta$ are evaluated in double precision, even for the single-precision library. For large tables, FFTW uses a two-level product $\omega^{a+b\sqrt n} = \omega^a\,\omega^{b\sqrt n}$.

These $\cos$/$\sin$ calls are the only point where the result depends on the C math library.

## 4. Prime sizes

- **dft-generic** computes an $O(n^2)$ direct sum with precomputed twiddles. It applies to odd primes $n$ with $16 < n < 173$ (GENERIC_MAX_SLOW, GENERIC_MIN_BAD).
- **Rader (dft/rader.c)**: for prime $p$ with a generator $g$ of $(\mathbb Z/p)^\times$, re-index $j = g^{a}$ and $k = g^{-b}$. Then
  $$X_{g^{-b}} - x_0 = \sum_{a=0}^{p-2} x_{g^a}\,\omega_p^{g^{a-b}},$$
  which is a cyclic convolution of length $p-1$. FFTW evaluates it with a forward DFT of length $p-1$, a pointwise multiply by the precomputed DFT of $\omega_p^{g^{-b}}$, and a backward DFT. In ESTIMATE mode Rader is allowed only for $p > 32$ and only when $p-1$ factors into 2, 3 and 5.
- **Bluestein (dft/bluestein.c)**: uses $jk = \tfrac12\big(j^2 + k^2 - (k-j)^2\big)$ to turn the DFT into a chirp convolution,
  $$X_k = \omega_n^{k^2/2} \sum_j \big(x_j\,\omega_n^{j^2/2}\big)\,\omega_n^{-(k-j)^2/2},$$
  which is zero-padded to the smallest length $\ge 2n-1$ that factors into 2, 3 and 5. It handles the primes that Rader leaves out.

## 5. Real data (rdft/)

- **r2c through rdft2**:
  - Small sizes go directly to the r2cf codelets.
  - Composite sizes use a real Cooley–Tukey step in **hc2c** form (ct-hc2c), with hc2cf/hc2cb twiddle codelets. These codelets combine the halfcomplex outputs of the real sub-transforms into complex outputs.
  - Alternatively, **rdft2-rdft** computes an R2HC halfcomplex transform and copies it out.
- **Halfcomplex storage** stores $r_0, r_1, \dots, r_{n/2}, i_{(n+1)/2-1}, \dots, i_1$. It needs no extra memory because $X_{n-k} = \overline{X_k}$.
- **Real Cooley–Tukey (hc2hc)**: the real counterpart of §3. It uses hf/hb codelets, and the "II" child transform takes care of the half-sample shift.
- **Real prime sizes** go through the discrete Hartley transform $H_k = \sum_j x_j\,\mathrm{cas}(2\pi jk/n)$ (rdft-dht), which is in turn computed by Rader on the DHT (dht-rader). The halfcomplex output follows from $\mathrm{Re}\,X_k = (H_k + H_{n-k})/2$ and $\mathrm{Im}\,X_k = (H_{n-k} - H_k)/2$.
- **DCT/DST (reodft/)**: types II/III (reodft010e-r2hc) are computed by an R2HC of the same length with $O(n)$ pre- and post-twiddles. Type IV uses an odd-length R2HC or a radix-2 step. Types I go through a padded R2HC or a split-radix recursion.

## 6. Multi-dimensional and batched problems

- **rank-geq2** splits a rank-$k$ problem into two problems of lower rank. One is applied along the leading dimensions and the other along the trailing dimensions, with each one's dimensions moved into the other's vector loop. This is the row–column algorithm.
- **vrank-geq1** loops a vector problem over one vector dimension.
- **indirect / buffered / transpose** solvers deal with strides and in-place layouts.
- **rdft2 rank≥2 (rank-geq2-rdft2)** runs real-to-complex along the last (contiguous) dimension and complex DFTs along the others.

## 7. ESTIMATE-mode planning (kernel/planner.c, api/mapflags.c)

\`FFTW_ESTIMATE\` turns on a fixed set of impatience flags: NO_SLOW, NO_UGLY, NO_VRECURSE, NO_RANK_SPLITS, NO_VRANK_SPLITS, NO_NONTHREADED, NO_DFT_R2HC, NO_FIXED_RADIX_LARGE_N, BELIEVE_PCOST and NO_INDIRECT_OP. Pruning is allowed.

For each problem the planner tries every registered solver of that problem kind, in registration order. Each solver recursively plans the child problems it creates. A plan's cost is its operation count,

$$\text{cost} = \text{adds} + \text{muls} + 2\cdot\text{fmas} + \text{other},$$

summed over the plan tree. The planner keeps the plan with the strictly lowest cost, so the earlier solver wins a tie. It stores the winner in a hash table keyed by an MD5 of the problem and the flags; this is the "wisdom", and it is reused for identical sub-problems. Nothing is timed, so the plan, and with it the exact sequence of floating-point operations, depends only on these inputs:

- the problem (sizes, strides, in-place or not, alignment);
- the flags;
- the thread count.

## 8. Threads (threads/)

With $T > 1$ threads, FFTW registers three extra solvers:

- **dft-thr-vrank≥1** and **rdft-thr-vrank≥1** split the outermost vector loop into $T$ blocks of $\lceil v/T \rceil$ iterations.
- The threaded Cooley–Tukey solver splits the $m$ butterfly columns into blocks of $\lceil m/T\rceil$.

The blocks run on a pthread pool through \`spawn_loop\`. ESTIMATE mode sets NO_NONTHREADED, so when $T>1$ the planner considers threaded solvers wherever they apply. That changes the plan tree, and so the rounding, which is why the output bits depend on the thread count used at planning time. The arithmetic inside each block is the same as in the serial code.

## 9. Floating point

Codelets and solvers use only IEEE additions, subtractions and multiplications, in the order the generated code writes them. The fidelity build uses \`-ffp-contract=off\`, which prevents the compiler from fusing a multiply and an add into an FMA. With that flag set, and with the same twiddle values, any IEEE-754 target produces the same bits for the same plan.
