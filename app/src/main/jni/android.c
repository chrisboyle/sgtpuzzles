/*
 * android.c: Android front end for my puzzle collection.
 */

#include <jni.h>

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <pthread.h>
#include <math.h>

#include <sys/time.h>

#include "puzzles.h"
#include "android.h"

#ifndef JNICALL
#define JNICALL
#endif
#ifndef JNIEXPORT
#define JNIEXPORT
#endif

/* https://github.com/chrisboyle/sgtpuzzles/issues/298 */
char *
stpcpy(char *dst, char const *src)
{
	size_t src_len = strlen(src);
	char *ret = strcpy(dst, src);
	return ret + src_len;
}

const struct game* thegame;

void fatal(const char *fmt, ...)
{
	va_list ap;
	fprintf(stderr, "fatal error: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	exit(1);
}

static frontend *fe = NULL;
static pthread_key_t envKey;
static jobject obj = NULL;

// TODO get rid of this horrible hack
/* This is so that the numerous callers of _() don't have to free strings.
 * Unfortunately they may need a few to be valid at a time (game config
 * dialogs). */
#define GETTEXTED_SIZE 256
#define GETTEXTED_COUNT 32
static char gettexted[GETTEXTED_COUNT][GETTEXTED_SIZE];
static int next_gettexted = 0;

static jobject ARROW_MODE_NONE = NULL,
	ARROW_MODE_ARROWS_ONLY = NULL,
	ARROW_MODE_ARROWS_LEFT_CLICK = NULL,
	ARROW_MODE_ARROWS_LEFT_RIGHT_CLICK = NULL,
	ARROW_MODE_DIAGONALS = NULL;

static jobject gameView = NULL;
static jclass enumCls = NULL;
static jmethodID
	blitterAlloc,
	blitterFree,
	blitterLoad,
	blitterSave,
	changedState,
	purgingStates,
	allowFlash,
	clipRect,
	dialogAddString,
	dialogAddBoolean,
	dialogAddChoices,
	dialogInit,
	dialogShow,
	drawCircle,
	drawLine,
	drawPoly,
	drawText,
	fillRect,
	getBackgroundColour,
	getText,
	postInvalidate,
	requestTimer,
	baosWrite,
	setStatus,
	showToast,
	unClip,
	completed,
	inertiaFollow,
	setKeys,
	byDisplayName,
	backendToString;

void throwIllegalArgumentException(JNIEnv *env, const char* reason) {
	jclass exCls = (*env)->FindClass(env, "java/lang/IllegalArgumentException");
	(*env)->ThrowNew(env, exCls, reason);
	(*env)->DeleteLocalRef(env, exCls);
}

void get_random_seed(void **randseed, int *randseedsize)
{
	struct timeval *tvp = snew(struct timeval);
	gettimeofday(tvp, NULL);
	*randseed = (void *)tvp;
	*randseedsize = sizeof(struct timeval);
}

void frontend_default_colour(frontend *f, float *output)
{
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	jint argb = (*env)->CallIntMethod(env, gameView, getBackgroundColour);
	output[0] = ((float)((argb & 0x00ff0000) >> 16)) / 255.0f;
	output[1] = ((float)((argb & 0x0000ff00) >> 8)) / 255.0f;
	output[2] = ((float)(argb & 0x000000ff)) / 255.0f;
}

void android_status_bar(void *handle, const char *text)
{
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	jstring js = (*env)->NewStringUTF(env, text);
	if( js == NULL ) return;
	(*env)->CallVoidMethod(env, obj, setStatus, js);
	(*env)->DeleteLocalRef(env, js);
}

#define CHECK_DR_HANDLE if ((frontend*)handle != fe) return;

void android_start_draw(void *handle)
{
	CHECK_DR_HANDLE
//	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
}

void android_clip(void *handle, int x, int y, int w, int h)
{
	CHECK_DR_HANDLE
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	(*env)->CallVoidMethod(env, gameView, clipRect, x + fe->ox, y + fe->oy, w, h);
}

void android_unclip(void *handle)
{
	CHECK_DR_HANDLE
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	(*env)->CallVoidMethod(env, gameView, unClip, fe->ox, fe->oy);
}

void android_draw_text(void *handle, int x, int y, int fonttype, int fontsize,
		int align, int colour, const char *text)
{
	CHECK_DR_HANDLE
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	jstring js = (*env)->NewStringUTF(env, text);
	if( js == NULL ) return;
	(*env)->CallVoidMethod(env, gameView, drawText, x + fe->ox, y + fe->oy,
			(fonttype == FONT_FIXED ? 0x10 : 0x0) | align,
			fontsize, colour, js);
	(*env)->DeleteLocalRef(env, js);
}

void android_draw_rect(void *handle, int x, int y, int w, int h, int colour)
{
	CHECK_DR_HANDLE
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	(*env)->CallVoidMethod(env, gameView, fillRect, x + fe->ox, y + fe->oy, w, h, colour);
}

void android_draw_thick_line(void *handle, float thickness, float x1, float y1, float x2, float y2, int colour)
{
	CHECK_DR_HANDLE
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	(*env)->CallVoidMethod(env, gameView, drawLine, thickness, x1 + (float)fe->ox, y1 + (float)fe->oy, x2 + (float)fe->ox, y2 + (float)fe->oy, colour);
}

void android_draw_line(void *handle, int x1, int y1, int x2, int y2, int colour)
{
	android_draw_thick_line(handle, 1.f, (float)x1, (float)y1, (float)x2, (float)y2, colour);
}

void android_draw_thick_poly(void *handle, float thickness, const int *coords, int npoints,
		int fillcolour, int outlinecolour)
{
	CHECK_DR_HANDLE
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	jintArray coordsJava = (*env)->NewIntArray(env, npoints*2);
	if (coordsJava == NULL) return;
	(*env)->SetIntArrayRegion(env, coordsJava, 0, npoints*2, coords);
	(*env)->CallVoidMethod(env, gameView, drawPoly, thickness, coordsJava, fe->ox, fe->oy, outlinecolour, fillcolour);
	(*env)->DeleteLocalRef(env, coordsJava);  // prevent ref table exhaustion on e.g. large Mines grids...
}

void android_draw_poly(void *handle, const int *coords, int npoints,
		int fillcolour, int outlinecolour)
{
	android_draw_thick_poly(handle, 1.f, coords, npoints, fillcolour, outlinecolour);
}

void android_draw_thick_circle(void *handle, float thickness, float cx, float cy, float radius, int fillcolour, int outlinecolour)
{
	CHECK_DR_HANDLE
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	(*env)->CallVoidMethod(env, gameView, drawCircle, thickness, cx+(float)fe->ox, cy+(float)fe->oy, radius, outlinecolour, fillcolour);
}

void android_draw_circle(void *handle, int cx, int cy, int radius, int fillcolour, int outlinecolour)
{
	android_draw_thick_circle(handle, 1.f, (float)cx, (float)cy, (float)radius, fillcolour, outlinecolour);
}

struct blitter {
	int handle, w, h, x, y;
};

blitter *android_blitter_new(void *handle, int w, int h)
{
	blitter *bl = snew(blitter);
	bl->handle = -1;
	bl->w = w;
	bl->h = h;
	return bl;
}

void android_blitter_free(void *handle, blitter *bl)
{
	if (bl->handle != -1) {
		JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
		(*env)->CallVoidMethod(env, gameView, blitterFree, bl->handle);
	}
	sfree(bl);
}

void android_blitter_save(void *handle, blitter *bl, int x, int y)
{
	CHECK_DR_HANDLE
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	if (bl->handle == -1)
		bl->handle = (*env)->CallIntMethod(env, gameView, blitterAlloc, bl->w, bl->h);
	bl->x = x;
	bl->y = y;
	(*env)->CallVoidMethod(env, gameView, blitterSave, bl->handle, x + fe->ox, y + fe->oy);
}

void android_blitter_load(void *handle, blitter *bl, int x, int y)
{
	CHECK_DR_HANDLE
	assert(bl->handle != -1);
	if (x == BLITTER_FROMSAVED && y == BLITTER_FROMSAVED) {
		x = bl->x;
		y = bl->y;
	}
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	(*env)->CallVoidMethod(env, gameView, blitterLoad, bl->handle, x + fe->ox, y + fe->oy);
}

void android_end_draw(void *handle)
{
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	(*env)->CallVoidMethod(env, gameView, postInvalidate);
}

void android_changed_state(void *handle, int can_undo, int can_redo)
{
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	(*env)->CallVoidMethod(env, obj, changedState, can_undo, can_redo);
}

void android_purging_states(void *handle)
{
    JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
    (*env)->CallVoidMethod(env, obj, purgingStates);
}

int allow_flash()
{
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	return (*env)->CallBooleanMethod(env, obj, allowFlash);
}

static char *android_text_fallback(void *handle, const char *const *strings,
			       int nstrings)
{
    /*
     * We assume Android can cope with any UTF-8 likely to be emitted
     * by a puzzle.
     */
    return dupstr(strings[0]);
}

const struct drawing_api android_drawing = {
	android_draw_text,
	android_draw_rect,
	android_draw_line,
	android_draw_poly,
	android_draw_thick_poly,
	android_draw_circle,
	android_draw_thick_circle,
	NULL, // draw_update,
	android_clip,
	android_unclip,
	android_start_draw,
	android_end_draw,
	android_status_bar,
	android_blitter_new,
	android_blitter_free,
	android_blitter_save,
	android_blitter_load,
	NULL, NULL, NULL, NULL, NULL, NULL, /* {begin,end}_{doc,page,puzzle} */
	NULL, NULL,				   /* line_width, line_dotted */
	android_text_fallback,
	android_changed_state,
    android_purging_states,
	android_draw_thick_line,
};

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_keyEvent(JNIEnv *env, jobject _obj, jint x, jint y, jint keyval)
{
	pthread_setspecific(envKey, env);
	if (fe->ox == -1 || keyval < 0) return;
	midend_process_key(fe->me, x - fe->ox, y - fe->oy, keyval);
}

jfloat JNICALL Java_name_boyle_chris_sgtpuzzles_GameView_suggestDensity(JNIEnv *env, jobject _view, jint viewWidth, jint viewHeight)
{
	if (!fe || !fe->me) return 1.f;
	pthread_setspecific(envKey, env);
	int defaultW = INT_MAX, defaultH = INT_MAX;
	midend_reset_tilesize(fe->me);
	midend_size(fe->me, &defaultW, &defaultH, false);
	return max(1.f, min(floor(((double)viewWidth) / defaultW), floor(((double)viewHeight) / defaultH)));
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_resizeEvent(JNIEnv *env, jobject _obj, jint viewWidth, jint viewHeight)
{
	pthread_setspecific(envKey, env);
	if (!fe || !fe->me) return;
	int w = viewWidth, h = viewHeight;
	midend_size(fe->me, &w, &h, true);
	fe->ox = (viewWidth - w) / 2;
	fe->oy = (viewHeight - h) / 2;
	if (gameView) (*env)->CallVoidMethod(env, gameView, unClip, fe->ox, fe->oy);
	midend_force_redraw(fe->me);
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_timerTick(JNIEnv *env, jobject _obj)
{
	if (! fe->timer_active) return;
	pthread_setspecific(envKey, env);
	struct timeval now;
	float elapsed;
	gettimeofday(&now, NULL);
	elapsed = (float)((now.tv_usec - fe->last_time.tv_usec) * 0.000001 +
			(now.tv_sec - fe->last_time.tv_sec));
		midend_timer(fe->me, elapsed);  // may clear timer_active
	fe->last_time = now;
}

void deactivate_timer(frontend *_fe)
{
	if (!fe) return;
	if (fe->timer_active) {
		JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
		(*env)->CallVoidMethod(env, obj, requestTimer, false);
	}
	fe->timer_active = false;
}

void activate_timer(frontend *_fe)
{
	if (!fe) return;
	if (!fe->timer_active) {
		JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
		(*env)->CallVoidMethod(env, obj, requestTimer, true);
		gettimeofday(&fe->last_time, NULL);
	}
	fe->timer_active = true;
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_resetTimerBaseline(JNIEnv *env, jobject _obj)
{
	if (!fe) return;
	gettimeofday(&fe->last_time, NULL);
}

config_item* configItemWithName(JNIEnv *env, jstring js)
{
	const char* name = (*env)->GetStringUTFChars(env, js, NULL);
	config_item* i;
	config_item* ret = NULL;
	for (i = fe->cfg; i->type != C_END; i++) {
		if (!strcmp(name, i->name)) {
			ret = i;
			break;
		}
	}
	(*env)->ReleaseStringUTFChars(env, js, name);
	return ret;
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_configSetString(JNIEnv *env, jobject _obj, jstring name, jstring s)
{
	pthread_setspecific(envKey, env);
	config_item *i = configItemWithName(env, name);
	const char* newval = (*env)->GetStringUTFChars(env, s, NULL);
	sfree(i->u.string.sval);
	i->u.string.sval = dupstr(newval);
	(*env)->ReleaseStringUTFChars(env, s, newval);
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_configSetBool(JNIEnv *env, jobject _obj, jstring name, jint selected)
{
	pthread_setspecific(envKey, env);
	config_item *i = configItemWithName(env, name);
	i->u.boolean.bval = selected != 0 ? true : false;
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_configSetChoice(JNIEnv *env, jobject _obj, jstring name, jint selected)
{
	pthread_setspecific(envKey, env);
	config_item *i = configItemWithName(env, name);
	i->u.choices.selected = selected;
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_solveEvent(JNIEnv *env, jobject _obj)
{
	pthread_setspecific(envKey, env);
	const char *msg = midend_solve(fe->me);
	if (! msg) return;
	jstring js = (*env)->NewStringUTF(env, msg);
	if( js == NULL ) return;
	throwIllegalArgumentException(env, msg);
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_restartEvent(JNIEnv *env, jobject _obj)
{
	pthread_setspecific(envKey, env);
	midend_restart_game(fe->me);
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_configEvent(JNIEnv *env, jobject _obj, jint whichEvent)
{
	pthread_setspecific(envKey, env);
	char *title;
	config_item *i;
	fe->cfg = midend_get_config(fe->me, whichEvent, &title);
	fe->cfg_which = whichEvent;
	jstring js = (*env)->NewStringUTF(env, title);
	if( js == NULL ) return;
	(*env)->CallVoidMethod(env, obj, dialogInit, whichEvent, js);
	for (i = fe->cfg; i->type != C_END; i++) {
		jstring name = NULL;
		if (i->name) {
			name = (*env)->NewStringUTF(env, i->name);
			if (!name) return;
		}
		jstring sval = NULL;
		switch (i->type) {
			case C_STRING:
				if (i->u.string.sval) {
					sval = (*env)->NewStringUTF(env, i->u.string.sval);
					if (!sval) return;
				}
				(*env)->CallVoidMethod(env, obj, dialogAddString, whichEvent, name, sval);
				break;
			case C_CHOICES:
				if (i->u.choices.choicenames) {
					sval = (*env)->NewStringUTF(env, i->u.choices.choicenames);
					if (!sval) return;
				}
				(*env)->CallVoidMethod(env, obj, dialogAddChoices, whichEvent, name, sval, i->u.choices.selected);
				break;
			case C_BOOLEAN: case C_END: default:
				(*env)->CallVoidMethod(env, obj, dialogAddBoolean, whichEvent, name, i->u.boolean.bval);
				break;
		}
		if (name) (*env)->DeleteLocalRef(env, name);
		if (sval) (*env)->DeleteLocalRef(env, sval);
	}
	(*env)->CallVoidMethod(env, obj, dialogShow);
}

jstring JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_configOK(JNIEnv *env, jobject _obj)
{
	pthread_setspecific(envKey, env);
	char *encoded;
	const char *err = midend_config_to_encoded_params(fe->me, fe->cfg, &encoded);

	if (err) {
		throwIllegalArgumentException(env, err);
		return NULL;
	}

	free_cfg(fe->cfg);
	fe->cfg = NULL;

	jstring ret = (*env)->NewStringUTF(env, encoded);
	sfree(encoded);
	return ret;
}

jstring getDescOrSeedFromDialog(JNIEnv *env, jobject _obj, int mode)
{
	/* we must build a fully-specified string (with params) so GameLaunch knows params,
	   and in the case of seed, so the game gen process generates with correct params */
	pthread_setspecific(envKey, env);
	char sep = (mode == CFG_SEED) ? (char)'#' : (char)':';
	char *buf;
	int free_buf = false;
	jstring ret = NULL;
	if (!strchr(fe->cfg[0].u.string.sval, sep)) {
		char *params = midend_get_current_params(fe->me, mode == CFG_SEED);
		size_t plen = strlen(params);
		buf = snewn(plen + strlen(fe->cfg[0].u.string.sval) + 2, char);
		sprintf(buf, "%s%c%s", params, sep, fe->cfg[0].u.string.sval);
		sfree(params);
		free_buf = true;
	} else {
		buf = fe->cfg[0].u.string.sval;
	}
	char *willBeMangled = dupstr(buf);
	const char *error = midend_game_id_int(fe->me, willBeMangled, mode, true);
	sfree(willBeMangled);
	if (!error) ret = (*env)->NewStringUTF(env, buf);
	if (free_buf) sfree(buf);
	if (error) {
		throwIllegalArgumentException(env, error);
	} else {
		free_cfg(fe->cfg);
		fe->cfg = NULL;
	}
	return ret;
}

jstring JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_getFullGameIDFromDialog(JNIEnv *env, jobject _obj)
{
	return getDescOrSeedFromDialog(env, _obj, CFG_DESC);
}

jstring JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_getFullSeedFromDialog(JNIEnv *env, jobject _obj)
{
	return getDescOrSeedFromDialog(env, _obj, CFG_SEED);
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_configCancel(JNIEnv *env, jobject _obj)
{
	pthread_setspecific(envKey, env);
	free_cfg(fe->cfg);
	fe->cfg = NULL;
}

void android_serialise_write(void *ctx, const void *buf, int len)
{
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	if ((*env)->ExceptionCheck(env)) return;
	jbyteArray bytesJava = (*env)->NewByteArray(env, len);
	if (bytesJava == NULL) return;
	(*env)->SetByteArrayRegion(env, bytesJava, 0, len, buf);
	(*env)->CallVoidMethod(env, (jobject)ctx, baosWrite, bytesJava);
	(*env)->DeleteLocalRef(env, bytesJava);
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_serialise(JNIEnv *env, jobject _obj, jobject baos)
{
	if (!fe) return;
	pthread_setspecific(envKey, env);
	midend_serialise(fe->me, android_serialise_write, (void*)baos);
}

static const char* deserialise_readptr = NULL;
static size_t deserialise_readlen = 0;

bool android_deserialise_read(void *ctx, void *buf, int len)
{
	if (len < 0) return false;
	size_t l = min((size_t)len, deserialise_readlen);
	if (l == 0) return len == 0;
	memcpy( buf, deserialise_readptr, l );
	deserialise_readptr += l;
	deserialise_readlen -= l;
	return l == len;
}

jobject deserialiseOrIdentify(frontend *new_fe, jstring s, jboolean identifyOnly) {
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	const char * c = (*env)->GetStringUTFChars(env, s, NULL);
	deserialise_readptr = c;
	deserialise_readlen = strlen(deserialise_readptr);
	char *name;
	const char *error = identify_game(&name, android_deserialise_read, NULL);
	const struct game* whichBackend = NULL;
	jobject backendEnum = NULL;
	if (! error) {
		int i;
		for (i = 0; i < gamecount; i++) {
			if (!strcmp(gamelist[i]->name, name)) {
				whichBackend = gamelist[i];
				backendEnum = (*env)->CallStaticObjectMethod(env, enumCls, byDisplayName, (*env)->NewStringUTF(env, name));
			}
		}
		if (whichBackend == NULL || backendEnum == NULL) error = "Internal error identifying game";
	}
	if (! error && ! identifyOnly) {
		thegame = whichBackend;
		new_fe->me = midend_new(new_fe, whichBackend, &android_drawing, new_fe);
		deserialise_readptr = c;
		deserialise_readlen = strlen(deserialise_readptr);
		error = midend_deserialise(new_fe->me, android_deserialise_read, NULL);
	}
	(*env)->ReleaseStringUTFChars(env, s, c);
	if (error) {
		throwIllegalArgumentException(env, error);
		if (!identifyOnly && new_fe->me) {
			midend_free(new_fe->me);
			new_fe->me = NULL;
		}
	}
	return backendEnum;
}

jobject JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_identifyBackend(JNIEnv *env, jclass type, jstring savedGame)
{
	pthread_setspecific(envKey, env);
	return deserialiseOrIdentify(NULL, savedGame, true);
}

jstring JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_getCurrentParams(JNIEnv *env, jobject _obj)
{
	if (! fe || ! fe->me) return NULL;
	char *params = midend_get_current_params(fe->me, true);
	jstring ret = (*env)->NewStringUTF(env, params);
	sfree(params);
	return ret;
}

jstring JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_htmlHelpTopic(JNIEnv *env, jobject _obj)
{
	//pthread_setspecific(envKey, env);
	return (*env)->NewStringUTF(env, thegame->htmlhelp_topic);
}

void android_completed()
{
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	(*env)->CallVoidMethod(env, obj, completed);
}

void android_inertia_follow(int is_solved)
{
	if (!obj) return;
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	(*env)->CallVoidMethod(env, obj, inertiaFollow, is_solved);
}

void android_toast(const char *msg, int fromPattern)
{
	if (!obj) return;
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	jstring js = (*env)->NewStringUTF(env, msg);
	if( js == NULL ) return;
	(*env)->CallVoidMethod(env, obj, showToast, js, fromPattern);
}

const game* game_by_name(const char* name) {
	int i;
	for (i = 0; i<gamecount; i++) {
		if (!strcmp(name, gamenames[i])) {
			return gamelist[i];
		}
	}
	return NULL;
}

game_params* oriented_params_from_str(const game* my_game, const char* params_str, const char** error) {
	game_params *params = my_game->default_params();
	if (params_str != NULL) {
		if (!strcmp(params_str, "--portrait") || !strcmp(params_str, "--landscape")) {
			unsigned int w, h;
			int pos;
			char * encoded = my_game->encode_params(params, true);
			if (sscanf(encoded, "%ux%u%n", &w, &h, &pos) >= 2) {
				if ((w > h) != (params_str[2] == 'l')) {
					sprintf(encoded, "%ux%u%s", h, w, encoded + pos);
					my_game->decode_params(params, encoded);
				}
			}
			sfree(encoded);
		} else {
			my_game->decode_params(params, params_str);
		}
	}
	const char *our_error = my_game->validate_params(params, true);
	if (our_error) {
		my_game->free_params(params);
		if (error) {
			(*error) = our_error;
		}
		return NULL;
	}
	return params;
}

const game* gameFromEnum(JNIEnv *env, jobject backendEnum)
{
    const jstring backendName = (jstring)(*env)->CallObjectMethod(env, backendEnum, backendToString);
    const char *backend = (*env)->GetStringUTFChars(env, backendName, NULL);
    const game *ret = game_by_name(backend);
    assert(ret != NULL);
    (*env)->ReleaseStringUTFChars(env, backendName, backend);
    return ret;
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_requestKeys(JNIEnv *env, jobject _obj, jobject backendEnum, jstring jParams)
{
	pthread_setspecific(envKey, env);
	if (obj) (*env)->DeleteGlobalRef(env, obj);  // this is called before startPlaying
	obj = (*env)->NewGlobalRef(env, _obj);
	const game *my_game = gameFromEnum(env, backendEnum);
	int nkeys = 0;
	const char *paramsStr = jParams ? (*env)->GetStringUTFChars(env, jParams, NULL) : NULL;
	game_params *params = oriented_params_from_str(my_game, paramsStr, NULL);
	if (jParams) (*env)->ReleaseStringUTFChars(env, jParams, paramsStr);
	if (params) {
		int arrowMode;
		const key_label *keys = midend_request_keys_by_game(&nkeys, my_game, params, &arrowMode);
		char *keyChars = snewn(nkeys + 1, char);
		char *keyCharsIfArrows = snewn(nkeys + 1, char);
		int pos = 0, posIfArrows = 0;
		for (int i = 0; i < nkeys; i++) {
			if (keys[i].needs_arrows) {
				keyCharsIfArrows[posIfArrows++] = (char)keys[i].button;
			} else {
				keyChars[pos++] = (char)keys[i].button;
			}
		}
		keyChars[pos] = '\0';
		keyCharsIfArrows[posIfArrows] = '\0';
		jstring jKeys = (*env)->NewStringUTF(env, keyChars);
		jstring jKeysIfArrows = (*env)->NewStringUTF(env, keyCharsIfArrows);
		jobject jArrowMode = (arrowMode == ANDROID_ARROWS_DIAGONALS) ? ARROW_MODE_DIAGONALS :
				(arrowMode == ANDROID_ARROWS_LEFT_RIGHT) ? ARROW_MODE_ARROWS_LEFT_RIGHT_CLICK :
				(arrowMode == ANDROID_ARROWS_LEFT) ? ARROW_MODE_ARROWS_LEFT_CLICK :
				(arrowMode == ANDROID_ARROWS_ONLY) ? ARROW_MODE_ARROWS_ONLY :
				ARROW_MODE_NONE;
		(*env)->CallVoidMethod(env, obj, setKeys, jKeys, jKeysIfArrows, jArrowMode);
		(*env)->DeleteLocalRef(env, jKeys);
		(*env)->DeleteLocalRef(env, jKeysIfArrows);
		sfree(keyChars);
		sfree(keyCharsIfArrows);
		sfree(params);
	}
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_setCursorVisibility(JNIEnv *env, jobject _obj, jboolean visible)
{
	if (!fe || !fe->me) return;
	pthread_setspecific(envKey, env);
	midend_android_cursor_visibility(fe->me, visible);
}

#if 0
char * get_text(const char *s)
{
	if (!s || ! s[0] || !fe) return (char*)s;  // slightly naughty cast...
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	jstring j = (jstring)(*env)->CallObjectMethod(env, obj, getText, (*env)->NewStringUTF(env, s));
	const char * c = (*env)->GetStringUTFChars(env, j, NULL);
	// TODO get rid of this horrible hack
	char * ret = gettexted[next_gettexted];
	next_gettexted = (next_gettexted + 1) % GETTEXTED_COUNT;
	strncpy(ret, c, GETTEXTED_SIZE);
	ret[GETTEXTED_SIZE-1] = '\0';
	(*env)->ReleaseStringUTFChars(env, j, c);
	return ret;
}
#endif

void startPlayingIntGameID(frontend* new_fe, jstring jsGameID, jobject backendEnum)
{
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	const jstring backendName = (jstring)(*env)->CallObjectMethod(env, backendEnum, backendToString);
	const char * backendChars = (*env)->GetStringUTFChars(env, backendName, NULL);
	const game * g = game_by_name(backendChars);
	(*env)->ReleaseStringUTFChars(env, backendName, backendChars);
	if (!g) {
		throwIllegalArgumentException(env, "Internal error identifying game");
		return;
	}
	thegame = g;
	new_fe->me = midend_new(new_fe, g, &android_drawing, new_fe);
	const char * gameIDjs = (*env)->GetStringUTFChars(env, jsGameID, NULL);
	char * gameID = dupstr(gameIDjs);
	(*env)->ReleaseStringUTFChars(env, jsGameID, gameIDjs);
	const char * error = midend_game_id(new_fe->me, gameID);
	sfree(gameID);
	if (error) {
		throwIllegalArgumentException(env, error);
		return;
	}
	midend_new_game(new_fe->me);
}

jfloatArray JNICALL Java_name_boyle_chris_sgtpuzzles_GameView_getColours(JNIEnv *env, jobject _obj)
{
	int n;
	float* colours;
	colours = midend_colours(fe->me, &n);
	jfloatArray jColours = (*env)->NewFloatArray(env, n*3);
	if (jColours == NULL) return NULL;
	(*env)->SetFloatArrayRegion(env, jColours, 0, n*3, colours);
	return jColours;
}

jobject getPresetInternal(JNIEnv *env, struct preset_menu_entry entry);

jobjectArray getPresetsInternal(JNIEnv *env, struct preset_menu *menu) {
    jclass MenuEntry = (*env)->FindClass(env, "name/boyle/chris/sgtpuzzles/MenuEntry");
    jobjectArray ret = (*env)->NewObjectArray(env, menu->n_entries, MenuEntry, NULL);
    for (int i = 0; i < menu->n_entries; i++) {
        jobject menuItem = getPresetInternal(env, menu->entries[i]);
        (*env)->SetObjectArrayElement(env, ret, i, menuItem);
    }
    return ret;
}

jobject getPresetInternal(JNIEnv *env, const struct preset_menu_entry entry) {
    jclass MenuEntry = (*env)->FindClass(env, "name/boyle/chris/sgtpuzzles/MenuEntry");
    jstring title = (*env)->NewStringUTF(env, entry.title);
    if (entry.submenu) {
        jobject submenu = getPresetsInternal(env, entry.submenu);
        jmethodID newEntryWithSubmenu = (*env)->GetMethodID(env, MenuEntry,  "<init>", "(ILjava/lang/String;[Lname/boyle/chris/sgtpuzzles/MenuEntry;)V");
        return (*env)->NewObject(env, MenuEntry, newEntryWithSubmenu, entry.id, title, submenu);
    } else {
        jstring params = (*env)->NewStringUTF(env, midend_android_preset_menu_get_encoded_params(fe->me, entry.id));
        jmethodID newEntryWithParams = (*env)->GetMethodID(env, MenuEntry,  "<init>", "(ILjava/lang/String;Ljava/lang/String;)V");
        return (*env)->NewObject(env, MenuEntry, newEntryWithParams, entry.id, title, params);
    }
}

jobjectArray JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_getPresets(JNIEnv *env, jobject _obj)
{
	struct preset_menu* menu = midend_get_presets(fe->me, NULL);
	return getPresetsInternal(env, menu);
}

jint JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_getUIVisibility(JNIEnv *env, jobject _obj) {
	return (midend_can_undo(fe->me))
			+ (midend_can_redo(fe->me) << 1)
			+ (thegame->can_configure << 2)
			+ (thegame->can_solve << 3)
			+ (midend_wants_statusbar(fe->me) << 4);
}

void startPlayingInt(JNIEnv *env, jobject _obj, jobject _gameView, jobject backend, jstring saveOrGameID, int isGameID)
{
	pthread_setspecific(envKey, env);
	if (obj) (*env)->DeleteGlobalRef(env, obj);
	obj = (*env)->NewGlobalRef(env, _obj);
	if (gameView) (*env)->DeleteGlobalRef(env, gameView);
	gameView = (*env)->NewGlobalRef(env, _gameView);

	frontend *new_fe = snew(frontend);
	memset(new_fe, 0, sizeof(frontend));
	new_fe->ox = -1;
	if (isGameID) {
		startPlayingIntGameID(new_fe, saveOrGameID, backend);
	} else {
		deserialiseOrIdentify(new_fe, saveOrGameID, false);
		if ((*env)->ExceptionCheck(env)) return;
	}

	if (fe) {
		if (fe->me) midend_free(fe->me);  // might use gameView (e.g. blitters)
		sfree(fe);
	}
	fe = new_fe;
	int x, y;
	x = INT_MAX;
	y = INT_MAX;
	midend_size(fe->me, &x, &y, false);
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_startPlaying(JNIEnv *env, jobject _obj, jobject _gameView, jstring savedGame)
{
	startPlayingInt(env, _obj, _gameView, NULL, savedGame, false);
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_startPlayingGameID(JNIEnv *env, jobject _obj, jobject _gameView, jobject backend, jstring gameID)
{
	startPlayingInt(env, _obj, _gameView, backend, gameID, true);
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_purgeStates(JNIEnv *env, jobject _obj)
{
    if (fe && fe->me) {
        midend_purge_states(fe->me);
    }
}

jboolean JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_isCompletedNow(JNIEnv *env, jobject _obj) {
    return fe && fe->me && midend_status(fe->me) ? true : false;
}

JNIEXPORT jobject JNICALL
Java_name_boyle_chris_sgtpuzzles_GameView_getCursorLocation(JNIEnv *env, jobject _obj) {
    int x, y, w, h;
    if (!fe || !fe->me) {
        return NULL;
    }
    if (!midend_get_cursor_location(fe->me, &x, &y, &w, &h)) {
        return NULL;
    }
    jclass RectF = (*env)->FindClass(env, "android/graphics/RectF");
    jmethodID newRectFWithLTRB = (*env)->GetMethodID(env, RectF, "<init>", "(FFFF)V");
    return (*env)->NewObject(env, RectF, newRectFWithLTRB,
            (float)(fe->ox + x), (float)(fe->oy + y), (float)(fe->ox + x + w), (float)(fe->oy + y + h));
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *jvm, void *reserved)
{
	jclass cls, vcls, arrowModeCls;
	JNIEnv *env;
	if ((*jvm)->GetEnv(jvm, (void **)&env, JNI_VERSION_1_2)) return JNI_ERR;
	pthread_key_create(&envKey, NULL);
	pthread_setspecific(envKey, env);
	cls = (*env)->FindClass(env, "name/boyle/chris/sgtpuzzles/GamePlay");
	vcls = (*env)->FindClass(env, "name/boyle/chris/sgtpuzzles/GameView");
	arrowModeCls = (*env)->FindClass(env, "name/boyle/chris/sgtpuzzles/SmallKeyboard$ArrowMode");
	enumCls = (jclass)(*env)->NewGlobalRef(env, (*env)->FindClass(env, "name/boyle/chris/sgtpuzzles/BackendName"));
	ARROW_MODE_NONE = (*env)->NewGlobalRef(env, (*env)->GetStaticObjectField(env, arrowModeCls,
			(*env)->GetStaticFieldID(env, arrowModeCls, "NO_ARROWS", "Lname/boyle/chris/sgtpuzzles/SmallKeyboard$ArrowMode;")));
	ARROW_MODE_ARROWS_ONLY = (*env)->NewGlobalRef(env, (*env)->GetStaticObjectField(env, arrowModeCls,
			(*env)->GetStaticFieldID(env, arrowModeCls, "ARROWS_ONLY", "Lname/boyle/chris/sgtpuzzles/SmallKeyboard$ArrowMode;")));
	ARROW_MODE_ARROWS_LEFT_CLICK = (*env)->NewGlobalRef(env, (*env)->GetStaticObjectField(env, arrowModeCls,
			(*env)->GetStaticFieldID(env, arrowModeCls, "ARROWS_LEFT_CLICK", "Lname/boyle/chris/sgtpuzzles/SmallKeyboard$ArrowMode;")));
	ARROW_MODE_ARROWS_LEFT_RIGHT_CLICK = (*env)->NewGlobalRef(env, (*env)->GetStaticObjectField(env, arrowModeCls,
			(*env)->GetStaticFieldID(env, arrowModeCls, "ARROWS_LEFT_RIGHT_CLICK", "Lname/boyle/chris/sgtpuzzles/SmallKeyboard$ArrowMode;")));
	ARROW_MODE_DIAGONALS = (*env)->NewGlobalRef(env, (*env)->GetStaticObjectField(env, arrowModeCls,
			(*env)->GetStaticFieldID(env, arrowModeCls, "ARROWS_DIAGONALS", "Lname/boyle/chris/sgtpuzzles/SmallKeyboard$ArrowMode;")));
	blitterAlloc   = (*env)->GetMethodID(env, vcls, "blitterAlloc", "(II)I");
	blitterFree    = (*env)->GetMethodID(env, vcls, "blitterFree", "(I)V");
	blitterLoad    = (*env)->GetMethodID(env, vcls, "blitterLoad", "(III)V");
	blitterSave    = (*env)->GetMethodID(env, vcls, "blitterSave", "(III)V");
	changedState   = (*env)->GetMethodID(env, cls,  "changedState", "(ZZ)V");
	purgingStates  = (*env)->GetMethodID(env, cls,  "purgingStates", "()V");
	allowFlash     = (*env)->GetMethodID(env, cls,  "allowFlash", "()Z");
	clipRect       = (*env)->GetMethodID(env, vcls, "clipRect", "(IIII)V");
	dialogAddString = (*env)->GetMethodID(env, cls,  "dialogAddString", "(ILjava/lang/String;Ljava/lang/String;)V");
    dialogAddBoolean = (*env)->GetMethodID(env, cls,  "dialogAddBoolean", "(ILjava/lang/String;Z)V");
    dialogAddChoices = (*env)->GetMethodID(env, cls,  "dialogAddChoices", "(ILjava/lang/String;Ljava/lang/String;I)V");
	dialogInit     = (*env)->GetMethodID(env, cls,  "dialogInit", "(ILjava/lang/String;)V");
	dialogShow     = (*env)->GetMethodID(env, cls,  "dialogShow", "()V");
	drawCircle     = (*env)->GetMethodID(env, vcls, "drawCircle", "(FFFFII)V");
	drawLine       = (*env)->GetMethodID(env, vcls, "drawLine", "(FFFFFI)V");
	drawPoly       = (*env)->GetMethodID(env, vcls,  "drawPoly", "(F[IIIII)V");
	drawText       = (*env)->GetMethodID(env, vcls, "drawText", "(IIIIILjava/lang/String;)V");
	fillRect       = (*env)->GetMethodID(env, vcls, "fillRect", "(IIIII)V");
	getBackgroundColour = (*env)->GetMethodID(env, vcls, "getDefaultBackgroundColour", "()I");
	getText        = (*env)->GetMethodID(env, cls,  "gettext", "(Ljava/lang/String;)Ljava/lang/String;");
	postInvalidate = (*env)->GetMethodID(env, vcls, "postInvalidateOnAnimation", "()V");
	requestTimer   = (*env)->GetMethodID(env, cls,  "requestTimer", "(Z)V");
	baosWrite      = (*env)->GetMethodID(env, (*env)->FindClass(env, "java/io/ByteArrayOutputStream"),  "write", "([B)V");
	setStatus      = (*env)->GetMethodID(env, cls,  "setStatus", "(Ljava/lang/String;)V");
	showToast      = (*env)->GetMethodID(env, cls,  "showToast", "(Ljava/lang/String;Z)V");
	unClip         = (*env)->GetMethodID(env, vcls, "unClip", "(II)V");
	completed      = (*env)->GetMethodID(env, cls,  "completed", "()V");
	inertiaFollow  = (*env)->GetMethodID(env, cls,  "inertiaFollow", "(Z)V");
	setKeys        = (*env)->GetMethodID(env, cls,  "setKeys",
			"(Ljava/lang/String;Ljava/lang/String;Lname/boyle/chris/sgtpuzzles/SmallKeyboard$ArrowMode;)V");
	byDisplayName  = (*env)->GetStaticMethodID(env, enumCls, "byDisplayName", "(Ljava/lang/String;)Lname/boyle/chris/sgtpuzzles/BackendName;");
	backendToString = (*env)->GetMethodID(env, enumCls, "toString", "()Ljava/lang/String;");

	return JNI_VERSION_1_6;
}
