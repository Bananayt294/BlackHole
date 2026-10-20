#pragma once
#include <SDL3/SDL_main.h>
#include "Application.h"

int main(int argc, char* argv[]) {
    Application app;
    if (app.initialize()) {
        app.run();
    }
    else {
        app.shutdown();
    }
    return 0;
}