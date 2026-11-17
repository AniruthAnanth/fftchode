# fftchode: FFTW 3.3.10-compatible FFT library in one C++ file.
# Fidelity flags: scalar code, no FMA contraction (MATLAB R2026b's FFTW build flags)
# make predefines CXX (g++) and CC (cc), so ?= never applies: default to clang unless set
ifeq ($(origin CXX),default)
CXX      = clang++
endif
ifeq ($(origin CC),default)
CC       = clang
endif
CXXFLAGS ?= -O2 -ffp-contract=off
FFT_DEFS ?=
PY       ?= python3
BUILD    ?= build
FFTW_VERSION = 3.3.10
FFTW_SHA256  = 56c932549852cddcfafdab3820b0200c7742675be92179e59e6215b340e26467

.PHONY: all lib test test-upstream test-fast test-matlab clean

all: $(BUILD)/fftchode.o
lib: $(BUILD)/libfftchode.a

$(BUILD)/fftchode.o: src/fftchode.cpp src/fftchode.h
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 $(CXXFLAGS) $(FFT_DEFS) -pthread -c src/fftchode.cpp -o $@

$(BUILD)/libfftchode.a: $(BUILD)/fftchode.o
	ar rcs $@ $<

$(BUILD)/fftchode_dump: tests/fft_dump.cpp $(BUILD)/fftchode.o
	$(CXX) -std=c++17 $(CXXFLAGS) -Isrc tests/fft_dump.cpp $(BUILD)/fftchode.o -pthread -lm -o $@

$(BUILD)/fftchode_mlfft: tests/mlfft.cpp $(BUILD)/fftchode.o
	$(CXX) -std=c++17 $(CXXFLAGS) -Isrc tests/mlfft.cpp $(BUILD)/fftchode.o -pthread -lm -o $@

# Upstream FFTW 3.3.10 built from the fftw.org tarball with the same compiler and flags
$(BUILD)/upstream/stamp:
	mkdir -p $(BUILD)/upstream
	cd $(BUILD)/upstream && curl -sSLO https://www.fftw.org/fftw-$(FFTW_VERSION).tar.gz
	echo "$(FFTW_SHA256)  $(BUILD)/upstream/fftw-$(FFTW_VERSION).tar.gz" | sha256sum -c -
	cd $(BUILD)/upstream && tar xzf fftw-$(FFTW_VERSION).tar.gz
	for p in double single; do \
	  extra=; [ $$p = single ] && extra=--enable-single; \
	  mkdir -p $(BUILD)/upstream/build-$$p && cd $(BUILD)/upstream/build-$$p && \
	  ../fftw-$(FFTW_VERSION)/configure --prefix=$$PWD/../inst-$$p --disable-shared --enable-static \
	    --enable-threads --with-combined-threads --disable-fortran $$extra \
	    CC="$(CC)" CFLAGS="$(CXXFLAGS) -pipe -fno-common" > configure.log && \
	  $(MAKE) -j4 > make.log && $(MAKE) install > install.log && cd - > /dev/null || exit 1; \
	done
	touch $@

$(BUILD)/upstream_dump: tests/fft_dump.cpp $(BUILD)/upstream/stamp
	$(CXX) -std=c++17 $(CXXFLAGS) -I$(BUILD)/upstream/inst-double/include tests/fft_dump.cpp \
	  $(BUILD)/upstream/inst-double/lib/libfftw3.a $(BUILD)/upstream/inst-single/lib/libfftw3f.a -pthread -lm -o $@

# Baseline mode is bit-for-bit FFTW 3.3.10: every output hash and every plan's wisdom must agree
test-upstream: $(BUILD)/fftchode_dump $(BUILD)/upstream_dump
	$(BUILD)/fftchode_dump > $(BUILD)/dump_fftchode.txt
	$(BUILD)/upstream_dump > $(BUILD)/dump_upstream.txt
	cmp $(BUILD)/dump_fftchode.txt $(BUILD)/dump_upstream.txt && echo "upstream identity: $$(wc -l < $(BUILD)/dump_fftchode.txt) cases bit-identical"

# Fast numerics mode: every transform must agree with baseline mode to within roundoff
$(BUILD)/fftchode_fast_check: tests/fast_check.cpp $(BUILD)/fftchode.o
	$(CXX) -std=c++17 $(CXXFLAGS) -Isrc tests/fast_check.cpp $(BUILD)/fftchode.o -pthread -lm -o $@

test-fast: $(BUILD)/fftchode_fast_check
	$(BUILD)/fftchode_fast_check

# Comparison with MATLAB ground truth (directory given by GT=..., never stored in this repo)
test-matlab: $(BUILD)/fftchode_mlfft
	@test -n "$(GT)" || (echo "usage: make test-matlab GT=<ground-truth dir>"; exit 2)
	$(PY) tests/matlab_compare.py $(GT) $(BUILD)/fftchode_mlfft --csv $(BUILD)/matlab_compare.csv

test: test-upstream test-fast

clean:
	rm -rf $(BUILD)
