#ifndef NP3D_VFS_H
#define NP3D_VFS_H

#include <stdbool.h>
#include "np3d_rom.h"

#ifdef __cplusplus
extern "C" {
#endif
int np3dDeviceInit(void);
const Np3dLaunch* np3dLaunchInfo(void);
#if NP3D_LAUNCH_TRACE
void np3dBootLog(const char* format, ...);
uint32_t np3dPrepareLoaderLog(void);
#else
#define np3dBootLog(...) ((void)0)
#endif
#ifdef __cplusplus
}
#endif

#endif
