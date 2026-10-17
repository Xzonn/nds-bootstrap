#ifndef NP3D_TRACE_H
#define NP3D_TRACE_H

#define NP3D_TRACE_BYTES 0x10000
#define NP3D_TRACE_RECORD 128

#if NP3D_LAUNCH_TRACE && defined(ARM7) && defined(TWOCARD) && !defined(B4DS)
#include <nds/ndstypes.h>

void np3dTraceEvent(const char* message, u32 a, u32 b, u32 c);
void np3dTraceSdError(u32 sector, u32 count, int result, bool writing);
void np3dTraceCheckSd(u32 cluster);
void np3dTraceFailure(u32 cluster, const char* message, u32 a, u32 b, u32 c) __attribute__((noreturn));
#ifdef BUILDFATTABLE
void np3dTraceBegin(u32 cluster);
void np3dTraceReady(void);
#endif
#else
#define np3dTraceEvent(...) ((void)0)
#define np3dTraceBegin(...) ((void)0)
#define np3dTraceReady(...) ((void)0)
#define np3dTraceSdError(...) ((void)0)
#define np3dTraceCheckSd(...) ((void)0)
#endif

#endif
