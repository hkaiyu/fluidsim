#include "arena.h"
#include "utils.h"

#include <string.h>

Arena *
ArenaAlloc(size_t capacity)
{
    size_t totalSpace = sizeof(Arena) + capacity;
    Arena *arena = (Arena *) malloc(totalSpace);
    arena->base = arena;
    arena->offset = sizeof(Arena); // we include the arena header in the heap allocation itself
    arena->capacity = capacity;
    return arena;
}

void
ArenaFree(Arena *arena)
{
    free(arena->base);
    *arena = {};
}

void *
ArenaPush(Arena* arena, size_t size, size_t alignment = 1)
{
    assert(alignment != 0);

    if (size == 0) return nullptr;

    size_t alignedOffset = (arena->offset + alignment - 1) & ~(alignment - 1);
    void *ptr = nullptr;

    if (alignedOffset + size <= arena->capacity) [[likely]]
    {
        ptr = (u8*) arena->base + alignedOffset;
        arena->offset = alignedOffset + size;
    }
    else
    {
        // Note: No memory-growth strategy required. We simply just won't hit this case.
        FATAL("Ran out of arena memory... aborting");
    }
    return ptr;
}

void *
ArenaPushZero(Arena *arena, size_t size, size_t alignment = 1)
{
    void *ptr = ArenaPush(arena, size, alignment);
    memset(ptr, 0, size);
    return ptr;
}

void
ArenaPop(Arena *arena, size_t size)
{
    if (size > arena->offset) [[unlikely]]
    {
        arena->offset = 0;
    }
    else
    {
        arena->offset -= size;
    }
}

void
ArenaPopTo(Arena *arena, size_t offset)
{
    arena->offset = offset;
}

void
ArenaClear(Arena *arena)
{
    arena->offset = sizeof(Arena);
}

ArenaTemp
ArenaTempBegin(Arena* arena)
{
    return ArenaTemp {
        .arena = arena,
        .pos = arena->offset
    };
}

void
ArenaTempEnd(ArenaTemp temp)
{
    ArenaPopTo(temp.arena, temp.pos);
}
