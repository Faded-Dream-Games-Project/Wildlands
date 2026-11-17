/* main.c — Wildlands entry point. Everything else lives in the modules. */
#include "wl/game.h"
#include "wl/log.h"

int main(int argc, char **argv)
{
    WlGame game;
    if (!wl_game_init(&game, argc, argv)) {
        wl_game_shutdown(&game);
        return 1;
    }

    while (!game.quit && !WindowShouldClose())
        wl_game_run_frame(&game);
    WL_LOGI("main loop ended (quit flag: %d, window close requested: %d)",
            game.quit ? 1 : 0, WindowShouldClose() ? 1 : 0);

    wl_game_shutdown(&game);
    return game.selftest_failed ? 1 : 0;
}
