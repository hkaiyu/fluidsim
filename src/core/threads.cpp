#include "threads.h"
#include "utils.h"
#include "arena.h"

ThreadContext *
GetThreadContext()
{
    global thread_local ThreadContext *tctx = nullptr;
    if (tctx == nullptr)
    {
        Arena *arena = ArenaAlloc(KB(8));
        tctx = (ThreadContext *) ArenaPush(arena, sizeof(ThreadContext), alignof(ThreadContext));
        tctx->arenas[0] = arena;
        tctx->arenas[1] = ArenaAlloc(KB(8));
    }
    return tctx;
}

// returns a scratch arena that is NOT listed in the list of persistentArenas
Arena* 
GetScratch(std::initializer_list<Arena *> persistentArenas)
{
    ThreadContext *tctx = GetThreadContext();
    Arena *res = nullptr;
    for (Arena *ta : tctx->arenas)
    {
        bool notInList = true;
        for (Arena *a : persistentArenas)
        {
            if (ta == a)
            {
                notInList = false;
                break;
            } 
        }
        if (notInList)
        {
            res = ta;
            break;
        }
    }
    return res;
}
