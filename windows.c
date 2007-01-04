/*
 * windows.c: Windows front end for my puzzle collection.
 */

#include <windows.h>
#include <commctrl.h>
#ifndef NO_HTMLHELP
#include <htmlhelp.h>
#endif /* NO_HTMLHELP */

#include <stdio.h>
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>

#include "puzzles.h"

#define IDM_NEW       0x0010
#define IDM_RESTART   0x0020
#define IDM_UNDO      0x0030
#define IDM_REDO      0x0040
#define IDM_COPY      0x0050
#define IDM_SOLVE     0x0060
#define IDM_QUIT      0x0070
#define IDM_CONFIG    0x0080
#define IDM_DESC      0x0090
#define IDM_SEED      0x00A0
#define IDM_HELPC     0x00B0
#define IDM_GAMEHELP  0x00C0
#define IDM_ABOUT     0x00D0
#define IDM_SAVE      0x00E0
#define IDM_LOAD      0x00F0
#define IDM_PRINT     0x0100
#define IDM_PRESETS   0x0110

#define HELP_FILE_NAME  "puzzles.hlp"
#define HELP_CNT_NAME   "puzzles.cnt"
#ifndef NO_HTMLHELP
#define CHM_FILE_NAME   "puzzles.chm"
#endif /* NO_HTMLHELP */

#ifndef NO_HTMLHELP
typedef HWND (CALLBACK *htmlhelp_t)(HWND, LPCSTR, UINT, DWORD);
static DWORD html_help_cookie;
static htmlhelp_t htmlhelp;
static HINSTANCE hh_dll;
#endif /* NO_HTMLHELP */
enum { NONE, HLP, CHM } help_type;
char *help_path;
const char *help_topic;
int help_has_contents;

#ifdef DEBUGGING
static FILE *debug_fp = NULL;
static HANDLE debug_hdl = INVALID_HANDLE_VALUE;
static int debug_got_console = 0;

void dputs(char *buf)
{
    DWORD dw;

    if (!debug_got_console) {
	if (AllocConsole()) {
	    debug_got_console = 1;
	    debug_hdl = GetStdHandle(STD_OUTPUT_HANDLE);
	}
    }
    if (!debug_fp) {
	debug_fp = fopen("debug.log", "w");
    }

    if (debug_hdl != INVALID_HANDLE_VALUE) {
	WriteFile(debug_hdl, buf, strlen(buf), &dw, NULL);
    }
    fputs(buf, debug_fp);
    fflush(debug_fp);
}

void debug_printf(char *fmt, ...)
{
    char buf[4096];
    va_list ap;

    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    dputs(buf);
    va_end(ap);
}
#endif

#define WINFLAGS (WS_OVERLAPPEDWINDOW &~ \
		      (WS_THICKFRAME | WS_MAXIMIZEBOX | WS_OVERLAPPED))

static void new_game_size(frontend *fe);

struct font {
    HFONT font;
    int type;
    int size;
};

struct cfg_aux {
    int ctlid;
};

struct blitter {
    HBITMAP bitmap;
    frontend *fe;
    int x, y, w, h;
};

enum { CFG_PRINT = CFG_FRONTEND_SPECIFIC };

struct frontend {
    midend *me;
    HWND hwnd, statusbar, cfgbox;
    HINSTANCE inst;
    HBITMAP bitmap, prevbm;
    HDC hdc;
    COLORREF *colours;
    HBRUSH *brushes;
    HPEN *pens;
    HRGN clip;
    UINT timer;
    DWORD timer_last_tickcount;
    int npresets;
    game_params **presets;
    struct font *fonts;
    int nfonts, fontsize;
    config_item *cfg;
    struct cfg_aux *cfgaux;
    int cfg_which, dlg_done;
    HFONT cfgfont;
    HBRUSH oldbr;
    HPEN oldpen;
    int help_running;
    enum { DRAWING, PRINTING, NOTHING } drawstatus;
    DOCINFO di;
    int printcount, printw, printh, printsolns, printcurr, printcolour;
    float printscale;
    int printoffsetx, printoffsety;
    float printpixelscale;
    int fontstart;
    int linewidth;
    drawing *dr;
};

void fatal(char *fmt, ...)
{
    char buf[2048];
    va_list ap;

    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);

    MessageBox(NULL, buf, "Fatal error", MB_ICONEXCLAMATION | MB_OK);

    exit(1);
}

char *geterrstr(void)
{
    LPVOID lpMsgBuf;
    DWORD dw = GetLastError();
    char *ret;

    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR) &lpMsgBuf,
        0, NULL );

    ret = dupstr(lpMsgBuf);

    LocalFree(lpMsgBuf);

    return ret;
}

void get_random_seed(void **randseed, int *randseedsize)
{
    time_t *tp = snew(time_t);
    time(tp);
    *randseed = (void *)tp;
    *randseedsize = sizeof(time_t);
}

static void win_status_bar(void *handle, char *text)
{
    frontend *fe = (frontend *)handle;

    SetWindowText(fe->statusbar, text);
}

static blitter *win_blitter_new(void *handle, int w, int h)
{
    blitter *bl = snew(blitter);

    memset(bl, 0, sizeof(blitter));
    bl->w = w;
    bl->h = h;
    bl->bitmap = 0;

    return bl;
}

static void win_blitter_free(void *handle, blitter *bl)
{
    if (bl->bitmap) DeleteObject(bl->bitmap);
    sfree(bl);
}

static void blitter_mkbitmap(frontend *fe, blitter *bl)
{
    HDC hdc = GetDC(fe->hwnd);
    bl->bitmap = CreateCompatibleBitmap(hdc, bl->w, bl->h);
    ReleaseDC(fe->hwnd, hdc);
}

/* BitBlt(dstDC, dstX, dstY, dstW, dstH, srcDC, srcX, srcY, dType) */

static void win_blitter_save(void *handle, blitter *bl, int x, int y)
{
    frontend *fe = (frontend *)handle;
    HDC hdc_win, hdc_blit;
    HBITMAP prev_blit;

    assert(fe->drawstatus == DRAWING);

    if (!bl->bitmap) blitter_mkbitmap(fe, bl);

    bl->x = x; bl->y = y;

    hdc_win = GetDC(fe->hwnd);
    hdc_blit = CreateCompatibleDC(hdc_win);
    if (!hdc_blit) fatal("hdc_blit failed: 0x%x", GetLastError());

    prev_blit = SelectObject(hdc_blit, bl->bitmap);
    if (prev_blit == NULL || prev_blit == HGDI_ERROR)
        fatal("SelectObject for hdc_main failed: 0x%x", GetLastError());

    if (!BitBlt(hdc_blit, 0, 0, bl->w, bl->h,
                fe->hdc, x, y, SRCCOPY))
        fatal("BitBlt failed: 0x%x", GetLastError());

    SelectObject(hdc_blit, prev_blit);
    DeleteDC(hdc_blit);
    ReleaseDC(fe->hwnd, hdc_win);
}

static void win_blitter_load(void *handle, blitter *bl, int x, int y)
{
    frontend *fe = (frontend *)handle;
    HDC hdc_win, hdc_blit;
    HBITMAP prev_blit;

    assert(fe->drawstatus == DRAWING);

    assert(bl->bitmap); /* we should always have saved before loading */

    if (x == BLITTER_FROMSAVED) x = bl->x;
    if (y == BLITTER_FROMSAVED) y = bl->y;

    hdc_win = GetDC(fe->hwnd);
    hdc_blit = CreateCompatibleDC(hdc_win);

    prev_blit = SelectObject(hdc_blit, bl->bitmap);

    BitBlt(fe->hdc, x, y, bl->w, bl->h,
           hdc_blit, 0, 0, SRCCOPY);

    SelectObject(hdc_blit, prev_blit);
    DeleteDC(hdc_blit);
    ReleaseDC(fe->hwnd, hdc_win);
}

void frontend_default_colour(frontend *fe, float *output)
{
    DWORD c = GetSysColor(COLOR_MENU); /* ick */

    output[0] = (float)(GetRValue(c) / 255.0);
    output[1] = (float)(GetGValue(c) / 255.0);
    output[2] = (float)(GetBValue(c) / 255.0);
}

static POINT win_transform_point(frontend *fe, int x, int y)
{
    POINT ret;

    assert(fe->drawstatus != NOTHING);

    if (fe->drawstatus == PRINTING) {
	ret.x = (int)(fe->printoffsetx + fe->printpixelscale * x);
	ret.y = (int)(fe->printoffsety + fe->printpixelscale * y);
    } else {
	ret.x = x;
	ret.y = y;
    }

    return ret;
}

static void win_text_colour(frontend *fe, int colour)
{
    assert(fe->drawstatus != NOTHING);

    if (fe->drawstatus == PRINTING) {
	int hatch;
	float r, g, b;
	print_get_colour(fe->dr, colour, &hatch, &r, &g, &b);
	if (fe->printcolour)
	    SetTextColor(fe->hdc, RGB(r * 255, g * 255, b * 255));
	else
	    SetTextColor(fe->hdc,
			 hatch == HATCH_CLEAR ? RGB(255,255,255) : RGB(0,0,0));
    } else {
	SetTextColor(fe->hdc, fe->colours[colour]);
    }
}

static void win_set_brush(frontend *fe, int colour)
{
    HBRUSH br;
    assert(fe->drawstatus != NOTHING);

    if (fe->drawstatus == PRINTING) {
	int hatch;
	float r, g, b;
	print_get_colour(fe->dr, colour, &hatch, &r, &g, &b);

	if (fe->printcolour)
	    br = CreateSolidBrush(RGB(r * 255, g * 255, b * 255));
	else if (hatch == HATCH_SOLID)
	    br = CreateSolidBrush(RGB(0,0,0));
	else if (hatch == HATCH_CLEAR)
	    br = CreateSolidBrush(RGB(255,255,255));
	else
	    br = CreateHatchBrush(hatch == HATCH_BACKSLASH ? HS_FDIAGONAL :
				  hatch == HATCH_SLASH ? HS_BDIAGONAL :
				  hatch == HATCH_HORIZ ? HS_HORIZONTAL :
				  hatch == HATCH_VERT ? HS_VERTICAL :
				  hatch == HATCH_PLUS ? HS_CROSS :
				  /* hatch == HATCH_X ? */ HS_DIAGCROSS,
				  RGB(0,0,0));
    } else {
	br = fe->brushes[colour];
    }
    fe->oldbr = SelectObject(fe->hdc, br);
}

static void win_reset_brush(frontend *fe)
{
    HBRUSH br;

    assert(fe->drawstatus != NOTHING);

    br = SelectObject(fe->hdc, fe->oldbr);
    if (fe->drawstatus == PRINTING)
	DeleteObject(br);
}

static void win_set_pen(frontend *fe, int colour, int thin)
{
    HPEN pen;
    assert(fe->drawstatus != NOTHING);

    if (fe->drawstatus == PRINTING) {
	int hatch;
	float r, g, b;
	int width = thin ? 0 : fe->linewidth;

	print_get_colour(fe->dr, colour, &hatch, &r, &g, &b);
	if (fe->printcolour)
	    pen = CreatePen(PS_SOLID, width,
			    RGB(r * 255, g * 255, b * 255));
	else if (hatch == HATCH_SOLID)
	    pen = CreatePen(PS_SOLID, width, RGB(0, 0, 0));
	else if (hatch == HATCH_CLEAR)
	    pen = CreatePen(PS_SOLID, width, RGB(255,255,255));
	else {
	    assert(!"This shouldn't happen");
	    pen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
	}
    } else {
	pen = fe->pens[colour];
    }
    fe->oldpen = SelectObject(fe->hdc, pen);
}

static void win_reset_pen(frontend *fe)
{
    HPEN pen;

    assert(fe->drawstatus != NOTHING);

    pen = SelectObject(fe->hdc, fe->oldpen);
    if (fe->drawstatus == PRINTING)
	DeleteObject(pen);
}

static void win_clip(void *handle, int x, int y, int w, int h)
{
    frontend *fe = (frontend *)handle;
    POINT p, q;

    if (fe->drawstatus == NOTHING)
	return;

    p = win_transform_point(fe, x, y);
    q = win_transform_point(fe, x+w, y+h);
    IntersectClipRect(fe->hdc, p.x, p.y, q.x, q.y);
}

static void win_unclip(void *handle)
{
    frontend *fe = (frontend *)handle;

    if (fe->drawstatus == NOTHING)
	return;

    SelectClipRgn(fe->hdc, NULL);
}

static void win_draw_text(void *handle, int x, int y, int fonttype,
			  int fontsize, int align, int colour, char *text)
{
    frontend *fe = (frontend *)handle;
    POINT xy;
    int i;

    if (fe->drawstatus == NOTHING)
	return;

    if (fe->drawstatus == PRINTING)
	fontsize = (int)(fontsize * fe->printpixelscale);

    xy = win_transform_point(fe, x, y);

    /*
     * Find or create the font.
     */
    for (i = fe->fontstart; i < fe->nfonts; i++)
        if (fe->fonts[i].type == fonttype && fe->fonts[i].size == fontsize)
            break;

    if (i == fe->nfonts) {
        if (fe->fontsize <= fe->nfonts) {
            fe->fontsize = fe->nfonts + 10;
            fe->fonts = sresize(fe->fonts, fe->fontsize, struct font);
        }

        fe->nfonts++;

        fe->fonts[i].type = fonttype;
        fe->fonts[i].size = fontsize;

        fe->fonts[i].font = CreateFont(-fontsize, 0, 0, 0,
				       fe->drawstatus == PRINTING ? 0 : FW_BOLD,
				       FALSE, FALSE, FALSE, DEFAULT_CHARSET,
				       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
				       DEFAULT_QUALITY,
				       (fonttype == FONT_FIXED ?
					FIXED_PITCH | FF_DONTCARE :
					VARIABLE_PITCH | FF_SWISS),
				       NULL);
    }

    /*
     * Position and draw the text.
     */
    {
	HFONT oldfont;
	TEXTMETRIC tm;
	SIZE size;

	oldfont = SelectObject(fe->hdc, fe->fonts[i].font);
	if (GetTextMetrics(fe->hdc, &tm)) {
	    if (align & ALIGN_VCENTRE)
		xy.y -= (tm.tmAscent+tm.tmDescent)/2;
	    else
		xy.y -= tm.tmAscent;
	}
	if (GetTextExtentPoint32(fe->hdc, text, strlen(text), &size)) {
	    if (align & ALIGN_HCENTRE)
		xy.x -= size.cx / 2;
	    else if (align & ALIGN_HRIGHT)
		xy.x -= size.cx;
	}
	SetBkMode(fe->hdc, TRANSPARENT);
	win_text_colour(fe, colour);
	TextOut(fe->hdc, xy.x, xy.y, text, strlen(text));
	SelectObject(fe->hdc, oldfont);
    }
}

static void win_draw_rect(void *handle, int x, int y, int w, int h, int colour)
{
    frontend *fe = (frontend *)handle;
    POINT p, q;

    if (fe->drawstatus == NOTHING)
	return;

    if (fe->drawstatus == DRAWING && w == 1 && h == 1) {
	/*
	 * Rectangle() appears to get uppity if asked to draw a 1x1
	 * rectangle, presumably on the grounds that that's beneath
	 * its dignity and you ought to be using SetPixel instead.
	 * So I will.
	 */
	SetPixel(fe->hdc, x, y, fe->colours[colour]);
    } else {
	win_set_brush(fe, colour);
	win_set_pen(fe, colour, TRUE);
	p = win_transform_point(fe, x, y);
	q = win_transform_point(fe, x+w, y+h);
	Rectangle(fe->hdc, p.x, p.y, q.x, q.y);
	win_reset_brush(fe);
	win_reset_pen(fe);
    }
}

static void win_draw_line(void *handle, int x1, int y1, int x2, int y2, int colour)
{
    frontend *fe = (frontend *)handle;
    POINT p, q;

    if (fe->drawstatus == NOTHING)
	return;

    win_set_pen(fe, colour, FALSE);
    p = win_transform_point(fe, x1, y1);
    q = win_transform_point(fe, x2, y2);
    MoveToEx(fe->hdc, p.x, p.y, NULL);
    LineTo(fe->hdc, q.x, q.y);
    if (fe->drawstatus == DRAWING)
	SetPixel(fe->hdc, q.x, q.y, fe->colours[colour]);
    win_reset_pen(fe);
}

static void win_draw_circle(void *handle, int cx, int cy, int radius,
			    int fillcolour, int outlinecolour)
{
    frontend *fe = (frontend *)handle;
    POINT p, q, r;

    assert(outlinecolour >= 0);

    if (fe->drawstatus == NOTHING)
	return;

    if (fillcolour >= 0) {
	win_set_brush(fe, fillcolour);
	win_set_pen(fe, outlinecolour, FALSE);
	p = win_transform_point(fe, cx - radius, cy - radius);
	q = win_transform_point(fe, cx + radius, cy + radius);
	Ellipse(fe->hdc, p.x, p.y, q.x+1, q.y+1);
	win_reset_brush(fe);
	win_reset_pen(fe);
    } else {
	win_set_pen(fe, outlinecolour, FALSE);
	p = win_transform_point(fe, cx - radius, cy - radius);
	q = win_transform_point(fe, cx + radius, cy + radius);
	r = win_transform_point(fe, cx - radius, cy);
	Arc(fe->hdc, p.x, p.y, q.x+1, q.y+1, r.x, r.y, r.x, r.y);
	win_reset_pen(fe);
    }
}

static void win_draw_polygon(void *handle, int *coords, int npoints,
			     int fillcolour, int outlinecolour)
{
    frontend *fe = (frontend *)handle;
    POINT *pts;
    int i;

    if (fe->drawstatus == NOTHING)
	return;

    pts = snewn(npoints+1, POINT);

    for (i = 0; i <= npoints; i++) {
	int j = (i < npoints ? i : 0);
	pts[i] = win_transform_point(fe, coords[j*2], coords[j*2+1]);
    }

    assert(outlinecolour >= 0);

    if (fillcolour >= 0) {
	win_set_brush(fe, fillcolour);
	win_set_pen(fe, outlinecolour, FALSE);
	Polygon(fe->hdc, pts, npoints);
	win_reset_brush(fe);
	win_reset_pen(fe);
    } else {
	win_set_pen(fe, outlinecolour, FALSE);
	Polyline(fe->hdc, pts, npoints+1);
	win_reset_pen(fe);
    }

    sfree(pts);
}

static void win_start_draw(void *handle)
{
    frontend *fe = (frontend *)handle;
    HDC hdc_win;

    assert(fe->drawstatus == NOTHING);

    hdc_win = GetDC(fe->hwnd);
    fe->hdc = CreateCompatibleDC(hdc_win);
    fe->prevbm = SelectObject(fe->hdc, fe->bitmap);
    ReleaseDC(fe->hwnd, hdc_win);
    fe->clip = NULL;
    SetMapMode(fe->hdc, MM_TEXT);
    fe->drawstatus = DRAWING;
}

static void win_draw_update(void *handle, int x, int y, int w, int h)
{
    frontend *fe = (frontend *)handle;
    RECT r;

    if (fe->drawstatus != DRAWING)
	return;

    r.left = x;
    r.top = y;
    r.right = x + w;
    r.bottom = y + h;

    InvalidateRect(fe->hwnd, &r, FALSE);
}

static void win_end_draw(void *handle)
{
    frontend *fe = (frontend *)handle;
    assert(fe->drawstatus == DRAWING);
    SelectObject(fe->hdc, fe->prevbm);
    DeleteDC(fe->hdc);
    if (fe->clip) {
	DeleteObject(fe->clip);
	fe->clip = NULL;
    }
    fe->drawstatus = NOTHING;
}

static void win_line_width(void *handle, float width)
{
    frontend *fe = (frontend *)handle;

    assert(fe->drawstatus != DRAWING);
    if (fe->drawstatus == NOTHING)
	return;

    fe->linewidth = (int)(width * fe->printpixelscale);
}

static void win_begin_doc(void *handle, int pages)
{
    frontend *fe = (frontend *)handle;

    assert(fe->drawstatus != DRAWING);
    if (fe->drawstatus == NOTHING)
	return;

    if (StartDoc(fe->hdc, &fe->di) <= 0) {
	char *e = geterrstr();
	MessageBox(fe->hwnd, e, "Error starting to print",
		   MB_ICONERROR | MB_OK);
	sfree(e);
	fe->drawstatus = NOTHING;
    }

    /*
     * Push a marker on the font stack so that we won't use the
     * same fonts for printing and drawing. (This is because
     * drawing seems to look generally better in bold, but printing
     * is better not in bold.)
     */
    fe->fontstart = fe->nfonts;
}

static void win_begin_page(void *handle, int number)
{
    frontend *fe = (frontend *)handle;

    assert(fe->drawstatus != DRAWING);
    if (fe->drawstatus == NOTHING)
	return;

    if (StartPage(fe->hdc) <= 0) {
	char *e = geterrstr();
	MessageBox(fe->hwnd, e, "Error starting a page",
		   MB_ICONERROR | MB_OK);
	sfree(e);
	fe->drawstatus = NOTHING;
    }
}

static void win_begin_puzzle(void *handle, float xm, float xc,
			     float ym, float yc, int pw, int ph, float wmm)
{
    frontend *fe = (frontend *)handle;
    int ppw, pph, pox, poy;
    float mmpw, mmph, mmox, mmoy;
    float scale;

    assert(fe->drawstatus != DRAWING);
    if (fe->drawstatus == NOTHING)
	return;

    ppw = GetDeviceCaps(fe->hdc, HORZRES);
    pph = GetDeviceCaps(fe->hdc, VERTRES);
    mmpw = (float)GetDeviceCaps(fe->hdc, HORZSIZE);
    mmph = (float)GetDeviceCaps(fe->hdc, VERTSIZE);

    /*
     * Compute the puzzle's position on the logical page.
     */
    mmox = xm * mmpw + xc;
    mmoy = ym * mmph + yc;

    /*
     * Work out what that comes to in pixels.
     */
    pox = (int)(mmox * (float)ppw / mmpw);
    poy = (int)(mmoy * (float)ppw / mmpw);

    /*
     * And determine the scale.
     * 
     * I need a scale such that the maximum puzzle-coordinate
     * extent of the rectangle (pw * scale) is equal to the pixel
     * equivalent of the puzzle's millimetre width (wmm * ppw /
     * mmpw).
     */
    scale = (wmm * ppw) / (mmpw * pw);

    /*
     * Now store pox, poy and scale for use in the main drawing
     * functions.
     */
    fe->printoffsetx = pox;
    fe->printoffsety = poy;
    fe->printpixelscale = scale;

    fe->linewidth = 1;
}

static void win_end_puzzle(void *handle)
{
    /* Nothing needs to be done here. */
}

static void win_end_page(void *handle, int number)
{
    frontend *fe = (frontend *)handle;

    assert(fe->drawstatus != DRAWING);

    if (fe->drawstatus == NOTHING)
	return;

    if (EndPage(fe->hdc) <= 0) {
	char *e = geterrstr();
	MessageBox(fe->hwnd, e, "Error finishing a page",
		   MB_ICONERROR | MB_OK);
	sfree(e);
	fe->drawstatus = NOTHING;
    }
}

static void win_end_doc(void *handle)
{
    frontend *fe = (frontend *)handle;

    assert(fe->drawstatus != DRAWING);

    /*
     * Free all the fonts created since we began printing.
     */
    while (fe->nfonts > fe->fontstart) {
	fe->nfonts--;
	DeleteObject(fe->fonts[fe->nfonts].font);
    }
    fe->fontstart = 0;

    /*
     * The MSDN web site sample code doesn't bother to call EndDoc
     * if an error occurs half way through printing. I expect doing
     * so would cause the erroneous document to actually be
     * printed, or something equally undesirable.
     */
    if (fe->drawstatus == NOTHING)
	return;

    if (EndDoc(fe->hdc) <= 0) {
	char *e = geterrstr();
	MessageBox(fe->hwnd, e, "Error finishing printing",
		   MB_ICONERROR | MB_OK);
	sfree(e);
	fe->drawstatus = NOTHING;
    }
}

const struct drawing_api win_drawing = {
    win_draw_text,
    win_draw_rect,
    win_draw_line,
    win_draw_polygon,
    win_draw_circle,
    win_draw_update,
    win_clip,
    win_unclip,
    win_start_draw,
    win_end_draw,
    win_status_bar,
    win_blitter_new,
    win_blitter_free,
    win_blitter_save,
    win_blitter_load,
    win_begin_doc,
    win_begin_page,
    win_begin_puzzle,
    win_end_puzzle,
    win_end_page,
    win_end_doc,
    win_line_width,
};

void print(frontend *fe)
{
    PRINTDLG pd;
    char doctitle[256];
    document *doc;
    midend *nme = NULL;  /* non-interactive midend for bulk puzzle generation */
    int i;
    char *err = NULL;

    /*
     * Create our document structure and fill it up with puzzles.
     */
    doc = document_new(fe->printw, fe->printh, fe->printscale / 100.0F);
    for (i = 0; i < fe->printcount; i++) {
	if (i == 0 && fe->printcurr) {
	    err = midend_print_puzzle(fe->me, doc, fe->printsolns);
	} else {
	    if (!nme) {
		game_params *params;

		nme = midend_new(NULL, &thegame, NULL, NULL);

		/*
		 * Set the non-interactive mid-end to have the same
		 * parameters as the standard one.
		 */
		params = midend_get_params(fe->me);
		midend_set_params(nme, params);
		thegame.free_params(params);
	    }

	    midend_new_game(nme);
	    err = midend_print_puzzle(nme, doc, fe->printsolns);
	}
	if (err)
	    break;
    }
    if (nme)
	midend_free(nme);

    if (err) {
	MessageBox(fe->hwnd, err, "Error preparing puzzles for printing",
		   MB_ICONERROR | MB_OK);
	document_free(doc);
	return;
    }

    memset(&pd, 0, sizeof(pd));
    pd.lStructSize = sizeof(pd);
    pd.hwndOwner = fe->hwnd;
    pd.hDevMode = NULL;
    pd.hDevNames = NULL;
    pd.Flags = PD_USEDEVMODECOPIESANDCOLLATE | PD_RETURNDC |
	PD_NOPAGENUMS | PD_NOSELECTION;
    pd.nCopies = 1;
    pd.nFromPage = pd.nToPage = 0xFFFF;
    pd.nMinPage = pd.nMaxPage = 1;

    if (!PrintDlg(&pd)) {
	document_free(doc);
	return;
    }

    /*
     * Now pd.hDC is a device context for the printer.
     */

    /*
     * FIXME: IWBNI we put up an Abort box here.
     */

    memset(&fe->di, 0, sizeof(fe->di));
    fe->di.cbSize = sizeof(fe->di);
    sprintf(doctitle, "Printed puzzles from %s (from Simon Tatham's"
	    " Portable Puzzle Collection)", thegame.name);
    fe->di.lpszDocName = doctitle;
    fe->di.lpszOutput = NULL;
    fe->di.lpszDatatype = NULL;
    fe->di.fwType = 0;

    fe->drawstatus = PRINTING;
    fe->hdc = pd.hDC;

    fe->dr = drawing_new(&win_drawing, NULL, fe);
    document_print(doc, fe->dr);
    drawing_free(fe->dr);
    fe->dr = NULL;

    fe->drawstatus = NOTHING;

    DeleteDC(pd.hDC);
    document_free(doc);
}

void deactivate_timer(frontend *fe)
{
    if (!fe)
	return;			       /* for non-interactive midend */
    if (fe->hwnd) KillTimer(fe->hwnd, fe->timer);
    fe->timer = 0;
}

void activate_timer(frontend *fe)
{
    if (!fe)
	return;			       /* for non-interactive midend */
    if (!fe->timer) {
	fe->timer = SetTimer(fe->hwnd, fe->timer, 20, NULL);
	fe->timer_last_tickcount = GetTickCount();
    }
}

void write_clip(HWND hwnd, char *data)
{
    HGLOBAL clipdata;
    int len, i, j;
    char *data2;
    void *lock;

    /*
     * Windows expects CRLF in the clipboard, so we must convert
     * any \n that has come out of the puzzle backend.
     */
    len = 0;
    for (i = 0; data[i]; i++) {
	if (data[i] == '\n')
	    len++;
	len++;
    }
    data2 = snewn(len+1, char);
    j = 0;
    for (i = 0; data[i]; i++) {
	if (data[i] == '\n')
	    data2[j++] = '\r';
	data2[j++] = data[i];
    }
    assert(j == len);
    data2[j] = '\0';

    clipdata = GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, len + 1);
    if (!clipdata)
	return;
    lock = GlobalLock(clipdata);
    if (!lock)
	return;
    memcpy(lock, data2, len);
    ((unsigned char *) lock)[len] = 0;
    GlobalUnlock(clipdata);

    if (OpenClipboard(hwnd)) {
	EmptyClipboard();
	SetClipboardData(CF_TEXT, clipdata);
	CloseClipboard();
    } else
	GlobalFree(clipdata);

    sfree(data2);
}

/*
 * Set up Help and see if we can find a help file.
 */
static void init_help(void)
{
    char b[2048], *p, *q, *r;
    FILE *fp;

    /*
     * Find the executable file path, so we can look alongside
     * it for help files. Trim the filename off the end.
     */
    GetModuleFileName(NULL, b, sizeof(b) - 1);
    r = b;
    p = strrchr(b, '\\');
    if (p && p >= r) r = p+1;
    q = strrchr(b, ':');
    if (q && q >= r) r = q+1;

#ifndef NO_HTMLHELP
    /*
     * Try HTML Help first.
     */
    strcpy(r, CHM_FILE_NAME);
    if ( (fp = fopen(b, "r")) != NULL) {
	fclose(fp);

	/*
	 * We have a .CHM. See if we can use it.
	 */
	hh_dll = LoadLibrary("hhctrl.ocx");
	if (hh_dll) {
	    htmlhelp = (htmlhelp_t)GetProcAddress(hh_dll, "HtmlHelpA");
	    if (!htmlhelp)
		FreeLibrary(hh_dll);
	}
	if (htmlhelp) {
	    htmlhelp(NULL, NULL, HH_INITIALIZE, (DWORD)&html_help_cookie);
	    help_path = dupstr(b);
	    help_type = CHM;
	    help_topic = thegame.htmlhelp_topic;
	    return;
	}
    }
#endif /* NO_HTMLHELP */

    /*
     * Now try old-style .HLP.
     */
    strcpy(r, HELP_FILE_NAME);
    if ( (fp = fopen(b, "r")) != NULL) {
	fclose(fp);

	help_path = dupstr(b);
	help_type = HLP;

	help_topic = thegame.winhelp_topic;

	/*
	 * See if there's a .CNT file alongside it.
	 */
	strcpy(r, HELP_CNT_NAME);
	if ( (fp = fopen(b, "r")) != NULL) {
	    fclose(fp);
	    help_has_contents = TRUE;
	} else
	    help_has_contents = FALSE;

	return;
    }

    help_type = NONE;	       /* didn't find any */
}

/*
 * Start Help.
 */
static void start_help(frontend *fe, const char *topic)
{
    char *str = NULL;
    int cmd;

    switch (help_type) {
      case HLP:
	assert(help_path);
	if (topic) {
	    str = snewn(10+strlen(topic), char);
	    sprintf(str, "JI(`',`%s')", topic);
	    cmd = HELP_COMMAND;
	} else if (help_has_contents) {
	    cmd = HELP_FINDER;
	} else {
	    cmd = HELP_CONTENTS;
	}
	WinHelp(fe->hwnd, help_path, cmd, (DWORD)str);
	fe->help_running = TRUE;
	break;
      case CHM:
#ifndef NO_HTMLHELP
	assert(help_path);
	assert(htmlhelp);
	if (topic) {
	    str = snewn(20 + strlen(topic) + strlen(help_path), char);
	    sprintf(str, "%s::/%s.html>main", help_path, topic);
	} else {
	    str = dupstr(help_path);
	}
	htmlhelp(fe->hwnd, str, HH_DISPLAY_TOPIC, 0);
	fe->help_running = TRUE;
	break;
#endif /* NO_HTMLHELP */
      case NONE:
	assert(!"This shouldn't happen");
	break;
    }

    sfree(str);
}

/*
 * Stop Help on window cleanup.
 */
static void stop_help(frontend *fe)
{
    if (fe->help_running) {
	switch (help_type) {
	  case HLP:
	    WinHelp(fe->hwnd, help_path, HELP_QUIT, 0);
	    break;
	  case CHM:
#ifndef NO_HTMLHELP
	    assert(htmlhelp);
	    htmlhelp(NULL, NULL, HH_CLOSE_ALL, 0);
	    break;
#endif /* NO_HTMLHELP */
	  case NONE:
	    assert(!"This shouldn't happen");
	    break;
	}
	fe->help_running = FALSE;
    }
}

/*
 * Terminate Help on process exit.
 */
static void cleanup_help(void)
{
#ifndef NO_HTMLHELP
    if (help_type == CHM) {
	assert(htmlhelp);
	htmlhelp(NULL, NULL, HH_UNINITIALIZE, html_help_cookie);
    }
#endif /* NO_HTMLHELP */
}

static void check_window_size(frontend *fe, int *px, int *py)
{
    RECT r;
    int x, y, sy;

    if (fe->statusbar) {
	RECT sr;
	GetWindowRect(fe->statusbar, &sr);
	sy = sr.bottom - sr.top;
    } else {
	sy = 0;
    }

    /*
     * See if we actually got the window size we wanted, and adjust
     * the puzzle size if not.
     */
    GetClientRect(fe->hwnd, &r);
    x = r.right - r.left;
    y = r.bottom - r.top - sy;
    midend_size(fe->me, &x, &y, FALSE);
    if (x != r.right - r.left || y != r.bottom - r.top) {
	/*
	 * Resize the window, now we know what size we _really_
	 * want it to be.
	 */
	r.left = r.top = 0;
	r.right = x;
	r.bottom = y + sy;
	AdjustWindowRectEx(&r, WINFLAGS, TRUE, 0);
	SetWindowPos(fe->hwnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top,
		     SWP_NOMOVE | SWP_NOZORDER);
    }

    if (fe->statusbar) {
	GetClientRect(fe->hwnd, &r);
	SetWindowPos(fe->statusbar, NULL, 0, r.bottom-r.top-sy, r.right-r.left,
		     sy, SWP_NOZORDER);
    }

    *px = x;
    *py = y;
}

static void get_max_puzzle_size(frontend *fe, int *x, int *y)
{
    RECT r, sr;

    if (SystemParametersInfo(SPI_GETWORKAREA, 0, &sr, FALSE)) {
	*x = sr.right - sr.left;
	*y = sr.bottom - sr.top;
	r.left = 100;
	r.right = 200;
	r.top = 100;
	r.bottom = 200;
	AdjustWindowRectEx(&r, WINFLAGS, TRUE, 0);
	*x -= r.right - r.left - 100;
	*y -= r.bottom - r.top - 100;
    } else {
	*x = *y = INT_MAX;
    }

    if (fe->statusbar != NULL) {
	GetWindowRect(fe->statusbar, &sr);
	*y -= sr.bottom - sr.top;
    }
}

static frontend *new_window(HINSTANCE inst, char *game_id, char **error)
{
    frontend *fe;
    int x, y;
    RECT r;

    fe = snew(frontend);

    fe->me = midend_new(fe, &thegame, &win_drawing, fe);

    if (game_id) {
        *error = midend_game_id(fe->me, game_id);
        if (*error) {
            midend_free(fe->me);
            sfree(fe);
            return NULL;
        }
    }

    fe->inst = inst;

    fe->timer = 0;
    fe->hwnd = NULL;

    fe->help_running = FALSE;

    fe->drawstatus = NOTHING;
    fe->dr = NULL;
    fe->fontstart = 0;

    midend_new_game(fe->me);

    fe->fonts = NULL;
    fe->nfonts = fe->fontsize = 0;

    {
	int i, ncolours;
        float *colours;

        colours = midend_colours(fe->me, &ncolours);

	fe->colours = snewn(ncolours, COLORREF);
	fe->brushes = snewn(ncolours, HBRUSH);
	fe->pens = snewn(ncolours, HPEN);

	for (i = 0; i < ncolours; i++) {
	    fe->colours[i] = RGB(255 * colours[i*3+0],
				 255 * colours[i*3+1],
				 255 * colours[i*3+2]);
	    fe->brushes[i] = CreateSolidBrush(fe->colours[i]);
	    fe->pens[i] = CreatePen(PS_SOLID, 1, fe->colours[i]);
	}
        sfree(colours);
    }

    if (midend_wants_statusbar(fe->me)) {
	fe->statusbar = CreateWindowEx(0, STATUSCLASSNAME, "ooh",
				       WS_CHILD | WS_VISIBLE,
				       0, 0, 0, 0, /* status bar does these */
				       NULL, NULL, inst, NULL);
    } else
        fe->statusbar = NULL;

    get_max_puzzle_size(fe, &x, &y);
    midend_size(fe->me, &x, &y, FALSE);

    r.left = r.top = 0;
    r.right = x;
    r.bottom = y;
    AdjustWindowRectEx(&r, WINFLAGS, TRUE, 0);

    fe->hwnd = CreateWindowEx(0, thegame.name, thegame.name,
			      WS_OVERLAPPEDWINDOW &~
			      (WS_THICKFRAME | WS_MAXIMIZEBOX),
			      CW_USEDEFAULT, CW_USEDEFAULT,
			      r.right - r.left, r.bottom - r.top,
			      NULL, NULL, inst, NULL);

    if (midend_wants_statusbar(fe->me)) {
	RECT sr;
	DestroyWindow(fe->statusbar);
	fe->statusbar = CreateWindowEx(0, STATUSCLASSNAME, "ooh",
				       WS_CHILD | WS_VISIBLE,
				       0, 0, 0, 0, /* status bar does these */
				       fe->hwnd, NULL, inst, NULL);
	/*
	 * Now resize the window to take account of the status bar.
	 */
	GetWindowRect(fe->statusbar, &sr);
	GetWindowRect(fe->hwnd, &r);
	SetWindowPos(fe->hwnd, NULL, 0, 0, r.right - r.left,
		     r.bottom - r.top + sr.bottom - sr.top,
		     SWP_NOMOVE | SWP_NOZORDER);
    } else {
	fe->statusbar = NULL;
    }

    {
	HMENU bar = CreateMenu();
	HMENU menu = CreateMenu();

	AppendMenu(bar, MF_ENABLED|MF_POPUP, (UINT)menu, "Game");
	AppendMenu(menu, MF_ENABLED, IDM_NEW, "New");
	AppendMenu(menu, MF_ENABLED, IDM_RESTART, "Restart");
	AppendMenu(menu, MF_ENABLED, IDM_DESC, "Specific...");
	AppendMenu(menu, MF_ENABLED, IDM_SEED, "Random Seed...");

	if ((fe->npresets = midend_num_presets(fe->me)) > 0 ||
	    thegame.can_configure) {
	    HMENU sub = CreateMenu();
	    int i;

	    AppendMenu(bar, MF_ENABLED|MF_POPUP, (UINT)sub, "Type");

	    fe->presets = snewn(fe->npresets, game_params *);

	    for (i = 0; i < fe->npresets; i++) {
		char *name;

		midend_fetch_preset(fe->me, i, &name, &fe->presets[i]);

		/*
		 * FIXME: we ought to go through and do something
		 * with ampersands here.
		 */

		AppendMenu(sub, MF_ENABLED, IDM_PRESETS + 0x10 * i, name);
	    }

	    if (thegame.can_configure) {
		AppendMenu(sub, MF_ENABLED, IDM_CONFIG, "Custom...");
	    }
	}

	AppendMenu(menu, MF_SEPARATOR, 0, 0);
	AppendMenu(menu, MF_ENABLED, IDM_LOAD, "Load...");
	AppendMenu(menu, MF_ENABLED, IDM_SAVE, "Save...");
	AppendMenu(menu, MF_SEPARATOR, 0, 0);
	if (thegame.can_print) {
	    AppendMenu(menu, MF_ENABLED, IDM_PRINT, "Print...");
	    AppendMenu(menu, MF_SEPARATOR, 0, 0);
	}
	AppendMenu(menu, MF_ENABLED, IDM_UNDO, "Undo");
	AppendMenu(menu, MF_ENABLED, IDM_REDO, "Redo");
	if (thegame.can_format_as_text) {
	    AppendMenu(menu, MF_SEPARATOR, 0, 0);
	    AppendMenu(menu, MF_ENABLED, IDM_COPY, "Copy");
	}
	if (thegame.can_solve) {
	    AppendMenu(menu, MF_SEPARATOR, 0, 0);
	    AppendMenu(menu, MF_ENABLED, IDM_SOLVE, "Solve");
	}
	AppendMenu(menu, MF_SEPARATOR, 0, 0);
	AppendMenu(menu, MF_ENABLED, IDM_QUIT, "Exit");
	menu = CreateMenu();
	AppendMenu(bar, MF_ENABLED|MF_POPUP, (UINT)menu, "Help");
	AppendMenu(menu, MF_ENABLED, IDM_ABOUT, "About");
        if (help_type != NONE) {
	    AppendMenu(menu, MF_SEPARATOR, 0, 0);
            AppendMenu(menu, MF_ENABLED, IDM_HELPC, "Contents");
            if (help_topic) {
                char *item;
                assert(thegame.name);
                item = snewn(9+strlen(thegame.name), char); /*ick*/
                sprintf(item, "Help on %s", thegame.name);
                AppendMenu(menu, MF_ENABLED, IDM_GAMEHELP, item);
                sfree(item);
            }
        }
	SetMenu(fe->hwnd, bar);
    }

    fe->bitmap = NULL;
    new_game_size(fe); /* initialises fe->bitmap */
    check_window_size(fe, &x, &y);

    SetWindowLong(fe->hwnd, GWL_USERDATA, (LONG)fe);

    ShowWindow(fe->hwnd, SW_NORMAL);
    SetForegroundWindow(fe->hwnd);

    midend_redraw(fe->me);

    return fe;
}

static int CALLBACK AboutDlgProc(HWND hwnd, UINT msg,
				 WPARAM wParam, LPARAM lParam)
{
    frontend *fe = (frontend *)GetWindowLong(hwnd, GWL_USERDATA);

    switch (msg) {
      case WM_INITDIALOG:
	return 0;

      case WM_COMMAND:
	if ((HIWORD(wParam) == BN_CLICKED ||
	     HIWORD(wParam) == BN_DOUBLECLICKED) &&
	    LOWORD(wParam) == IDOK)
	    fe->dlg_done = 1;
	return 0;

      case WM_CLOSE:
	fe->dlg_done = 1;
	return 0;
    }

    return 0;
}

/*
 * Wrappers on midend_{get,set}_config, which extend the CFG_*
 * enumeration to add CFG_PRINT.
 */
static config_item *frontend_get_config(frontend *fe, int which,
					char **wintitle)
{
    if (which < CFG_FRONTEND_SPECIFIC) {
	return midend_get_config(fe->me, which, wintitle);
    } else if (which == CFG_PRINT) {
	config_item *ret;
	int i;

	*wintitle = snewn(40 + strlen(thegame.name), char);
	sprintf(*wintitle, "%s print setup", thegame.name);

	ret = snewn(8, config_item);

	i = 0;

	ret[i].name = "Number of puzzles to print";
	ret[i].type = C_STRING;
	ret[i].sval = dupstr("1");
	ret[i].ival = 0;
	i++;

	ret[i].name = "Number of puzzles across the page";
	ret[i].type = C_STRING;
	ret[i].sval = dupstr("1");
	ret[i].ival = 0;
	i++;

	ret[i].name = "Number of puzzles down the page";
	ret[i].type = C_STRING;
	ret[i].sval = dupstr("1");
	ret[i].ival = 0;
	i++;

	ret[i].name = "Percentage of standard size";
	ret[i].type = C_STRING;
	ret[i].sval = dupstr("100.0");
	ret[i].ival = 0;
	i++;

	ret[i].name = "Include currently shown puzzle";
	ret[i].type = C_BOOLEAN;
	ret[i].sval = NULL;
	ret[i].ival = TRUE;
	i++;

	ret[i].name = "Print solutions";
	ret[i].type = C_BOOLEAN;
	ret[i].sval = NULL;
	ret[i].ival = FALSE;
	i++;

	if (thegame.can_print_in_colour) {
	    ret[i].name = "Print in colour";
	    ret[i].type = C_BOOLEAN;
	    ret[i].sval = NULL;
	    ret[i].ival = FALSE;
	    i++;
	}

	ret[i].name = NULL;
	ret[i].type = C_END;
	ret[i].sval = NULL;
	ret[i].ival = 0;
	i++;

	return ret;
    } else {
	assert(!"We should never get here");
	return NULL;
    }
}

static char *frontend_set_config(frontend *fe, int which, config_item *cfg)
{
    if (which < CFG_FRONTEND_SPECIFIC) {
	return midend_set_config(fe->me, which, cfg);
    } else if (which == CFG_PRINT) {
	if ((fe->printcount = atoi(cfg[0].sval)) <= 0)
	    return "Number of puzzles to print should be at least one";
	if ((fe->printw = atoi(cfg[1].sval)) <= 0)
	    return "Number of puzzles across the page should be at least one";
	if ((fe->printh = atoi(cfg[2].sval)) <= 0)
	    return "Number of puzzles down the page should be at least one";
	if ((fe->printscale = (float)atof(cfg[3].sval)) <= 0)
	    return "Print size should be positive";
	fe->printcurr = cfg[4].ival;
	fe->printsolns = cfg[5].ival;
	fe->printcolour = thegame.can_print_in_colour && cfg[6].ival;
	return NULL;
    } else {
	assert(!"We should never get here");
	return "Internal error";
    }
}

static int CALLBACK ConfigDlgProc(HWND hwnd, UINT msg,
				  WPARAM wParam, LPARAM lParam)
{
    frontend *fe = (frontend *)GetWindowLong(hwnd, GWL_USERDATA);
    config_item *i;
    struct cfg_aux *j;

    switch (msg) {
      case WM_INITDIALOG:
	return 0;

      case WM_COMMAND:
	/*
	 * OK and Cancel are special cases.
	 */
	if ((HIWORD(wParam) == BN_CLICKED ||
	     HIWORD(wParam) == BN_DOUBLECLICKED) &&
	    (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)) {
	    if (LOWORD(wParam) == IDOK) {
		char *err = frontend_set_config(fe, fe->cfg_which, fe->cfg);

		if (err) {
		    MessageBox(hwnd, err, "Validation error",
			       MB_ICONERROR | MB_OK);
		} else {
		    fe->dlg_done = 2;
		}
	    } else {
		fe->dlg_done = 1;
	    }
	    return 0;
	}

	/*
	 * First find the control whose id this is.
	 */
	for (i = fe->cfg, j = fe->cfgaux; i->type != C_END; i++, j++) {
	    if (j->ctlid == LOWORD(wParam))
		break;
	}
	if (i->type == C_END)
	    return 0;		       /* not our problem */

	if (i->type == C_STRING && HIWORD(wParam) == EN_CHANGE) {
	    char buffer[4096];
	    GetDlgItemText(fe->cfgbox, j->ctlid, buffer, lenof(buffer));
	    buffer[lenof(buffer)-1] = '\0';
	    sfree(i->sval);
	    i->sval = dupstr(buffer);
	} else if (i->type == C_BOOLEAN && 
		   (HIWORD(wParam) == BN_CLICKED ||
		    HIWORD(wParam) == BN_DOUBLECLICKED)) {
	    i->ival = IsDlgButtonChecked(fe->cfgbox, j->ctlid);
	} else if (i->type == C_CHOICES &&
		   HIWORD(wParam) == CBN_SELCHANGE) {
	    i->ival = SendDlgItemMessage(fe->cfgbox, j->ctlid,
					 CB_GETCURSEL, 0, 0);
	}

	return 0;

      case WM_CLOSE:
	fe->dlg_done = 1;
	return 0;
    }

    return 0;
}

HWND mkctrl(frontend *fe, int x1, int x2, int y1, int y2,
	    char *wclass, int wstyle,
	    int exstyle, const char *wtext, int wid)
{
    HWND ret;
    ret = CreateWindowEx(exstyle, wclass, wtext,
			 wstyle | WS_CHILD | WS_VISIBLE, x1, y1, x2-x1, y2-y1,
			 fe->cfgbox, (HMENU) wid, fe->inst, NULL);
    SendMessage(ret, WM_SETFONT, (WPARAM)fe->cfgfont, MAKELPARAM(TRUE, 0));
    return ret;
}

static void about(frontend *fe)
{
    int i;
    WNDCLASS wc;
    MSG msg;
    TEXTMETRIC tm;
    HDC hdc;
    HFONT oldfont;
    SIZE size;
    int gm, id;
    int winwidth, winheight, y;
    int height, width, maxwid;
    const char *strings[16];
    int lengths[16];
    int nstrings = 0;
    char titlebuf[512];

    sprintf(titlebuf, "About %.250s", thegame.name);

    strings[nstrings++] = thegame.name;
    strings[nstrings++] = "from Simon Tatham's Portable Puzzle Collection";
    strings[nstrings++] = ver;

    wc.style = CS_DBLCLKS | CS_SAVEBITS | CS_BYTEALIGNWINDOW;
    wc.lpfnWndProc = DefDlgProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = DLGWINDOWEXTRA + 8;
    wc.hInstance = fe->inst;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH) (COLOR_BACKGROUND +1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "GameAboutBox";
    RegisterClass(&wc);

    hdc = GetDC(fe->hwnd);
    SetMapMode(hdc, MM_TEXT);

    fe->dlg_done = FALSE;

    fe->cfgfont = CreateFont(-MulDiv(8, GetDeviceCaps(hdc, LOGPIXELSY), 72),
			     0, 0, 0, 0,
			     FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			     DEFAULT_QUALITY,
			     FF_SWISS,
			     "MS Shell Dlg");

    oldfont = SelectObject(hdc, fe->cfgfont);
    if (GetTextMetrics(hdc, &tm)) {
	height = tm.tmAscent + tm.tmDescent;
	width = tm.tmAveCharWidth;
    } else {
	height = width = 30;
    }

    /*
     * Figure out the layout of the About box by measuring the
     * length of each piece of text.
     */
    maxwid = 0;
    winheight = height/2;

    for (i = 0; i < nstrings; i++) {
	if (GetTextExtentPoint32(hdc, strings[i], strlen(strings[i]), &size))
	    lengths[i] = size.cx;
	else
	    lengths[i] = 0;	       /* *shrug* */
	if (maxwid < lengths[i])
	    maxwid = lengths[i];
	winheight += height * 3 / 2 + (height / 2);
    }

    winheight += height + height * 7 / 4;      /* OK button */
    winwidth = maxwid + 4*width;

    SelectObject(hdc, oldfont);
    ReleaseDC(fe->hwnd, hdc);

    /*
     * Create the dialog, now that we know its size.
     */
    {
	RECT r, r2;

	r.left = r.top = 0;
	r.right = winwidth;
	r.bottom = winheight;

	AdjustWindowRectEx(&r, (WS_OVERLAPPEDWINDOW /*|
				DS_MODALFRAME | WS_POPUP | WS_VISIBLE |
				WS_CAPTION | WS_SYSMENU*/) &~
			   (WS_MAXIMIZEBOX | WS_OVERLAPPED),
			   FALSE, 0);

	/*
	 * Centre the dialog on its parent window.
	 */
	r.right -= r.left;
	r.bottom -= r.top;
	GetWindowRect(fe->hwnd, &r2);
	r.left = (r2.left + r2.right - r.right) / 2;
	r.top = (r2.top + r2.bottom - r.bottom) / 2;
	r.right += r.left;
	r.bottom += r.top;

	fe->cfgbox = CreateWindowEx(0, wc.lpszClassName, titlebuf,
				    DS_MODALFRAME | WS_POPUP | WS_VISIBLE |
				    WS_CAPTION | WS_SYSMENU,
				    r.left, r.top,
				    r.right-r.left, r.bottom-r.top,
				    fe->hwnd, NULL, fe->inst, NULL);
    }

    SendMessage(fe->cfgbox, WM_SETFONT, (WPARAM)fe->cfgfont, FALSE);

    SetWindowLong(fe->cfgbox, GWL_USERDATA, (LONG)fe);
    SetWindowLong(fe->cfgbox, DWL_DLGPROC, (LONG)AboutDlgProc);

    id = 1000;
    y = height/2;
    for (i = 0; i < nstrings; i++) {
	int border = width*2 + (maxwid - lengths[i]) / 2;
	mkctrl(fe, border, border+lengths[i], y+height*1/8, y+height*9/8,
	       "Static", 0, 0, strings[i], id++);
	y += height*3/2;

	assert(y < winheight);
	y += height/2;
    }

    y += height/2;		       /* extra space before OK */
    mkctrl(fe, width*2, maxwid+width*2, y, y+height*7/4, "BUTTON",
	   BS_PUSHBUTTON | BS_NOTIFY | WS_TABSTOP | BS_DEFPUSHBUTTON, 0,
	   "OK", IDOK);

    SendMessage(fe->cfgbox, WM_INITDIALOG, 0, 0);

    EnableWindow(fe->hwnd, FALSE);
    ShowWindow(fe->cfgbox, SW_NORMAL);
    while ((gm=GetMessage(&msg, NULL, 0, 0)) > 0) {
	if (!IsDialogMessage(fe->cfgbox, &msg))
	    DispatchMessage(&msg);
	if (fe->dlg_done)
	    break;
    }
    EnableWindow(fe->hwnd, TRUE);
    SetForegroundWindow(fe->hwnd);
    DestroyWindow(fe->cfgbox);
    DeleteObject(fe->cfgfont);
}

static int get_config(frontend *fe, int which)
{
    config_item *i;
    struct cfg_aux *j;
    char *title;
    WNDCLASS wc;
    MSG msg;
    TEXTMETRIC tm;
    HDC hdc;
    HFONT oldfont;
    SIZE size;
    HWND ctl;
    int gm, id, nctrls;
    int winwidth, winheight, col1l, col1r, col2l, col2r, y;
    int height, width, maxlabel, maxcheckbox;

    wc.style = CS_DBLCLKS | CS_SAVEBITS | CS_BYTEALIGNWINDOW;
    wc.lpfnWndProc = DefDlgProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = DLGWINDOWEXTRA + 8;
    wc.hInstance = fe->inst;
    wc.hIcon = NULL;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH) (COLOR_BACKGROUND +1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "GameConfigBox";
    RegisterClass(&wc);

    hdc = GetDC(fe->hwnd);
    SetMapMode(hdc, MM_TEXT);

    fe->dlg_done = FALSE;

    fe->cfgfont = CreateFont(-MulDiv(8, GetDeviceCaps(hdc, LOGPIXELSY), 72),
			     0, 0, 0, 0,
			     FALSE, FALSE, FALSE, DEFAULT_CHARSET,
			     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
			     DEFAULT_QUALITY,
			     FF_SWISS,
			     "MS Shell Dlg");

    oldfont = SelectObject(hdc, fe->cfgfont);
    if (GetTextMetrics(hdc, &tm)) {
	height = tm.tmAscent + tm.tmDescent;
	width = tm.tmAveCharWidth;
    } else {
	height = width = 30;
    }

    fe->cfg = frontend_get_config(fe, which, &title);
    fe->cfg_which = which;

    /*
     * Figure out the layout of the config box by measuring the
     * length of each piece of text.
     */
    maxlabel = maxcheckbox = 0;
    winheight = height/2;

    for (i = fe->cfg; i->type != C_END; i++) {
	switch (i->type) {
	  case C_STRING:
	  case C_CHOICES:
	    /*
	     * Both these control types have a label filling only
	     * the left-hand column of the box.
	     */
	    if (GetTextExtentPoint32(hdc, i->name, strlen(i->name), &size) &&
		maxlabel < size.cx)
		maxlabel = size.cx;
	    winheight += height * 3 / 2 + (height / 2);
	    break;

	  case C_BOOLEAN:
	    /*
	     * Checkboxes take up the whole of the box width.
	     */
	    if (GetTextExtentPoint32(hdc, i->name, strlen(i->name), &size) &&
		maxcheckbox < size.cx)
		maxcheckbox = size.cx;
	    winheight += height + (height / 2);
	    break;
	}
    }

    winheight += height + height * 7 / 4;      /* OK / Cancel buttons */

    col1l = 2*width;
    col1r = col1l + maxlabel;
    col2l = col1r + 2*width;
    col2r = col2l + 30*width;
    if (col2r < col1l+2*height+maxcheckbox)
	col2r = col1l+2*height+maxcheckbox;
    winwidth = col2r + 2*width;

    SelectObject(hdc, oldfont);
    ReleaseDC(fe->hwnd, hdc);

    /*
     * Create the dialog, now that we know its size.
     */
    {
	RECT r, r2;

	r.left = r.top = 0;
	r.right = winwidth;
	r.bottom = winheight;

	AdjustWindowRectEx(&r, (WS_OVERLAPPEDWINDOW /*|
				DS_MODALFRAME | WS_POPUP | WS_VISIBLE |
				WS_CAPTION | WS_SYSMENU*/) &~
			   (WS_MAXIMIZEBOX | WS_OVERLAPPED),
			   FALSE, 0);

	/*
	 * Centre the dialog on its parent window.
	 */
	r.right -= r.left;
	r.bottom -= r.top;
	GetWindowRect(fe->hwnd, &r2);
	r.left = (r2.left + r2.right - r.right) / 2;
	r.top = (r2.top + r2.bottom - r.bottom) / 2;
	r.right += r.left;
	r.bottom += r.top;

	fe->cfgbox = CreateWindowEx(0, wc.lpszClassName, title,
				    DS_MODALFRAME | WS_POPUP | WS_VISIBLE |
				    WS_CAPTION | WS_SYSMENU,
				    r.left, r.top,
				    r.right-r.left, r.bottom-r.top,
				    fe->hwnd, NULL, fe->inst, NULL);
	sfree(title);
    }

    SendMessage(fe->cfgbox, WM_SETFONT, (WPARAM)fe->cfgfont, FALSE);

    SetWindowLong(fe->cfgbox, GWL_USERDATA, (LONG)fe);
    SetWindowLong(fe->cfgbox, DWL_DLGPROC, (LONG)ConfigDlgProc);

    /*
     * Count the controls so we can allocate cfgaux.
     */
    for (nctrls = 0, i = fe->cfg; i->type != C_END; i++)
	nctrls++;
    fe->cfgaux = snewn(nctrls, struct cfg_aux);

    id = 1000;
    y = height/2;
    for (i = fe->cfg, j = fe->cfgaux; i->type != C_END; i++, j++) {
	switch (i->type) {
	  case C_STRING:
	    /*
	     * Edit box with a label beside it.
	     */
	    mkctrl(fe, col1l, col1r, y+height*1/8, y+height*9/8,
		   "Static", 0, 0, i->name, id++);
	    ctl = mkctrl(fe, col2l, col2r, y, y+height*3/2,
			 "EDIT", WS_TABSTOP | ES_AUTOHSCROLL,
			 WS_EX_CLIENTEDGE, "", (j->ctlid = id++));
	    SetWindowText(ctl, i->sval);
	    y += height*3/2;
	    break;

	  case C_BOOLEAN:
	    /*
	     * Simple checkbox.
	     */
	    mkctrl(fe, col1l, col2r, y, y+height, "BUTTON",
		   BS_NOTIFY | BS_AUTOCHECKBOX | WS_TABSTOP,
		   0, i->name, (j->ctlid = id++));
	    CheckDlgButton(fe->cfgbox, j->ctlid, (i->ival != 0));
	    y += height;
	    break;

	  case C_CHOICES:
	    /*
	     * Drop-down list with a label beside it.
	     */
	    mkctrl(fe, col1l, col1r, y+height*1/8, y+height*9/8,
		   "STATIC", 0, 0, i->name, id++);
	    ctl = mkctrl(fe, col2l, col2r, y, y+height*41/2,
			 "COMBOBOX", WS_TABSTOP |
			 CBS_DROPDOWNLIST | CBS_HASSTRINGS,
			 WS_EX_CLIENTEDGE, "", (j->ctlid = id++));
	    {
		char c, *p, *q, *str;

		SendMessage(ctl, CB_RESETCONTENT, 0, 0);
		p = i->sval;
		c = *p++;
		while (*p) {
		    q = p;
		    while (*q && *q != c) q++;
		    str = snewn(q-p+1, char);
		    strncpy(str, p, q-p);
		    str[q-p] = '\0';
		    SendMessage(ctl, CB_ADDSTRING, 0, (LPARAM)str);
		    sfree(str);
		    if (*q) q++;
		    p = q;
		}
	    }

	    SendMessage(ctl, CB_SETCURSEL, i->ival, 0);

	    y += height*3/2;
	    break;
	}

	assert(y < winheight);
	y += height/2;
    }

    y += height/2;		       /* extra space before OK and Cancel */
    mkctrl(fe, col1l, (col1l+col2r)/2-width, y, y+height*7/4, "BUTTON",
	   BS_PUSHBUTTON | BS_NOTIFY | WS_TABSTOP | BS_DEFPUSHBUTTON, 0,
	   "OK", IDOK);
    mkctrl(fe, (col1l+col2r)/2+width, col2r, y, y+height*7/4, "BUTTON",
	   BS_PUSHBUTTON | BS_NOTIFY | WS_TABSTOP, 0, "Cancel", IDCANCEL);

    SendMessage(fe->cfgbox, WM_INITDIALOG, 0, 0);

    EnableWindow(fe->hwnd, FALSE);
    ShowWindow(fe->cfgbox, SW_NORMAL);
    while ((gm=GetMessage(&msg, NULL, 0, 0)) > 0) {
	if (!IsDialogMessage(fe->cfgbox, &msg))
	    DispatchMessage(&msg);
	if (fe->dlg_done)
	    break;
    }
    EnableWindow(fe->hwnd, TRUE);
    SetForegroundWindow(fe->hwnd);
    DestroyWindow(fe->cfgbox);
    DeleteObject(fe->cfgfont);

    free_cfg(fe->cfg);
    sfree(fe->cfgaux);

    return (fe->dlg_done == 2);
}

static void new_game_size(frontend *fe)
{
    RECT r, sr;
    HDC hdc;
    int x, y;

    get_max_puzzle_size(fe, &x, &y);
    midend_size(fe->me, &x, &y, FALSE);

    r.left = r.top = 0;
    r.right = x;
    r.bottom = y;
    AdjustWindowRectEx(&r, WINFLAGS, TRUE, 0);

    if (fe->statusbar != NULL) {
	GetWindowRect(fe->statusbar, &sr);
    } else {
	sr.left = sr.right = sr.top = sr.bottom = 0;
    }
    SetWindowPos(fe->hwnd, NULL, 0, 0,
		 r.right - r.left,
		 r.bottom - r.top + sr.bottom - sr.top,
		 SWP_NOMOVE | SWP_NOZORDER);

    check_window_size(fe, &x, &y);

    if (fe->statusbar != NULL)
	SetWindowPos(fe->statusbar, NULL, 0, y, x,
		     sr.bottom - sr.top, SWP_NOZORDER);

    if (fe->bitmap) DeleteObject(fe->bitmap);

    hdc = GetDC(fe->hwnd);
    fe->bitmap = CreateCompatibleBitmap(hdc, x, y);
    ReleaseDC(fe->hwnd, hdc);

    midend_redraw(fe->me);
}

static void new_game_type(frontend *fe)
{
    midend_new_game(fe->me);
    new_game_size(fe);
}

static int is_alt_pressed(void)
{
    BYTE keystate[256];
    int r = GetKeyboardState(keystate);
    if (!r)
	return FALSE;
    if (keystate[VK_MENU] & 0x80)
	return TRUE;
    if (keystate[VK_RMENU] & 0x80)
	return TRUE;
    return FALSE;
}

static void savefile_write(void *wctx, void *buf, int len)
{
    FILE *fp = (FILE *)wctx;
    fwrite(buf, 1, len, fp);
}

static int savefile_read(void *wctx, void *buf, int len)
{
    FILE *fp = (FILE *)wctx;
    int ret;

    ret = fread(buf, 1, len, fp);
    return (ret == len);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    frontend *fe = (frontend *)GetWindowLong(hwnd, GWL_USERDATA);
    int cmd;

    switch (message) {
      case WM_CLOSE:
	DestroyWindow(hwnd);
	return 0;
      case WM_COMMAND:
	cmd = wParam & ~0xF;	       /* low 4 bits reserved to Windows */
	switch (cmd) {
	  case IDM_NEW:
	    if (!midend_process_key(fe->me, 0, 0, 'n'))
		PostQuitMessage(0);
	    break;
	  case IDM_RESTART:
	    midend_restart_game(fe->me);
	    break;
	  case IDM_UNDO:
	    if (!midend_process_key(fe->me, 0, 0, 'u'))
		PostQuitMessage(0);
	    break;
	  case IDM_REDO:
	    if (!midend_process_key(fe->me, 0, 0, '\x12'))
		PostQuitMessage(0);
	    break;
	  case IDM_COPY:
	    {
		char *text = midend_text_format(fe->me);
		if (text)
		    write_clip(hwnd, text);
		else
		    MessageBeep(MB_ICONWARNING);
		sfree(text);
	    }
	    break;
	  case IDM_SOLVE:
	    {
		char *msg = midend_solve(fe->me);
		if (msg)
		    MessageBox(hwnd, msg, "Unable to solve",
			       MB_ICONERROR | MB_OK);
	    }
	    break;
	  case IDM_QUIT:
	    if (!midend_process_key(fe->me, 0, 0, 'q'))
		PostQuitMessage(0);
	    break;
	  case IDM_CONFIG:
	    if (get_config(fe, CFG_SETTINGS))
		new_game_type(fe);
	    break;
	  case IDM_SEED:
	    if (get_config(fe, CFG_SEED))
		new_game_type(fe);
	    break;
	  case IDM_DESC:
	    if (get_config(fe, CFG_DESC))
		new_game_type(fe);
	    break;
	  case IDM_PRINT:
	    if (get_config(fe, CFG_PRINT))
		print(fe);
	    break;
          case IDM_ABOUT:
	    about(fe);
            break;
	  case IDM_LOAD:
	  case IDM_SAVE:
	    {
		OPENFILENAME of;
		char filename[FILENAME_MAX];
		int ret;

		memset(&of, 0, sizeof(of));
		of.hwndOwner = hwnd;
		of.lpstrFilter = "All Files (*.*)\0*\0\0\0";
		of.lpstrCustomFilter = NULL;
		of.nFilterIndex = 1;
		of.lpstrFile = filename;
		filename[0] = '\0';
		of.nMaxFile = lenof(filename);
		of.lpstrFileTitle = NULL;
		of.lpstrTitle = (cmd == IDM_SAVE ?
				 "Enter name of game file to save" :
				 "Enter name of saved game file to load");
		of.Flags = 0;
#ifdef OPENFILENAME_SIZE_VERSION_400
		of.lStructSize = OPENFILENAME_SIZE_VERSION_400;
#else
		of.lStructSize = sizeof(of);
#endif
		of.lpstrInitialDir = NULL;

		if (cmd == IDM_SAVE)
		    ret = GetSaveFileName(&of);
		else
		    ret = GetOpenFileName(&of);

		if (ret) {
		    if (cmd == IDM_SAVE) {
			FILE *fp;

			if ((fp = fopen(filename, "r")) != NULL) {
			    char buf[256 + FILENAME_MAX];
			    fclose(fp);
			    /* file exists */

			    sprintf(buf, "Are you sure you want to overwrite"
				    " the file \"%.*s\"?",
				    FILENAME_MAX, filename);
			    if (MessageBox(hwnd, buf, "Question",
					   MB_YESNO | MB_ICONQUESTION)
				!= IDYES)
				break;
			}

			fp = fopen(filename, "w");

			if (!fp) {
			    MessageBox(hwnd, "Unable to open save file",
				       "Error", MB_ICONERROR | MB_OK);
			    break;
			}

			midend_serialise(fe->me, savefile_write, fp);

			fclose(fp);
		    } else {
			FILE *fp = fopen(filename, "r");
			char *err;

			if (!fp) {
			    MessageBox(hwnd, "Unable to open saved game file",
				       "Error", MB_ICONERROR | MB_OK);
			    break;
			}

			err = midend_deserialise(fe->me, savefile_read, fp);

			fclose(fp);

			if (err) {
			    MessageBox(hwnd, err, "Error", MB_ICONERROR|MB_OK);
			    break;
			}

			new_game_size(fe);
		    }
		}
	    }

	    break;
          case IDM_HELPC:
	    start_help(fe, NULL);
	    break;
          case IDM_GAMEHELP:
	    start_help(fe, help_topic);
            break;
	  default:
	    {
		int p = ((wParam &~ 0xF) - IDM_PRESETS) / 0x10;

		if (p >= 0 && p < fe->npresets) {
		    midend_set_params(fe->me, fe->presets[p]);
		    new_game_type(fe);
		}
	    }
	    break;
	}
	break;
      case WM_DESTROY:
	stop_help(fe);
	PostQuitMessage(0);
	return 0;
      case WM_PAINT:
	{
	    PAINTSTRUCT p;
	    HDC hdc, hdc2;
	    HBITMAP prevbm;

	    hdc = BeginPaint(hwnd, &p);
	    hdc2 = CreateCompatibleDC(hdc);
	    prevbm = SelectObject(hdc2, fe->bitmap);
	    BitBlt(hdc,
		   p.rcPaint.left, p.rcPaint.top,
		   p.rcPaint.right - p.rcPaint.left,
		   p.rcPaint.bottom - p.rcPaint.top,
		   hdc2,
		   p.rcPaint.left, p.rcPaint.top,
		   SRCCOPY);
	    SelectObject(hdc2, prevbm);
	    DeleteDC(hdc2);
	    EndPaint(hwnd, &p);
	}
	return 0;
      case WM_KEYDOWN:
	{
	    int key = -1;
            BYTE keystate[256];
            int r = GetKeyboardState(keystate);
            int shift = (r && (keystate[VK_SHIFT] & 0x80)) ? MOD_SHFT : 0;
            int ctrl = (r && (keystate[VK_CONTROL] & 0x80)) ? MOD_CTRL : 0;

	    switch (wParam) {
	      case VK_LEFT:
		if (!(lParam & 0x01000000))
		    key = MOD_NUM_KEYPAD | '4';
                else
		    key = shift | ctrl | CURSOR_LEFT;
		break;
	      case VK_RIGHT:
		if (!(lParam & 0x01000000))
		    key = MOD_NUM_KEYPAD | '6';
                else
		    key = shift | ctrl | CURSOR_RIGHT;
		break;
	      case VK_UP:
		if (!(lParam & 0x01000000))
		    key = MOD_NUM_KEYPAD | '8';
                else
		    key = shift | ctrl | CURSOR_UP;
		break;
	      case VK_DOWN:
		if (!(lParam & 0x01000000))
		    key = MOD_NUM_KEYPAD | '2';
                else
		    key = shift | ctrl | CURSOR_DOWN;
		break;
		/*
		 * Diagonal keys on the numeric keypad.
		 */
	      case VK_PRIOR:
		if (!(lParam & 0x01000000)) key = MOD_NUM_KEYPAD | '9';
		break;
	      case VK_NEXT:
		if (!(lParam & 0x01000000)) key = MOD_NUM_KEYPAD | '3';
		break;
	      case VK_HOME:
		if (!(lParam & 0x01000000)) key = MOD_NUM_KEYPAD | '7';
		break;
	      case VK_END:
		if (!(lParam & 0x01000000)) key = MOD_NUM_KEYPAD | '1';
		break;
	      case VK_INSERT:
		if (!(lParam & 0x01000000)) key = MOD_NUM_KEYPAD | '0';
		break;
	      case VK_CLEAR:
		if (!(lParam & 0x01000000)) key = MOD_NUM_KEYPAD | '5';
		break;
		/*
		 * Numeric keypad keys with Num Lock on.
		 */
	      case VK_NUMPAD4: key = MOD_NUM_KEYPAD | '4'; break;
	      case VK_NUMPAD6: key = MOD_NUM_KEYPAD | '6'; break;
	      case VK_NUMPAD8: key = MOD_NUM_KEYPAD | '8'; break;
	      case VK_NUMPAD2: key = MOD_NUM_KEYPAD | '2'; break;
	      case VK_NUMPAD5: key = MOD_NUM_KEYPAD | '5'; break;
	      case VK_NUMPAD9: key = MOD_NUM_KEYPAD | '9'; break;
	      case VK_NUMPAD3: key = MOD_NUM_KEYPAD | '3'; break;
	      case VK_NUMPAD7: key = MOD_NUM_KEYPAD | '7'; break;
	      case VK_NUMPAD1: key = MOD_NUM_KEYPAD | '1'; break;
	      case VK_NUMPAD0: key = MOD_NUM_KEYPAD | '0'; break;
	    }

	    if (key != -1) {
		if (!midend_process_key(fe->me, 0, 0, key))
		    PostQuitMessage(0);
	    } else {
		MSG m;
		m.hwnd = hwnd;
		m.message = WM_KEYDOWN;
		m.wParam = wParam;
		m.lParam = lParam & 0xdfff;
		TranslateMessage(&m);
	    }
	}
	break;
      case WM_LBUTTONDOWN:
      case WM_RBUTTONDOWN:
      case WM_MBUTTONDOWN:
	{
	    int button;

	    /*
	     * Shift-clicks count as middle-clicks, since otherwise
	     * two-button Windows users won't have any kind of
	     * middle click to use.
	     */
	    if (message == WM_MBUTTONDOWN || (wParam & MK_SHIFT))
		button = MIDDLE_BUTTON;
	    else if (message == WM_RBUTTONDOWN || is_alt_pressed())
		button = RIGHT_BUTTON;
	    else
		button = LEFT_BUTTON;

	    if (!midend_process_key(fe->me, (signed short)LOWORD(lParam),
				    (signed short)HIWORD(lParam), button))
		PostQuitMessage(0);

	    SetCapture(hwnd);
	}
	break;
      case WM_LBUTTONUP:
      case WM_RBUTTONUP:
      case WM_MBUTTONUP:
	{
	    int button;

	    /*
	     * Shift-clicks count as middle-clicks, since otherwise
	     * two-button Windows users won't have any kind of
	     * middle click to use.
	     */
	    if (message == WM_MBUTTONUP || (wParam & MK_SHIFT))
		button = MIDDLE_RELEASE;
	    else if (message == WM_RBUTTONUP || is_alt_pressed())
		button = RIGHT_RELEASE;
	    else
		button = LEFT_RELEASE;

	    if (!midend_process_key(fe->me, (signed short)LOWORD(lParam),
				    (signed short)HIWORD(lParam), button))
		PostQuitMessage(0);

	    ReleaseCapture();
	}
	break;
      case WM_MOUSEMOVE:
	{
	    int button;

	    if (wParam & (MK_MBUTTON | MK_SHIFT))
		button = MIDDLE_DRAG;
	    else if (wParam & MK_RBUTTON || is_alt_pressed())
		button = RIGHT_DRAG;
	    else
		button = LEFT_DRAG;
	    
	    if (!midend_process_key(fe->me, (signed short)LOWORD(lParam),
				    (signed short)HIWORD(lParam), button))
		PostQuitMessage(0);
	}
	break;
      case WM_CHAR:
	if (!midend_process_key(fe->me, 0, 0, (unsigned char)wParam))
	    PostQuitMessage(0);
	return 0;
      case WM_TIMER:
	if (fe->timer) {
	    DWORD now = GetTickCount();
	    float elapsed = (float) (now - fe->timer_last_tickcount) * 0.001F;
	    midend_timer(fe->me, elapsed);
	    fe->timer_last_tickcount = now;
	}
	return 0;
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    MSG msg;
    char *error;

    InitCommonControls();

    if (!prev) {
	WNDCLASS wndclass;

	wndclass.style = 0;
	wndclass.lpfnWndProc = WndProc;
	wndclass.cbClsExtra = 0;
	wndclass.cbWndExtra = 0;
	wndclass.hInstance = inst;
	wndclass.hIcon = LoadIcon(inst, MAKEINTRESOURCE(200));
	if (!wndclass.hIcon)	       /* in case resource file is absent */
	    wndclass.hIcon = LoadIcon(inst, IDI_APPLICATION);
	wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
	wndclass.hbrBackground = NULL;
	wndclass.lpszMenuName = NULL;
	wndclass.lpszClassName = thegame.name;

	RegisterClass(&wndclass);
    }

    while (*cmdline && isspace((unsigned char)*cmdline))
	cmdline++;

    init_help();

    if (!new_window(inst, *cmdline ? cmdline : NULL, &error)) {
	char buf[128];
	sprintf(buf, "%.100s Error", thegame.name);
	MessageBox(NULL, error, buf, MB_OK|MB_ICONERROR);
	return 1;
    }

    while (GetMessage(&msg, NULL, 0, 0)) {
	DispatchMessage(&msg);
    }

    cleanup_help();

    return msg.wParam;
}
