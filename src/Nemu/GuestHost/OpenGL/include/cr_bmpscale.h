#ifndef ___cr_bmpscale_h__
#define ___cr_bmpscale_h__

#include <iprt/types.h>
#include <iprt/cdefs.h>


RT_C_DECLS_BEGIN

#ifndef IN_RING0
# define NEMUBMPSCALEDECL(_type) DECLEXPORT(_type)
#else
# define NEMUBLITTERDECL(_type) RTDECL(_type)
#endif

NEMUBMPSCALEDECL(void) CrBmpScale32 (uint8_t *dst,
                        int iDstDeltaLine,
                        int dstW, int dstH,
                        const uint8_t *src,
                        int iSrcDeltaLine,
                        int srcW, int srcH);

RT_C_DECLS_END

#endif /* #ifndef ___cr_bmpscale_h__ */
