/*
 * spatial_hash.h - uniform grid over the region volume, rebuilt every
 * simulation tick with a counting sort (O(n), cache friendly). Keeps
 * neighbour queries cheap for 2,000+ agents.
 */
#ifndef PZ_SPATIAL_HASH_H
#define PZ_SPATIAL_HASH_H
#include "core/game.h"

#define KMASK(k) (1u << (k))
#define AGENT_MASK_IMMUNE (KMASK(AK_NEUTROPHIL) | KMASK(AK_MACROPHAGE) | KMASK(AK_NK) | KMASK(AK_TCELL) | KMASK(AK_BCELL) | KMASK(AK_ANTIBODY) | KMASK(AK_BOSS_PART))
#define AGENT_MASK_IMMUNE_CELLS (KMASK(AK_NEUTROPHIL) | KMASK(AK_MACROPHAGE) | KMASK(AK_NK) | KMASK(AK_TCELL) | KMASK(AK_BCELL))
#define AGENT_MASK_PLAYER_SIDE (KMASK(AK_MINION) | KMASK(AK_DECOY))
#define AGENT_MASK_AMBIENT (KMASK(AK_DRIFTER) | KMASK(AK_PLATELET) | KMASK(AK_BACTERIA))

void SH_Build(void);
int  SH_Query(Vector3 c, float r, int *out, int max, uint32_t mask);
int  SH_Nearest(Vector3 c, float r, uint32_t mask, int exclude);

#endif
