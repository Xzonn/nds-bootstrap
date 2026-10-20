#include "np3d_rom.h"
#if defined(NP3D_FRONTEND) || defined(NP3D_VIRTUAL_FAT) || !defined(__arm__)
#ifdef B4DS
#pragma GCC optimize("Oz")
#else
#pragma GCC optimize("Os")
#endif
#include "np3d_rom.h"
#include <string.h>
#ifdef __arm__
#include "tonccpy.h"
#endif

#if !defined(B4DS) || !defined(ARM9) || defined(NP3D_FRONTEND)
bool np3dRomInit(Np3dRom* rom, const Np3dLaunch* launch, Np3dReadSource read, void* context)
{
    uint32_t header[8];
    if (launch->magic != NP3D_LAUNCH_MAGIC || launch->version != 1 || launch->mapSize < 32) return false;
    rom->launch = *launch;
    rom->read = read;
    rom->context = context;
    rom->cached = 0;
    rom->last = 0;
    if (!read(context, NP3D_MAP_SOURCE, 0, header, sizeof(header))) return false;
    rom->count = header[4];
    if (!(header[0] == 0x4433504e && header[1] == 0x0050414d && header[2] == 1 && header[3] == 3
        && header[5] == launch->romSize && header[6] == sizeof(Np3dExtent) && rom->count
        && rom->count <= (UINT32_MAX - 32) / sizeof(Np3dExtent)
        && 32 + rom->count * sizeof(Np3dExtent) == launch->mapSize)) return false;
    rom->stride = (rom->count + 63) / 64;
    rom->indexCount = 0;
    for (uint32_t number = 0; number < rom->count; number += rom->stride) {
        Np3dExtent item;
        if (!read(context, NP3D_MAP_SOURCE, 32 + number * sizeof(item), &item, sizeof(item))) return false;
        if (rom->indexCount && item.start <= rom->starts[rom->indexCount - 1]) return false;
        rom->starts[rom->indexCount++] = item.start;
    }
    return rom->starts[0] == 0;
}
#endif

static const Np3dExtent* extentAt(Np3dRom* rom, uint32_t number)
{
    if (!rom->cached || number < rom->first || number - rom->first >= rom->cached) {
        rom->first = number & ~31u;
        rom->cached = rom->count - rom->first;
        if (rom->cached > 32) rom->cached = 32;
        if (!rom->read(rom->context, NP3D_MAP_SOURCE, 32 + rom->first * sizeof(Np3dExtent), rom->entries,
            rom->cached * sizeof(Np3dExtent))) {
            rom->cached = 0;
            return NULL;
        }
    }
    const Np3dExtent* item = &rom->entries[number - rom->first];
    if (!item->length || item->start > rom->launch.romSize || item->length > rom->launch.romSize - item->start) return NULL;
    return item;
}

bool np3dRomRead(Np3dRom* rom, uint32_t offset, void* output, uint32_t length)
{
    if (offset > rom->launch.romSize || length > rom->launch.romSize - offset) return false;
    uint8_t* destination = output;
    while (length) {
        const Np3dExtent* found = extentAt(rom, rom->last);
        if (!found) return false;
        if (offset == found->start + found->length && rom->last + 1 < rom->count) {
            found = extentAt(rom, ++rom->last);
            if (!found) return false;
        }
        if (offset < found->start || offset - found->start >= found->length) {
            uint32_t first = 0, end = rom->indexCount;
            while (first < end) {
                uint32_t middle = first + (end - first) / 2;
                if (rom->starts[middle] <= offset) first = middle + 1;
                else end = middle;
            }
            first = (first - 1) * rom->stride;
            end = first + rom->stride;
            if (end > rom->count) end = rom->count;
            found = NULL;
            while (first < end) {
                uint32_t middle = first + (end - first) / 2;
                const Np3dExtent* item = extentAt(rom, middle);
                if (!item) return false;
                if (offset < item->start) end = middle;
                else if (offset - item->start >= item->length) first = middle + 1;
                else { found = item; rom->last = middle; break; }
            }
        }
        if (!found) return false;
        Np3dExtent item = *found;
        uint32_t take = item.length - (offset - item.start);
        if (take > length) take = length;
        if (item.kind == 2) {
            if (item.source > 255) return false;
#ifdef __arm__
            toncset(destination, item.source, take);
#else
            memset(destination, item.source, take);
#endif
        } else {
            uint32_t available = item.kind == 0 ? rom->launch.cardSize : rom->launch.dataSize;
            if (item.kind > 1 || item.source > available || item.length > available - item.source) return false;
            if (!rom->read(rom->context, item.kind, item.source + offset - item.start, destination, take)) return false;
        }
        offset += take;
        destination += take;
        length -= take;
    }
    return true;
}

#ifndef B4DS
bool np3dRomReadStep(Np3dRom* rom, Np3dRead* request)
{
    if (request->offset > rom->launch.romSize || request->length > rom->launch.romSize - request->offset) return false;
    uint32_t take = 0x1000 - (request->offset & 0xfff);
    if (take > request->length) take = request->length;
    if (!np3dRomRead(rom, request->offset, request->output, take)) return false;
    request->offset += take;
    request->output += take;
    request->length -= take;
    return true;
}
#endif
#endif
