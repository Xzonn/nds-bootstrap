#ifndef NP3D_FAT_H
#define NP3D_FAT_H

#include "my_fat.h"
#include "np3d_rom.h"

typedef struct {
    Np3dRom rom;
    aFile mapFile, patchFile, prefixFile;
    u32 cardBuffer[128];
    u32 cardOffset;
#ifdef B4DS
    u32 fatBuffer[128];
    u32 sectorBuffer[128];
#endif
#if NP3D_LAUNCH_TRACE
    u32 traceCluster;
#endif
} Np3dFatState;

u32 np3dFileRead(char* output, aFile* file, u32 offset, u32 length);
bool np3dFileReadNonBlocking(char* output, aFile* file, u32 offset, u32 length);
bool np3dResumeFileRead(void);
bool np3dFileReadPending(void);
bool np3dOriginalCardCode(aFile* file, u32* code);
#ifdef BUILDFATTABLE
void np3dStoreRuntime(void* output);
#endif

#endif
