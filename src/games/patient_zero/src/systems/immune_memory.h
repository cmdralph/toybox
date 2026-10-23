/*
 * immune_memory.h - the adaptive immune system. Every ability you lean on
 * builds a resistance meter; enemies spawn with counters to your favourite
 * tactic, nudging you to vary your play.
 */
#ifndef PZ_IMMUNE_MEMORY_H
#define PZ_IMMUNE_MEMORY_H
#include "core/game.h"

void  Immune_Reset(bool carry);
void  Immune_StoreCarry(void);
void  Immune_Use(ImmuneCat c, float amount);
void  Immune_Update(float dt);
float Immune_Resist(ImmuneCat c);
void  Immune_ApplyToSpawn(Agent *a);
const char *Immune_CatName(ImmuneCat c);
const char *Immune_CounterText(ImmuneCat c);

#endif
