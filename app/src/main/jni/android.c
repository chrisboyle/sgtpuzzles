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

#include <sys/time.h>

#include "puzzles.h"

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
	return memcpy(dst, src, src_len) + src_len;
}

const struct game* thegame;

void fatal(char *fmt, ...)
{
	va_list ap;
	fprintf(stderr, "fatal error: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	exit(1);
}

struct frontend {
	midend *me;
	int timer_active;
	struct timeval last_time;
	config_item *cfg;
	int cfg_which;
	int ox, oy;
};

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
static jmethodID
	blitterAlloc,
	blitterFree,
	blitterLoad,
	blitterSave,
	changedState,
	clipRect,
	dialogAdd,
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
	serialiseWrite,
	setStatus,
	showToast,
	unClip,
	completed,
	inertiaFollow,
	setKeys;

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

void frontend_default_colour(frontend *fe, float *output)
{
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	jint argb = (*env)->CallIntMethod(env, gameView, getBackgroundColour);
	output[0] = ((argb & 0x00ff0000) >> 16) / 255.0f;
	output[1] = ((argb & 0x0000ff00) >> 8) / 255.0f;
	output[2] = (argb & 0x000000ff) / 255.0f;
}

void android_status_bar(void *handle, char *text)
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
		int align, int colour, char *text)
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
	(*env)->CallVoidMethod(env, gameView, drawLine, thickness, x1 + fe->ox, y1 + fe->oy, x2 + fe->ox, y2 + fe->oy, colour);
}

void android_draw_line(void *handle, int x1, int y1, int x2, int y2, int colour)
{
	android_draw_thick_line(handle, 1.f, x1, y1, x2, y2, colour);
}

void android_draw_thick_poly(void *handle, float thickness, int *coords, int npoints,
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

void android_draw_poly(void *handle, int *coords, int npoints,
		int fillcolour, int outlinecolour)
{
	android_draw_thick_poly(handle, 1.f, coords, npoints, fillcolour, outlinecolour);
}

void android_draw_thick_circle(void *handle, float thickness, float cx, float cy, float radius, int fillcolour, int outlinecolour)
{
	CHECK_DR_HANDLE
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	(*env)->CallVoidMethod(env, gameView, drawCircle, thickness, cx+fe->ox, cy+fe->oy, radius, outlinecolour, fillcolour);
}

void android_draw_circle(void *handle, int cx, int cy, int radius, int fillcolour, int outlinecolour)
{
	android_draw_thick_circle(handle, 1.f, cx, cy, radius, fillcolour, outlinecolour);
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
	midend_size(fe->me, &defaultW, &defaultH, FALSE);
	return max(1.f, min(floor(((float)viewWidth) / defaultW), floor(((float)viewHeight) / defaultH)));
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_resizeEvent(JNIEnv *env, jobject _obj, jint viewWidth, jint viewHeight)
{
	pthread_setspecific(envKey, env);
	if (!fe || !fe->me) return;
	int w = viewWidth, h = viewHeight;
	midend_size(fe->me, &w, &h, TRUE);
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
	elapsed = ((now.tv_usec - fe->last_time.tv_usec) * 0.000001F +
			(now.tv_sec - fe->last_time.tv_sec));
		midend_timer(fe->me, elapsed);  // may clear timer_active
	fe->last_time = now;
}

void deactivate_timer(frontend *_fe)
{
	if (!fe) return;
	if (fe->timer_active) {
		JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
		(*env)->CallVoidMethod(env, obj, requestTimer, FALSE);
	}
	fe->timer_active = FALSE;
}

void activate_timer(frontend *_fe)
{
	if (!fe) return;
	if (!fe->timer_active) {
		JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
		(*env)->CallVoidMethod(env, obj, requestTimer, TRUE);
		gettimeofday(&fe->last_time, NULL);
	}
	fe->timer_active = TRUE;
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
	sfree(i->sval);
	i->sval = dupstr(newval);
	(*env)->ReleaseStringUTFChars(env, s, newval);
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_configSetBool(JNIEnv *env, jobject _obj, jstring name, jint selected)
{
	pthread_setspecific(envKey, env);
	config_item *i = configItemWithName(env, name);
	i->ival = selected != 0 ? TRUE : FALSE;
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_configSetChoice(JNIEnv *env, jobject _obj, jstring name, jint selected)
{
	pthread_setspecific(envKey, env);
	config_item *i = configItemWithName(env, name);
	i->ival = selected;
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_solveEvent(JNIEnv *env, jobject _obj)
{
	pthread_setspecific(envKey, env);
	char *msg = midend_solve(fe->me);
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
			case C_STRING: case C_CHOICES:
				if (i->sval) {
					sval = (*env)->NewStringUTF(env, i->sval);
					if (!sval) return;
				}
				break;
			case C_BOOLEAN: case C_END:
				break;
		}
		(*env)->CallVoidMethod(env, obj, dialogAdd, whichEvent, i->type, name, sval, i->ival);
		if (name) (*env)->DeleteLocalRef(env, name);
		if (sval) (*env)->DeleteLocalRef(env, sval);
	}
	(*env)->CallVoidMethod(env, obj, dialogShow);
}

jstring JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_configOK(JNIEnv *env, jobject _obj)
{
	pthread_setspecific(envKey, env);
	char *encoded;
	char *err = midend_config_to_encoded_params(fe->me, fe->cfg, &encoded);

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
	char sep = (mode == CFG_SEED) ? '#' : ':';
	char *buf;
	int free_buf = FALSE;
	jstring ret = NULL;
	if (!strchr(fe->cfg[0].sval, sep)) {
		char *params = midend_get_current_params(fe->me, mode == CFG_SEED);
		size_t plen = strlen(params);
		buf = snewn(plen + strlen(fe->cfg[0].sval) + 2, char);
		sprintf(buf, "%s%c%s", params, sep, fe->cfg[0].sval);
		sfree(params);
		free_buf = TRUE;
	} else {
		buf = fe->cfg[0].sval;
	}
	char *willBeMangled = dupstr(buf);
	char *error = midend_game_id_int(fe->me, willBeMangled, mode, TRUE);
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

void android_serialise_write(void *ctx, void *buf, int len)
{
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	jbyteArray bytesJava = (*env)->NewByteArray(env, len);
	if (bytesJava == NULL) return;
	(*env)->SetByteArrayRegion(env, bytesJava, 0, len, buf);
	(*env)->CallVoidMethod(env, obj, serialiseWrite, bytesJava);
	(*env)->DeleteLocalRef(env, bytesJava);
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_serialise(JNIEnv *env, jobject _obj)
{
	if (!fe) return;
	pthread_setspecific(envKey, env);
	midend_serialise(fe->me, android_serialise_write, (void*)0);
}

static const char* deserialise_readptr = NULL;
static size_t deserialise_readlen = 0;

int android_deserialise_read(void *ctx, void *buf, int len)
{
	size_t l = min((size_t)len, deserialise_readlen);
	if (l < 0) return FALSE;
	else if (l == 0) return len == 0;
	memcpy( buf, deserialise_readptr, l );
	deserialise_readptr += l;
	deserialise_readlen -= l;
	return l == len;
}

int deserialiseOrIdentify(frontend *new_fe, jstring s, jboolean identifyOnly) {
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	const char * c = (*env)->GetStringUTFChars(env, s, NULL);
	deserialise_readptr = c;
	deserialise_readlen = strlen(deserialise_readptr);
	char *name;
	const char *error = identify_game(&name, android_deserialise_read, NULL);
	int whichBackend = -1;
	if (! error) {
		int i;
		for (i = 0; i < gamecount; i++) {
			if (!strcmp(gamelist[i]->name, name)) {
				whichBackend = i;
			}
		}
		if (whichBackend < 0) error = "Internal error identifying game";
	}
	if (! error && ! identifyOnly) {
		thegame = gamelist[whichBackend];
		new_fe->me = midend_new(new_fe, gamelist[whichBackend], &android_drawing, new_fe);
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
	return whichBackend;
}

jint JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_identifyBackend(JNIEnv *env, jclass c, jstring savedGame)
{
	pthread_setspecific(envKey, env);
	return deserialiseOrIdentify(NULL, savedGame, TRUE);
}

jstring JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_getCurrentParams(JNIEnv *env, jobject _obj)
{
	if (! fe || ! fe->me) return NULL;
	char *params = midend_get_current_params(fe->me, TRUE);
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

game_params* oriented_params_from_str(const game* my_game, const char* params_str, char** error) {
	game_params *params = my_game->default_params();
	if (params_str != NULL) {
		if (!strcmp(params_str, "--portrait") || !strcmp(params_str, "--landscape")) {
			unsigned int w, h;
			int pos;
			char * encoded = my_game->encode_params(params, TRUE);
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
	char *our_error = my_game->validate_params(params, TRUE);
	if (our_error) {
		my_game->free_params(params);
		if (error) {
			(*error) = our_error;
		}
		return NULL;
	}
	return params;
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_requestKeys(JNIEnv *env, jobject _obj, jstring jBackend, jstring jParams)
{
	pthread_setspecific(envKey, env);
	if (obj) (*env)->DeleteGlobalRef(env, obj);  // this is called before startPlaying
	obj = (*env)->NewGlobalRef(env, _obj);
	const char *backend = (*env)->GetStringUTFChars(env, jBackend, NULL);
	const game *my_game = game_by_name(backend);
	assert(my_game != NULL);
	if (my_game->android_request_keys == NULL) {
		android_keys("", ANDROID_ARROWS_LEFT_RIGHT);
	} else {
		const char *paramsStr = jParams ? (*env)->GetStringUTFChars(env, jParams, NULL) : NULL;
		game_params *params = oriented_params_from_str(my_game, paramsStr, NULL);
		if (jParams) (*env)->ReleaseStringUTFChars(env, jParams, paramsStr);
		if (params) {
			my_game->android_request_keys(params);
			sfree(params);
		}
	}
	(*env)->ReleaseStringUTFChars(env, jBackend, backend);
}

void android_keys(const char *keys, int arrowMode)
{
    android_keys2(keys, NULL, arrowMode);
}

void android_keys2(const char *keys, const char *extraKeysIfArrows, int arrowMode)
{
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	jobject jArrowMode = (arrowMode == ANDROID_ARROWS_DIAGONALS) ? ARROW_MODE_DIAGONALS :
			(arrowMode == ANDROID_ARROWS_LEFT_RIGHT) ? ARROW_MODE_ARROWS_LEFT_RIGHT_CLICK :
			(arrowMode == ANDROID_ARROWS_LEFT) ? ARROW_MODE_ARROWS_LEFT_CLICK :
			(arrowMode == ANDROID_ARROWS_ONLY) ? ARROW_MODE_ARROWS_ONLY :
			ARROW_MODE_NONE;
	jstring jKeys = (*env)->NewStringUTF(env, keys ? keys : "");
	jstring jKeysIfArrows = (*env)->NewStringUTF(env, extraKeysIfArrows ? extraKeysIfArrows : "");
	(*env)->CallVoidMethod(env, obj, setKeys, jKeys, jKeysIfArrows, jArrowMode);
	(*env)->DeleteLocalRef(env, jKeys);
	(*env)->DeleteLocalRef(env, jKeysIfArrows);
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_setCursorVisibility(JNIEnv *env, jobject _obj, jboolean visible)
{
	if (!fe || !fe->me) return;
	pthread_setspecific(envKey, env);
	midend_android_cursor_visibility(fe->me, visible);
}

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

void startPlayingIntGameID(frontend* new_fe, jstring jsGameID, jstring backend)
{
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	const char * backendChars = (*env)->GetStringUTFChars(env, backend, NULL);
	const game * g = game_by_name(backendChars);
	(*env)->ReleaseStringUTFChars(env, backend, backendChars);
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

jobjectArray JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_getPresets(JNIEnv *env, jobject _obj)
{
	int n = midend_num_presets(fe->me);
	int i;
	jclass String = (*env)->FindClass(env, "java/lang/String");
	jobjectArray ret = (*env)->NewObjectArray(env, n * 2, String, NULL);
	for (i = 0; i < n; i++) {
		char *name;
		game_params *params;
		char *encoded;
		midend_fetch_preset(fe->me, i, &name, &params, &encoded);
		(*env)->SetObjectArrayElement(env, ret, 2 * i, (*env)->NewStringUTF(env, encoded));
		(*env)->SetObjectArrayElement(env, ret, (2 * i) + 1, (*env)->NewStringUTF(env, name));
	}
	return ret;
}

jint JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_getUIVisibility(JNIEnv *env, jobject _obj) {
	return (midend_can_undo(fe->me))
			+ (midend_can_redo(fe->me) << 1)
			+ (thegame->can_configure << 2)
			+ (thegame->can_solve << 3)
			+ (midend_wants_statusbar(fe->me) << 4);
}

void startPlayingInt(JNIEnv *env, jobject _obj, jobject _gameView, jstring backend, jstring saveOrGameID, int isGameID)
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
		deserialiseOrIdentify(new_fe, saveOrGameID, FALSE);
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
	midend_size(fe->me, &x, &y, FALSE);
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_startPlaying(JNIEnv *env, jobject _obj, jobject _gameView, jstring savedGame)
{
	startPlayingInt(env, _obj, _gameView, NULL, savedGame, FALSE);
}

void JNICALL Java_name_boyle_chris_sgtpuzzles_GamePlay_startPlayingGameID(JNIEnv *env, jobject _obj, jobject _gameView, jstring backend, jstring gameID)
{
	startPlayingInt(env, _obj, _gameView, backend, gameID, TRUE);
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
	clipRect       = (*env)->GetMethodID(env, vcls, "clipRect", "(IIII)V");
	dialogAdd      = (*env)->GetMethodID(env, cls,  "dialogAdd", "(IILjava/lang/String;Ljava/lang/String;I)V");
	dialogInit     = (*env)->GetMethodID(env, cls,  "dialogInit", "(ILjava/lang/String;)V");
	dialogShow     = (*env)->GetMethodID(env, cls,  "dialogShow", "()V");
	drawCircle     = (*env)->GetMethodID(env, vcls, "drawCircle", "(FFFFII)V");
	drawLine       = (*env)->GetMethodID(env, vcls, "drawLine", "(FFFFFI)V");
	drawPoly       = (*env)->GetMethodID(env, vcls,  "drawPoly", "(F[IIIII)V");
	drawText       = (*env)->GetMethodID(env, vcls, "drawText", "(IIIIILjava/lang/String;)V");
	fillRect       = (*env)->GetMethodID(env, vcls, "fillRect", "(IIIII)V");
	getBackgroundColour = (*env)->GetMethodID(env, vcls, "getDefaultBackgroundColour", "()I");
	getText        = (*env)->GetMethodID(env, cls,  "gettext", "(Ljava/lang/String;)Ljava/lang/String;");
	postInvalidate = (*env)->GetMethodID(env, vcls, "postInvalidate", "()V");
	requestTimer   = (*env)->GetMethodID(env, cls,  "requestTimer", "(Z)V");
	serialiseWrite = (*env)->GetMethodID(env, cls,  "serialiseWrite", "([B)V");
	setStatus      = (*env)->GetMethodID(env, cls,  "setStatus", "(Ljava/lang/String;)V");
	showToast      = (*env)->GetMethodID(env, cls,  "showToast", "(Ljava/lang/String;Z)V");
	unClip         = (*env)->GetMethodID(env, vcls, "unClip", "(II)V");
	completed      = (*env)->GetMethodID(env, cls,  "completed", "()V");
	inertiaFollow  = (*env)->GetMethodID(env, cls,  "inertiaFollow", "(Z)V");
	setKeys        = (*env)->GetMethodID(env, cls,  "setKeys",
			"(Ljava/lang/String;Ljava/lang/String;Lname/boyle/chris/sgtpuzzles/SmallKeyboard$ArrowMode;)V");

	return JNI_VERSION_1_6;
}
