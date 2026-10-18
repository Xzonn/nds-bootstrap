#include "np3d_vfs.h"
#include "np3d_trace.h"
#include "read_card.h"
#include <nds.h>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#if NP3D_LAUNCH_TRACE
#include <stdarg.h>
#include <unistd.h>
#endif

static Np3dRom rom;
static FILE* sources[4];
static uint32_t descriptorCluster;
static uint32_t cardHeader[1024];
static uint32_t cardBlock[128];
static uint32_t cardOffset = UINT32_MAX;

#if NP3D_LAUNCH_TRACE
uint32_t np3dPrepareLoaderLog(void)
{
    const char* path = "sd:/nitro-patcher/logs/arm7.log";
    FILE* file = fopen(path, "wb");
    bool success = file != NULL;
    char record[NP3D_TRACE_RECORD];
    memset(record, ' ', sizeof(record));
    const char* message = "ARM7: reserved log; waiting for loader SD/FAT initialization.";
    memcpy(record, message, strlen(message));
    record[sizeof(record) - 1] = '\n';
    for (unsigned int offset = 0; success && offset < NP3D_TRACE_BYTES; offset += sizeof(record)) {
        success = fwrite(record, 1, sizeof(record), file) == sizeof(record);
        memset(record, ' ', sizeof(record) - 1);
    }
    if (file) {
        if (fflush(file)) success = false;
        if (fsync(fileno(file))) success = false;
        if (fclose(file)) success = false;
    }
    struct stat info;
    if (success && stat(path, &info) == 0 && info.st_size == NP3D_TRACE_BYTES
        && info.st_ino >= 2 && info.st_ino < 0x0ffffff0) return info.st_ino;
    consoleDemoInit();
    iprintf("Cannot prepare arm7.log.\n");
    while (1) swiWaitForVBlank();
}

void np3dBootLog(const char* format, ...)
{
    FILE* file = fopen("sd:/nitro-patcher/logs/launch.log", "a");
    if (file) {
        va_list arguments;
        va_start(arguments, format);
        int written = vfprintf(file, format, arguments);
        va_end(arguments);
        int flushed = fflush(file);
        int synced = fsync(fileno(file));
        int closed = fclose(file);
        if (written >= 0 && flushed == 0 && synced == 0 && closed == 0) return;
    }
    consoleDemoInit();
    iprintf("Cannot write launch.log.\n");
    while (1) swiWaitForVBlank();
}
#endif

static bool readSource(void* context, uint32_t kind, uint32_t offset, void* output, uint32_t length)
{
    (void)context;
    if (kind != 0) {
        FILE* file = sources[kind];
        return fseek(file, offset, SEEK_SET) == 0 && fread(output, 1, length, file) == length;
    }
    uint8_t* destination = output;
    while (length) {
        uint32_t take;
        if (offset < 0x8000) {
            take = 0x8000 - offset;
            if (take > length) take = length;
            if (fseek(sources[0], offset, SEEK_SET) || fread(destination, 1, take, sources[0]) != take) return false;
        } else {
            take = 512 - (offset & 511);
            if (take > length) take = length;
            if (cardOffset != (offset & ~511u)) {
                bool firstCardRead = cardOffset == UINT32_MAX;
                if (firstCardRead) np3dBootLog("vfs: first physical ROM read begin offset=%08lX\n", (unsigned long)(offset & ~511u));
                if (isDSiMode()) cardRead(offset & ~511u, cardBlock, sizeof(cardBlock));
                else cardParamCommand(CARD_CMD_DATA_READ, offset & ~511u,
                    (rom.launch.cardControl & ~CARD_BLK_SIZE(7)) | CARD_ACTIVATE | CARD_nRESET | CARD_BLK_SIZE(1),
                    cardBlock, sizeof(cardBlock)/sizeof(uint32_t));
                if (firstCardRead) np3dBootLog("vfs: first physical ROM read complete\n");
                cardOffset = offset & ~511u;
            }
            memcpy(destination, (uint8_t*)cardBlock + (offset & 511), take);
        }
        destination += take;
        offset += take;
        length -= take;
    }
    return true;
}

static void fillStat(struct stat* info)
{
    memset(info, 0, sizeof(*info));
    info->st_mode = S_IFREG | 0444;
    info->st_size = rom.launch.romSize;
    info->st_ino = descriptorCluster | NP3D_VIRTUAL_CLUSTER;
    info->st_nlink = 1;
}

static int openRom(struct _reent* state, void* file, const char* path, int flags, int mode)
{
    (void)mode;
    if (strcmp(path, "np3d:/rom.nds")) { state->_errno = ENOENT; return -1; }
    if ((flags & O_ACCMODE) != O_RDONLY) { state->_errno = EACCES; return -1; }
    *(uint32_t*)file = 0;
    return (int)(uintptr_t)file;
}

static int closeRom(struct _reent* state, void* file)
{
    (void)state;
    (void)file;
    return 0;
}

static ssize_t readRom(struct _reent* state, void* file, char* output, size_t length)
{
    uint32_t* position = file;
    if (length > rom.launch.romSize - *position) length = rom.launch.romSize - *position;
    if (!np3dRomRead(&rom, *position, output, length)) { state->_errno = EIO; return -1; }
    *position += length;
    return length;
}

static off_t seekRom(struct _reent* state, void* file, off_t offset, int origin)
{
    uint32_t* position = file;
    if (origin != SEEK_SET && origin != SEEK_CUR && origin != SEEK_END) { state->_errno = EINVAL; return -1; }
    int64_t base = origin == SEEK_SET ? 0 : origin == SEEK_CUR ? *position : rom.launch.romSize;
    if (offset < -base || offset > rom.launch.romSize - base) { state->_errno = EINVAL; return -1; }
    int64_t result = base + offset;
    *position = result;
    return result;
}

static int statRom(struct _reent* state, const char* path, struct stat* info)
{
    if (strcmp(path, "np3d:/rom.nds")) { state->_errno = ENOENT; return -1; }
    fillStat(info);
    return 0;
}

static int fstatRom(struct _reent* state, void* file, struct stat* info)
{
    (void)file;
    (void)state;
    fillStat(info);
    return 0;
}

static const devoptab_t device = {
    .name = "np3d",
    .structSize = sizeof(uint32_t),
    .open_r = openRom,
    .close_r = closeRom,
    .read_r = readRom,
    .seek_r = seekRom,
    .fstat_r = fstatRom,
    .stat_r = statRom,
};

int np3dDeviceInit(void)
{
    np3dBootLog("vfs: reading launch descriptor\n");
    struct stat info;
    const char* path = "sd:/nitro-patcher/config/launch.bin";
    Np3dLaunch launch;
    if (stat(path, &info) || info.st_size != sizeof(launch)) return 1;
    descriptorCluster = info.st_ino;
    FILE* descriptor = fopen(path, "rb");
    if (!descriptor) return 2;
    bool read = fread(&launch, 1, sizeof(launch), descriptor) == sizeof(launch);
    int closed = fclose(descriptor);
    if (!read || closed) return 3;
    if (launch.magic != NP3D_LAUNCH_MAGIC || launch.version != 1
        || !memchr(launch.cachePath, 0, sizeof(launch.cachePath))) return 4;
    np3dBootLog("vfs: descriptor cluster=%08lX map=%08lX data=%08lX prefix=%08lX rom=%lu card=%lu id=%08lX cache=%s\n",
        (unsigned long)descriptorCluster, (unsigned long)launch.mapCluster, (unsigned long)launch.dataCluster,
        (unsigned long)launch.prefixCluster, (unsigned long)launch.romSize, (unsigned long)launch.cardSize,
        (unsigned long)launch.cardId, launch.cachePath);
    char filename[288];
    snprintf(filename, sizeof(filename), "%s/map.bin", launch.cachePath);
    sources[NP3D_MAP_SOURCE] = fopen(filename, "rb");
    snprintf(filename, sizeof(filename), "%s/data.bin", launch.cachePath);
    sources[1] = fopen(filename, "rb");
    sources[0] = fopen("sd:/nitro-patcher/runtime/card-prefix.bin", "rb");
    if (!sources[NP3D_MAP_SOURCE] || !sources[1] || !sources[0]) return 5;
    if (!isDSiMode()) {
        sysSetCardOwner(BUS_OWNER_ARM9);
        if (!np3dRomInit(&rom, &launch, readSource, NULL)) return 9;
        return AddDevice(&device) >= 0 ? 0 : 10;
    }
    np3dBootLog("vfs: cache sources open; requesting ARM7 card reset\n");
    sysSetCardOwner(BUS_OWNER_ARM7);
    fifoSendValue32(FIFO_USER_08, 1);
    fifoWaitValue32(FIFO_USER_08);
    if (fifoGetValue32(FIFO_USER_08) != 0) return 11;
    np3dBootLog("vfs: ARM7 card reset acknowledged\n");
    for (int frame = 0; frame < 30; frame++) swiWaitForVBlank();
    sysSetCardOwner(BUS_OWNER_ARM9);
    cardReadHeader((uint8_t*)cardHeader);
    np3dBootLog("vfs: physical header read\n");
    if (fread(cardBlock, 1, sizeof(cardBlock), sources[0]) != sizeof(cardBlock)
        || memcmp(cardBlock, cardHeader, sizeof(cardBlock))) return 6;
    if (memcmp((uint8_t*)cardHeader + 12, &launch.cardCode, 4)) return 7;
    uint32_t id;
    int initialized = cardInit((sNDSHeaderExt*)cardHeader, &id);
    np3dBootLog("vfs: physical initialization result=%d id=%08lX\n", initialized, (unsigned long)id);
    if (initialized) return 100 + initialized;
    if (id != launch.cardId) return 8;
    if (!np3dRomInit(&rom, &launch, readSource, NULL)) return 9;
    np3dBootLog("vfs: mapping initialized extents=%lu\n", (unsigned long)rom.count);
    return AddDevice(&device) >= 0 ? 0 : 10;
}

const Np3dLaunch* np3dLaunchInfo(void) { return &rom.launch; }
