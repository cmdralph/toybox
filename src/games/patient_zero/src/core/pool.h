/*
 * pool.h - generic fixed-capacity pool helpers.
 * Pools are plain arrays of structs whose first member is `bool alive`
 * (or a known offset); a rolling cursor keeps allocation O(1) amortised.
 */
#ifndef PZ_POOL_H
#define PZ_POOL_H
#include "core/game.h"

int  Pool_Alloc(void *base, size_t stride, size_t aliveOffset, int cap, int *cursor);
int  Pool_CountAlive(const void *base, size_t stride, size_t aliveOffset, int cap);

/* Agent pool with generational references. */
int      Agent_Alloc(void);
void     Agent_ResetPool(void);
Agent   *Agent_Get(AgentRef r);
AgentRef Agent_Ref(int idx);
bool     Agent_RefValid(AgentRef r);
void     Agent_Free(int idx);

#endif
