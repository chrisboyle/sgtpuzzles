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

static frontend *_fe;
static JNIEnv *env;
static jobject obj;
static int cancelled;

static jobject gameView;
static jmethodID
	addTypeItem,
	blitterAlloc,
	blitterFree,
	blitterLoad,
	blitterSave,
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
	messageBox,
	postInvalidate,
	requestResize,
	requestTimer,
	serialiseWrite,
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
	jstring js = (*env)->NewStringUTF(env, text);
	if( js == NULL ) return;
	(*env)->CallVoidMethod(env, obj, setStatus, js);
	(*env)->DeleteLocalRef(env, js);
}

void android_start_draw(void *handle)
{
	frontend *fe = (frontend *)handle;
	if (!fe) return;
	(*env)->CallVoidMethod(env, gameView, setMargins, fe->ox, fe->oy);
}

void android_clip(void *handle, int x, int y, int w, int h)
{
	frontend *fe = (frontend *)handle;
	(*env)->CallVoidMethod(env, gameView, clipRect, x + fe->ox, y + fe->oy, w, h);
}

void android_unclip(void *handle)
{
	frontend *fe = (frontend *)handle;
	(*env)->CallVoidMethod(env, gameView, unClip, fe->ox, fe->oy);
}

void android_draw_text(void *handle, int x, int y, int fonttype, int fontsize,
		int align, int colour, char *text)
{
	frontend *fe = (frontend *)handle;
	jstring js = (*env)->NewStringUTF(env, text);
	if( js == NULL ) return;
	(*env)->CallVoidMethod(env, gameView, drawText, x + fe->ox, y + fe->oy,
			(fonttype == FONT_FIXED ? 0x10 : 0x0) | align,
			fontsize, colour, js);
	(*env)->DeleteLocalRef(env, js);
}

void android_draw_rect(void *handle, int x, int y, int w, int h, int colour)
{
	frontend *fe = (frontend *)handle;
	(*env)->CallVoidMethod(env, gameView, fillRect, x + fe->ox, y + fe->oy, w, h, colour);
}

void android_draw_line(void *handle, int x1, int y1, int x2, int y2, 
		int colour)
{
	frontend *fe = (frontend *)handle;
	(*env)->CallVoidMethod(env, gameView, drawLine, x1 + fe->ox, y1 + fe->oy, x2 + fe->ox, y2 + fe->oy, colour);
}

void android_draw_poly(void *handle, int *coords, int npoints,
		int fillcolour, int outlinecolour)
{
	frontend *fe = (frontend *)handle;
	jintArray coordsj = (*env)->NewIntArray(env, npoints*2);
	if (coordsj == NULL) return;
	(*env)->SetIntArrayRegion(env, coordsj, 0, npoints*2, coords);
	(*env)->CallVoidMethod(env, obj, drawPoly, coordsj, fe->ox, fe->oy, outlinecolour, fillcolour);
	(*env)->DeleteLocalRef(env, coordsj);  // prevent ref table exhaustion on e.g. large Mines grids...
}

void android_draw_circle(void *handle, int cx, int cy, int radius,
		 int fillcolour, int outlinecolour)
{
	frontend *fe = (frontend *)handle;
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
	if (bl->handle != -1)
		(*env)->CallVoidMethod(env, gameView, blitterFree, bl->handle);
	sfree(bl);
}

void android_blitter_save(void *handle, blitter *bl, int x, int y)
{
	frontend *fe = (frontend *)handle;	
	if (bl->handle == -1)
	bl->handle = (*env)->CallIntMethod(env, gameView, blitterAlloc, bl->w, bl->h);
	bl->x = x;
	bl->y = y;
	(*env)->CallVoidMethod(env, gameView, blitterSave, bl->handle, x + fe->ox, y + fe->oy);
}

void android_blitter_load(void *handle, blitter *bl, int x, int y)
{
	frontend *fe = (frontend *)handle;
	assert(bl->handle != -1);
	if (x == BLITTER_FROMSAVED && y == BLITTER_FROMSAVED) {
		x = bl->x;
		y = bl->y;
	}
	(*env)->CallVoidMethod(env, gameView, blitterLoad, bl->handle, x + fe->ox, y + fe->oy);
}

void android_end_draw(void *handle)
{
	(*env)->CallVoidMethod(env, gameView, postInvalidate);
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
};

jint Java_name_boyle_chris_sgtpuzzles_SGTPuzzles_keyEvent(JNIEnv *_env, jobject _obj, jint x, jint y, jint keyval)
{
	env = _env;
	frontend *fe = (frontend *)_fe;
	if (fe->ox == -1)
		return 1;
	if (keyval >= 0 &&
		!midend_process_key(fe->me, x - fe->ox, y - fe->oy, keyval))
	return 42;
	return 1;
}

void Java_name_boyle_chris_sgtpuzzles_SGTPuzzles_resizeEvent(JNIEnv *_env, jobject _obj, jint width, jint height)
{
	env = _env;
	frontend *fe = (frontend *)_fe;
	int x, y;
	if (!fe) return;
	x = width;
	y = height;
	midend_size(fe->me, &x, &y, TRUE);
	fe->ox = (width - x) / 2;
	fe->oy = (height - y) / 2;
	midend_force_redraw(fe->me);
}

void Java_name_boyle_chris_sgtpuzzles_SGTPuzzles_timerTick(JNIEnv *_env, jobject _obj)
{
	env = _env;
	frontend *fe = (frontend *)_fe;
	if (fe->timer_active) {
	struct timeval now;
	float elapsed;
	gettimeofday(&now, NULL);
	elapsed = ((now.tv_usec - fe->last_time.tv_usec) * 0.000001F +
			(now.tv_sec - fe->last_time.tv_sec));
		midend_timer(fe->me, elapsed);  // may clear timer_active
	fe->last_time = now;
	}
	return;
}

void deactivate_timer(frontend *fe)
{
	if (fe->timer_active)
		(*env)->CallVoidMethod(env, obj, requestTimer, FALSE);
	fe->timer_active = FALSE;
}

void activate_timer(frontend *fe)
{
	if (!fe->timer_active) {
		(*env)->CallVoidMethod(env, obj, requestTimer, TRUE);
		gettimeofday(&fe->last_time, NULL);
	}
	fe->timer_active = TRUE;
}

void Java_name_boyle_chris_sgtpuzzles_SGTPuzzles_configSetString(JNIEnv *_env, jobject _obj, jint item_ptr, jstring s)
{
	env = _env;
	config_item *i = (config_item *)item_ptr;
	const char* newval = (*env)->GetStringUTFChars(env, s, NULL);
	sfree(i->sval);
	i->sval = dupstr(newval);
	(*env)->ReleaseStringUTFChars(env, s, newval);
}

void Java_name_boyle_chris_sgtpuzzles_SGTPuzzles_configSetBool(JNIEnv *_env, jobject _obj, jint item_ptr, jint selected)
{
	env = _env;
	config_item *i = (config_item *)item_ptr;
	i->ival = selected != 0 ? TRUE : FALSE;
}

void Java_name_boyle_chris_sgtpuzzles_SGTPuzzles_configSetChoice(JNIEnv *_env, jobject _obj, jint item_ptr, jint selected)
{
	env = _env;
	config_item *i = (config_item *)item_ptr;
	i->ival = selected;
}

jint Java_name_boyle_chris_sgtpuzzles_SGTPuzzles_menuKeyEvent(JNIEnv *_env, jobject _obj, jint key)
{
	frontend *fe = (frontend *)_fe;
	env = _env;
	return midend_process_key(fe->me, 0, 0, key) ? 0 : 42;
}

static void resize_fe(frontend *fe)
{
	int x, y;
	if (!fe) return;
	x = INT_MAX;
	y = INT_MAX;
	midend_size(fe->me, &x, &y, FALSE);
	(*env)->CallVoidMethod(env, obj, requestResize, x, y);
}

void Java_name_boyle_chris_sgtpuzzles_SGTPuzzles_presetEvent(JNIEnv *_env, jobject _obj, jint ptr_game_params)
{
	frontend *fe = (frontend *)_fe;
	game_params *params = (game_params *)ptr_game_params;
	env = _env;

	midend_set_params(fe->me, params);
	midend_new_game(fe->me);
	if (cancelled) return;
	resize_fe(fe);
	(*env)->CallVoidMethod(env, obj, tickTypeItem, midend_which_preset(fe->me));
}

void Java_name_boyle_chris_sgtpuzzles_SGTPuzzles_solveEvent(JNIEnv *_env, jobject _obj)
{
	frontend *fe = (frontend *)_fe;
	char *msg;
	env = _env;

	msg = midend_solve(fe->me);
	if (msg) {
		jstring js = (*env)->NewStringUTF(env, msg);
		if( js == NULL ) return;
		jstring js2 = (*env)->NewStringUTF(env, "Error");
		if( js2 == NULL ) return;
		(*env)->CallVoidMethod(env, obj, messageBox, js2, js, 1);
	}
}

void Java_name_boyle_chris_sgtpuzzles_SGTPuzzles_restartEvent(JNIEnv *_env, jobject _obj)
{
	frontend *fe = (frontend *)_fe;
	env = _env;
	midend_restart_game(fe->me);
}

void Java_name_boyle_chris_sgtpuzzles_SGTPuzzles_configEvent(JNIEnv *_env, jobject _obj, jint which)
{
	env = _env;
	frontend *fe = (frontend *)_fe;
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

void Java_name_boyle_chris_sgtpuzzles_SGTPuzzles_configOK(JNIEnv *_env, jobject _obj)
{
	env = _env;
	frontend *fe = (frontend *)_fe;
	char *err;

	err = midend_set_config(fe->me, fe->cfg_which, fe->cfg);
	free_cfg(fe->cfg);

	if (err) {
		jstring js = (*env)->NewStringUTF(env, err);
		if( js == NULL ) return;
		jstring js2 = (*env)->NewStringUTF(env, "Error");
		if( js2 == NULL ) return;
		(*env)->CallVoidMethod(env, obj, messageBox, js2, js, 1);
		return;
	}
	midend_new_game(fe->me);
	if (cancelled) return;
	resize_fe(fe);
	(*env)->CallVoidMethod(env, obj, tickTypeItem, midend_which_preset(fe->me));
}

void Java_name_boyle_chris_sgtpuzzles_SGTPuzzles_configCancel(JNIEnv *_env, jobject _obj)
{
	env = _env;
	frontend *fe = (frontend *)_fe;
	free_cfg(fe->cfg);
}

void android_serialise_write(void *ctx, void *buf, int len)
{
	jbyteArray bytesj = (*env)->NewByteArray(env, len);
	if (bytesj == NULL) return;
	(*env)->SetByteArrayRegion(env, bytesj, 0, len, buf);
	(*env)->CallVoidMethod(env, obj, serialiseWrite, bytesj);
	(*env)->DeleteLocalRef(env, bytesj);
}

void Java_name_boyle_chris_sgtpuzzles_SGTPuzzles_serialise(JNIEnv *_env, jobject _obj)
{
	env = _env;
	if (!_fe) return;
	midend_serialise(_fe->me, android_serialise_write, (void*)0);
}

int android_deserialise_read(void *ctx, void *buf, int len)
{
	int l = min(len, _fe->readlen);
	if (l <= 0) return FALSE;
	memcpy( buf, _fe->readptr, l );
	_fe->readptr += l;
	_fe->readlen -= l;
	return l == len;
}

int android_deserialise(jstring s)
{
	const char * c = (*env)->GetStringUTFChars(env, s, NULL);
	_fe->readptr = c;
	_fe->readlen = strlen(_fe->readptr);
	int ret = (int)midend_deserialise(_fe->me, android_deserialise_read, NULL);
	(*env)->ReleaseStringUTFChars(env, s, c);
	return ret;
}

jint Java_name_boyle_chris_sgtpuzzles_SGTPuzzles_deserialise(JNIEnv *_env, jobject _obj, jstring s)
{
	env = _env;
	return android_deserialise(s);
}

void Java_name_boyle_chris_sgtpuzzles_SGTPuzzles_aboutEvent(JNIEnv *_env, jobject _obj)
{
	char titlebuf[256];
	char textbuf[1024];
	env = _env;

	sprintf(titlebuf, "About %.200s", thegame.name);
	sprintf(textbuf,
			"From Simon Tatham's Portable Puzzle Collection\n\n"
			"%.500s", ver);
	jstring js = (*env)->NewStringUTF(env, titlebuf);
	if( js == NULL ) return;
	jstring js2 = (*env)->NewStringUTF(env, textbuf);
	if( js2 == NULL ) return;
	(*env)->CallVoidMethod(env, obj, messageBox, js, js2, 0);
	return;
}

jstring Java_name_boyle_chris_sgtpuzzles_SGTPuzzles_htmlHelpTopic(JNIEnv *_env, jobject _obj)
{
	env = _env;
	return (*env)->NewStringUTF(env, thegame.htmlhelp_topic);
}

void android_completed()
{
	jstring js = (*env)->NewStringUTF(env, "COMPLETED!");
	if( js == NULL ) return;
	(*env)->CallVoidMethod(env, obj, messageBox, NULL, js, 0);
}

inline int android_cancelled()
{
	return cancelled;
}

void Java_name_boyle_chris_sgtpuzzles_SGTPuzzles_initNative(JNIEnv *_env, jclass cls, jclass vcls)
{
	env = _env;
	addTypeItem    = (*env)->GetMethodID(env, cls,  "addTypeItem", "(ILjava/lang/String;)V");
	blitterAlloc   = (*env)->GetMethodID(env, vcls, "blitterAlloc", "(II)I");
	blitterFree    = (*env)->GetMethodID(env, vcls, "blitterFree", "(I)V");
	blitterLoad    = (*env)->GetMethodID(env, vcls, "blitterLoad", "(III)V");
	blitterSave    = (*env)->GetMethodID(env, vcls, "blitterSave", "(III)V");
	clipRect       = (*env)->GetMethodID(env, vcls, "clipRect", "(IIII)V");
	dialogAdd      = (*env)->GetMethodID(env, cls,  "dialogAdd", "(IILjava/lang/String;Ljava/lang/String;I)V");
	dialogInit     = (*env)->GetMethodID(env, cls,  "dialogInit", "(Ljava/lang/String;)V");
	dialogShow     = (*env)->GetMethodID(env, cls,  "dialogShow", "()V");
	drawCircle     = (*env)->GetMethodID(env, vcls, "drawCircle", "(IIIII)V");
	drawLine       = (*env)->GetMethodID(env, vcls, "drawLine", "(IIIII)V");
	drawPoly       = (*env)->GetMethodID(env, cls,  "drawPoly", "([IIIII)V");
	drawText       = (*env)->GetMethodID(env, vcls, "drawText", "(IIIIILjava/lang/String;)V");
	fillRect       = (*env)->GetMethodID(env, vcls, "fillRect", "(IIIII)V");
	gameStarted    = (*env)->GetMethodID(env, cls,  "gameStarted", "(Ljava/lang/String;I[F)V");
	messageBox     = (*env)->GetMethodID(env, cls,  "messageBox", "(Ljava/lang/String;Ljava/lang/String;I)V");
	postInvalidate = (*env)->GetMethodID(env, vcls, "postInvalidate", "()V");
	requestResize  = (*env)->GetMethodID(env, cls,  "requestResize", "(II)V");
	requestTimer   = (*env)->GetMethodID(env, cls,  "requestTimer", "(Z)V");
	serialiseWrite = (*env)->GetMethodID(env, cls,  "serialiseWrite", "([B)V");
	setMargins     = (*env)->GetMethodID(env, vcls, "setMargins", "(II)V");
	setStatus      = (*env)->GetMethodID(env, cls,  "setStatus", "(Ljava/lang/String;)V");
	tickTypeItem   = (*env)->GetMethodID(env, cls,  "tickTypeItem", "(I)V");
	unClip         = (*env)->GetMethodID(env, vcls, "unClip", "(II)V");
}

void Java_name_boyle_chris_sgtpuzzles_SGTPuzzles_cancel(JNIEnv *_env, jobject _obj)
{
	cancelled = TRUE;
}

void Java_name_boyle_chris_sgtpuzzles_SGTPuzzles_init(JNIEnv *_env, jobject _obj, jobject _gameView, jint whichGame, jstring gameState)
{
	int n;
	float* colours;

	cancelled = FALSE;
	env = _env;
	obj = _obj;
	gameView = (*env)->NewGlobalRef(env, _gameView);

	_fe = snew(frontend);
	_fe->timer_active = FALSE;
	// Android special
	if (whichGame >= 0) {
		thegame = *(gamelist[whichGame]);
	} else {
		// Find out which game the savefile is from
		_fe->me = NULL;  // magic in midend_deserialise
		if (android_deserialise(gameState) != 0) exit(1);
		// thegame is now set
	}
	_fe->me = midend_new(_fe, &thegame, &android_drawing, _fe);
	if( whichGame >= 0 || android_deserialise(gameState) != 0 )
		midend_new_game(_fe->me);
	if (cancelled) return;

	if ((n = midend_num_presets(_fe->me)) > 0) {
		int i;
		for (i = 0; i < n; i++) {
			char *name;
			game_params *params;
			midend_fetch_preset(_fe->me, i, &name, &params);
			(*env)->CallVoidMethod(env, obj, addTypeItem, params, (*env)->NewStringUTF(env, name));
		}
	}

	colours = midend_colours(_fe->me, &n);
	_fe->ox = -1;

	jfloatArray colsj = (*env)->NewFloatArray(env, n*3);
	if (colsj == NULL) return;
	(*env)->SetFloatArrayRegion(env, colsj, 0, n*3, colours);
	(*env)->CallVoidMethod(env, obj, gameStarted,
			(*env)->NewStringUTF(env, thegame.name),
			(thegame.can_configure ? 1 : 0) |
			(midend_wants_statusbar(_fe->me) ? 2 : 0) |
			(thegame.can_solve ? 4 : 0), colsj);
	resize_fe(_fe);

	(*env)->CallVoidMethod(env, obj, tickTypeItem, midend_which_preset(_fe->me));

	// shut down when the VM is resumed.
	//deactivate_timer(_fe);
	//midend_free(_fe->me);
	//return 0;
}
