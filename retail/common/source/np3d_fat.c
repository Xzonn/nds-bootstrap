#include "np3d_rom.h"
#ifdef NP3D_VIRTUAL_FAT
#ifdef B4DS
#pragma GCC optimize("Oz")
#else
#pragma GCC optimize("Os")
#endif
#include "np3d_fat.h"
#include "np3d_rom.h"
#include "np3d_trace.h"
#include "tonccpy.h"
#include "nocashMessage.h"
#include <nds/card.h>
#include <nds/interrupts.h>
#include <nds/memory.h>
#include <nds/ipc.h>
#include <nds/bios.h>
#if defined(B4DS) && defined(ARM9)
extern vu32* volatile sharedAddr;
#endif

#ifdef BUILDFATTABLE
static Np3dFatState bootState;
#endif
static Np3dFatState* state;
#if !defined(B4DS) || defined(BUILDFATTABLE)
static u32 descriptorCluster;
#endif
#ifndef B4DS
static Np3dRead request;
static aFile* requestFile;
#endif
#if NP3D_LAUNCH_TRACE && defined(BUILDFATTABLE)
extern u32 np3dTraceCluster;
static u32 tracedSources;
static u32 readCount;
#endif
#if NP3D_LAUNCH_TRACE && (!defined(B4DS) || defined(BUILDFATTABLE))
#ifdef BUILDFATTABLE
#define TRACE_CLUSTER np3dTraceCluster
#else
#define TRACE_CLUSTER (state ? state->traceCluster : 0)
#endif
#endif

static void readFailure(const char* message, u32 a, u32 b, u32 c) __attribute__((noreturn));
static void readFailure(const char* message, u32 a, u32 b, u32 c)
{
#if NP3D_LAUNCH_TRACE && (!defined(B4DS) || defined(BUILDFATTABLE))
#ifdef BUILDFATTABLE
    np3dTraceFailure(np3dTraceCluster, message, a, b, c);
#else
    np3dTraceFailure(state ? state->traceCluster : 0, message, a, b, c);
#endif
#else
    nocashMessage(message);
    while (1);
#endif
}

static void readCardBlock(Np3dFatState* state, u32 offset)
{
#if defined(B4DS) && defined(ARM9)
    vu32* request = sharedAddr + 3;
    while (1) {
        *request = 0;
        IPC_SendSync(8);
        while (!*request);
        sysSetCardOwner(BUS_OWNER_ARM9);
        u16 spi = REG_AUXSPICNT;
        if (!(spi & CARD_SPI_BUSY)
            && (spi & (CARD_SPI_ENABLE | CARD_SPI_HOLD)) != (CARD_SPI_ENABLE | CARD_SPI_HOLD)) break;
        sysSetCardOwner(BUS_OWNER_ARM7);
        *request = 0;
        swiDelay(64);
    }
#endif
    u32 control = state->rom.launch.cardControl & ~CARD_BLK_SIZE(7);
    vu8* command = (vu8*)0x040001a8;
#if NP3D_LAUNCH_TRACE
    u32 polls = 0;
#endif
    while (REG_ROMCTRL & CARD_BUSY) {
#if NP3D_LAUNCH_TRACE
        if (++polls == 0x1000000) readFailure("ERROR card busy before B7", offset, REG_ROMCTRL, REG_AUXSPICNT);
#endif
    }
    u16 previous = REG_AUXSPICNT;
    REG_AUXSPICNT = CARD_CR1_ENABLE << 8;
    command[0] = CARD_CMD_DATA_READ;
    command[1] = offset >> 24;
    command[2] = offset >> 16;
    command[3] = offset >> 8;
    command[4] = offset;
    command[5] = command[6] = command[7] = 0;
    REG_ROMCTRL = control | CARD_ACTIVATE | CARD_nRESET | CARD_BLK_SIZE(1);
    u32 count = 0;
#if NP3D_LAUNCH_TRACE
    polls = 0;
#endif
    do {
#if NP3D_LAUNCH_TRACE
        if (++polls == 0x1000000) readFailure("ERROR card B7 transfer timeout", offset, REG_ROMCTRL, count);
#endif
        if (REG_ROMCTRL & CARD_DATA_READY) {
            u32 value = REG_CARD_DATA_RD;
            if (count < 128) state->cardBuffer[count++] = value;
        }
    } while (REG_ROMCTRL & CARD_BUSY);
    REG_AUXSPICNT = previous;
#if defined(ARM9)
    sysSetCardOwner(BUS_OWNER_ARM7);
    REG_IPC_SYNC &= ~0x0f00;
    *request = 0;
#endif
    if (count != 128) {
        readFailure("ERROR incomplete physical Game Card read", offset, count, REG_ROMCTRL);
    }
}

static bool readSource(void* context, u32 kind, u32 offset, void* output, u32 length)
{
#if defined(B4DS) && defined(ARM9)
    Np3dFatState* state = context;
#else
    (void)context;
#endif
#if NP3D_LAUNCH_TRACE && defined(BUILDFATTABLE)
    bool trace = !(tracedSources & BIT(kind));
    if (trace) {
        tracedSources |= BIT(kind);
        np3dTraceEvent("source read begin (a=kind b=offset c=length)", kind, offset, length);
        np3dTraceEvent("card registers before first source read", REG_ROMCTRL, REG_AUXSPICNT, REG_EXMEMSTAT);
    }
#endif
    if (kind == NP3D_MAP_SOURCE || kind == 1) {
        u32 received = fileRead(output, kind == 1 ? &state->patchFile : &state->mapFile, offset, length);
        np3dTraceCheckSd(TRACE_CLUSTER);
        if (received != length) readFailure("ERROR SD map/patch read", kind, offset, received);
#if NP3D_LAUNCH_TRACE && defined(BUILDFATTABLE)
        if (trace) np3dTraceEvent("source SD read complete", kind, offset, received);
#endif
        return true;
    }
    u8* destination = output;
    while (length) {
        u32 take;
        if (offset < 0x8000) {
            take = 0x8000 - offset;
            if (take > length) take = length;
            u32 received = fileRead((char*)destination, &state->prefixFile, offset, take);
            np3dTraceCheckSd(TRACE_CLUSTER);
            if (received != take) readFailure("ERROR SD card prefix read", offset, take, received);
#ifndef B4DS
        } else if (state->rom.launch.twlSize && offset >= state->rom.launch.twlStart
            && offset - state->rom.launch.twlStart < state->rom.launch.twlSize) {
            take = state->rom.launch.twlSize - (offset - state->rom.launch.twlStart);
            if (take > length) take = length;
            u32 received = fileRead((char*)destination, &state->prefixFile, 0x8000 + offset - state->rom.launch.twlStart, take);
            np3dTraceCheckSd(TRACE_CLUSTER);
            if (received != take) readFailure("ERROR SD TWL region read", offset, take, received);
#endif
        } else {
            take = 512 - (offset & 511);
            if (take > length) take = length;
            if (state->cardOffset != (offset & ~511u)) {
#ifdef BUILDFATTABLE
                if (state->cardOffset == UINT32_MAX) np3dTraceEvent("first physical B7 read begin", offset & ~511u, state->rom.launch.cardControl, REG_EXMEMSTAT);
#endif
                readCardBlock(state, offset & ~511u);
#ifdef BUILDFATTABLE
                if (state->cardOffset == UINT32_MAX) np3dTraceEvent("first physical B7 read complete", offset & ~511u, REG_ROMCTRL, REG_AUXSPICNT);
#endif
                state->cardOffset = offset & ~511u;
            }
            tonccpy(destination, (u8*)state->cardBuffer + (offset & 511), take);
        }
        offset += take;
        destination += take;
        length -= take;
    }
#if NP3D_LAUNCH_TRACE && defined(BUILDFATTABLE)
    if (trace) np3dTraceEvent("source card/prefix read complete", kind, state->cardOffset, 0);
#endif
    return true;
}

static void initialize(aFile* file)
{
#if defined(B4DS) && defined(ARM9)
    if (state) return;
#else
    if (descriptorCluster == file->firstCluster) return;
#endif
#ifdef BUILDFATTABLE
    state = &bootState;
    aFile descriptor = *file;
    descriptor.fatTableSettings &= ~fatVirtual;
    Np3dLaunch* launch = &state->rom.launch;
    np3dTraceEvent("descriptor read begin", file->firstCluster, sizeof(*launch), file->fatTableSettings);
    u32 received = fileRead((char*)launch, &descriptor, 0, sizeof(*launch));
    np3dTraceCheckSd(TRACE_CLUSTER);
    if (received != sizeof(*launch)) readFailure("ERROR descriptor SD read", file->firstCluster, sizeof(*launch), received);
    np3dTraceEvent("descriptor read complete", launch->magic, launch->version, launch->mapSize);
    np3dTraceEvent("descriptor source clusters", launch->mapCluster, launch->dataCluster, launch->prefixCluster);
#ifdef TWOCARD
    getFileFromCluster(&state->mapFile, launch->mapCluster, false);
    getFileFromCluster(&state->patchFile, launch->dataCluster, false);
    getFileFromCluster(&state->prefixFile, launch->prefixCluster, false);
#else
    getFileFromCluster(&state->mapFile, launch->mapCluster);
    getFileFromCluster(&state->patchFile, launch->dataCluster);
    getFileFromCluster(&state->prefixFile, launch->prefixCluster);
#endif
    if (!np3dRomInit(&state->rom, launch, readSource, NULL)) readFailure("ERROR virtual map initialization", launch->magic, launch->version, launch->mapSize);
#if NP3D_LAUNCH_TRACE
    state->traceCluster = np3dTraceCluster;
#endif
    np3dTraceEvent("virtual map initialized", state->rom.count, launch->romSize, launch->cardControl);
#else
    state = (Np3dFatState*)file->fatTableCache;
    if (!state || state->rom.launch.magic != NP3D_LAUNCH_MAGIC || state->rom.launch.version != 2)
        readFailure("ERROR runtime virtual ROM state", (u32)state, file->firstCluster, file->fatTableCacheSize);
    state->rom.read = readSource;
#if defined(B4DS) && defined(ARM9)
    state->rom.context = state;
#else
    state->rom.context = NULL;
#endif
#endif
#if !defined(B4DS) || defined(BUILDFATTABLE)
    state->cardOffset = UINT32_MAX;
    descriptorCluster = file->firstCluster;
#endif
    return;
}

u32 np3dFileRead(char* output, aFile* file, u32 offset, u32 length)
{
    int interrupts = enterCriticalSection();
#if NP3D_LAUNCH_TRACE && defined(BUILDFATTABLE)
    readCount++;
    bool trace = readCount <= 64 || !(readCount & (readCount - 1));
    if (trace) np3dTraceEvent("virtual ROM read begin (a=offset b=length c=destination)", offset, length, (u32)output);
#endif
    initialize(file);
    if (!np3dRomRead(&state->rom, offset, output, length)) {
        readFailure("ERROR virtual ROM read", offset, length, (u32)output);
    }
#if NP3D_LAUNCH_TRACE && defined(BUILDFATTABLE)
    if (trace) np3dTraceEvent("virtual ROM read complete", offset, length, state->rom.last);
#endif
    leaveCriticalSection(interrupts);
    return length;
}

#ifndef B4DS
bool np3dFileReadPending(void)
{
    return request.length != 0;
}

bool np3dResumeFileRead(void)
{
    int interrupts = enterCriticalSection();
    u16 spi = REG_AUXSPICNT;
    if (request.length && ((spi & CARD_SPI_BUSY)
        || (spi & (CARD_SPI_ENABLE | CARD_SPI_HOLD)) == (CARD_SPI_ENABLE | CARD_SPI_HOLD))) {
        leaveCriticalSection(interrupts);
        return false;
    }
    if (requestFile->fatTableSettings & fatVirtual) {
        initialize(requestFile);
        if (!np3dRomReadStep(&state->rom, &request)) {
            readFailure("ERROR asynchronous virtual ROM read", request.offset, request.length, (u32)request.output);
        }
    } else if (request.length) {
        u32 take = 0x1000 - (request.offset & 0xfff);
        if (take > request.length) take = request.length;
        if (fileRead((char*)request.output, requestFile, request.offset, take) != take) {
            readFailure("ERROR asynchronous SD file read", request.offset, take, (u32)request.output);
        }
        request.offset += take;
        request.output += take;
        request.length -= take;
    }
    bool complete = !request.length;
    leaveCriticalSection(interrupts);
    return complete;
}

bool np3dFileReadNonBlocking(char* output, aFile* file, u32 offset, u32 length)
{
    int interrupts = enterCriticalSection();
    requestFile = file;
    request.offset = offset;
    request.length = length;
    request.output = (u8*)output;
    bool complete = np3dResumeFileRead();
    leaveCriticalSection(interrupts);
    return complete;
}

#endif

#if !defined(B4DS) || defined(BUILDFATTABLE)
bool np3dOriginalCardCode(aFile* file, u32* code)
{
    if (!(file->fatTableSettings & fatVirtual)) return false;
    initialize(file);
    *code = state->rom.launch.cardCode;
    return true;
}
#endif

#ifdef BUILDFATTABLE
void np3dStoreRuntime(void* output)
{
    tonccpy(output, &bootState, sizeof(bootState));
}
#endif
#endif
