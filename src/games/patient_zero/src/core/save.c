/*
 * save.c - binary profile save. The whole Profile struct (settings, codex,
 * achievements, tutorials, and an in-progress run snapshot taken at region
 * start) is written atomically via a temp file + rename.
 */
#include "core/save.h"
#include "core/settings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

static char sDir[512];
static char sPath[600];

static void MakeDirs(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

void Save_Init(void)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) home = ".";
#if defined(__APPLE__)
    snprintf(sDir, sizeof(sDir), "%s/Library/Application Support/PatientZero", home);
#else
    snprintf(sDir, sizeof(sDir), "%s/.local/share/PatientZero", home);
#endif
    MakeDirs(sDir);
    snprintf(sPath, sizeof(sPath), "%s/profile.dat", sDir);
}

const char *Save_Dir(void) { return sDir; }

bool Save_LoadProfile(void)
{
    Profile p;
    FILE *f = fopen(sPath, "rb");
    bool ok = false;
    if (f) {
        ok = fread(&p, sizeof(p), 1, f) == 1 && p.magic == SAVE_MAGIC && p.version == SAVE_VERSION;
        fclose(f);
    }
    if (ok) {
        G.profile = p;
    } else {
        memset(&G.profile, 0, sizeof(G.profile));
        G.profile.magic = SAVE_MAGIC;
        G.profile.version = SAVE_VERSION;
        Settings_Default(&G.profile.settings);
    }
    G.settings = G.profile.settings;
    return ok;
}

bool Save_WriteProfile(void)
{
    G.profile.magic = SAVE_MAGIC;
    G.profile.version = SAVE_VERSION;
    G.profile.settings = G.settings;
    char tmp[620];
    snprintf(tmp, sizeof(tmp), "%s.tmp", sPath);
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(&G.profile, sizeof(G.profile), 1, f) == 1;
    fclose(f);
    if (ok) ok = rename(tmp, sPath) == 0;
    return ok;
}

void Save_StoreRun(void)
{
    RunSave *r = &G.profile.run;
    memset(r, 0, sizeof(*r));
    r->regionIndex = G.run.regionIndex;
    r->seed = G.run.seed;
    r->mode = (int)G.run.mode;
    r->difficulty = (int)G.run.difficulty;
    r->ngPlus = G.run.ngPlus;
    r->daily = G.run.daily;
    r->stage = G.player.stage;
    r->ep = G.player.ep;
    r->gm = G.player.gm;
    memcpy(r->owned, G.mut.owned, sizeof(r->owned));
    r->gmSpent = G.mut.gmSpent;
    r->respecs = G.mut.respecs;
    memcpy(r->immuneCarry, G.run.immuneCarry, sizeof(r->immuneCarry));
    r->stats = G.run.stats;
    r->wave = G.run.wave;
    G.profile.hasRun = true;
}

bool Save_RestoreRun(void)
{
    if (!G.profile.hasRun) return false;
    RunSave *r = &G.profile.run;
    memset(&G.run, 0, sizeof(G.run));
    G.run.regionIndex = r->regionIndex;
    G.run.seed = r->seed;
    G.run.mode = (RunMode)r->mode;
    G.run.difficulty = (Difficulty)r->difficulty;
    G.run.ngPlus = r->ngPlus;
    G.run.daily = r->daily;
    G.run.stats = r->stats;
    G.run.wave = r->wave;
    memcpy(G.run.immuneCarry, r->immuneCarry, sizeof(G.run.immuneCarry));
    G.run.active = true;
    memset(&G.player, 0, sizeof(G.player));
    G.player.stage = r->stage;
    G.player.ep = r->ep;
    G.player.gm = r->gm;
    memset(&G.mut, 0, sizeof(G.mut));
    memcpy(G.mut.owned, r->owned, sizeof(G.mut.owned));
    G.mut.gmSpent = r->gmSpent;
    G.mut.respecs = r->respecs;
    for (int i = 0; i < MAX_MUTATIONS; i++) if (G.mut.owned[i]) G.mut.count++;
    return true;
}

void Save_ClearRun(void)
{
    G.profile.hasRun = false;
    memset(&G.profile.run, 0, sizeof(G.profile.run));
}
