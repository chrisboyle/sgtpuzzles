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

struct game thegame;

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
	const char *readptr;
	int readlen;
};

static frontend *fe = NULL;
static pthread_key_t envKey;
static jobject obj = NULL;
static int cancelled;

// TODO get rid of this horrible hack
/* This is so that the numerous callers of _() don't have to free strings.
 * Unfortunately they may need a few to be valid at a time (game config
 * dialogs). */
#define GETTEXTED_SIZE 256
#define GETTEXTED_COUNT 32
static char gettexted[GETTEXTED_COUNT][GETTEXTED_SIZE];
static int next_gettexted = 0;

static jobject gameView = NULL;
static jmethodID
	abortMethod,
	addTypeItem,
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
	gameStarted,
	getText,
	messageBox,
	nativeCrashed,
	postInvalidate,
	requestResize,
	requestTimer,
	serialiseWrite,
	setKeys,
	setMargins,
	setStatus,
	tickTypeItem,
	unClip;

void get_random_seed(void **randseed, int *randseedsize)
{
	struct timeval *tvp = snew(struct timeval);
	gettimeofday(tvp, NULL);
	*randseed = (void *)tvp;
	*randseedsize = sizeof(struct timeval);
}

void frontend_default_colour(frontend *fe, float *output)
{
	output[0] = output[1]= output[2] = 0.8f;
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
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	(*env)->CallVoidMethod(env, gameView, setMargins, fe->ox, fe->oy);
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

void android_draw_line(void *handle, int x1, int y1, int x2, int y2, 
		int colour)
{
	CHECK_DR_HANDLE
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	(*env)->CallVoidMethod(env, gameView, drawLine, x1 + fe->ox, y1 + fe->oy, x2 + fe->ox, y2 + fe->oy, colour);
}

void android_draw_poly(void *handle, int *coords, int npoints,
		int fillcolour, int outlinecolour)
{
	CHECK_DR_HANDLE
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	jintArray coordsj = (*env)->NewIntArray(env, npoints*2);
	if (coordsj == NULL) return;
	(*env)->SetIntArrayRegion(env, coordsj, 0, npoints*2, coords);
	(*env)->CallVoidMethod(env, obj, drawPoly, coordsj, fe->ox, fe->oy, outlinecolour, fillcolour);
	(*env)->DeleteLocalRef(env, coordsj);  // prevent ref table exhaustion on e.g. large Mines grids...
}

void android_draw_circle(void *handle, int cx, int cy, int radius,
		 int fillcolour, int outlinecolour)
{
	CHECK_DR_HANDLE
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	(*env)->CallVoidMethod(env, gameView, drawCircle, cx+fe->ox, cy+fe->oy, radius, outlinecolour, fillcolour);
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
	android_draw_circle,
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
};

void JNICALL keyEvent(JNIEnv *env, jobject _obj, jint x, jint y, jint keyval)
{
	pthread_setspecific(envKey, env);
	if (fe->ox == -1 || keyval < 0) return;
	midend_process_key(fe->me, x - fe->ox, y - fe->oy, keyval);
}

void JNICALL resizeEvent(JNIEnv *env, jobject _obj, jint width, jint height)
{
	pthread_setspecific(envKey, env);
	int x, y;
	if (!fe || !fe->me) return;
	x = width;
	y = height;
	midend_size(fe->me, &x, &y, TRUE);
	fe->ox = (width - x) / 2;
	fe->oy = (height - y) / 2;
	midend_force_redraw(fe->me);
}

void JNICALL timerTick(JNIEnv *env, jobject _obj)
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
	if (fe->timer_active) {
		JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
		(*env)->CallVoidMethod(env, obj, requestTimer, FALSE);
	}
	fe->timer_active = FALSE;
}

void activate_timer(frontend *_fe)
{
	if (!fe->timer_active) {
		JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
		(*env)->CallVoidMethod(env, obj, requestTimer, TRUE);
		gettimeofday(&fe->last_time, NULL);
	}
	fe->timer_active = TRUE;
}

void JNICALL configSetString(JNIEnv *env, jobject _obj, jint item_ptr, jstring s)
{
	pthread_setspecific(envKey, env);
	config_item *i = (config_item *)item_ptr;
	const char* newval = (*env)->GetStringUTFChars(env, s, NULL);
	sfree(i->sval);
	i->sval = dupstr(newval);
	(*env)->ReleaseStringUTFChars(env, s, newval);
}

void JNICALL configSetBool(JNIEnv *env, jobject _obj, jint item_ptr, jint selected)
{
	pthread_setspecific(envKey, env);
	config_item *i = (config_item *)item_ptr;
	i->ival = selected != 0 ? TRUE : FALSE;
}

void JNICALL configSetChoice(JNIEnv *env, jobject _obj, jint item_ptr, jint selected)
{
	pthread_setspecific(envKey, env);
	config_item *i = (config_item *)item_ptr;
	i->ival = selected;
}

static void resize_fe()
{
	int x, y;
	if (!fe || !fe->me) return;
	x = INT_MAX;
	y = INT_MAX;
	midend_size(fe->me, &x, &y, FALSE);
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	(*env)->CallVoidMethod(env, obj, requestResize, x, y);
}

void JNICALL presetEvent(JNIEnv *env, jobject _obj, jint ptr_game_params)
{
	pthread_setspecific(envKey, env);
	game_params *params = (game_params *)ptr_game_params;

	midend_set_params(fe->me, params);
	midend_new_game(fe->me);
	if (cancelled) return;
	resize_fe();
	(*env)->CallVoidMethod(env, obj, tickTypeItem, midend_which_preset(fe->me));
}

void JNICALL solveEvent(JNIEnv *env, jobject _obj)
{
	pthread_setspecific(envKey, env);
	char *msg = midend_solve(fe->me);
	if (! msg) return;
	jstring js = (*env)->NewStringUTF(env, msg);
	if( js == NULL ) return;
	jstring js2 = (*env)->NewStringUTF(env, _("Error"));
	if( js2 == NULL ) return;
	(*env)->CallVoidMethod(env, obj, messageBox, js2, js, 1);
}

void JNICALL restartEvent(JNIEnv *env, jobject _obj)
{
	pthread_setspecific(envKey, env);
	midend_restart_game(fe->me);
}

void JNICALL configEvent(JNIEnv *env, jobject _obj, jint which)
{
	pthread_setspecific(envKey, env);
	char *title;
	config_item *i;
	(*env)->CallVoidMethod(env, obj, tickTypeItem, midend_which_preset(fe->me));
	fe->cfg = midend_get_config(fe->me, which, &title);
	fe->cfg_which = which;
	jstring js = (*env)->NewStringUTF(env, title);
	if( js == NULL ) return;
	(*env)->CallVoidMethod(env, obj, dialogInit, js);
	for (i = fe->cfg; i->type != C_END; i++) {
		jstring js2 = i->name ? (*env)->NewStringUTF(env, i->name) : NULL;
		if( i->name && js2 == NULL ) return;
		jstring js3 = i->sval ? (*env)->NewStringUTF(env, i->sval) : NULL;
		if( i->sval && js3 == NULL ) return;
		(*env)->CallVoidMethod(env, obj, dialogAdd, (int)i, i->type, js2, js3, i->ival);
		if( i->name ) (*env)->DeleteLocalRef(env, js2);
		if( i->sval ) (*env)->DeleteLocalRef(env, js3);
	}
	(*env)->CallVoidMethod(env, obj, dialogShow);
}

void JNICALL configOK(JNIEnv *env, jobject _obj)
{
	pthread_setspecific(envKey, env);
	char *err = midend_set_config(fe->me, fe->cfg_which, fe->cfg);
	free_cfg(fe->cfg);
	fe->cfg = NULL;

	if (err) {
		jstring js = (*env)->NewStringUTF(env, err);
		if( js == NULL ) return;
		jstring js2 = (*env)->NewStringUTF(env, _("Error"));
		if( js2 == NULL ) return;
		(*env)->CallVoidMethod(env, obj, messageBox, js2, js, 1);
		return;
	}
	midend_new_game(fe->me);
	if (cancelled) return;
	resize_fe();
	(*env)->CallVoidMethod(env, obj, tickTypeItem, midend_which_preset(fe->me));
}

void JNICALL configCancel(JNIEnv *env, jobject _obj)
{
	pthread_setspecific(envKey, env);
	free_cfg(fe->cfg);
	fe->cfg = NULL;
}

void android_serialise_write(void *ctx, void *buf, int len)
{
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	jbyteArray bytesj = (*env)->NewByteArray(env, len);
	if (bytesj == NULL) return;
	(*env)->SetByteArrayRegion(env, bytesj, 0, len, buf);
	(*env)->CallVoidMethod(env, obj, serialiseWrite, bytesj);
	(*env)->DeleteLocalRef(env, bytesj);
}

void JNICALL serialise(JNIEnv *env, jobject _obj)
{
	if (!fe) return;
	pthread_setspecific(envKey, env);
	midend_serialise(fe->me, android_serialise_write, (void*)0);
}

int android_deserialise_read(void *ctx, void *buf, int len)
{
	int l = min(len, fe->readlen);
	if (l <= 0) return FALSE;
	memcpy( buf, fe->readptr, l );
	fe->readptr += l;
	fe->readlen -= l;
	return l == len;
}

const char* android_deserialise(jstring s)
{
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	const char * c = (*env)->GetStringUTFChars(env, s, NULL);
	fe->readptr = c;
	fe->readlen = strlen(fe->readptr);
	const char * ret = midend_deserialise(fe->me, android_deserialise_read, NULL);
	(*env)->ReleaseStringUTFChars(env, s, c);
	return ret;
}

jstring JNICALL htmlHelpTopic(JNIEnv *env, jobject _obj)
{
	//pthread_setspecific(envKey, env);
	return (*env)->NewStringUTF(env, thegame.htmlhelp_topic);
}

void android_completed()
{
	android_toast(_("COMPLETED!"), 0);
}

void android_toast(const char *msg, int fromPattern)
{
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	jstring js = (*env)->NewStringUTF(env, msg);
	if( js == NULL ) return;
	(*env)->CallVoidMethod(env, obj, messageBox, NULL, js, 0, fromPattern);
}

inline int android_cancelled()
{
	return cancelled;
}

void android_keys(const char *keys, int arrowMode)
{
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	(*env)->CallVoidMethod(env, obj, setKeys, (*env)->NewStringUTF(env, keys), arrowMode);
}

char * get_text(const char *s)
{
	if (!s || ! s[0]) return (char*)s;  // slightly naughty cast...
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

static struct sigaction old_sa[NSIG];

void android_sigaction(int signal, siginfo_t *info, void *reserved)
{
	JNIEnv *env = (JNIEnv*)pthread_getspecific(envKey);
	(*env)->CallVoidMethod(env, obj, nativeCrashed);
	old_sa[signal].sa_handler(signal);
}

void cancel(JNIEnv *env, jobject _obj)
{
	//pthread_setspecific(envKey, env);
	cancelled = TRUE;
}

void crashMeHarder(JNIEnv *env, jobject _obj)
{
	//pthread_setspecific(envKey, env);
	// Dear debuggerd, please give me a native stack trace in logcat. And a pony.
	abort();
}

void init(JNIEnv *env, jobject _obj, jobject _gameView, jint whichGame, jstring gameState)
{
	int n;
	float* colours;
	pthread_setspecific(envKey, env);

	cancelled = FALSE;
	if (fe) {
		if (fe->me) midend_free(fe->me);  // might use gameView (e.g. blitters)
		sfree(fe);
	}
	fe = snew(frontend);
	memset(fe, 0, sizeof(frontend));
	fe->timer_active = FALSE;
	if (obj) (*env)->DeleteGlobalRef(env, obj);
	obj = (*env)->NewGlobalRef(env, _obj);
	if (gameView) (*env)->DeleteGlobalRef(env, gameView);
	gameView = (*env)->NewGlobalRef(env, _gameView);

	// Android special
	if (whichGame >= 0) {
		thegame = *(gamelist[whichGame]);
	} else {
		// Find out which game the savefile is from
		fe->me = NULL;  // magic in midend_deserialise
		const char *reason = android_deserialise(gameState);
		if (reason) {
			(*env)->CallVoidMethod(env, obj, abortMethod, (*env)->NewStringUTF(env, reason));
			return;
		}
		// thegame is now set
	}
	fe->me = midend_new(fe, &thegame, &android_drawing, fe);
	if( whichGame >= 0 ) {
		midend_new_game(fe->me);
	} else {
		const char *reason = android_deserialise(gameState);
		if (reason) {
			(*env)->CallVoidMethod(env, obj, abortMethod, (*env)->NewStringUTF(env, reason));
			midend_free(fe->me);
			fe->me = NULL;
			return;
		}
	}
	if (cancelled) return;

	if ((n = midend_num_presets(fe->me)) > 0) {
		int i;
		for (i = 0; i < n; i++) {
			char *name;
			game_params *params;
			midend_fetch_preset(fe->me, i, &name, &params);
			(*env)->CallVoidMethod(env, obj, addTypeItem, params, (*env)->NewStringUTF(env, name));
		}
	}

	colours = midend_colours(fe->me, &n);
	fe->ox = -1;

	jfloatArray colsj = (*env)->NewFloatArray(env, n*3);
	if (colsj == NULL) return;
	(*env)->SetFloatArrayRegion(env, colsj, 0, n*3, colours);
	(*env)->CallVoidMethod(env, obj, gameStarted,
			(*env)->NewStringUTF(env, thegame.name), thegame.can_configure,
			midend_wants_statusbar(fe->me), thegame.can_solve, colsj);
	resize_fe();

	(*env)->CallVoidMethod(env, obj, tickTypeItem, midend_which_preset(fe->me));
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *jvm, void *reserved)
{
	jclass cls, vcls;
	JNIEnv *env;
	if ((*jvm)->GetEnv(jvm, (void **)&env, JNI_VERSION_1_2)) return JNI_ERR;
	pthread_key_create(&envKey, NULL);
	pthread_setspecific(envKey, env);
	cls = (*env)->FindClass(env, "name/boyle/chris/sgtpuzzles/SGTPuzzles");
	vcls = (*env)->FindClass(env, "name/boyle/chris/sgtpuzzles/GameView");
	abortMethod    = (*env)->GetMethodID(env, cls,  "abort", "(Ljava/lang/String;)V");
	addTypeItem    = (*env)->GetMethodID(env, cls,  "addTypeItem", "(ILjava/lang/String;)V");
	blitterAlloc   = (*env)->GetMethodID(env, vcls, "blitterAlloc", "(II)I");
	blitterFree    = (*env)->GetMethodID(env, vcls, "blitterFree", "(I)V");
	blitterLoad    = (*env)->GetMethodID(env, vcls, "blitterLoad", "(III)V");
	blitterSave    = (*env)->GetMethodID(env, vcls, "blitterSave", "(III)V");
	changedState   = (*env)->GetMethodID(env, cls,  "changedState", "(ZZ)V");
	clipRect       = (*env)->GetMethodID(env, vcls, "clipRect", "(IIII)V");
	dialogAdd      = (*env)->GetMethodID(env, cls,  "dialogAdd", "(IILjava/lang/String;Ljava/lang/String;I)V");
	dialogInit     = (*env)->GetMethodID(env, cls,  "dialogInit", "(Ljava/lang/String;)V");
	dialogShow     = (*env)->GetMethodID(env, cls,  "dialogShow", "()V");
	drawCircle     = (*env)->GetMethodID(env, vcls, "drawCircle", "(IIIII)V");
	drawLine       = (*env)->GetMethodID(env, vcls, "drawLine", "(IIIII)V");
	drawPoly       = (*env)->GetMethodID(env, cls,  "drawPoly", "([IIIII)V");
	drawText       = (*env)->GetMethodID(env, vcls, "drawText", "(IIIIILjava/lang/String;)V");
	fillRect       = (*env)->GetMethodID(env, vcls, "fillRect", "(IIIII)V");
	gameStarted    = (*env)->GetMethodID(env, cls,  "gameStarted", "(Ljava/lang/String;ZZZ[F)V");
	getText        = (*env)->GetMethodID(env, cls,  "gettext", "(Ljava/lang/String;)Ljava/lang/String;");
	messageBox     = (*env)->GetMethodID(env, cls,  "messageBox", "(Ljava/lang/String;Ljava/lang/String;IZ)V");
	nativeCrashed  = (*env)->GetMethodID(env, cls,  "nativeCrashed", "()V");
	postInvalidate = (*env)->GetMethodID(env, vcls, "postInvalidate", "()V");
	requestResize  = (*env)->GetMethodID(env, cls,  "requestResize", "(II)V");
	requestTimer   = (*env)->GetMethodID(env, cls,  "requestTimer", "(Z)V");
	serialiseWrite = (*env)->GetMethodID(env, cls,  "serialiseWrite", "([B)V");
	setKeys        = (*env)->GetMethodID(env, cls,  "setKeys", "(Ljava/lang/String;I)V");
	setMargins     = (*env)->GetMethodID(env, vcls, "setMargins", "(II)V");
	setStatus      = (*env)->GetMethodID(env, cls,  "setStatus", "(Ljava/lang/String;)V");
	tickTypeItem   = (*env)->GetMethodID(env, cls,  "tickTypeItem", "(I)V");
	unClip         = (*env)->GetMethodID(env, vcls, "unClip", "(II)V");

	JNINativeMethod methods[] = {
		{ "keyEvent", "(III)V", keyEvent },
		{ "resizeEvent", "(II)V", resizeEvent },
		{ "timerTick", "()V", timerTick },
		{ "configSetString", "(ILjava/lang/String;)V", configSetString },
		{ "configSetBool", "(II)V", configSetBool },
		{ "configSetChoice", "(II)V", configSetChoice },
		{ "presetEvent", "(I)V", presetEvent },
		{ "solveEvent", "()V", solveEvent },
		{ "restartEvent", "()V", restartEvent },
		{ "configEvent", "(I)V", configEvent },
		{ "configOK", "()V", configOK },
		{ "configCancel", "()V", configCancel },
		{ "serialise", "()V", serialise },
		{ "htmlHelpTopic", "()Ljava/lang/String;", htmlHelpTopic },
		{ "cancel", "()V", cancel },
		{ "crashMeHarder", "()V", crashMeHarder },
		{ "init", "(Lname/boyle/chris/sgtpuzzles/GameView;ILjava/lang/String;)V", init },
	};
	(*env)->RegisterNatives(env, cls, methods, sizeof(methods)/sizeof(JNINativeMethod));

	// Try to catch crashes...
	struct sigaction handler;
	memset(&handler, 0, sizeof(sigaction));
	handler.sa_sigaction = android_sigaction;
	handler.sa_flags = SA_RESETHAND;
#define CATCHSIG(X) sigaction(X, &handler, &old_sa[X])
	CATCHSIG(SIGILL);
	CATCHSIG(SIGABRT);
	CATCHSIG(SIGBUS);
	CATCHSIG(SIGFPE);
	CATCHSIG(SIGSEGV);
	CATCHSIG(SIGSTKFLT);
	CATCHSIG(SIGPIPE);

	return JNI_VERSION_1_2;
}
