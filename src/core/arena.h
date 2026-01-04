#pragma once

struct Arena
{
    void *base;
    size_t offset;
    size_t capacity;
};

struct ArenaTemp
{
    Arena *arena;
    size_t pos;
};

Arena* ArenaAlloc(size_t capacity);
void   ArenaFree(Arena *arena);
void*  ArenaPush(Arena *arena, size_t size, size_t alignment);
void*  ArenaPushZero(Arena *arena, size_t size, size_t alignment);
void   ArenaPop(Arena *arena, size_t size);
void   ArenaPopTo(Arena *arena, size_t offset);
void   ArenaClear(Arena *arena);

#define ArenaPushArray(arena, T, count) (T *) ArenaPush((arena), sizeof(T) * count, alignof(T))
#define ArenaPushArrayZero(arena, T, count) (T *) ArenaPushZero((arena), sizeof(T) * count, alignof(T))

ArenaTemp  ArenaTempBegin(Arena *arena);
void       ArenaTempEnd(ArenaTemp temp);
