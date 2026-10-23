/*
 * pool.c - slot allocation for every entity pool. The agent pool hands out
 * generational handles so stale targets (a killed cell whose slot was
 * reused) are detected instead of silently pointing at a new entity.
 */
#include "core/pool.h"
#include <string.h>

int Pool_Alloc(void *base, size_t stride, size_t aliveOffset, int cap, int *cursor)
{
    unsigned char *b = (unsigned char *)base;
    for (int n = 0; n < cap; n++) {
        int i = (*cursor + n) % cap;
        bool *alive = (bool *)(b + (size_t)i * stride + aliveOffset);
        if (!*alive) {
            *cursor = (i + 1) % cap;
            return i;
        }
    }
    return -1;
}

int Pool_CountAlive(const void *base, size_t stride, size_t aliveOffset, int cap)
{
    const unsigned char *b = (const unsigned char *)base;
    int n = 0;
    for (int i = 0; i < cap; i++) if (*(const bool *)(b + (size_t)i * stride + aliveOffset)) n++;
    return n;
}

static int sAgentCursor = 0;

void Agent_ResetPool(void) { sAgentCursor = 0; }

int Agent_Alloc(void)
{
    int i = Pool_Alloc(G.agents, sizeof(Agent), offsetof(Agent, alive), MAX_AGENTS, &sAgentCursor);
    if (i < 0) return -1;
    uint16_t gen = (uint16_t)(G.agents[i].gen + 1);
    memset(&G.agents[i], 0, sizeof(Agent));
    G.agents[i].gen = gen;
    G.agents[i].alive = true;
    G.agents[i].target = REF_NONE;
    G.agents[i].owner = REF_NONE;
    G.agents[i].speedMul = 1.0f;
    if (i + 1 > G.agentHigh) G.agentHigh = i + 1;
    return i;
}

AgentRef Agent_Ref(int idx)
{
    if (idx < 0 || idx >= MAX_AGENTS) return REF_NONE;
    return (AgentRef){ idx, G.agents[idx].gen };
}

bool Agent_RefValid(AgentRef r)
{
    return r.idx >= 0 && r.idx < MAX_AGENTS && G.agents[r.idx].alive && G.agents[r.idx].gen == r.gen;
}

Agent *Agent_Get(AgentRef r) { return Agent_RefValid(r) ? &G.agents[r.idx] : NULL; }

void Agent_Free(int idx)
{
    if (idx < 0 || idx >= MAX_AGENTS) return;
    G.agents[idx].alive = false;
    while (G.agentHigh > 0 && !G.agents[G.agentHigh - 1].alive) G.agentHigh--;
}
