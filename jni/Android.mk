LOCAL_PATH := $(call my-dir)/..

include $(CLEAR_VARS)

LOCAL_MODULE    := puzzles
LOCAL_CFLAGS    := -DSLOW_SYSTEM -DANDROID -DSTYLUS_BASED -DNO_PRINTING -DCOMBINED
LOCAL_SRC_FILES := android.c blackbox.c bridges.c combi.c cube.c divvy.c dominosa.c drawing.c dsf.c fifteen.c filling.c flip.c galaxies.c grid.c guess.c inertia.c keen.c latin.c laydomino.c lightup.c list.c loopy.c magnets.c malloc.c map.c maxflow.c midend.c mines.c misc.c net.c netslide.c obfusc.c pattern.c pegs.c penrose.c random.c range.c rect.c samegame.c signpost.c singles.c sixteen.c slant.c solo.c tents.c towers.c tree234.c twiddle.c unequal.c untangle.c version.c

include $(BUILD_SHARED_LIBRARY)
