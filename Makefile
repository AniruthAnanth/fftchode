# Fidelity configuration: scalar code, no FMA contraction (matches MATLAB R2026b's FFTW build flags)
CXX      ?= clang++
CXXFLAGS ?= -O2 -ffp-contract=off
FFT_DEFS ?=
PY       ?= python3
BUILD    ?= build
FFTW_VERSION = 3.3.10
FFTW_SHA256  = 56c932549852cddcfafdab3820b0200c7742675be92179e59e6215b340e26467

.PHONY: all test test-upstream test-matlab clean

all: $(BUILD)/fft.o

$(BUILD)/fft.o: src/fft.cpp src/fftw3.h
	mkdir -p $(BUILD)
	$(CXX) -std=c++17 $(CXXFLAGS) $(FFT_DEFS) -pthread -c src/fft.cpp -o $@

$(BUILD)/libfft.a: $(BUILD)/fft.o
	ar rcs $@ $<

$(BUILD)/fft_dump: tests/fft_dump.cpp $(BUILD)/fft.o
	$(CXX) -std=c++17 $(CXXFLAGS) -Isrc tests/fft_dump.cpp $(BUILD)/fft.o -pthread -lm -o $@

$(BUILD)/mlfft: tests/mlfft.cpp $(BUILD)/fft.o
	$(CXX) -std=c++17 $(CXXFLAGS) -Isrc tests/mlfft.cpp $(BUILD)/fft.o -pthread -lm -o $@

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

$(BUILD)/fft_dump_upstream: tests/fft_dump.cpp $(BUILD)/upstream/stamp
	$(CXX) -std=c++17 $(CXXFLAGS) -I$(BUILD)/upstream/inst-double/include tests/fft_dump.cpp \
	  $(BUILD)/upstream/inst-double/lib/libfftw3.a $(BUILD)/upstream/inst-single/lib/libfftw3f.a -pthread -lm -o $@

# Bit-for-bit identity with upstream: every output hash and every plan's wisdom must agree
test-upstream: $(BUILD)/fft_dump $(BUILD)/fft_dump_upstream
	$(BUILD)/fft_dump > $(BUILD)/dump_fft.txt
	$(BUILD)/fft_dump_upstream > $(BUILD)/dump_upstream.txt
	cmp $(BUILD)/dump_fft.txt $(BUILD)/dump_upstream.txt && echo "upstream identity: $$(wc -l < $(BUILD)/dump_fft.txt) cases bit-identical"

# Comparison with MATLAB ground truth (directory given by GT=..., never stored in this repo)
test-matlab: $(BUILD)/mlfft
	@test -n "$(GT)" || (echo "usage: make test-matlab GT=<ground-truth dir>"; exit 2)
	$(PY) tests/matlab_compare.py $(GT) $(BUILD)/mlfft --csv $(BUILD)/matlab_compare.csv

test: test-upstream

clean:
	rm -rf $(BUILD)
