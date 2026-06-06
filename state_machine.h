#pragma once
#include <Arduino.h>

enum class Scene {
    BLIND_TIMER, CLOCK, MENU, SETUP_TIME, SETUP_BLINDS, START_GAME
};

struct BlindRound {
    uint32_t smallBlind = 0;
    uint32_t bigBlind   = 0;
};

#define MAX_ROUNDS 20

struct GameState {
    uint32_t   blindMinutes  = 20;
    bool       speedMode     = false;
    bool       hasValues     = false;
    BlindRound rounds[MAX_ROUNDS];
    uint8_t    numRounds     = 0;
    bool       running       = false;
    bool       paused        = false;
    uint32_t   pausedAt      = 0;    // millis() when paused
    uint8_t    currentRound  = 0;
    uint32_t   levelStart    = 0;
    bool       overtime      = false;
};

struct AppState {
    Scene   scene          = Scene::SETUP_TIME;
    uint8_t setupRound     = 0;
    bool    enteringBB     = false;
    char    numpadBuffer[16] = {0};
};

extern GameState gGame;
extern AppState  gApp;
