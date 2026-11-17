/* benchFFT's FFTE benchee also drives FFTE's 2-D and 3-D routines; this harness only runs
 * 1-D problems (can_do rejects rank > 1), so the references are satisfied by stubs and the
 * binary carries only the 1-D FFTE code. */
#include <stdlib.h>
void zfft2d_(void) { abort(); }
void zfft3d_(void) { abort(); }
