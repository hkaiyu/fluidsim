#pragma once

#include "arena.h"

#include <initializer_list>

struct ThreadContext
{
    Arena *arenas[2];
};

ThreadContext* GetThreadContext();

// returns a scratch arena that is NOT listed in the list of persistentArenas
Arena* GetScratch(std::initializer_list<Arena *> persistentArenas = {});

#define ScratchBegin(...) ArenaTempBegin(GetScratch({__VA_ARGS__}))
#define ScratchEnd(temp) ArenaTempEnd(temp)
