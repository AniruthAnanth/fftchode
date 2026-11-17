#!/usr/bin/env bash
# Writes the machine/toolchain description used for a run. Usage: env.sh <build dir>
B=$1
HERE=$(cd "$(dirname "$0")/.." && pwd)
. "$HERE/versions.sh"
echo "date (UTC):   $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "uname:        $(uname -srmo)"
echo "cpu model:    $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
echo "cpu cores:    $(nproc) logical (threads/core, cores/socket, sockets: $(lscpu | grep -E '^(Socket|Core|Thread)' | tr -s ' ' | sed 's/(s)//;s/ per socket//;s/ per core//' | cut -d: -f2 | tr -d ' ' | tr '\n' ' '))"
echo "cpu flags:    $(grep -m1 '^flags' /proc/cpuinfo | tr ' ' '\n' | grep -E '^(sse2|avx|avx2|fma|avx512f)$' | sort -u | tr '\n' ' ')"
echo "cpu mhz:      $(grep -m1 'cpu MHz' /proc/cpuinfo | cut -d: -f2 | sed 's/^ //') (at time of listing; governor may vary)"
lscpu | grep -E 'L1d|L1i|L2|L3|Model name|Hypervisor' | sed 's/  */ /g' | sed 's/^/lscpu:        /'
echo "memory:       $(free -m | awk '/Mem:/{print $2" MiB"}')"
echo "glibc:        $(ldd --version | head -1)"
echo "C compiler:   $($CC --version | head -1)  [$CC $OPT_C]"
echo "C++ compiler: $($CXX --version | head -1)  [$CXX -std=c++17 $OPT_C]"
if command -v "$FC" >/dev/null 2>&1; then echo "Fortran:      $($FC --version | head -1)  [$FC $OPT_F -std=legacy -fallow-argument-mismatch]"; fi
echo "binutils:     $(ld --version | head -1)"
echo "linking:      all benchmark binaries are static (-static)"
echo "benchFFT:     $BENCHFFT_URL @ $BENCHFFT_COMMIT (libbench, benchees fftw3/ffte/kissfft)"
echo "pocketfft:    $POCKETFFT_URL branch $POCKETFFT_BRANCH @ $POCKETFFT_COMMIT"
echo "kissfft:      $KISSFFT_URL @ $KISSFFT_COMMIT"
echo "FFTW:         $FFTW_VERSION ($FFTW_URL, sha256 $FFTW_SHA256)"
echo "FFTE:         $FFTE_VERSION ($FFTE_URL, sha256 $FFTE_SHA256)"
echo "fft.cpp:      $(git -C "$HERE/.." rev-parse HEAD) sha256(src/fft.cpp)=$(sha256sum "$HERE/../src/fft.cpp" | cut -d' ' -f1)"
for fl in scalar simd; do for p in d s; do
  c=$B/fftw-$fl-$p/build/config.status
  [ -e "$c" ] && echo "FFTW $fl-$p configure: $(grep -m1 '^ac_cs_config=' "$c" | sed "s/^ac_cs_config=//")"
done; done
for lib in fftcpp fftw-scalar fftw-simd pocketfft kissfft ffte; do
  for p in d s; do [ -x "$B/bin/$lib-$p" ] && "$B/bin/$lib-$p" --lib $lib --mode info | sed 's/^/benchee:      /'; done
done
exit 0
