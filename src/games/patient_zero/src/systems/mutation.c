/*
 * mutation.c - mutation data and the stat pipeline:
 *   base stats (by evolution stage) -> owned mutations -> synergies.
 * Stats are recomputed whenever the genome changes, never per frame.
 */
#include "systems/mutation.h"
#include "core/settings.h"
#include "ui/hud.h"
#include "audio/sfx.h"
#include <stdio.h>

#define NR { -1, -1 }
#define R1(a) { a, -1 }
#define R2(a, b) { a, b }

const char *BRANCH_NAMES[BR_COUNT] = { "MORPHOLOGY", "REPLICATION", "TOXINS", "STEALTH", "SWARM" };
const Color BRANCH_COLORS[BR_COUNT] = { { 120, 220, 255, 255 }, { 120, 255, 150, 255 }, { 190, 255, 80, 255 }, { 190, 140, 255, 255 }, { 255, 170, 90, 255 } };

const MutationDef MUTATIONS[MUT_COUNT] = {
    /* MORPHOLOGY */
    [M_FLAGELLA_1] = { "Flagellum", BR_MORPH, 1, 25, 0, NR, "A whip-like tail. +12% swim speed.", NULL },
    [M_SPIKES_1]   = { "Capsid Spikes", BR_MORPH, 1, 25, 0, NR, "Contact lysis: touching immune cells deals 7 damage/s.", NULL },
    [M_CAPSID_1]   = { "Reinforced Capsid", BR_MORPH, 1, 30, 0, NR, "+12% armour and an extra membrane layer.", NULL },
    [M_FLAGELLA_2] = { "Flagellar Bundle", BR_MORPH, 2, 55, 1, R1(M_FLAGELLA_1), "+15% speed, dashes cost 25% less ATP.", NULL },
    [M_SPIKES_2]   = { "Barbed Spikes", BR_MORPH, 2, 55, 1, R1(M_SPIKES_1), "Contact lysis 16/s, dashes deal +20 damage.", NULL },
    [M_CAPSID_2]   = { "Lipid Envelope", BR_MORPH, 2, 60, 1, R1(M_CAPSID_1), "+30 max biomass, +10% armour.", NULL },
    [M_GROWTH]     = { "Hypertrophy", BR_MORPH, 2, 70, 1, R1(M_CAPSID_1), "+20% size, +45 max biomass.", "-6% speed, easier to spot" },
    [M_ORGANELLES] = { "Mitochondrial Theft", BR_MORPH, 3, 95, 2, R1(M_CAPSID_2), "Stolen mitochondria: +40 max ATP, +30% ATP regen.", NULL },
    [M_APEX_FORM]  = { "Apex Morphology", BR_MORPH, 4, 160, 3, R2(M_SPIKES_2, M_GROWTH), "Contact lysis 34/s, +15% armour, +15% size.", NULL },
    [M_REGEN]      = { "Autophagic Repair", BR_MORPH, 3, 90, 2, R1(M_CAPSID_2), "Biomass regenerates 3/s faster.", NULL },
    /* REPLICATION */
    [R_FAST_INJECT]= { "Rapid Injection", BR_REPL, 1, 25, 0, NR, "Infect host cells 30% faster.", NULL },
    [R_GM_YIELD]   = { "Genome Harvest", BR_REPL, 1, 30, 0, NR, "+20% Genetic Material from every source.", NULL },
    [R_VIRION_RATE]= { "Accelerated Assembly", BR_REPL, 2, 50, 1, R1(R_FAST_INJECT), "Infected cells assemble virions 40% faster.", NULL },
    [R_CAPACITY]   = { "Viral Factories", BR_REPL, 2, 55, 1, R1(R_FAST_INJECT), "Each infected cell sustains +1 virion.", NULL },
    [R_BURST_REPL] = { "Burst Replication", BR_REPL, 2, 65, 1, R1(R_VIRION_RATE), "Infecting a cell instantly releases 2 virions.", NULL },
    [R_DRIP]       = { "Metabolic Siphon", BR_REPL, 2, 50, 0, R1(R_GM_YIELD), "Infected cells near you feed you ATP and GM.", NULL },
    [R_LATENCY]    = { "Latent Infection", BR_REPL, 3, 90, 2, R1(R_CAPACITY), "NK cells take twice as long to cleanse your cells.", NULL },
    [R_POLYMERASE] = { "Error-Prone Polymerase", BR_REPL, 3, 100, 2, R1(R_VIRION_RATE), "+30% Evolution Points.", "+15% Alert gain" },
    [R_HIJACK]     = { "Total Hijack", BR_REPL, 4, 170, 3, R2(R_BURST_REPL, R_LATENCY), "Perfect beats inject twice as much; +2 virions per cell.", NULL },
    /* TOXINS */
    [T_POTENCY]    = { "Concentrated Toxin", BR_TOXIN, 1, 25, 0, NR, "+30% toxin damage.", NULL },
    [T_RAPID]      = { "Rapid Secretion", BR_TOXIN, 1, 30, 0, NR, "+35% fire rate.", NULL },
    [T_SPLIT]      = { "Split Spit", BR_TOXIN, 2, 55, 1, R1(T_RAPID), "Fire 3 globules in a spread.", "+30% ATP per shot" },
    [T_POISON]     = { "Neurotoxin", BR_TOXIN, 2, 55, 1, R1(T_POTENCY), "Hits poison for 7 damage/s over 3 s.", NULL },
    [T_CLOUD]      = { "Toxic Bloom", BR_TOXIN, 2, 65, 1, R1(T_POISON), "30% of hits leave a lingering poison cloud.", NULL },
    [T_ACID]       = { "Acidic Payload", BR_TOXIN, 3, 90, 2, R1(T_POTENCY), "Toxin pierces 2 targets and dissolves adaptive shields.", NULL },
    [T_CHAIN]      = { "Chain Lysis", BR_TOXIN, 3, 100, 2, R1(T_CLOUD), "Kills have a 40% chance to rupture, damaging nearby cells.", NULL },
    [T_NOVA]       = { "Lytic Nova", BR_TOXIN, 2, 60, 1, R1(T_POTENCY), "Lysis burst +60% damage, +30% radius.", NULL },
    [T_NECROTOXIN] = { "Necrotoxin", BR_TOXIN, 4, 170, 3, R2(T_ACID, T_CHAIN), "+60% toxin damage, poison 16/s, toxin costs 40% less.", NULL },
    /* STEALTH */
    [S_DRIFT_1]    = { "Antigenic Drift", BR_STEALTH, 1, 30, 0, NR, "The immune system adapts to you 30% slower.", NULL },
    [S_CAMO]       = { "Self-Mimicry", BR_STEALTH, 1, 30, 0, NR, "Immune cells detect you from 25% shorter range.", NULL },
    [S_QUIET]      = { "Silent Injection", BR_STEALTH, 2, 50, 1, R1(S_CAMO), "Infecting raises 60% less Alert.", NULL },
    [S_DECOY]      = { "Decoy Virions", BR_STEALTH, 2, 60, 1, R1(S_CAMO), "Dashing sheds a decoy that immune cells chase.", NULL },
    [S_DRIFT_2]    = { "Antigenic Shift", BR_STEALTH, 2, 65, 1, R1(S_DRIFT_1), "Adaptation slowed further; resistances fade twice as fast.", NULL },
    [S_CLOAK]      = { "Cloaking Membrane", BR_STEALTH, 3, 95, 2, R1(S_DECOY), "Unlocks CLOAK (R): turn nearly invisible for 5 s.", NULL },
    [S_DAMPEN]     = { "Cytokine Dampening", BR_STEALTH, 3, 90, 2, R1(S_QUIET), "Alert decays twice as fast, -20% Alert gain.", NULL },
    [S_GHOST]      = { "Ghost Genome", BR_STEALTH, 4, 160, 3, R2(S_CLOAK, S_DAMPEN), "Cloak lasts twice as long; the first attack from cloak deals double damage.", NULL },
    /* SWARM */
    [W_SWARM_1]    = { "Swarm Instinct", BR_SWARM, 1, 25, 1, NR, "+4 maximum virions.", NULL },
    [W_FANGS]      = { "Virion Fangs", BR_SWARM, 1, 30, 1, NR, "Virions deal +40% damage.", NULL },
    [W_SPEED]      = { "Ciliated Virions", BR_SWARM, 2, 50, 1, R1(W_SWARM_1), "Virions are 35% faster and 30% tougher.", NULL },
    [W_HIVE]       = { "Hive Link", BR_SWARM, 2, 65, 1, R1(W_FANGS), "Every virion hit restores a little of your biomass.", NULL },
    [W_SWARM_2]    = { "Legion", BR_SWARM, 2, 70, 2, R1(W_SWARM_1), "+8 maximum virions.", "+10% Alert gain" },
    [W_SYMBIOSIS]  = { "Symbiosis", BR_SWARM, 2, 55, 1, R1(W_SWARM_1), "Gut bacteria become allies and fight alongside you.", NULL },
    [W_EXPLODE]    = { "Kamikaze Capsids", BR_SWARM, 3, 90, 2, R1(W_SPEED), "Virions burst violently when destroyed.", NULL },
    [W_ELITE]      = { "Elite Brood", BR_SWARM, 3, 110, 2, R2(W_HIVE, W_SWARM_2), "New virions are larger elites: +50% damage and health.", NULL },
    [W_OVERMIND]   = { "Overmind", BR_SWARM, 4, 180, 3, R1(W_ELITE), "+12 virions, +50% virion damage, Hive Call recharges twice as fast.", NULL },
};

const SynergyDef SYNERGIES[SYNERGY_COUNT] = {
    { "Cytotoxic Burst", T_CLOUD, R_BURST_REPL, "Virions release toxic clouds when they die." },
    { "Viral Load", R_VIRION_RATE, W_SWARM_1, "+2 virions per evolution stage." },
    { "Necrotic Spikes", M_SPIKES_2, T_POISON, "Contact lysis also poisons." },
    { "Ghost Swarm", S_CAMO, W_SPEED, "Your virions never raise Alert and vanish while you cloak." },
    { "Symbiotic Factory", W_SYMBIOSIS, R_DRIP, "Allied bacteria multiply and feed you ATP." },
    { "Apex Predator", M_APEX_FORM, T_NECROTOXIN, "Dash damage doubled; kills restore biomass." },
};

bool Mutation_Owned(int id) { return id >= 0 && id < MUT_COUNT && G.mut.owned[id]; }

int Mutation_CanBuy(int id)
{
    const MutationDef *m = &MUTATIONS[id];
    if (G.mut.owned[id]) return 1;
    if (G.player.stage < m->minStage) return 2;
    for (int k = 0; k < 2; k++) if (m->req[k] >= 0 && !G.mut.owned[m->req[k]]) return 3;
    if (G.player.gm < m->cost) return 4;
    return 0;
}

const char *Mutation_Reason(int code)
{
    switch (code) {
        case 0: return "Click to mutate";
        case 1: return "Already part of your genome";
        case 2: return "Requires a later evolution stage";
        case 3: return "Requires the connected mutation first";
        case 4: return "Not enough Genetic Material";
        default: return "";
    }
}

static int CountSynergies(void)
{
    int n = 0;
    for (int s = 0; s < SYNERGY_COUNT; s++) if (Mutation_SynergyActive(s)) n++;
    return n;
}

bool Mutation_Buy(int id)
{
    if (Mutation_CanBuy(id) != 0) return false;
    int synBefore = CountSynergies();
    G.player.gm -= MUTATIONS[id].cost;
    G.mut.owned[id] = true;
    G.mut.gmSpent += MUTATIONS[id].cost;
    G.mut.count++;
    G.mut.unlockAnim[id] = 1.0f;
    G.run.stats.mutations++;
    int synAfter = CountSynergies();
    if (synAfter > synBefore) {
        for (int s = 0; s < SYNERGY_COUNT; s++) {
            if (Mutation_SynergyActive(s) && (SYNERGIES[s].a == id || SYNERGIES[s].b == id)) {
                char t[64];
                snprintf(t, sizeof(t), "SYNERGY: %s", SYNERGIES[s].name);
                HUD_Toast(t, SYNERGIES[s].desc, (Color){ 255, 220, 120, 255 });
            }
        }
        HUD_Achievement(ACH_SYNERGIST);
    }
    if (G.mut.count >= 20) HUD_Achievement(ACH_CHIMERA);
    Sfx_Play(SFX_MUTATE, 1.0f, 0.9f + 0.05f * (float)MUTATIONS[id].tier);
    return true;
}

int Mutation_RespecRefund(void) { return (G.mut.gmSpent * 7) / 10; }
int Mutation_RespecCost(void) { return 20 + 15 * G.mut.respecs; }

bool Mutation_Respec(void)
{
    if (G.mut.gmSpent == 0) return false;
    int refund = Mutation_RespecRefund() - Mutation_RespecCost();
    if (refund < 0) refund = 0;
    for (int i = 0; i < MUT_COUNT; i++) G.mut.owned[i] = false;
    G.player.gm += refund;
    G.mut.gmSpent = 0;
    G.mut.count = 0;
    G.mut.respecs++;
    Sfx_Play(SFX_RESPEC, 1.0f, 1.0f);
    return true;
}

bool Mutation_SynergyActive(int s) { return G.mut.owned[SYNERGIES[s].a] && G.mut.owned[SYNERGIES[s].b]; }

void Mutation_ComputeStats(PlayerStats *st, int stage)
{
    static const float BIO[STAGE_COUNT] = { 60, 100, 160, 240, 340 };
    PlayerStats s = { 0 };
    float fs = (float)stage;
    s.speed = 15.0f + fs * 1.6f;
    s.accel = 46.0f;
    s.dashPower = 34.0f; s.dashCost = 18.0f; s.dashCd = 0.55f;
    s.armor = 0.0f;
    s.maxBiomass = BIO[stage];
    s.regen = 1.5f * DIFFICULTIES[G.run.difficulty].playerRegen;
    s.maxAtp = 100.0f + fs * 15.0f; s.atpRegen = 14.0f;
    s.sizeMul = 1.0f;
    s.contactDps = 0.0f; s.dashDamage = 8.0f + fs * 3.0f;
    s.toxinDamage = 10.0f + fs * 3.0f; s.toxinInterval = 0.2f; s.toxinSpeed = 58.0f; s.toxinCost = 2.5f; s.toxinCount = 1; s.toxinSpread = 0.08f;
    s.poisonDps = 0.0f; s.poisonTime = 3.0f;
    s.burstDamage = 26.0f + fs * 8.0f; s.burstRadius = 8.0f + fs * 1.5f; s.burstCost = 32.0f; s.burstCd = 6.0f;
    s.infectSpeed = 1.0f; s.virionInterval = 7.0f; s.hostCapacity = 2; s.burstReplicate = 0;
    s.gmMul = 1.0f; s.epMul = 1.0f;
    s.minionMax = stage >= 1 ? 6 + (stage - 1) * 3 : 0;
    s.minionDamage = 5.0f + fs * 1.5f; s.minionSpeed = 18.0f; s.minionHp = 14.0f + fs * 4.0f; s.minionTier = 1;
    s.swarmCd = SWARM_COOLDOWN;
    s.alertMul = 1.0f; s.adaptMul = 1.0f; s.detectMul = 1.0f; s.alertDecayMul = 1.0f; s.adaptDecayMul = 1.0f; s.injectAlertMul = 1.0f;
    s.cloakDuration = 5.0f;
    s.flagella = stage >= 1 ? 2 : 1;
    s.spikes = 0;
    s.membraneLayers = 1 + stage / 2;
    s.glowNodes = stage * 2;
    s.organelles = stage;

    bool *o = G.mut.owned;
    if (o[M_FLAGELLA_1]) { s.speed *= 1.12f; s.flagella += 1; }
    if (o[M_SPIKES_1]) { s.contactDps = fmaxf(s.contactDps, 7.0f); s.spikes += 4; }
    if (o[M_CAPSID_1]) { s.armor += 0.12f; s.membraneLayers += 1; }
    if (o[M_FLAGELLA_2]) { s.speed *= 1.15f; s.dashCost *= 0.75f; s.flagella += 2; }
    if (o[M_SPIKES_2]) { s.contactDps = fmaxf(s.contactDps, 16.0f); s.dashDamage += 20.0f; s.spikes += 6; }
    if (o[M_CAPSID_2]) { s.maxBiomass += 30.0f; s.armor += 0.10f; s.membraneLayers += 1; }
    if (o[M_GROWTH]) { s.sizeMul *= 1.2f; s.maxBiomass += 45.0f; s.speed *= 0.94f; s.detectMul *= 1.1f; }
    if (o[M_ORGANELLES]) { s.maxAtp += 40.0f; s.atpRegen *= 1.3f; s.organelles += 3; }
    if (o[M_APEX_FORM]) { s.contactDps = fmaxf(s.contactDps, 34.0f); s.armor += 0.15f; s.sizeMul *= 1.15f; s.spikes += 8; }
    if (o[M_REGEN]) s.regen += 3.0f;

    if (o[R_FAST_INJECT]) s.infectSpeed *= 1.3f;
    if (o[R_GM_YIELD]) s.gmMul *= 1.2f;
    if (o[R_VIRION_RATE]) s.virionInterval /= 1.4f;
    if (o[R_CAPACITY]) s.hostCapacity += 1;
    if (o[R_BURST_REPL]) s.burstReplicate = 2;
    if (o[R_DRIP]) s.hostDrip = true;
    if (o[R_LATENCY]) s.latentInfection = true;
    if (o[R_POLYMERASE]) { s.epMul *= 1.3f; s.alertMul *= 1.15f; }
    if (o[R_HIJACK]) { s.perfectRhythm = true; s.hostCapacity += 2; }

    if (o[T_POTENCY]) s.toxinDamage *= 1.3f;
    if (o[T_RAPID]) s.toxinInterval /= 1.35f;
    if (o[T_SPLIT]) { s.toxinCount = 3; s.toxinCost *= 1.3f; s.toxinSpread = 0.12f; }
    if (o[T_POISON]) s.poisonDps = fmaxf(s.poisonDps, 7.0f);
    if (o[T_CLOUD]) s.cloudChance = 0.3f;
    if (o[T_ACID]) { s.pierce = 2; s.shieldShred = true; }
    if (o[T_CHAIN]) s.chainLysis = 0.4f;
    if (o[T_NOVA]) { s.burstDamage *= 1.6f; s.burstRadius *= 1.3f; }
    if (o[T_NECROTOXIN]) { s.toxinDamage *= 1.6f; s.poisonDps = fmaxf(s.poisonDps, 16.0f); s.toxinCost *= 0.6f; }

    if (o[S_DRIFT_1]) s.adaptMul *= 0.7f;
    if (o[S_CAMO]) s.detectMul *= 0.75f;
    if (o[S_QUIET]) s.injectAlertMul *= 0.4f;
    if (o[S_DECOY]) s.decoys = true;
    if (o[S_DRIFT_2]) { s.adaptMul *= 0.7f; s.adaptDecayMul *= 2.0f; }
    if (o[S_CLOAK]) s.cloakUnlocked = true;
    if (o[S_DAMPEN]) { s.alertDecayMul *= 2.0f; s.alertMul *= 0.8f; }
    if (o[S_GHOST]) { s.cloakDuration *= 2.0f; s.cloakAmbush = 1.0f; s.detectMul *= 0.75f; }

    if (o[W_SWARM_1]) s.minionMax += 4;
    if (o[W_FANGS]) s.minionDamage *= 1.4f;
    if (o[W_SPEED]) { s.minionSpeed *= 1.35f; s.minionHp *= 1.3f; }
    if (o[W_HIVE]) s.hiveLink = true;
    if (o[W_SWARM_2]) { s.minionMax += 8; s.alertMul *= 1.1f; }
    if (o[W_SYMBIOSIS]) s.bacteriaAllies = true;
    if (o[W_EXPLODE]) s.minionExplode = true;
    if (o[W_ELITE]) { s.minionTier = 2; s.minionDamage *= 1.5f; s.minionHp *= 1.5f; }
    if (o[W_OVERMIND]) { s.minionMax += 12; s.minionDamage *= 1.5f; s.swarmCd *= 0.5f; }

    s.synCytoBurst = Mutation_SynergyActive(0);
    s.synViralLoad = Mutation_SynergyActive(1);
    s.synNecrotic = Mutation_SynergyActive(2);
    s.synGhostSwarm = Mutation_SynergyActive(3);
    s.synSymbiotic = Mutation_SynergyActive(4);
    s.synApex = Mutation_SynergyActive(5);
    if (s.synViralLoad) s.minionMax += 2 * (stage + 1);
    if (s.synApex) s.dashDamage *= 2.0f;
    if (stage < 1) s.minionMax = 0;   /* replication needs the Replicator stage */

    s.armor = fminf(s.armor, 0.7f);
    s.glowNodes += s.membraneLayers;
    *st = s;
}
