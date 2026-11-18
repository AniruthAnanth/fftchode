// Force-included when KissFFT is compiled in single precision, so that its double and float builds can live in one executable
// (KissFFT's symbols are not precision-suffixed).  Applied to the library sources and to adapters/kissfft_plan.cpp.
#define kiss_fft_alloc kissf_fft_alloc
#define kiss_fft kissf_fft
#define kiss_fft_stride kissf_fft_stride
#define kiss_fft_cleanup kissf_fft_cleanup
#define kiss_fft_next_fast_size kissf_fft_next_fast_size
#define kiss_fftr_alloc kissf_fftr_alloc
#define kiss_fftr kissf_fftr
#define kiss_fftri kissf_fftri
#define kiss_fftnd_alloc kissf_fftnd_alloc
#define kiss_fftnd kissf_fftnd
#define kiss_fftndr_alloc kissf_fftndr_alloc
#define kiss_fftndr kissf_fftndr
#define kiss_fftndri kissf_fftndri
