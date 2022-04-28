#ifndef PUZZLES_ANDROID_H
#define PUZZLES_ANDROID_H

#include <jni.h>
#include "puzzles.h"

struct frontend {
    midend *me;
    const struct game* thegame;
    JNIEnv *env;
    jobject activityCallbacks;
    jobject viewCallbacks;
    int timer_active;
    struct timeval last_time;
    config_item *cfg;
    int cfg_which;
    int ox, oy;
    int winwidth, winheight;
};

#endif /* PUZZLES_ANDROID_H */
