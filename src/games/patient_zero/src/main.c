/*
 * PATIENT ZERO - a microbial survival odyssey.
 *
 * You are a pathogen. Infect host cells, replicate, mutate and evolve while
 * an adaptive immune system learns to fight back, through five regions of
 * the human body. C11 + raylib, zero external assets.
 *
 * Command line:
 *   --autotest        run the scripted smoke test (screenshots + stats)
 *   --windowed WxH    override the window size for this launch
 */
#include "core/game_state.h"
#include "core/game.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    bool autotest = false;
    int ww = 0, wh = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--autotest") == 0) autotest = true;
        else if (strcmp(argv[i], "--windowed") == 0 && i + 1 < argc) {
            if (sscanf(argv[++i], "%dx%d", &ww, &wh) != 2) { ww = wh = 0; }
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("%s %s\n  --autotest       scripted smoke test\n  --windowed WxH   window size override\n", PZ_TITLE, PZ_VERSION);
            return 0;
        }
    }
    SetTraceLogLevel(autotest ? LOG_INFO : LOG_WARNING);
    G.debug.autotest = autotest;
    Game_Init();
    if (ww > 0 && wh > 0) SetWindowSize(ww, wh);
    while (!G.quit && !WindowShouldClose()) Game_Frame();
    Game_Shutdown();
    return 0;
}
