#if NP3D_LAUNCH_TRACE && defined(ARM7) && ((defined(TWOCARD) && !defined(B4DS)) || (defined(B4DS) && defined(BUILDFATTABLE)))
#pragma GCC optimize("Os")
#include "np3d_trace.h"
#include "my_fat.h"
#ifndef B4DS
#include "my_sdmmc.h"
#endif
#include "tonccpy.h"
#include "hex.h"
#include "nocashMessage.h"
#include <string.h>
#ifdef BUILDFATTABLE
#include "common.h"
#endif

static aFile traceFile;
static char record[NP3D_TRACE_RECORD] __attribute__((aligned(4)));
static char verified[NP3D_TRACE_RECORD] __attribute__((aligned(4)));
static u32 sequence;
static u32 failedSector, failedCount;
static int failedResult;
static bool failedWrite;
#ifdef BUILDFATTABLE
static u32 position = NP3D_TRACE_RECORD;
static bool ready;
static u32 pendingCount;
static char pending[8][NP3D_TRACE_RECORD] __attribute__((aligned(4)));
#endif

static void formatRecord(const char* message, u32 a, u32 b, u32 c)
{
    toncset(record, ' ', sizeof(record));
    tonccpy(record, tohex(++sequence), 8);
    size_t length = strlen(message);
    if (length > 64) length = 64;
    tonccpy(record + 9, message, length);
    tonccpy(record + 74, "a=", 2);
    tonccpy(record + 76, tohex(a), 8);
    tonccpy(record + 87, "b=", 2);
    tonccpy(record + 89, tohex(b), 8);
    tonccpy(record + 100, "c=", 2);
    tonccpy(record + 102, tohex(c), 8);
    record[sizeof(record) - 1] = '\n';
}

static void writeRecord(const char* data, u32 offset)
{
#ifndef B4DS
    int slot = sdmmc_get_ndma_slot();
    sdmmc_set_ndma_slot(4);
#endif
    if (fileWrite(data, &traceFile, offset, NP3D_TRACE_RECORD) == NP3D_TRACE_RECORD) {
        resetPrevSect(&traceFile);
        if (fileRead(verified, &traceFile, offset, sizeof(verified)) == sizeof(verified)
            && memcmp(data, verified, sizeof(verified)) == 0) {
#ifndef B4DS
            sdmmc_set_ndma_slot(slot);
#endif
            return;
        }
    }
#ifndef B4DS
    sdmmc_set_ndma_slot(slot);
#endif
    nocashMessage("NitroPatcher: ARM7 diagnostic write failed");
#ifdef BUILDFATTABLE
    arm9_stateFlag = ARM9_DISPERR;
#endif
    while (1);
}

void np3dTraceEvent(const char* message, u32 a, u32 b, u32 c)
{
#ifdef BUILDFATTABLE
    if (!traceFile.firstCluster) return;
    np3dTraceCheckSd(traceFile.firstCluster);
    formatRecord(message, a, b, c);
    if (!ready) {
        if (pendingCount >= sizeof(pending) / sizeof(pending[0])) {
            nocashMessage("NitroPatcher: early diagnostic buffer exhausted");
            while (1);
        }
        tonccpy(pending[pendingCount++], record, sizeof(record));
        return;
    }
    if (position >= NP3D_TRACE_BYTES - NP3D_TRACE_RECORD) {
        np3dTraceFailure(traceFile.firstCluster, "ERROR diagnostic file capacity exceeded", position, sequence, 0);
    }
    writeRecord(record, position);
    position += sizeof(record);
#endif
}

void np3dTraceSdError(u32 sector, u32 count, int result, bool writing)
{
    if (failedResult) return;
    failedSector = sector;
    failedCount = count;
    failedResult = result;
    failedWrite = writing;
}

void np3dTraceCheckSd(u32 cluster)
{
#ifdef BUILDFATTABLE
    if (!ready) return;
#endif
    if (failedResult) np3dTraceFailure(cluster, failedWrite ? "ERROR SD write (a=sector b=count c=result)"
        : "ERROR SD read (a=sector b=count c=result)", failedSector, failedCount, failedResult);
}

void np3dTraceFailure(u32 cluster, const char* message, u32 a, u32 b, u32 c)
{
#ifdef BUILDFATTABLE
    if (ready && traceFile.firstCluster) {
#else
    if (cluster) {
        getFileFromCluster(&traceFile, cluster, false);
#endif
        formatRecord(message, a, b, c);
        writeRecord(record, NP3D_TRACE_BYTES - NP3D_TRACE_RECORD);
    }
    nocashMessage(message);
#ifdef BUILDFATTABLE
    arm9_stateFlag = ARM9_DISPERR;
#endif
    while (1);
}

#ifdef BUILDFATTABLE
void np3dTraceBegin(u32 cluster)
{
#ifdef B4DS
    getFileFromCluster(&traceFile, cluster);
#else
    getFileFromCluster(&traceFile, cluster, false);
#endif
}

void np3dTraceReady(void)
{
    if (!traceFile.firstCluster) return;
    ready = true;
    for (u32 index = 0; index < pendingCount; index++) {
        writeRecord(pending[index], position);
        position += NP3D_TRACE_RECORD;
    }
    pendingCount = 0;
}
#endif
#endif
