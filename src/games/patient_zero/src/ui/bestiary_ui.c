/*
 * bestiary_ui.c - the Codex. Entries unlock the first time you see a cell
 * type; each has a rotating 3D specimen rendered into its own target.
 */
#include "ui/bestiary_ui.h"
#include "ui/widgets.h"
#include "ui/hud.h"
#include "gfx/render.h"
#include "gfx/shaders.h"
#include "entities/bosses.h"
#include "core/game_state.h"
#include "core/input.h"
#include "core/save.h"
#include "audio/sfx.h"
#include <stdio.h>
#include <string.h>

const CodexDef CODEX[CX_COUNT] = {
    [CX_PLAYER] = { "Your Pathogen", "Pathogen", "An icosahedral capsid of self-assembling protein wrapped around a genome. A virus cannot reproduce on its own: it docks on a host cell's receptors, injects its genome and hijacks the cell's ribosomes to build copies of itself. Every mutation you choose rewrites that genome." },
    [CX_RBC] = { "Erythrocyte", "Bloodstream", "Red blood cells are biconcave discs about 7.5 micrometres across with no nucleus, each packed with roughly 270 million haemoglobin molecules. The dimpled shape maximises surface area for gas exchange and lets them fold through capillaries narrower than themselves. An adult carries about 25 trillion." },
    [CX_PLATELET] = { "Platelet", "Bloodstream", "Cell fragments shed from giant megakaryocytes in the bone marrow. When a vessel is injured they activate, sprout spiky projections and bind together with fibrin into a clot - the same cascade that can seal a vessel in front of you." },
    [CX_SURFACTANT] = { "Surfactant Vesicle", "Lungs", "Lamellar bodies of phospholipid secreted by type II pneumocytes. Surfactant lowers the surface tension of the fluid lining the alveoli so they don't collapse every time you breathe out." },
    [CX_CHYME] = { "Chyme", "Gut", "Partly digested food churned with gastric acid and enzymes. It is driven down the intestine by peristalsis - waves of smooth-muscle contraction that sweep everything before them." },
    [CX_LYMPHGLOB] = { "Chylomicron", "Lymph", "Lipoprotein droplets that ferry absorbed dietary fat through the lymphatic vessels before it ever reaches the bloodstream." },
    [CX_VESICLE] = { "Synaptic Vesicle", "Brain", "Membrane spheres around 40 nanometres wide, loaded with neurotransmitter. When an action potential arrives they fuse with the membrane and release their cargo across the synapse in under a millisecond." },
    [CX_ENDOTHELIAL] = { "Endothelial Cell", "Host cell", "The single flattened layer lining every blood vessel. Endothelium decides what crosses from blood into tissue - and raises molecular alarms when it is infected." },
    [CX_PNEUMOCYTE] = { "Type II Pneumocyte", "Host cell", "Cuboidal cells of the alveolar wall that secrete surfactant and regenerate the delicate type I cells. Respiratory viruses such as influenza and SARS-CoV-2 target them." },
    [CX_ENTEROCYTE] = { "Enterocyte", "Host cell", "Tall columnar cells of the intestinal lining, crowned by a brush border of microvilli that multiplies the absorptive surface. The whole lining is replaced every three to five days." },
    [CX_LYMPHOCYTE] = { "Resting Lymphocyte", "Host cell", "Naive lymphocytes waiting in the node for the one antigen they were made to recognise. Infecting them strikes at the immune system's own command centre - the strategy HIV uses against CD4 T cells." },
    [CX_NEURON] = { "Neuron", "Host cell", "Signalling cells with branching dendrites and a long axon. Mature neurons rarely divide, which makes an infection of the nervous system extremely difficult for the body to clear." },
    [CX_BACTERIA] = { "Commensal Bacterium", "Gut flora", "The gut hosts roughly as many bacterial cells as the body has human ones. Most are harmless or helpful, crowding out pathogens and training the immune system - unless something turns them." },
    [CX_NEUTROPHIL] = { "Neutrophil", "Innate immunity", "The most abundant white blood cell and the first responder, arriving within minutes of an alarm. Its nucleus has 3-5 lobes. It swallows invaders, sprays granule enzymes and, as a last resort, casts out a sticky Neutrophil Extracellular Trap woven from its own DNA." },
    [CX_MACROPHAGE] = { "Macrophage", "Innate immunity", "Literally 'big eater'. Large, long-lived phagocytes that crawl through tissue engulfing debris and microbes whole, digesting them in acidic phagolysosomes and presenting the fragments to T cells. Small pathogens are swallowed outright." },
    [CX_NK] = { "Natural Killer Cell", "Innate immunity", "NK cells inspect other cells for MHC class I, the 'self' badge that infected cells often stop displaying. Anything missing it is killed: perforin punches pores, granzymes slip through and trigger apoptosis." },
    [CX_TCELL] = { "Cytotoxic T Cell", "Adaptive immunity", "CD8+ T cells each recognise one specific antigen through their T-cell receptor. Once activated they multiply into an army that seeks out infected cells and destroys them with perforin bolts." },
    [CX_BCELL] = { "Plasma B Cell", "Adaptive immunity", "Activated B cells mature into plasma cells that secrete thousands of antibodies per second. Their memory-cell siblings persist for decades - the principle behind every vaccine." },
    [CX_ANTIBODY] = { "Antibody (IgG)", "Adaptive immunity", "Y-shaped proteins about 10 nanometres long. They neutralise pathogens by blocking their receptors and opsonise them - coating them so phagocytes and complement destroy them far faster." },
    [CX_BOSS_SENTINEL] = { "Sentinel Macrophage", "Guardian", NULL },
    [CX_BOSS_WARDEN] = { "Alveolar Warden", "Guardian", NULL },
    [CX_BOSS_LEVIATHAN] = { "Peristaltic Leviathan", "Guardian", NULL },
    [CX_BOSS_CITADEL] = { "Germinal Citadel", "Guardian", NULL },
    [CX_BOSS_OVERMIND] = { "Microglial Overmind", "Guardian", NULL },
};

static RenderTexture2D sView;
static bool sViewOk = false;
static float sScroll = 0.0f;

void Codex_Unlock(int id)
{
    if (id < 0 || id >= CX_COUNT || G.profile.codex[id]) return;
    G.profile.codex[id] = 1;
    char t[96];
    snprintf(t, sizeof(t), "New entry: %s", CODEX[id].name);
    HUD_Toast("CODEX UPDATED", t, (Color){ 150, 220, 255, 255 });
}

int Codex_Count(void)
{
    int n = 0;
    for (int i = 0; i < CX_COUNT; i++) n += G.profile.codex[i] ? 1 : 0;
    return n;
}

void Bestiary_Shutdown(void)
{
    if (sViewOk) UnloadRenderTexture(sView);
    sViewOk = false;
}

static void RenderSpecimen(int id)
{
    if (!sViewOk) {
        sView = LoadRenderTexture(420, 420);
        sViewOk = sView.id != 0;
        if (!sViewOk) return;
    }
    float t = G.realTime;
    Camera3D cam = { 0 };
    cam.position = (Vector3){ 0.0f, 0.9f, 3.6f };
    cam.target = (Vector3){ 0, 0, 0 };
    cam.up = (Vector3){ 0, 1, 0 };
    cam.fovy = 40.0f;
    cam.projection = CAMERA_PERSPECTIVE;
    Vector3 lp[2] = { { 2.5f, 2.0f, 2.0f }, { -2.5f, -1.0f, 1.5f } };
    Vector3 lc[2] = { { 0.6f, 1.4f, 1.2f }, { 1.3f, 0.5f, 0.8f } };
    float lr[2] = { 8.0f, 8.0f };
    Shaders_SetFrame(cam.position, t, (Vector3){ 0, 0, 0 }, 0.0f, (Vector3){ -0.4f, -1.0f, -0.5f }, (Vector3){ 1.0f, 0.95f, 0.9f }, (Vector3){ 0.6f, 0.62f, 0.66f });
    Shaders_SetLights(lp, lc, lr, 2);
    BeginTextureMode(sView);
    ClearBackground((Color){ 10, 22, 26, 255 });
    BeginMode3D(cam);
    Render_DrawCodexModel(id, G.ui.codexSpin + t * 0.4f, t);
    EndMode3D();
    EndTextureMode();
}

void Bestiary_Draw(void)
{
    float W = UI_Width(), H = UI_Height();
    UI_Backdrop(0.7f);
    float pw = fminf(W - 40, 1080), ph = H - 60;
    float x = W * 0.5f - pw * 0.5f, y = 30;
    UI_Panel((Rectangle){ x, y, pw, ph }, (Color){ 6, 12, 14, 240 }, UI_Fade(UI_Accent(), 0.5f));
    char buf[64];
    UI_Text("CODEX", x + 24, y + 16, 30, (Color){ 230, 255, 240, 255 });
    snprintf(buf, sizeof(buf), "%d / %d specimens catalogued", Codex_Count(), CX_COUNT);
    UI_Text(buf, x + 150, y + 28, 12, (Color){ 160, 200, 190, 255 });

    /* list */
    Rectangle list = { x + 18, y + 64, 300, ph - 130 };
    UI_Panel(list, (Color){ 10, 18, 20, 200 }, (Color){ 0, 0, 0, 0 });
    if (UI_Hover(list)) sScroll -= UI_Wheel() * 40.0f;
    float rowH = 30;
    float maxScroll = fmaxf(0.0f, (float)CX_COUNT * rowH + 60.0f - list.height);
    sScroll = Clamp(sScroll, 0.0f, maxScroll);
    BeginScissorMode((int)(list.x * UI_Scale()), (int)(list.y * UI_Scale()), (int)(list.width * UI_Scale()), (int)(list.height * UI_Scale()));
    float yy = list.y + 8 - sScroll;
    const char *lastCat = "";
    for (int i = 0; i < CX_COUNT; i++) {
        if (strcmp(lastCat, CODEX[i].category) != 0) {
            lastCat = CODEX[i].category;
            UI_Text(lastCat, list.x + 10, yy + 4, 10, UI_Fade(UI_Accent(), 0.8f));
            yy += 18;
        }
        Rectangle r = { list.x + 6, yy, list.width - 12, rowH - 4 };
        bool known = G.profile.codex[i] != 0;
        bool sel = G.ui.codexSel == i;
        bool hov = UI_Hover(r) && UI_Hover(list);
        if (sel || hov) UI_Panel(r, sel ? UI_Fade(UI_Accent(), 0.2f) : (Color){ 255, 255, 255, 12 }, sel ? UI_Accent() : (Color){ 0, 0, 0, 0 });
        UI_Text(known ? CODEX[i].name : "? ? ?", r.x + 12, r.y + 7, 14, known ? (Color){ 220, 240, 235, 255 } : (Color){ 90, 100, 100, 255 });
        if (hov && UI_MousePressed()) { G.ui.codexSel = i; Sfx_Play(SFX_UI_CLICK, 0.5f, 1.0f); }
        yy += rowH;
    }
    EndScissorMode();

    /* detail */
    int s = G.ui.codexSel;
    if (s < 0 || s >= CX_COUNT) s = 0;
    float dx = x + 340, dw = pw - 360;
    bool known = G.profile.codex[s] != 0;
    float viewSize = fminf(360.0f, ph - 260.0f);
    Rectangle vr = { dx + (dw - viewSize) * 0.5f, y + 64, viewSize, viewSize };
    if (known) {
        RenderSpecimen(s);
        if (sViewOk) {
            Rectangle src = { 0, 0, (float)sView.texture.width, -(float)sView.texture.height };
            Rectangle dst = { vr.x * UI_Scale(), vr.y * UI_Scale(), vr.width * UI_Scale(), vr.height * UI_Scale() };
            DrawTexturePro(sView.texture, src, dst, (Vector2){ 0, 0 }, 0.0f, WHITE);
        }
        UI_RectLines(vr, 1.5f, UI_Fade(UI_Accent(), 0.4f));
        if (UI_Hover(vr) && UI_MouseDown()) G.ui.codexSpin += GetMouseDelta().x * 0.01f;
        UI_Text(CODEX[s].name, dx, vr.y + vr.height + 14, 26, (Color){ 230, 255, 240, 255 });
        UI_Text(CODEX[s].category, dx, vr.y + vr.height + 44, 12, UI_Accent());
        const char *text = CODEX[s].text;
        if (!text && s >= CX_BOSS_SENTINEL) text = BOSSES[s - CX_BOSS_SENTINEL].lore;
        if (text) UI_TextWrapped(text, dx, vr.y + vr.height + 66, dw, 14, 19, (Color){ 205, 222, 216, 255 });
    } else {
        UI_Panel(vr, (Color){ 8, 14, 16, 255 }, (Color){ 50, 60, 60, 255 });
        UI_TextCentered("?", vr.x + vr.width * 0.5f, vr.y + vr.height * 0.5f - 40, 80, (Color){ 50, 60, 60, 255 });
        UI_Text("Not yet encountered", dx, vr.y + vr.height + 14, 22, (Color){ 120, 130, 130, 255 });
        UI_Text("Explore the body to catalogue this specimen.", dx, vr.y + vr.height + 44, 12, (Color){ 100, 110, 110, 255 });
    }
    if (UI_Button(61, (Rectangle){ x + pw - 184, y + ph - 54, 160, 38 }, "BACK", true) || Input_Back()) {
        Save_WriteProfile();
        Sfx_Play(SFX_UI_BACK, 0.6f, 1.0f);
        Game_CloseOverlay();
    }
}
