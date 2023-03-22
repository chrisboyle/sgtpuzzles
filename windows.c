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
#define IDM_GAMES     0x0300

#define IDM_KEYEMUL   0x0400

#define HELP_FILE_NAME  "puzzles.hlp"
#define HELP_CNT_NAME   "puzzles.cnt"
#ifndef NO_HTMLHELP
#define CHM_FILE_NAME   "puzzles.chm"
#endif /* NO_HTMLHELP */

#ifndef NO_HTMLHELP
typedef HWND (CALLBACK *htmlhelp_t)(HWND, LPCSTR, UINT, DWORD);
static htmlhelp_t htmlhelp;
static HINSTANCE hh_dll;
#endif /* NO_HTMLHELP */
enum { NONE, HLP, CHM } help_type;
char *help_path;
bool help_has_contents;

#ifndef FILENAME_MAX
#define	FILENAME_MAX	(260)
#endif

#ifndef HGDI_ERROR
#define HGDI_ERROR ((HANDLE)GDI_ERROR)
#endif

#ifdef COMBINED
#define CLASSNAME "Puzzles"
#else
#define CLASSNAME thegame.name
#endif

#ifdef DEBUGGING
static FILE *debug_fp = NULL;
static HANDLE debug_hdl = INVALID_HANDLE_VALUE;
static int debug_got_console = 0;

static void dputs(char *buf)
{
    /*DWORD dw;

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
    if (debug_fp) {
        fputs(buf, debug_fp);
        fflush(debug_fp);
    }*/
    OutputDebugString(buf);
}

void debug_printf(const char *fmt, ...)
{
    char buf[4096];
    va_list ap;
    static int debugging = -1;

    if (debugging == -1)
        debugging = getenv_bool("DEBUG_PUZZLES", false);

    if (debugging) {
        va_start(ap, fmt);
        _vsnprintf(buf, 4095, fmt, ap);
	dputs(buf);
        va_end(ap);
    }
}
#endif

#define WINFLAGS (WS_OVERLAPPEDWINDOW &~ \
		      (WS_MAXIMIZEBOX | WS_OVERLAPPED))

static void new_game_size(frontend *fe, float scale);

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

struct preset_menuitemref {
    HMENU which_menu;
    int item_index;
};

struct frontend {
    const game *game;
    midend *me;
    HWND hwnd, statusbar, cfgbox;
    HINSTANCE inst;
    HBITMAP bitmap, prevbm;
    RECT bitmapPosition;  /* game bitmap position within game window */
    HDC hdc;
    COLORREF *colours;
    HBRUSH *brushes;
    HPEN *pens;
    HRGN clip;
    HMENU gamemenu, typemenu;
    UINT timer;
    DWORD timer_last_tickcount;
    struct preset_menu *preset_menu;
    struct preset_menuitemref *preset_menuitems;
    int n_preset_menuitems;
    struct font *fonts;
    int nfonts, fontsize;
    config_item *cfg;
    struct cfg_aux *cfgaux;
    int cfg_which, dlg_done;
    HFONT cfgfont;
    HBRUSH oldbr;
    HPEN oldpen;
    bool help_running;
    enum { DRAWING, PRINTING, NOTHING } drawstatus;
    DOCINFO di;
    int printcount, printw, printh;
    bool printsolns, printcurr, printcolour;
    float printscale;
    int printoffsetx, printoffsety;
    float printpixelscale;
    int fontstart;
    int linewidth;
    bool linedotted;
    drawing *dr;
    int xmin, ymin;
    float puzz_scale;
};

void frontend_free(frontend *fe)
{
    midend_free(fe->me);

    sfree(fe->colours);
    sfree(fe->brushes);
    sfree(fe->pens);
    sfree(fe->fonts);

    sfree(fe);
}

static void update_type_menu_tick(frontend *fe);
static void update_copy_menu_greying(frontend *fe);

void fatal(const char *fmt, ...)
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
    SYSTEMTIME *st = snew(SYSTEMTIME);

    GetLocalTime(st);

    *randseed = (void *)st;
    *randseedsize = sizeof(SYSTEMTIME);
}

static void win_status_bar(void *handle, const char *text)
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
	print_get_colour(fe->dr, colour, fe->printcolour, &hatch, &r, &g, &b);

	/*
	 * Displaying text in hatched colours is not permitted.
	 */
	assert(hatch < 0);

	SetTextColor(fe->hdc, RGB(r * 255, g * 255, b * 255));
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
	print_get_colour(fe->dr, colour, fe->printcolour, &hatch, &r, &g, &b);

	if (hatch < 0) {
	    br = CreateSolidBrush(RGB(r * 255, g * 255, b * 255));
	} else {
	    br = CreateHatchBrush(hatch == HATCH_BACKSLASH ? HS_FDIAGONAL :
				  hatch == HATCH_SLASH ? HS_BDIAGONAL :
				  hatch == HATCH_HORIZ ? HS_HORIZONTAL :
				  hatch == HATCH_VERT ? HS_VERTICAL :
				  hatch == HATCH_PLUS ? HS_CROSS :
				  /* hatch == HATCH_X ? */ HS_DIAGCROSS,
				  RGB(0,0,0));
	}
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

static void win_set_pen(frontend *fe, int colour, bool thin)
{
    HPEN pen;
    assert(fe->drawstatus != NOTHING);

    if (fe->drawstatus == PRINTING) {
	int hatch;
	float r, g, b;
	int width = thin ? 0 : fe->linewidth;

	if (fe->linedotted)
	    width = 0;

	print_get_colour(fe->dr, colour, fe->printcolour, &hatch, &r, &g, &b);
	/*
	 * Stroking in hatched colours is not permitted.
	 */
	assert(hatch < 0);
	pen = CreatePen(fe->linedotted ? PS_DOT : PS_SOLID,
			width, RGB(r * 255, g * 255, b * 255));
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
			  int fontsize, int align, int colour,
                          const char *text)
{
    frontend *fe = (frontend *)handle;
    POINT xy;
    int i;
    LOGFONT lf;

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

        memset (&lf, 0, sizeof(LOGFONT));
        lf.lfHeight = -fontsize;
        lf.lfWeight = (fe->drawstatus == PRINTING ? 0 : FW_BOLD);
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfOutPrecision = OUT_DEFAULT_PRECIS;
        lf.lfClipPrecision = CLIP_DEFAULT_PRECIS;
        lf.lfQuality = DEFAULT_QUALITY;
        lf.lfPitchAndFamily = (fonttype == FONT_FIXED ?
                               FIXED_PITCH | FF_DONTCARE :
                               VARIABLE_PITCH | FF_SWISS);

        fe->fonts[i].font = CreateFontIndirect(&lf);
    }

    /*
     * Position and draw the text.
     */
    {
	HFONT oldfont;
	TEXTMETRIC tm;
	SIZE size;
	WCHAR wText[256];
	MultiByteToWideChar (CP_UTF8, 0, text, -1, wText, 256);

	oldfont = SelectObject(fe->hdc, fe->fonts[i].font);
	if (GetTextMetrics(fe->hdc, &tm)) {
	    if (align & ALIGN_VCENTRE)
		xy.y -= (tm.tmAscent+tm.tmDescent)/2;
	    else
		xy.y -= tm.tmAscent;
	}
	if (GetTextExtentPoint32W(fe->hdc, wText, wcslen(wText), &size))
	{
	    if (align & ALIGN_HCENTRE)
		xy.x -= size.cx / 2;
	    else if (align & ALIGN_HRIGHT)
		xy.x -= size.cx;
	}
	SetBkMode(fe->hdc, TRANSPARENT);
	win_text_colour(fe, colour);
	ExtTextOutW(fe->hdc, xy.x, xy.y, 0, NULL, wText, wcslen(wText), NULL);
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
	win_set_pen(fe, colour, true);
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
    POINT pp[2];

    if (fe->drawstatus == NOTHING)
	return;

    win_set_pen(fe, colour, false);
    pp[0] = win_transform_point(fe, x1, y1);
    pp[1] = win_transform_point(fe, x2, y2);
    Polyline(fe->hdc, pp, 2);
    if (fe->drawstatus == DRAWING)
	SetPixel(fe->hdc, pp[1].x, pp[1].y, fe->colours[colour]);
    win_reset_pen(fe);
}

static void win_draw_circle(void *handle, int cx, int cy, int radius,
			    int fillcolour, int outlinecolour)
{
    frontend *fe = (frontend *)handle;
    POINT p, q;

    assert(outlinecolour >= 0);

    if (fe->drawstatus == NOTHING)
	return;

    if (fillcolour >= 0)
	win_set_brush(fe, fillcolour);
    else
	fe->oldbr = SelectObject(fe->hdc, GetStockObject(NULL_BRUSH));

    win_set_pen(fe, outlinecolour, false);
    p = win_transform_point(fe, cx - radius, cy - radius);
    q = win_transform_point(fe, cx + radius, cy + radius);
    Ellipse(fe->hdc, p.x, p.y, q.x+1, q.y+1);
    win_reset_brush(fe);
    win_reset_pen(fe);
}

static void win_draw_polygon(void *handle, const int *coords, int npoints,
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
	win_set_pen(fe, outlinecolour, false);
	Polygon(fe->hdc, pts, npoints);
	win_reset_brush(fe);
	win_reset_pen(fe);
    } else {
	win_set_pen(fe, outlinecolour, false);
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

    OffsetRect(&r, fe->bitmapPosition.left, fe->bitmapPosition.top);
    InvalidateRect(fe->hwnd, &r, false);
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

static void win_line_dotted(void *handle, bool dotted)
{
    frontend *fe = (frontend *)handle;

    assert(fe->drawstatus != DRAWING);
    if (fe->drawstatus == NOTHING)
	return;

    fe->linedotted = dotted;
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
    poy = (int)(mmoy * (float)pph / mmph);

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
    fe->linedotted = false;
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

char *win_text_fallback(void *handle, const char *const *strings, int nstrings)
{
    /*
     * We assume Windows can cope with any UTF-8 likely to be
     * emitted by a puzzle.
     */
    return dupstr(strings[0]);
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
    win_line_dotted,
    win_text_fallback,
};

void print(frontend *fe)
{
    PRINTDLG pd;
    char doctitle[256];
    document *doc;
    midend *nme = NULL;  /* non-interactive midend for bulk puzzle generation */
    int i;
    const char *err = NULL;

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

		nme = midend_new(NULL, fe->game, NULL, NULL);

		/*
		 * Set the non-interactive mid-end to have the same
		 * parameters as the standard one.
		 */
		params = midend_get_params(fe->me);
		midend_set_params(nme, params);
		fe->game->free_params(params);
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
	    " Portable Puzzle Collection)", fe->game->name);
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
	fe->timer = SetTimer(fe->hwnd, 1, 20, NULL);
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
    if (!clipdata) {
        sfree(data2);
	return;
    }
    lock = GlobalLock(clipdata);
    if (!lock) {
        GlobalFree(clipdata);
        sfree(data2);
	return;
    }
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
	    help_path = dupstr(b);
	    help_type = CHM;
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

	/*
	 * See if there's a .CNT file alongside it.
	 */
	strcpy(r, HELP_CNT_NAME);
	if ( (fp = fopen(b, "r")) != NULL) {
	    fclose(fp);
	    help_has_contents = true;
	} else
	    help_has_contents = false;

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
	WinHelp(fe->hwnd, help_path, cmd, (ULONG_PTR)str);
	fe->help_running = true;
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
	fe->help_running = true;
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
	fe->help_running = false;
    }
}

/*
 * Terminate Help on process exit.
 */
static void cleanup_help(void)
{
    /* Nothing to do currently.
     * (If we were running HTML Help single-threaded, this is where we'd
     * call HH_UNINITIALIZE.) */
}

static int get_statusbar_height(frontend *fe)
{
    int sy;
    if (fe->statusbar) {
	RECT sr;
	GetWindowRect(fe->statusbar, &sr);
	sy = sr.bottom - sr.top;
    } else {
	sy = 0;
    }
    return sy;
}

static void adjust_statusbar(frontend *fe, RECT *r)
{
    int sy;

    if (!fe->statusbar) return;

    sy = get_statusbar_height(fe);
    SetWindowPos(fe->statusbar, NULL, 0, r->bottom-r->top-sy, r->right-r->left,
                 sy, SWP_NOZORDER);
}

static void get_menu_size(HWND wh, RECT *r)
{
    HMENU bar = GetMenu(wh);
    RECT rect;
    int i;

    SetRect(r, 0, 0, 0, 0);
    for (i = 0; i < GetMenuItemCount(bar); i++) {
        GetMenuItemRect(wh, bar, i, &rect);
        UnionRect(r, r, &rect);
    }
}

/*
 * Given a proposed new puzzle size (cx,cy), work out the actual
 * puzzle size that would be (px,py) and the window size including
 * furniture (wx,wy).
 */

static bool check_window_resize(frontend *fe, int cx, int cy,
                                int *px, int *py, int *wx, int *wy)
{
    RECT r;
    int x, y, sy = get_statusbar_height(fe);
    bool changed = false;

    /* disallow making window thinner than menu bar */
    x = max(cx, fe->xmin);
    y = max(cy - sy, fe->ymin);

    /*
     * See if we actually got the window size we wanted, and adjust
     * the puzzle size if not.
     */
    midend_size(fe->me, &x, &y, true, 1.0);
    if (x != cx || y != cy) {
        /*
         * Resize the window, now we know what size we _really_
         * want it to be.
         */
        r.left = r.top = 0;
        r.right = x;
        r.bottom = y + sy;
        AdjustWindowRectEx(&r, WINFLAGS, true, 0);
        *wx = r.right - r.left;
        *wy = r.bottom - r.top;
        changed = true;
    }

    *px = x;
    *py = y;

    fe->puzz_scale =
      (float)midend_tilesize(fe->me) / (float)fe->game->preferred_tilesize;

    return changed;
}

/*
 * Given the current window size, make sure it's sane for the
 * current puzzle and resize if necessary.
 */

static void check_window_size(frontend *fe, int *px, int *py)
{
    RECT r;
    int wx, wy, cx, cy;

    GetClientRect(fe->hwnd, &r);
    cx = r.right - r.left;
    cy = r.bottom - r.top;

    if (check_window_resize(fe, cx, cy, px, py, &wx, &wy))
        SetWindowPos(fe->hwnd, NULL, 0, 0, wx, wy, SWP_NOMOVE | SWP_NOZORDER);

    GetClientRect(fe->hwnd, &r);
    adjust_statusbar(fe, &r);
}

static void get_max_puzzle_size(frontend *fe, int *x, int *y)
{
    RECT r, sr;

    if (SystemParametersInfo(SPI_GETWORKAREA, 0, &sr, false)) {
	*x = sr.right - sr.left;
	*y = sr.bottom - sr.top;
	r.left = 100;
	r.right = 200;
	r.top = 100;
	r.bottom = 200;
	AdjustWindowRectEx(&r, WINFLAGS, true, 0);
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

/*
 * Allocate a new frontend structure and create its main window.
 */
static frontend *frontend_new(HINSTANCE inst)
{
    frontend *fe;
    const char *nogame = "Puzzles (no game selected)";

    fe = snew(frontend);

    fe->inst = inst;

    fe->game = NULL;
    fe->me = NULL;

    fe->timer = 0;
    fe->hwnd = NULL;

    fe->help_running = false;

    fe->drawstatus = NOTHING;
    fe->dr = NULL;
    fe->fontstart = 0;

    fe->fonts = NULL;
    fe->nfonts = fe->fontsize = 0;

    fe->colours = NULL;
    fe->brushes = NULL;
    fe->pens = NULL;

    fe->puzz_scale = 1.0;

    fe->hwnd = CreateWindowEx(0, CLASSNAME, nogame,
			      WS_OVERLAPPEDWINDOW &~
			      (WS_MAXIMIZEBOX),
			      CW_USEDEFAULT, CW_USEDEFAULT,
			      CW_USEDEFAULT, CW_USEDEFAULT,
			      NULL, NULL, inst, NULL);
    if (!fe->hwnd) {
        DWORD lerr = GetLastError();
        printf("no window: 0x%x\n", (unsigned)lerr);
    }

    fe->gamemenu = NULL;
    fe->preset_menu = NULL;

    fe->statusbar = NULL;
    fe->bitmap = NULL;

    SetWindowLongPtr(fe->hwnd, GWLP_USERDATA, (LONG_PTR)fe);

    return fe;
}

static void savefile_write(void *wctx, const void *buf, int len)
{
    FILE *fp = (FILE *)wctx;
    fwrite(buf, 1, len, fp);
}

static bool savefile_read(void *wctx, void *buf, int len)
{
    FILE *fp = (FILE *)wctx;
    int ret;

    ret = fread(buf, 1, len, fp);
    return (ret == len);
}

/*
 * Create an appropriate midend structure to go in a puzzle window,
 * given a game type and/or a command-line argument.
 *
 * 'arg' can be either a game ID string (descriptive, random, or a
 * plain set of parameters) or the filename of a save file. The two
 * boolean flag arguments indicate which possibilities are
 * permissible.
 */
static midend *midend_for_new_game(frontend *fe, const game *cgame,
                                   char *arg, bool maybe_game_id,
                                   bool maybe_save_file, char **error)
{
    midend *me = NULL;

    if (!arg) {
        if (me) midend_free(me);
        me = midend_new(fe, cgame, &win_drawing, fe);
        midend_new_game(me);
    } else {
        FILE *fp;
        const char *err_param, *err_load;

        /*
         * See if arg is a valid filename of a save game file.
         */
        err_load = NULL;
        if (maybe_save_file && (fp = fopen(arg, "r")) != NULL) {
            const game *loadgame;

#ifdef COMBINED
            /*
             * Find out what kind of game is stored in the save
             * file; if we're going to end up loading that, it
             * will have to override our caller's judgment as to
             * what game to initialise our midend with.
             */
            char *id_name;
            err_load = identify_game(&id_name, savefile_read, fp);
            if (!err_load) {
                int i;
                for (i = 0; i < gamecount; i++)
                    if (!strcmp(id_name, gamelist[i]->name))
                        break;
                if (i == gamecount) {
                    err_load = "Save file is for a game not supported by"
                        " this program";
                } else {
                    loadgame = gamelist[i];
                    rewind(fp); /* go back to the start for actual load */
                }
            }
#else
            loadgame = cgame;
#endif
            if (!err_load) {
                if (me) midend_free(me);
                me = midend_new(fe, loadgame, &win_drawing, fe);
                err_load = midend_deserialise(me, savefile_read, fp);
            }
        } else {
            err_load = "Unable to open file";
        }

        if (maybe_game_id && (!maybe_save_file || err_load)) {
            /*
             * See if arg is a game description.
             */
            if (me) midend_free(me);
            me = midend_new(fe, cgame, &win_drawing, fe);
            err_param = midend_game_id(me, arg);
            if (!err_param) {
                midend_new_game(me);
            } else {
                if (maybe_save_file) {
                    *error = snewn(256 + strlen(arg) + strlen(err_param) +
                                   strlen(err_load), char);
                    sprintf(*error, "Supplied argument \"%s\" is neither a"
                            " game ID (%s) nor a save file (%s)",
                            arg, err_param, err_load);
                } else {
                    *error = dupstr(err_param);
                }
                midend_free(me);
                sfree(fe);
                return NULL;
            }
        } else if (err_load) {
            *error = dupstr(err_load);
            midend_free(me);
            sfree(fe);
            return NULL;
        }
    }

    return me;
}

static void populate_preset_menu(frontend *fe,
                                 struct preset_menu *menu, HMENU winmenu)
{
    int i;
    for (i = 0; i < menu->n_entries; i++) {
        struct preset_menu_entry *entry = &menu->entries[i];
        UINT_PTR id_or_sub;
        UINT flags = MF_ENABLED;

        if (entry->params) {
            id_or_sub = (UINT_PTR)(IDM_PRESETS + 0x10 * entry->id);

            fe->preset_menuitems[entry->id].which_menu = winmenu;
            fe->preset_menuitems[entry->id].item_index =
                GetMenuItemCount(winmenu);
        } else {
            HMENU winsubmenu = CreateMenu();
            id_or_sub = (UINT_PTR)winsubmenu;
            flags |= MF_POPUP;

            populate_preset_menu(fe, entry->submenu, winsubmenu);
        }

        /*
         * FIXME: we ought to go through and do something with ampersands
         * here.
         */

        AppendMenu(winmenu, flags, id_or_sub, entry->title);
    }
}

/*
 * Populate a frontend structure with a new midend structure, and
 * create any window furniture that it needs.
 *
 * Previously-allocated memory and window furniture will be freed by
 * this function.
 *
 */
static int fe_set_midend(frontend *fe, midend *me)
{
    int x, y;
    RECT r;

    if (fe->me) {
        midend_free(fe->me);
        fe->preset_menu = NULL;
        sfree(fe->preset_menuitems);
    }
    fe->me = me;
    fe->game = midend_which_game(fe->me);

    {
	int i, ncolours;
        float *colours;

        colours = midend_colours(fe->me, &ncolours);

        if (fe->colours) sfree(fe->colours);
        if (fe->brushes) sfree(fe->brushes);
        if (fe->pens) sfree(fe->pens);

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

    if (fe->statusbar)
        DestroyWindow(fe->statusbar);
    if (midend_wants_statusbar(fe->me)) {
	fe->statusbar = CreateWindowEx(0, STATUSCLASSNAME,
				       TEXT(DEFAULT_STATUSBAR_TEXT),
				       WS_CHILD | WS_VISIBLE,
				       0, 0, 0, 0, /* status bar does these */
				       NULL, NULL, fe->inst, NULL);
    } else
        fe->statusbar = NULL;

    get_max_puzzle_size(fe, &x, &y);
    midend_size(fe->me, &x, &y, false, 1.0);

    r.left = r.top = 0;
    r.right = x;
    r.bottom = y;
    AdjustWindowRectEx(&r, WINFLAGS, true, 0);

    SetWindowText(fe->hwnd, fe->game->name);

    if (fe->statusbar)
        DestroyWindow(fe->statusbar);
    if (midend_wants_statusbar(fe->me)) {
	RECT sr;
	fe->statusbar = CreateWindowEx(0, STATUSCLASSNAME, TEXT("ooh"),
				       WS_CHILD | WS_VISIBLE,
				       0, 0, 0, 0, /* status bar does these */
				       fe->hwnd, NULL, fe->inst, NULL);

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
        HMENU oldmenu = GetMenu(fe->hwnd);

	HMENU bar = CreateMenu();
	HMENU menu = CreateMenu();
        RECT menusize;

	AppendMenu(bar, MF_ENABLED|MF_POPUP, (UINT_PTR)menu, "&Game");
	fe->gamemenu = menu;
	AppendMenu(menu, MF_ENABLED, IDM_NEW, TEXT("&New"));
	AppendMenu(menu, MF_ENABLED, IDM_RESTART, TEXT("&Restart"));
        /* ...here I run out of sensible accelerator characters. */
	AppendMenu(menu, MF_ENABLED, IDM_DESC, TEXT("Speci&fic..."));
	AppendMenu(menu, MF_ENABLED, IDM_SEED, TEXT("Rando&m Seed..."));

        assert(!fe->preset_menu);

        fe->preset_menu = midend_get_presets(
            fe->me, &fe->n_preset_menuitems);
        fe->preset_menuitems = snewn(fe->n_preset_menuitems,
                                     struct preset_menuitemref);
        {
            int i;
            for (i = 0; i < fe->n_preset_menuitems; i++)
                fe->preset_menuitems[i].which_menu = NULL;
        }
	if (fe->preset_menu->n_entries > 0 || fe->game->can_configure) {
	    HMENU sub = CreateMenu();

	    AppendMenu(bar, MF_ENABLED|MF_POPUP, (UINT_PTR)sub, "&Type");

            populate_preset_menu(fe, fe->preset_menu, sub);

	    if (fe->game->can_configure) {
		AppendMenu(sub, MF_ENABLED, IDM_CONFIG, TEXT("&Custom..."));
	    }

	    fe->typemenu = sub;
	} else {
	    fe->typemenu = INVALID_HANDLE_VALUE;
        }

#ifdef COMBINED
        {
            HMENU games = CreateMenu();
            int i;

            AppendMenu(menu, MF_SEPARATOR, 0, 0);
            AppendMenu(menu, MF_ENABLED|MF_POPUP, (UINT_PTR)games, "&Other");
            for (i = 0; i < gamecount; i++) {
                if (strcmp(gamelist[i]->name, fe->game->name) != 0) {
                    /* only include those games that aren't the same as the
                     * game we're currently playing. */
                    AppendMenu(games, MF_ENABLED, IDM_GAMES + i, gamelist[i]->name);
                }
            }
        }
#endif

	AppendMenu(menu, MF_SEPARATOR, 0, 0);
	AppendMenu(menu, MF_ENABLED, IDM_LOAD, TEXT("&Load..."));
	AppendMenu(menu, MF_ENABLED, IDM_SAVE, TEXT("&Save..."));
	AppendMenu(menu, MF_SEPARATOR, 0, 0);
	if (fe->game->can_print) {
	    AppendMenu(menu, MF_ENABLED, IDM_PRINT, TEXT("&Print..."));
	    AppendMenu(menu, MF_SEPARATOR, 0, 0);
	}
	AppendMenu(menu, MF_ENABLED, IDM_UNDO, TEXT("Undo"));
	AppendMenu(menu, MF_ENABLED, IDM_REDO, TEXT("Redo"));
	if (fe->game->can_format_as_text_ever) {
	    AppendMenu(menu, MF_SEPARATOR, 0, 0);
	    AppendMenu(menu, MF_ENABLED, IDM_COPY, TEXT("&Copy"));
	}
	if (fe->game->can_solve) {
	    AppendMenu(menu, MF_SEPARATOR, 0, 0);
	    AppendMenu(menu, MF_ENABLED, IDM_SOLVE, TEXT("Sol&ve"));
	}
	AppendMenu(menu, MF_SEPARATOR, 0, 0);
	AppendMenu(menu, MF_ENABLED, IDM_QUIT, TEXT("E&xit"));
	menu = CreateMenu();
	AppendMenu(bar, MF_ENABLED|MF_POPUP, (UINT_PTR)menu, TEXT("&Help"));
	AppendMenu(menu, MF_ENABLED, IDM_ABOUT, TEXT("&About"));
        if (help_type != NONE) {
            char *item;
            AppendMenu(menu, MF_SEPARATOR, 0, 0);
            AppendMenu(menu, MF_ENABLED, IDM_HELPC, TEXT("&Contents"));
            assert(fe->game->name);
            item = snewn(10+strlen(fe->game->name), char); /*ick*/
            sprintf(item, "&Help on %s", fe->game->name);
            AppendMenu(menu, MF_ENABLED, IDM_GAMEHELP, item);
            sfree(item);
        }
        DestroyMenu(oldmenu);
	SetMenu(fe->hwnd, bar);
        get_menu_size(fe->hwnd, &menusize);
        fe->xmin = (menusize.right - menusize.left) + 25;
    }

    if (fe->bitmap) DeleteObject(fe->bitmap);
    fe->bitmap = NULL;
    new_game_size(fe, fe->puzz_scale); /* initialises fe->bitmap */

    return 0;
}

static void show_window(frontend *fe)
{
    ShowWindow(fe->hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(fe->hwnd);

    update_type_menu_tick(fe);
    update_copy_menu_greying(fe);

    midend_redraw(fe->me);
}

static int CALLBACK AboutDlgProc(HWND hwnd, UINT msg,
				 WPARAM wParam, LPARAM lParam)
{
    frontend *fe = (frontend *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    switch (msg) {
      case WM_INITDIALOG:
	return 1;

      case WM_COMMAND:
	if (LOWORD(wParam) == IDOK)
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

	*wintitle = snewn(40 + strlen(fe->game->name), char);
	sprintf(*wintitle, "%s print setup", fe->game->name);

	ret = snewn(8, config_item);

	i = 0;

	ret[i].name = "Number of puzzles to print";
	ret[i].type = C_STRING;
	ret[i].u.string.sval = dupstr("1");
	i++;

	ret[i].name = "Number of puzzles across the page";
	ret[i].type = C_STRING;
	ret[i].u.string.sval = dupstr("1");
	i++;

	ret[i].name = "Number of puzzles down the page";
	ret[i].type = C_STRING;
	ret[i].u.string.sval = dupstr("1");
	i++;

	ret[i].name = "Percentage of standard size";
	ret[i].type = C_STRING;
	ret[i].u.string.sval = dupstr("100.0");
	i++;

	ret[i].name = "Include currently shown puzzle";
	ret[i].type = C_BOOLEAN;
	ret[i].u.boolean.bval = true;
	i++;

	ret[i].name = "Print solutions";
	ret[i].type = C_BOOLEAN;
	ret[i].u.boolean.bval = false;
	i++;

	if (fe->game->can_print_in_colour) {
	    ret[i].name = "Print in colour";
	    ret[i].type = C_BOOLEAN;
	    ret[i].u.boolean.bval = false;
	    i++;
	}

	ret[i].name = NULL;
	ret[i].type = C_END;
	i++;

	return ret;
    } else {
	assert(!"We should never get here");
	return NULL;
    }
}

static const char *frontend_set_config(
    frontend *fe, int which, config_item *cfg)
{
    if (which < CFG_FRONTEND_SPECIFIC) {
	return midend_set_config(fe->me, which, cfg);
    } else if (which == CFG_PRINT) {
	if ((fe->printcount = atoi(cfg[0].u.string.sval)) <= 0)
	    return "Number of puzzles to print should be at least one";
	if ((fe->printw = atoi(cfg[1].u.string.sval)) <= 0)
	    return "Number of puzzles across the page should be at least one";
	if ((fe->printh = atoi(cfg[2].u.string.sval)) <= 0)
	    return "Number of puzzles down the page should be at least one";
	if ((fe->printscale = (float)atof(cfg[3].u.string.sval)) <= 0)
	    return "Print size should be positive";
	fe->printcurr = cfg[4].u.boolean.bval;
	fe->printsolns = cfg[5].u.boolean.bval;
	fe->printcolour = fe->game->can_print_in_colour &&
            cfg[6].u.boolean.bval;
	return NULL;
    } else {
	assert(!"We should never get here");
	return "Internal error";
    }
}

static int CALLBACK ConfigDlgProc(HWND hwnd, UINT msg,
				  WPARAM wParam, LPARAM lParam)
{
    frontend *fe = (frontend *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    config_item *i;
    struct cfg_aux *j;

    switch (msg) {
      case WM_INITDIALOG:
	return 1;

      case WM_COMMAND:
	/*
	 * OK and Cancel are special cases.
	 */
	if ((LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)) {
	    if (LOWORD(wParam) == IDOK) {
		const char *err = frontend_set_config(
                    fe, fe->cfg_which, fe->cfg);

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
	    sfree(i->u.string.sval);
	    i->u.string.sval = dupstr(buffer);
	} else if (i->type == C_BOOLEAN && 
		   (HIWORD(wParam) == BN_CLICKED ||
		    HIWORD(wParam) == BN_DBLCLK)) {
	    i->u.boolean.bval = IsDlgButtonChecked(fe->cfgbox, j->ctlid);
	} else if (i->type == C_CHOICES &&
		   HIWORD(wParam) == CBN_SELCHANGE) {
	    i->u.choices.selected = SendDlgItemMessage(fe->cfgbox, j->ctlid,
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
	    int exstyle, const char *wtext, INT_PTR wid)
{
    HWND ret;
    ret = CreateWindowEx(exstyle, wclass, wtext,
			 wstyle | WS_CHILD | WS_VISIBLE, x1, y1, x2-x1, y2-y1,
			 fe->cfgbox, (HMENU) wid, fe->inst, NULL);
    SendMessage(ret, WM_SETFONT, (WPARAM)fe->cfgfont, MAKELPARAM(true, 0));
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

    sprintf(titlebuf, "About %.250s", fe->game->name);

    strings[nstrings++] = fe->game->name;
    strings[nstrings++] = "from Simon Tatham's Portable Puzzle Collection";
    strings[nstrings++] = ver;

    wc.style = CS_DBLCLKS | CS_SAVEBITS;
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

    fe->dlg_done = 0;

    fe->cfgfont = CreateFont(-MulDiv(8, GetDeviceCaps(hdc, LOGPIXELSY), 72),
			     0, 0, 0, 0,
			     false, false, false, DEFAULT_CHARSET,
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
			   false, 0);

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

    SendMessage(fe->cfgbox, WM_SETFONT, (WPARAM)fe->cfgfont, false);

    SetWindowLongPtr(fe->cfgbox, GWLP_USERDATA, (LONG_PTR)fe);
    SetWindowLongPtr(fe->cfgbox, DWLP_DLGPROC, (LONG_PTR)AboutDlgProc);

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
	   BS_PUSHBUTTON | WS_TABSTOP | BS_DEFPUSHBUTTON, 0,
	   "OK", IDOK);

    SendMessage(fe->cfgbox, WM_INITDIALOG, 0, 0);

    EnableWindow(fe->hwnd, false);
    ShowWindow(fe->cfgbox, SW_SHOWNORMAL);
    while ((gm=GetMessage(&msg, NULL, 0, 0)) > 0) {
	if (!IsDialogMessage(fe->cfgbox, &msg))
	    DispatchMessage(&msg);
	if (fe->dlg_done)
	    break;
    }
    EnableWindow(fe->hwnd, true);
    SetForegroundWindow(fe->hwnd);
    DestroyWindow(fe->cfgbox);
    DeleteObject(fe->cfgfont);
}

static bool get_config(frontend *fe, int which)
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

    wc.style = CS_DBLCLKS | CS_SAVEBITS;
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

    fe->dlg_done = 0;

    fe->cfgfont = CreateFont(-MulDiv(8, GetDeviceCaps(hdc, LOGPIXELSY), 72),
			     0, 0, 0, 0,
			     false, false, false, DEFAULT_CHARSET,
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
			   false, 0);

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

    SendMessage(fe->cfgbox, WM_SETFONT, (WPARAM)fe->cfgfont, false);

    SetWindowLongPtr(fe->cfgbox, GWLP_USERDATA, (LONG_PTR)fe);
    SetWindowLongPtr(fe->cfgbox, DWLP_DLGPROC, (LONG_PTR)ConfigDlgProc);

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
	    SetWindowText(ctl, i->u.string.sval);
	    y += height*3/2;
	    break;

	  case C_BOOLEAN:
	    /*
	     * Simple checkbox.
	     */
	    mkctrl(fe, col1l, col2r, y, y+height, "BUTTON",
		   BS_NOTIFY | BS_AUTOCHECKBOX | WS_TABSTOP,
		   0, i->name, (j->ctlid = id++));
	    CheckDlgButton(fe->cfgbox, j->ctlid, i->u.boolean.bval);
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
		char c;
                const char *p, *q;
                char *str;

		SendMessage(ctl, CB_RESETCONTENT, 0, 0);
		p = i->u.choices.choicenames;
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

	    SendMessage(ctl, CB_SETCURSEL, i->u.choices.selected, 0);

	    y += height*3/2;
	    break;
	}

	assert(y < winheight);
	y += height/2;
    }

    y += height/2;		       /* extra space before OK and Cancel */
    mkctrl(fe, col1l, (col1l+col2r)/2-width, y, y+height*7/4, "BUTTON",
	   BS_PUSHBUTTON | WS_TABSTOP | BS_DEFPUSHBUTTON, 0,
	   "OK", IDOK);
    mkctrl(fe, (col1l+col2r)/2+width, col2r, y, y+height*7/4, "BUTTON",
	   BS_PUSHBUTTON | WS_TABSTOP, 0, "Cancel", IDCANCEL);

    SendMessage(fe->cfgbox, WM_INITDIALOG, 0, 0);

    EnableWindow(fe->hwnd, false);
    ShowWindow(fe->cfgbox, SW_SHOWNORMAL);
    while ((gm=GetMessage(&msg, NULL, 0, 0)) > 0) {
	if (!IsDialogMessage(fe->cfgbox, &msg))
	    DispatchMessage(&msg);
	if (fe->dlg_done)
	    break;
    }
    EnableWindow(fe->hwnd, true);
    SetForegroundWindow(fe->hwnd);
    DestroyWindow(fe->cfgbox);
    DeleteObject(fe->cfgfont);

    free_cfg(fe->cfg);
    sfree(fe->cfgaux);

    return (fe->dlg_done == 2);
}

static void calculate_bitmap_position(frontend *fe, int x, int y)
{
    /* Plain Windows - position the game in the upper-left corner */
    fe->bitmapPosition.left = 0;
    fe->bitmapPosition.top = 0;
    fe->bitmapPosition.right  = fe->bitmapPosition.left + x;
    fe->bitmapPosition.bottom = fe->bitmapPosition.top  + y;
}

static void new_bitmap(frontend *fe, int x, int y)
{
    HDC hdc;

    if (fe->bitmap) DeleteObject(fe->bitmap);

    hdc = GetDC(fe->hwnd);
    fe->bitmap = CreateCompatibleBitmap(hdc, x, y);
    calculate_bitmap_position(fe, x, y);
    ReleaseDC(fe->hwnd, hdc);
}

static void new_game_size(frontend *fe, float scale)
{
    RECT r, sr;
    int x, y;

    get_max_puzzle_size(fe, &x, &y);
    midend_size(fe->me, &x, &y, false, 1.0);

    if (scale != 1.0) {
        x = (int)((float)x * fe->puzz_scale);
        y = (int)((float)y * fe->puzz_scale);
        midend_size(fe->me, &x, &y, true, 1.0);
    }
    fe->ymin = (fe->xmin * y) / x;

    r.left = r.top = 0;
    r.right = x;
    r.bottom = y;
    AdjustWindowRectEx(&r, WINFLAGS, true, 0);

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

    new_bitmap(fe, x, y);

    midend_redraw(fe->me);
}

/*
 * Given a proposed new window rect, work out the resulting
 * difference in client size (from current), and use to try
 * and resize the puzzle, returning (wx,wy) as the actual
 * new window size.
 */

static void adjust_game_size(frontend *fe, RECT *proposed, bool isedge,
                             int *wx_r, int *wy_r)
{
    RECT cr, wr;
    int nx, ny, xdiff, ydiff, wx, wy;

    /* Work out the current window sizing, and thus the
     * difference in size we're asking for. */
    GetClientRect(fe->hwnd, &cr);
    wr = cr;
    AdjustWindowRectEx(&wr, WINFLAGS, true, 0);

    xdiff = (proposed->right - proposed->left) - (wr.right - wr.left);
    ydiff = (proposed->bottom - proposed->top) - (wr.bottom - wr.top);

    if (isedge) {
        /* These next four lines work around the fact that midend_size
         * is happy to shrink _but not grow_ if you change one dimension
         * but not the other. */
        if (xdiff > 0 && ydiff == 0)
            ydiff = (xdiff * (wr.right - wr.left)) / (wr.bottom - wr.top);
        if (xdiff == 0 && ydiff > 0)
            xdiff = (ydiff * (wr.bottom - wr.top)) / (wr.right - wr.left);
    }

    if (check_window_resize(fe,
                            (cr.right - cr.left) + xdiff,
                            (cr.bottom - cr.top) + ydiff,
                            &nx, &ny, &wx, &wy)) {
        new_bitmap(fe, nx, ny);
        midend_force_redraw(fe->me);
    } else {
        /* reset size to current window size */
        wx = wr.right - wr.left;
        wy = wr.bottom - wr.top;
    }
    /* Re-fetch rectangle; size limits mean we might not have
     * taken it quite to the mouse drag positions. */
    GetClientRect(fe->hwnd, &cr);
    adjust_statusbar(fe, &cr);

    *wx_r = wx; *wy_r = wy;
}

static void update_type_menu_tick(frontend *fe)
{
    int total, n, i;

    if (fe->typemenu == INVALID_HANDLE_VALUE)
	return;

    n = midend_which_preset(fe->me);

    for (i = 0; i < fe->n_preset_menuitems; i++) {
        if (fe->preset_menuitems[i].which_menu) {
            int flag = (i == n ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(fe->preset_menuitems[i].which_menu,
                          fe->preset_menuitems[i].item_index,
                          MF_BYPOSITION | flag);
        }
    }

    if (fe->game->can_configure) {
	int flag = (n < 0 ? MF_CHECKED : MF_UNCHECKED);
        /* "Custom" menu item is at the bottom of the top-level Type menu */
        total = GetMenuItemCount(fe->typemenu);
	CheckMenuItem(fe->typemenu, total - 1, MF_BYPOSITION | flag);
    }

    DrawMenuBar(fe->hwnd);
}

static void update_copy_menu_greying(frontend *fe)
{
    UINT enable = (midend_can_format_as_text_now(fe->me) ?
		   MF_ENABLED : MF_GRAYED);
    EnableMenuItem(fe->gamemenu, IDM_COPY, MF_BYCOMMAND | enable);
}

static void new_game_type(frontend *fe)
{
    midend_new_game(fe->me);
    new_game_size(fe, 1.0);
    update_type_menu_tick(fe);
    update_copy_menu_greying(fe);
}

static bool is_alt_pressed(void)
{
    BYTE keystate[256];
    int r = GetKeyboardState(keystate);
    if (!r)
	return false;
    if (keystate[VK_MENU] & 0x80)
	return true;
    if (keystate[VK_RMENU] & 0x80)
	return true;
    return false;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT message,
				WPARAM wParam, LPARAM lParam)
{
    frontend *fe = (frontend *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    int cmd;

    switch (message) {
      case WM_CLOSE:
	DestroyWindow(hwnd);
	return 0;
      case WM_COMMAND:
	cmd = wParam & ~0xF;	       /* low 4 bits reserved to Windows */
	switch (cmd) {
	  case IDM_NEW:
	    if (!midend_process_key(fe->me, 0, 0, UI_NEWGAME, NULL))
		PostQuitMessage(0);
	    break;
	  case IDM_RESTART:
	    midend_restart_game(fe->me);
	    break;
	  case IDM_UNDO:
	    if (!midend_process_key(fe->me, 0, 0, UI_UNDO, NULL))
		PostQuitMessage(0);
	    break;
	  case IDM_REDO:
	    if (!midend_process_key(fe->me, 0, 0, UI_REDO, NULL))
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
		const char *msg = midend_solve(fe->me);
		if (msg)
		    MessageBox(hwnd, msg, "Unable to solve",
			       MB_ICONERROR | MB_OK);
	    }
	    break;
	  case IDM_QUIT:
	    if (!midend_process_key(fe->me, 0, 0, UI_QUIT, NULL))
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
			const char *err = NULL;
                        char *err_w = NULL;
                        midend *me = fe->me;
#ifdef COMBINED
                        char *id_name;
#endif

			if (!fp) {
			    MessageBox(hwnd, "Unable to open saved game file",
				       "Error", MB_ICONERROR | MB_OK);
			    break;
			}

#ifdef COMBINED
                        /*
                         * This save file might be from a different
                         * game.
                         */
                        err = identify_game(&id_name, savefile_read, fp);
                        if (!err) {
                            int i;
                            for (i = 0; i < gamecount; i++)
                                if (!strcmp(id_name, gamelist[i]->name))
                                    break;
                            if (i == gamecount) {
                                err = "Save file is for a game not "
                                    "supported by this program";
                            } else {
                                me = midend_for_new_game(fe, gamelist[i], NULL,
                                                         false, false, &err_w);
                                err = err_w;
                                rewind(fp); /* for the actual load */
                            }
                            sfree(id_name);
                        }
#endif
                        if (!err)
                            err = midend_deserialise(me, savefile_read, fp);

			fclose(fp);

			if (err) {
			    MessageBox(hwnd, err, "Error", MB_ICONERROR|MB_OK);
                            sfree(err_w);
			    break;
			}

                        if (fe->me != me)
                            fe_set_midend(fe, me);
			new_game_size(fe, 1.0);
		    }
		}
	    }

	    break;
          case IDM_HELPC:
	    start_help(fe, NULL);
	    break;
          case IDM_GAMEHELP:
            assert(help_type != NONE);
	    start_help(fe, help_type == CHM ?
                       fe->game->htmlhelp_topic : fe->game->winhelp_topic);
            break;
	  default:
#ifdef COMBINED
            if (wParam >= IDM_GAMES && wParam < (IDM_GAMES + (WPARAM)gamecount)) {
                int p = wParam - IDM_GAMES;
                char *error = NULL;
                fe_set_midend(fe, midend_for_new_game(fe, gamelist[p], NULL,
                                                      false, false, &error));
                sfree(error);
            } else
#endif
	    {
                game_params *preset = preset_menu_lookup_by_id(
                    fe->preset_menu,
                    ((wParam &~ 0xF) - IDM_PRESETS) / 0x10);

		if (preset) {
		    midend_set_params(fe->me, preset);
		    new_game_type(fe);
		}
	    }
	    break;
	}
	break;
      case WM_DESTROY:
	stop_help(fe);
        frontend_free(fe);
        PostQuitMessage(0);
	return 0;
      case WM_PAINT:
	{
	    PAINTSTRUCT p;
	    HDC hdc, hdc2;
	    HBITMAP prevbm;
	    RECT rcDest;

	    hdc = BeginPaint(hwnd, &p);
	    hdc2 = CreateCompatibleDC(hdc);
	    prevbm = SelectObject(hdc2, fe->bitmap);
	    IntersectRect(&rcDest, &(fe->bitmapPosition), &(p.rcPaint));
	    BitBlt(hdc,
		   rcDest.left, rcDest.top,
		   rcDest.right - rcDest.left,
		   rcDest.bottom - rcDest.top,
		   hdc2,
		   rcDest.left - fe->bitmapPosition.left,
		   rcDest.top - fe->bitmapPosition.top,
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
                if (!midend_process_key(fe->me, 0, 0, key, NULL))
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

	    if (!midend_process_key(fe->me,
				    (signed short)LOWORD(lParam) - fe->bitmapPosition.left,
				    (signed short)HIWORD(lParam) - fe->bitmapPosition.top,
				    button, NULL))
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

	    if (!midend_process_key(fe->me,
				    (signed short)LOWORD(lParam) - fe->bitmapPosition.left,
				    (signed short)HIWORD(lParam) - fe->bitmapPosition.top,
				    button, NULL))
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
	    
	    if (!midend_process_key(fe->me,
				    (signed short)LOWORD(lParam) - fe->bitmapPosition.left,
				    (signed short)HIWORD(lParam) - fe->bitmapPosition.top,
				    button, NULL))
		PostQuitMessage(0);
	}
	break;
      case WM_CHAR:
        {
            int key = (unsigned char)wParam;
            if (key == '\x1A') {
                BYTE keystate[256];
                if (GetKeyboardState(keystate) &&
                    (keystate[VK_SHIFT] & 0x80) &&
                    (keystate[VK_CONTROL] & 0x80))
                    key = UI_REDO;
            }
            if (!midend_process_key(fe->me, 0, 0, key, NULL))
                PostQuitMessage(0);
        }
	return 0;
      case WM_TIMER:
	if (fe->timer) {
	    DWORD now = GetTickCount();
	    float elapsed = (float) (now - fe->timer_last_tickcount) * 0.001F;
	    midend_timer(fe->me, elapsed);
	    fe->timer_last_tickcount = now;
	}
	return 0;
      case WM_SIZING:
        {
            RECT *sr = (RECT *)lParam;
            int wx, wy;
            bool isedge = false;

            if (wParam == WMSZ_TOP ||
                wParam == WMSZ_RIGHT ||
                wParam == WMSZ_BOTTOM ||
                wParam == WMSZ_LEFT) isedge = true;
            adjust_game_size(fe, sr, isedge, &wx, &wy);

            /* Given the window size the puzzles constrain
             * us to, work out which edge we should be moving. */
            if (wParam == WMSZ_TOP ||
                wParam == WMSZ_TOPLEFT ||
                wParam == WMSZ_TOPRIGHT) {
                sr->top = sr->bottom - wy;
            } else {
                sr->bottom = sr->top + wy;
            }
            if (wParam == WMSZ_LEFT ||
                wParam == WMSZ_TOPLEFT ||
                wParam == WMSZ_BOTTOMLEFT) {
                sr->left = sr->right - wx;
            } else {
                sr->right = sr->left + wx;
            }
            return true;
        }
        break;
    }

    return DefWindowProc(hwnd, message, wParam, lParam);
}

/*
 * Split a complete command line into argc/argv, attempting to do it
 * exactly the same way the Visual Studio C library would do it (so
 * that our console utilities, which receive argc and argv already
 * broken apart by the C library, will have their command lines
 * processed in the same way as the GUI utilities which get a whole
 * command line and must call this function).
 * 
 * Does not modify the input command line.
 * 
 * The final parameter (argstart) is used to return a second array
 * of char * pointers, the same length as argv, each one pointing
 * at the start of the corresponding element of argv in the
 * original command line. So if you get half way through processing
 * your command line in argc/argv form and then decide you want to
 * treat the rest as a raw string, you can. If you don't want to,
 * `argstart' can be safely left NULL.
 */
void split_into_argv(char *cmdline, int *argc, char ***argv,
		     char ***argstart)
{
    char *p;
    char *outputline, *q;
    char **outputargv, **outputargstart;
    int outputargc;

    /*
     * These argument-breaking rules apply to Visual Studio 7, which
     * is currently the compiler expected to be used for the Windows
     * port of my puzzles. Visual Studio 10 has different rules,
     * lacking the curious mod 3 behaviour of consecutive quotes
     * described below; I presume they fixed a bug. As and when we
     * migrate to a newer compiler, we'll have to adjust this to
     * match; however, for the moment we faithfully imitate in our GUI
     * utilities what our CLI utilities can't be prevented from doing.
     *
     * When I investigated this, at first glance the rules appeared to
     * be:
     *
     *  - Single quotes are not special characters.
     *
     *  - Double quotes are removed, but within them spaces cease
     *    to be special.
     *
     *  - Backslashes are _only_ special when a sequence of them
     *    appear just before a double quote. In this situation,
     *    they are treated like C backslashes: so \" just gives a
     *    literal quote, \\" gives a literal backslash and then
     *    opens or closes a double-quoted segment, \\\" gives a
     *    literal backslash and then a literal quote, \\\\" gives
     *    two literal backslashes and then opens/closes a
     *    double-quoted segment, and so forth. Note that this
     *    behaviour is identical inside and outside double quotes.
     *
     *  - Two successive double quotes become one literal double
     *    quote, but only _inside_ a double-quoted segment.
     *    Outside, they just form an empty double-quoted segment
     *    (which may cause an empty argument word).
     *
     *  - That only leaves the interesting question of what happens
     *    when one or more backslashes precedes two or more double
     *    quotes, starting inside a double-quoted string. And the
     *    answer to that appears somewhat bizarre. Here I tabulate
     *    number of backslashes (across the top) against number of
     *    quotes (down the left), and indicate how many backslashes
     *    are output, how many quotes are output, and whether a
     *    quoted segment is open at the end of the sequence:
     * 
     *                      backslashes
     * 
     *               0         1      2      3      4
     * 
     *         0   0,0,y  |  1,0,y  2,0,y  3,0,y  4,0,y
     *            --------+-----------------------------
     *         1   0,0,n  |  0,1,y  1,0,n  1,1,y  2,0,n
     *    q    2   0,1,n  |  0,1,n  1,1,n  1,1,n  2,1,n
     *    u    3   0,1,y  |  0,2,n  1,1,y  1,2,n  2,1,y
     *    o    4   0,1,n  |  0,2,y  1,1,n  1,2,y  2,1,n
     *    t    5   0,2,n  |  0,2,n  1,2,n  1,2,n  2,2,n
     *    e    6   0,2,y  |  0,3,n  1,2,y  1,3,n  2,2,y
     *    s    7   0,2,n  |  0,3,y  1,2,n  1,3,y  2,2,n
     *         8   0,3,n  |  0,3,n  1,3,n  1,3,n  2,3,n
     *         9   0,3,y  |  0,4,n  1,3,y  1,4,n  2,3,y
     *        10   0,3,n  |  0,4,y  1,3,n  1,4,y  2,3,n
     *        11   0,4,n  |  0,4,n  1,4,n  1,4,n  2,4,n
     * 
     * 
     *      [Test fragment was of the form "a\\\"""b c" d.]
     * 
     * There is very weird mod-3 behaviour going on here in the
     * number of quotes, and it even applies when there aren't any
     * backslashes! How ghastly.
     * 
     * With a bit of thought, this extremely odd diagram suddenly
     * coalesced itself into a coherent, if still ghastly, model of
     * how things work:
     * 
     *  - As before, backslashes are only special when one or more
     *    of them appear contiguously before at least one double
     *    quote. In this situation the backslashes do exactly what
     *    you'd expect: each one quotes the next thing in front of
     *    it, so you end up with n/2 literal backslashes (if n is
     *    even) or (n-1)/2 literal backslashes and a literal quote
     *    (if n is odd). In the latter case the double quote
     *    character right after the backslashes is used up.
     * 
     *  - After that, any remaining double quotes are processed. A
     *    string of contiguous unescaped double quotes has a mod-3
     *    behaviour:
     * 
     *     * inside a quoted segment, a quote ends the segment.
     *     * _immediately_ after ending a quoted segment, a quote
     *       simply produces a literal quote.
     *     * otherwise, outside a quoted segment, a quote begins a
     *       quoted segment.
     * 
     *    So, for example, if we started inside a quoted segment
     *    then two contiguous quotes would close the segment and
     *    produce a literal quote; three would close the segment,
     *    produce a literal quote, and open a new segment. If we
     *    started outside a quoted segment, then two contiguous
     *    quotes would open and then close a segment, producing no
     *    output (but potentially creating a zero-length argument);
     *    but three quotes would open and close a segment and then
     *    produce a literal quote.
     */

    /*
     * First deal with the simplest of all special cases: if there
     * aren't any arguments, return 0,NULL,NULL.
     */
    while (*cmdline && isspace(*cmdline)) cmdline++;
    if (!*cmdline) {
	if (argc) *argc = 0;
	if (argv) *argv = NULL;
	if (argstart) *argstart = NULL;
	return;
    }

    /*
     * This will guaranteeably be big enough; we can realloc it
     * down later.
     */
    outputline = snewn(1+strlen(cmdline), char);
    outputargv = snewn(strlen(cmdline)+1 / 2, char *);
    outputargstart = snewn(strlen(cmdline)+1 / 2, char *);

    p = cmdline; q = outputline; outputargc = 0;

    while (*p) {
	bool quote;

	/* Skip whitespace searching for start of argument. */
	while (*p && isspace(*p)) p++;
	if (!*p) break;

	/* We have an argument; start it. */
	outputargv[outputargc] = q;
	outputargstart[outputargc] = p;
	outputargc++;
	quote = false;

	/* Copy data into the argument until it's finished. */
	while (*p) {
	    if (!quote && isspace(*p))
		break;		       /* argument is finished */

	    if (*p == '"' || *p == '\\') {
		/*
		 * We have a sequence of zero or more backslashes
		 * followed by a sequence of zero or more quotes.
		 * Count up how many of each, and then deal with
		 * them as appropriate.
		 */
		int i, slashes = 0, quotes = 0;
		while (*p == '\\') slashes++, p++;
		while (*p == '"') quotes++, p++;

		if (!quotes) {
		    /*
		     * Special case: if there are no quotes,
		     * slashes are not special at all, so just copy
		     * n slashes to the output string.
		     */
		    while (slashes--) *q++ = '\\';
		} else {
		    /* Slashes annihilate in pairs. */
		    while (slashes >= 2) slashes -= 2, *q++ = '\\';

		    /* One remaining slash takes out the first quote. */
		    if (slashes) quotes--, *q++ = '"';

		    if (quotes > 0) {
			/* Outside a quote segment, a quote starts one. */
			if (!quote) quotes--, quote = true;

			/* Now we produce (n+1)/3 literal quotes... */
			for (i = 3; i <= quotes+1; i += 3) *q++ = '"';

			/* ... and end in a quote segment iff 3 divides n. */
			quote = (quotes % 3 == 0);
		    }
		}
	    } else {
		*q++ = *p++;
	    }
	}

	/* At the end of an argument, just append a trailing NUL. */
	*q++ = '\0';
    }

    outputargv = sresize(outputargv, outputargc, char *);
    outputargstart = sresize(outputargstart, outputargc, char *);

    if (argc) *argc = outputargc;
    if (argv) *argv = outputargv; else sfree(outputargv);
    if (argstart) *argstart = outputargstart; else sfree(outputargstart);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    MSG msg;
    char *error = NULL;
    const game *gg;
    frontend *fe;
    midend *me;
    int argc;
    char **argv;

    split_into_argv(cmdline, &argc, &argv, NULL);

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
	wndclass.lpszClassName = CLASSNAME;

	RegisterClass(&wndclass);
    }

    while (*cmdline && isspace((unsigned char)*cmdline))
	cmdline++;

    init_help();

#ifdef COMBINED
    gg = gamelist[0];
    if (argc > 0) {
        int i;
        for (i = 0; i < gamecount; i++) {
	    const char *p = gamelist[i]->name;
	    char *q = argv[0];
	    while (*p && *q) {
		if (isspace((unsigned char)*p)) {
		    while (*q && isspace((unsigned char)*q))
			q++;
		} else {
		    if (tolower((unsigned char)*p) !=
			tolower((unsigned char)*q))
			break;
		    q++;
		}
		p++;
	    }
	    if (!*p) {
                gg = gamelist[i];
                --argc;
                ++argv;
                break;
            }
        }
    }
#else
    gg = &thegame;
#endif

    fe = frontend_new(inst);
    me = midend_for_new_game(fe, gg, argc > 0 ? argv[0] : NULL,
                             true, true, &error);
    if (!me) {
	char buf[128];
#ifdef COMBINED
	sprintf(buf, "Puzzles Error");
#else
	sprintf(buf, "%.100s Error", gg->name);
#endif
	MessageBox(NULL, error, buf, MB_OK|MB_ICONERROR);
        sfree(error);
	return 1;
    }
    fe_set_midend(fe, me);
    show_window(fe);

    while (GetMessage(&msg, NULL, 0, 0)) {
	DispatchMessage(&msg);
    }

    DestroyWindow(fe->hwnd);
    cleanup_help();

    return msg.wParam;
}
/* vim: set shiftwidth=4 tabstop=8: */
