/* ===============================
 * File: functions.h
 * =============================== */
#ifndef FUNCTIONS_H
#define FUNCTIONS_H
#include <stdbool.h>

#define MAX_H 64
#define MAX_W 128
#define MAX_NAME 32

typedef enum { CELL_EMPTY=0, CELL_WALL=1, CELL_OBS=2 } Cell;
typedef enum { CHAR_GOJO=0, CHAR_TOJI=1 } CharKind;

typedef struct { int y, x; } Pos;

typedef struct {
    bool active; int ownerId; Pos pos;
    unsigned long armedAt, explodeAt, explodingUntil;
    int range;
} Bomb;

typedef struct {
    long cooldownMs;
    unsigned long activeUntil;
    long lastUsedMs;
} Ability;

typedef struct {
    char name[MAX_NAME];
    Pos pos;
    bool alive;
    int bombsPlaced;
    int obstaclesDestroyed;
    Bomb bomb;
    CharKind kind;
    Ability ability;
} Player;

typedef struct {
    int h, w;
    Cell grid[MAX_H][MAX_W];
} Arena;

void run_game(void);
#endif
