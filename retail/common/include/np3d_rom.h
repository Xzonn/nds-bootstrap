#ifndef NP3D_ROM_H
#define NP3D_ROM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#if (defined(B4DS) && (defined(ARM9) || defined(BUILDFATTABLE))) || (defined(ARM7) && defined(TWOCARD) && !defined(B4DS))
#define NP3D_VIRTUAL_FAT
#endif

#define NP3D_VIRTUAL_CLUSTER 0x80000000u
#define NP3D_LAUNCH_MAGIC 0x4c33504eu
#define NP3D_MAP_SOURCE 3

typedef struct {
    uint32_t magic, version;
    uint32_t mapCluster, dataCluster, prefixCluster;
    uint32_t mapSize, dataSize, romSize, cardSize;
    uint32_t cardControl, cardCode, cardId;
    char cachePath[256];
} Np3dLaunch;

typedef struct {
    uint32_t start, length, kind, source;
} Np3dExtent;

typedef bool (*Np3dReadSource)(void*, uint32_t, uint32_t, void*, uint32_t);

typedef struct {
    Np3dLaunch launch;
    Np3dReadSource read;
    void* context;
    uint32_t count, first, cached, last;
    uint32_t stride, indexCount, starts[64];
    Np3dExtent entries[32];
} Np3dRom;

typedef struct {
    uint32_t offset, length;
    uint8_t* output;
} Np3dRead;

#ifdef __cplusplus
extern "C" {
#endif
bool np3dRomInit(Np3dRom* rom, const Np3dLaunch* launch, Np3dReadSource read, void* context);
bool np3dRomRead(Np3dRom* rom, uint32_t offset, void* output, uint32_t length);
bool np3dRomReadStep(Np3dRom* rom, Np3dRead* request);
#ifdef __cplusplus
}
#endif

#endif
