/* main.c — Wildlands entry point. Everything else lives in the modules. */
#include "wl/game.h"

int main(int argc, char **argv)
{
    WlGame game;
    if (!wl_game_init(&game, argc, argv)) {
        wl_game_shutdown(&game);
        return 1;
    }

    while (!game.quit && !WindowShouldClose())
        wl_game_run_frame(&game);

    wl_game_shutdown(&game);
    return 0;
}
