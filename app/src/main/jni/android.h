#ifndef PUZZLES_ANDROID_H
#define PUZZLES_ANDROID_H

#include "puzzles.h"

struct frontend {
    midend *me;
    int timer_active;
    struct timeval last_time;
    config_item *cfg;
    int cfg_which;
    int ox, oy;
};

#endif /* PUZZLES_ANDROID_H */
