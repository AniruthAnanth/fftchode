# Sourced by build.sh: libbench, benchees, per-library archives and benchmark binaries.
# Binary naming: $B/bin/<lib>-<d|s> (speed), <lib>-<d|s>-mem (heap-tracking), plus null-<d|s>.
LB=$DL/benchfft/libbench
FFTW_INC_PP=$REPO/src
CFLAGS_B="$OPT_C -I$HERE/src -I$LB"
LIBS_ALL="fftcpp fftw-scalar fftw-simd pocketfft kissfft ffte"
MEMWRAP="-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=posix_memalign,--wrap=aligned_alloc,--wrap=memalign"
LIBBENCH_EXCLUDE="main.c bench-main.c speed.c report.c timer.c info.c can-do.c useropt.c"

build_libbench() { # <d|s>
  local prec=$1 flag="" d=$B/obj/libbench-$1 f
  [ "$prec" = s ] && flag="-DBENCHFFT_SINGLE"
  rm -rf "$d"; mkdir -p "$d"
  for f in "$LB"/*.c; do
    case " $LIBBENCH_EXCLUDE " in *" $(basename "$f") "*) continue;; esac
    $CC $CFLAGS_B $flag -w -c "$f" -o "$d/$(basename "$f" .c).o"
  done
  rm -f "$B/lib/libbench-$prec.a"; ar rcs "$B/lib/libbench-$prec.a" "$d"/*.o
}

build_driver() { # <d|s>, cheap, redone on every build
  local flag="" d=$B/obj/libbench-$1
  [ "$1" = s ] && flag="-DBENCHFFT_SINGLE"
  $CC $CFLAGS_B $flag -c "$HERE/src/driver.c" -o "$d/driver.o"
  $CC $CFLAGS_B -c "$HERE/src/memtrack.c" -o "$d/memtrack.o"
  $CC $CFLAGS_B -c "$HERE/src/memtrack_stub.c" -o "$d/memtrack_stub.o"
}

build_kiss() { # <d|s>
  local prec=$1 def=-Dkiss_fft_scalar=float d=$B/obj/kissfft-$1
  [ "$prec" = d ] && def="-Dkiss_fft_scalar=double -DKISS_DOUBLE"
  rm -rf "$d"; mkdir -p "$d"
  for f in kiss_fft kiss_fftr; do $CC $OPT_C $def -I"$DL/kissfft" -c "$DL/kissfft/$f.c" -o "$d/$f.o"; done
  rm -f "$B/lib/libkissfft-$prec.a"; ar rcs "$B/lib/libkissfft-$prec.a" "$d"/*.o
}

build_ffte_lib() {
  local d=$B/obj/ffte src=$DL/ffte-$FFTE_VERSION f
  rm -rf "$d"; mkdir -p "$d"
  sed 's,^C\(.*\)$,/* \1 */,;s,[Pp][Aa][Rr][Aa][Mm][Ee][Tt][Ee][Rr] *(\([^)]*\)),static const int \1;,' "$src/param.h" > "$B/cparam.h"
  sed 's/\* ((p->rank == 1) + FFTE_VECTOR)/* (2 * (p->rank == 1) + FFTE_VECTOR)/' "$DL/benchfft/benchees/ffte/doit-ffte.c" > "$B/doit-ffte.c"
  grep -q '2 \* (p->rank == 1)' "$B/doit-ffte.c" || { echo "work-array patch did not apply" >&2; exit 1; }
  for f in factor fft235 kernel zfft1d; do
    $FC $OPT_F -std=legacy -fallow-argument-mismatch -I"$src" -c "$src/$f.f" -o "$d/$f.o"
  done
  rm -f "$B/lib/libffte.a"; ar rcs "$B/lib/libffte.a" "$d"/*.o
}

# Compile a benchee object. compile_benchee <lib> <d|s>  -> $B/obj/bn-<lib>-<prec>.o
compile_benchee() {
  local lib=$1 prec=$2 flag="" o=$B/obj/bn-$1-$2.o
  [ "$prec" = s ] && flag="-DBENCHFFT_SINGLE"
  case $lib in
    fftcpp)       $CC $CFLAGS_B $flag -I"$REPO/src" -c "$HERE/benchees/fftw3.c" -o "$o";;
    fftw-scalar)  $CC $CFLAGS_B $flag -I"$B/fftw-scalar-$prec/inst/include" -c "$HERE/benchees/fftw3.c" -o "$o";;
    fftw-simd)    $CC $CFLAGS_B $flag -I"$B/fftw-simd-$prec/inst/include" -c "$HERE/benchees/fftw3.c" -o "$o";;
    pocketfft)    $CXX -std=c++17 $OPT_C $flag -DPOCKETFFT_CACHE_SIZE=16 -I"$HERE/src" -I"$LB" -I"$DL/pocketfft" -pthread -c "$HERE/benchees/pocketfft.cc" -o "$o";;
    kissfft)      local def=-Dkiss_fft_scalar=float; [ "$prec" = d ] && def="-Dkiss_fft_scalar=double -DKISS_DOUBLE"
                  $CC $CFLAGS_B $flag $def -I"$DL/kissfft" -c "$HERE/benchees/kissfft.c" -o "$o";;
    ffte)         $CC $CFLAGS_B $flag -I"$B" -c "$HERE/benchees/ffte.c" -o "$o"
                  $CC $OPT_C -c "$HERE/benchees/ffte_stubs.c" -o "$B/obj/ffte_stubs.o";;
    null)         $CC $CFLAGS_B $flag -c "$HERE/benchees/null.c" -o "$o";;
  esac
}

# Link a benchmark binary. link_bin <lib> <d|s>
link_bin() {
  local lib=$1 prec=$2 o=$B/obj/bn-$1-$2.o ld d=$B/obj/libbench-$2 libs="" sfx=""
  ld=$CC
  if [ "$prec" = s ]; then sfx=f; fi
  case $lib in
    fftcpp)       libs="$REPO/build/fft.o -lstdc++ -lm -pthread"; ld=$CXX;;
    fftw-scalar)  libs="$B/fftw-scalar-$prec/inst/lib/libfftw3$sfx.a -lm -pthread";;
    fftw-simd)    libs="$B/fftw-simd-$prec/inst/lib/libfftw3$sfx.a -lm -pthread";;
    pocketfft)    libs="-lm -pthread"; ld=$CXX;;
    kissfft)      libs="$B/lib/libkissfft-$prec.a -lm";;
    ffte)         libs="$B/obj/ffte_stubs.o $B/lib/libffte.a -lgfortran -lquadmath -lm"; ld=$FC;;
    null)         libs="-lm";;
  esac
  $ld -static -o "$B/bin/$lib-$prec" "$d/driver.o" "$d/memtrack_stub.o" "$o" "$B/lib/libbench-$prec.a" $libs
  $ld -static $MEMWRAP -o "$B/bin/$lib-$prec-mem" "$d/driver.o" "$d/memtrack.o" "$o" "$B/lib/libbench-$prec.a" $libs
  strip -o "$B/bin/$lib-$prec.stripped" "$B/bin/$lib-$prec"
}

link_all() {
  local lib prec
  for prec in d s; do
    build_driver $prec
    for lib in null $LIBS_ALL; do
      [ "$lib" = ffte ] && { [ "$prec" = s ] || [ "$HAVE_FC" = 0 ]; } && continue
      compile_benchee $lib $prec
      link_bin $lib $prec
    done
  done
}

for p in d s; do step libbench-$p build_libbench $p; step kissfft-$p build_kiss $p; done
[ "$HAVE_FC" = 1 ] && step ffte-lib build_ffte_lib
# benchees and binaries are cheap; always relink so source edits are picked up
say "benchees + binaries"
link_all > "$LOG/link.log" 2>&1 &
wait $! || { tail -30 "$LOG/link.log" >&2; exit 1; }
say "done"
