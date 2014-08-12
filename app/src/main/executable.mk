LOCAL_PATH := $(call my-dir)

# Built earlier by Gradle's generated Android.mk
include $(CLEAR_VARS)
LOCAL_MODULE    := libpuzzles-prebuilt
LOCAL_SRC_FILES := $(PUZZLES_PREBUILT_DIR)/$(TARGET_ARCH_ABI)/libpuzzles.so
include $(PREBUILT_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE    := puzzlesgen$(PUZZLESGEN_SUFFIX)
LOCAL_CFLAGS    := -DSLOW_SYSTEM -DANDROID -DSTYLUS_BASED -DNO_PRINTING -DCOMBINED -DEXECUTABLE
LOCAL_SRC_FILES := jni/android-gen.c
LOCAL_SHARED_LIBRARIES := libpuzzles-prebuilt
include $(BUILD_EXECUTABLE)
