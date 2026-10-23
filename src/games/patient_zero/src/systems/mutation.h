/*
 * mutation.h - the 45-node mutation tree (5 branches x 4 tiers), synergy
 * pairs, prerequisites, stage gates, trade-offs and respec.
 */
#ifndef PZ_MUTATION_H
#define PZ_MUTATION_H
#include "core/game.h"

typedef enum {
    M_FLAGELLA_1 = 0, M_SPIKES_1, M_CAPSID_1, M_FLAGELLA_2, M_SPIKES_2, M_CAPSID_2, M_GROWTH, M_ORGANELLES, M_APEX_FORM, M_REGEN,
    R_FAST_INJECT, R_GM_YIELD, R_VIRION_RATE, R_CAPACITY, R_BURST_REPL, R_DRIP, R_LATENCY, R_POLYMERASE, R_HIJACK,
    T_POTENCY, T_RAPID, T_SPLIT, T_POISON, T_CLOUD, T_ACID, T_CHAIN, T_NOVA, T_NECROTOXIN,
    S_DRIFT_1, S_CAMO, S_QUIET, S_DECOY, S_DRIFT_2, S_CLOAK, S_DAMPEN, S_GHOST,
    W_SWARM_1, W_FANGS, W_SPEED, W_HIVE, W_SWARM_2, W_SYMBIOSIS, W_EXPLODE, W_ELITE, W_OVERMIND,
    MUT_COUNT
} MutId;

typedef struct {
    const char *name;
    MutBranch branch;
    int tier, cost, minStage;
    int req[2];
    const char *desc;
    const char *tradeoff;
} MutationDef;

typedef struct {
    const char *name;
    int a, b;
    const char *desc;
} SynergyDef;

#define SYNERGY_COUNT 6
extern const MutationDef MUTATIONS[MUT_COUNT];
extern const SynergyDef SYNERGIES[SYNERGY_COUNT];
extern const char *BRANCH_NAMES[BR_COUNT];
extern const Color BRANCH_COLORS[BR_COUNT];

bool Mutation_Owned(int id);
int  Mutation_CanBuy(int id);               /* 0 = ok, else reason code   */
const char *Mutation_Reason(int code);
bool Mutation_Buy(int id);
int  Mutation_RespecRefund(void);
int  Mutation_RespecCost(void);
bool Mutation_Respec(void);
bool Mutation_SynergyActive(int s);
void Mutation_ComputeStats(PlayerStats *st, int stage);

#endif
