/*
 * Mac OS X / Cocoa front end to puzzles.
 *
 * TODO:
 *
 *  - status bar support.
 *
 *  - configurability. Will no doubt involve learning all about the
 *    dialog control side of Cocoa.
 *
 *  - needs an icon.
 *
 *  - not sure what I should be doing about default window
 *    placement. Centring new windows is a bit feeble, but what's
 *    better? Is there a standard way to tell the OS "here's the
 *    _size_ of window I want, now use your best judgment about the
 *    initial position"?
 *
 *  - a brief frob of the Mac numeric keypad suggests that it
 *    generates numbers no matter what you do. I wonder if I should
 *    try to figure out a way of detecting keypad codes so I can
 *    implement UP_LEFT and friends. Alternatively, perhaps I
 *    should simply assign the number keys to UP_LEFT et al?
 *    They're not in use for anything else right now.
 *
 *  - proper fatal errors.
 *
 *  - is there a better approach to frontend_default_colour?
 *
 *  - do we need any more options in the Window menu?
 *
 *  - see if we can do anything to one-button-ise the multi-button
 *    dependent puzzle UIs:
 *     - Pattern is a _little_ unwieldy but not too bad (since
 * 	 generally you never need the middle button unless you've
 * 	 made a mistake, so it's just click versus command-click).
 *     - Net is utterly vile; having normal click be one rotate and
 * 	 command-click be the other introduces a horrid asymmetry,
 * 	 and yet requiring a shift key for _each_ click would be
 * 	 even worse because rotation feels as if it ought to be the
 * 	 default action. I fear this is why the Flash Net had the
 * 	 UI it did...
 *
 *  - Find out how to do help, and do some. We have a help file; at
 *    _worst_ this should involve a new Halibut back end, but I
 *    think help is HTML round here anyway so perhaps we can work
 *    with what we already have.
 * 
 *  - Can we arrange for a pop-up menu from the Dock icon which
 *    launches specific games, perhaps?
 * 
 *  - Why are the right and bottom edges of the Pattern grid one
 *    pixel thinner than they should be?
 * 
 * Grotty implementation details that could probably be improved:
 * 
 *  - I am _utterly_ unconvinced that NSImageView was the right way
 *    to go about having a window with a reliable backing store! It
 *    just doesn't feel right; NSImageView is a _control_. Is there
 *    a simpler way?
 * 
 *  - Resizing is currently very bad; rather than bother to work
 *    out how to resize the NSImageView, I just splatter and
 *    recreate it.
 */

#include <ctype.h>
#include <sys/time.h>
#import <Cocoa/Cocoa.h>
#include "puzzles.h"

void fatal(char *fmt, ...)
{
    /* FIXME: This will do for testing, but should be GUI-ish instead. */
    va_list ap;

    fprintf(stderr, "fatal error: ");

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fprintf(stderr, "\n");
    exit(1);
}

void frontend_default_colour(frontend *fe, float *output)
{
    /* FIXME */
    output[0] = output[1] = output[2] = 0.8F;
}
void status_bar(frontend *fe, char *text)
{
    /* FIXME */
}

void get_random_seed(void **randseed, int *randseedsize)
{
    time_t *tp = snew(time_t);
    time(tp);
    *randseed = (void *)tp;
    *randseedsize = sizeof(time_t);
}

/* ----------------------------------------------------------------------
 * Global variables.
 */

/*
 * The `Type' menu. We frob this dynamically to allow the user to
 * choose a preset set of settings from the current game.
 */
NSMenu *typemenu;

/* ----------------------------------------------------------------------
 * Tiny extension to NSMenuItem which carries a payload of a `void
 * *', allowing several menu items to invoke the same message but
 * pass different data through it.
 */
@interface DataMenuItem : NSMenuItem
{
    void *payload;
}
- (void)setPayload:(void *)d;
- (void *)getPayload;
@end
@implementation DataMenuItem
- (void)setPayload:(void *)d
{
    payload = d;
}
- (void *)getPayload
{
    return payload;
}
@end

/* ----------------------------------------------------------------------
 * The front end presented to midend.c.
 * 
 * This is mostly a subclass of NSWindow. The actual `frontend'
 * structure passed to the midend contains a variety of pointers,
 * including that window object but also including the image we
 * draw on, an ImageView to display it in the window, and so on.
 */

@class GameWindow;
@class MyImageView;

struct frontend {
    GameWindow *window;
    NSImage *image;
    MyImageView *view;
    NSColor **colours;
    int ncolours;
    int clipped;
};

@interface MyImageView : NSImageView
{
    GameWindow *ourwin;
}
- (void)setWindow:(GameWindow *)win;
- (BOOL)isFlipped;
- (void)mouseEvent:(NSEvent *)ev button:(int)b;
- (void)mouseDown:(NSEvent *)ev;
- (void)mouseDragged:(NSEvent *)ev;
- (void)mouseUp:(NSEvent *)ev;
- (void)rightMouseDown:(NSEvent *)ev;
- (void)rightMouseDragged:(NSEvent *)ev;
- (void)rightMouseUp:(NSEvent *)ev;
- (void)otherMouseDown:(NSEvent *)ev;
- (void)otherMouseDragged:(NSEvent *)ev;
- (void)otherMouseUp:(NSEvent *)ev;
@end

@interface GameWindow : NSWindow
{
    const game *ourgame;
    midend_data *me;
    struct frontend fe;
    struct timeval last_time;
    NSTimer *timer;
}
- (id)initWithGame:(const game *)g;
- dealloc;
- (void)processButton:(int)b x:(int)x y:(int)y;
- (void)keyDown:(NSEvent *)ev;
- (void)activateTimer;
- (void)deactivateTimer;
@end

@implementation MyImageView

- (void)setWindow:(GameWindow *)win
{
    ourwin = win;
}

- (BOOL)isFlipped
{
    return YES;
}

- (void)mouseEvent:(NSEvent *)ev button:(int)b
{
    NSPoint point = [self convertPoint:[ev locationInWindow] fromView:nil];
    [ourwin processButton:b x:point.x y:point.y];
}

- (void)mouseDown:(NSEvent *)ev
{
    unsigned mod = [ev modifierFlags];
    [self mouseEvent:ev button:((mod & NSCommandKeyMask) ? RIGHT_BUTTON :
				(mod & NSShiftKeyMask) ? MIDDLE_BUTTON :
				LEFT_BUTTON)];
}
- (void)mouseDragged:(NSEvent *)ev
{
    unsigned mod = [ev modifierFlags];
    [self mouseEvent:ev button:((mod & NSCommandKeyMask) ? RIGHT_DRAG :
				(mod & NSShiftKeyMask) ? MIDDLE_DRAG :
				LEFT_DRAG)];
}
- (void)mouseUp:(NSEvent *)ev
{
    unsigned mod = [ev modifierFlags];
    [self mouseEvent:ev button:((mod & NSCommandKeyMask) ? RIGHT_RELEASE :
				(mod & NSShiftKeyMask) ? MIDDLE_RELEASE :
				LEFT_RELEASE)];
}
- (void)rightMouseDown:(NSEvent *)ev
{
    unsigned mod = [ev modifierFlags];
    [self mouseEvent:ev button:((mod & NSShiftKeyMask) ? MIDDLE_BUTTON :
				RIGHT_BUTTON)];
}
- (void)rightMouseDragged:(NSEvent *)ev
{
    unsigned mod = [ev modifierFlags];
    [self mouseEvent:ev button:((mod & NSShiftKeyMask) ? MIDDLE_DRAG :
				RIGHT_DRAG)];
}
- (void)rightMouseUp:(NSEvent *)ev
{
    unsigned mod = [ev modifierFlags];
    [self mouseEvent:ev button:((mod & NSShiftKeyMask) ? MIDDLE_RELEASE :
				RIGHT_RELEASE)];
}
- (void)otherMouseDown:(NSEvent *)ev
{
    [self mouseEvent:ev button:MIDDLE_BUTTON];
}
- (void)otherMouseDragged:(NSEvent *)ev
{
    [self mouseEvent:ev button:MIDDLE_DRAG];
}
- (void)otherMouseUp:(NSEvent *)ev
{
    [self mouseEvent:ev button:MIDDLE_RELEASE];
}
@end

@implementation GameWindow
- (void)setupContentView
{
    NSSize size = {0,0};
    int w, h;

    midend_size(me, &w, &h);
    size.width = w;
    size.height = h;

    fe.image = [[NSImage alloc] initWithSize:size];
    [fe.image setFlipped:YES];
    fe.view = [[MyImageView alloc]
	       initWithFrame:[self contentRectForFrameRect:[self frame]]];
    [fe.view setImage:fe.image];
    [fe.view setWindow:self];

    midend_redraw(me);

    [self setContentView:fe.view];
}
- (id)initWithGame:(const game *)g
{
    NSRect rect = { {0,0}, {0,0} };
    int w, h;

    ourgame = g;

    fe.window = self;

    me = midend_new(&fe, ourgame);
    /*
     * If we ever need to open a fresh window using a provided game
     * ID, I think the right thing is to move most of this method
     * into a new initWithGame:gameID: method, and have
     * initWithGame: simply call that one and pass it NULL.
     */
    midend_new_game(me);
    midend_size(me, &w, &h);
    rect.size.width = w;
    rect.size.height = h;

    self = [super initWithContentRect:rect
	    styleMask:(NSTitledWindowMask | NSMiniaturizableWindowMask |
		       NSClosableWindowMask)
	    backing:NSBackingStoreBuffered
	    defer:true];
    [self setTitle:[NSString stringWithCString:ourgame->name]];

    {
	float *colours;
	int i, ncolours;

	colours = midend_colours(me, &ncolours);
	fe.ncolours = ncolours;
	fe.colours = snewn(ncolours, NSColor *);

	for (i = 0; i < ncolours; i++) {
	    fe.colours[i] = [[NSColor colorWithDeviceRed:colours[i*3]
			      green:colours[i*3+1] blue:colours[i*3+2]
			      alpha:1.0] retain];
	}
    }

    [self setupContentView];
    [self setIgnoresMouseEvents:NO];

    [self center];		       /* :-) */

    return self;
}

- dealloc
{
    int i;
    for (i = 0; i < fe.ncolours; i++) {
	[fe.colours[i] release];
    }
    sfree(fe.colours);
    midend_free(me);
    return [super dealloc];
}

- (void)processButton:(int)b x:(int)x y:(int)y
{
    if (!midend_process_key(me, x, y, b))
	[self close];
}

- (void)keyDown:(NSEvent *)ev
{
    NSString *s = [ev characters];
    int i, n = [s length];

    for (i = 0; i < n; i++) {
	int c = [s characterAtIndex:i];

	/*
	 * ASCII gets passed straight to midend_process_key.
	 * Anything above that has to be translated to our own
	 * function key codes.
	 */
	if (c >= 0x80) {
	    switch (c) {
	      case NSUpArrowFunctionKey:
		c = CURSOR_UP;
		break;
	      case NSDownArrowFunctionKey:
		c = CURSOR_DOWN;
		break;
	      case NSLeftArrowFunctionKey:
		c = CURSOR_LEFT;
		break;
	      case NSRightArrowFunctionKey:
		c = CURSOR_RIGHT;
		break;
	      default:
		continue;
	    }
	}

	[self processButton:c x:-1 y:-1];
    }
}

- (void)activateTimer
{
    if (timer != nil)
	return;

    timer = [NSTimer scheduledTimerWithTimeInterval:0.02
	     target:self selector:@selector(timerTick:)
	     userInfo:nil repeats:YES];
    gettimeofday(&last_time, NULL);
}

- (void)deactivateTimer
{
    if (timer == nil)
	return;

    [timer invalidate];
    timer = nil;
}

- (void)timerTick:(id)sender
{
    struct timeval now;
    float elapsed;
    gettimeofday(&now, NULL);
    elapsed = ((now.tv_usec - last_time.tv_usec) * 0.000001F +
	       (now.tv_sec - last_time.tv_sec));
    midend_timer(me, elapsed);
    last_time = now;
}

- (void)newGame:(id)sender
{
    [self processButton:'n' x:-1 y:-1];
}
- (void)restartGame:(id)sender
{
    [self processButton:'r' x:-1 y:-1];
}
- (void)undoMove:(id)sender
{
    [self processButton:'u' x:-1 y:-1];
}
- (void)redoMove:(id)sender
{
    [self processButton:'r'&0x1F x:-1 y:-1];
}

- (void)clearTypeMenu
{
    while ([typemenu numberOfItems] > 1)
	[typemenu removeItemAtIndex:0];
}

- (void)becomeKeyWindow
{
    int n;

    [self clearTypeMenu];

    [super becomeKeyWindow];

    n = midend_num_presets(me);

    if (n > 0) {
	[typemenu insertItem:[NSMenuItem separatorItem] atIndex:0];
	while (n--) {
	    char *name;
	    game_params *params;
	    DataMenuItem *item;

	    midend_fetch_preset(me, n, &name, &params);

	    item = [[[DataMenuItem alloc]
		     initWithTitle:[NSString stringWithCString:name]
		     action:NULL keyEquivalent:@""]
		    autorelease];

	    [item setEnabled:YES];
	    [item setTarget:self];
	    [item setAction:@selector(presetGame:)];
	    [item setPayload:params];

	    [typemenu insertItem:item atIndex:0];
	}
    }
}

- (void)resignKeyWindow
{
    [self clearTypeMenu];
    [super resignKeyWindow];
}

- (void)close
{
    [self clearTypeMenu];
    [super close];
}

- (void)resizeForNewGameParams
{
    NSSize size = {0,0};
    int w, h;

    midend_size(me, &w, &h);
    size.width = w;
    size.height = h;

    NSDisableScreenUpdates();
    [self setContentSize:size];
    [self setupContentView];
    NSEnableScreenUpdates();
}

- (void)presetGame:(id)sender
{
    game_params *params = [sender getPayload];

    midend_set_params(me, params);
    midend_new_game(me);

    [self resizeForNewGameParams];
}

@end

/*
 * Drawing routines called by the midend.
 */
void draw_polygon(frontend *fe, int *coords, int npoints,
                  int fill, int colour)
{
    NSBezierPath *path = [NSBezierPath bezierPath];
    int i;

    [[NSGraphicsContext currentContext] setShouldAntialias:YES];

    assert(colour >= 0 && colour < fe->ncolours);
    [fe->colours[colour] set];

    for (i = 0; i < npoints; i++) {
	NSPoint p = { coords[i*2] + 0.5, coords[i*2+1] + 0.5 };
	if (i == 0)
	    [path moveToPoint:p];
	else
	    [path lineToPoint:p];
    }

    [path closePath];

    if (fill)
	[path fill];
    else
	[path stroke];
}
void draw_line(frontend *fe, int x1, int y1, int x2, int y2, int colour)
{
    NSBezierPath *path = [NSBezierPath bezierPath];
    NSPoint p1 = { x1 + 0.5, y1 + 0.5 }, p2 = { x2 + 0.5, y2 + 0.5 };

    [[NSGraphicsContext currentContext] setShouldAntialias:NO];

    assert(colour >= 0 && colour < fe->ncolours);
    [fe->colours[colour] set];

    [path moveToPoint:p1];
    [path lineToPoint:p2];
    [path stroke];
}
void draw_rect(frontend *fe, int x, int y, int w, int h, int colour)
{
    NSRect r = { {x,y}, {w,h} };

    [[NSGraphicsContext currentContext] setShouldAntialias:NO];

    assert(colour >= 0 && colour < fe->ncolours);
    [fe->colours[colour] set];

    NSRectFill(r);
}
void draw_text(frontend *fe, int x, int y, int fonttype, int fontsize,
               int align, int colour, char *text)
{
    NSString *string = [NSString stringWithCString:text];
    NSDictionary *attr;
    NSFont *font;
    NSSize size;
    NSPoint point;

    [[NSGraphicsContext currentContext] setShouldAntialias:YES];

    assert(colour >= 0 && colour < fe->ncolours);

    if (fonttype == FONT_FIXED)
	font = [NSFont userFixedPitchFontOfSize:fontsize];
    else
	font = [NSFont userFontOfSize:fontsize];

    attr = [NSDictionary dictionaryWithObjectsAndKeys:
	    fe->colours[colour], NSForegroundColorAttributeName,
	    font, NSFontAttributeName, nil];

    point.x = x;
    point.y = y;

    size = [string sizeWithAttributes:attr];
    if (align & ALIGN_HRIGHT)
	point.x -= size.width;
    else if (align & ALIGN_HCENTRE)
	point.x -= size.width / 2;
    if (align & ALIGN_VCENTRE)
	point.y -= size.height / 2;

    [string drawAtPoint:point withAttributes:attr];
}
void draw_update(frontend *fe, int x, int y, int w, int h)
{
    /* FIXME */
}
void clip(frontend *fe, int x, int y, int w, int h)
{
    NSRect r = { {x,y}, {w,h} };

    if (!fe->clipped)
	[[NSGraphicsContext currentContext] saveGraphicsState];
    [NSBezierPath clipRect:r];
    fe->clipped = TRUE;
}
void unclip(frontend *fe)
{
    if (fe->clipped)
	[[NSGraphicsContext currentContext] restoreGraphicsState];
    fe->clipped = FALSE;
}
void start_draw(frontend *fe)
{
    [fe->image lockFocus];
    fe->clipped = FALSE;
}
void end_draw(frontend *fe)
{
    [fe->image unlockFocus];
    [fe->view setNeedsDisplay];
}

void deactivate_timer(frontend *fe)
{
    [fe->window deactivateTimer];
}
void activate_timer(frontend *fe)
{
    [fe->window activateTimer];
}

/* ----------------------------------------------------------------------
 * Utility routines for constructing OS X menus.
 */

NSMenu *newmenu(const char *title)
{
    return [[[NSMenu allocWithZone:[NSMenu menuZone]]
	     initWithTitle:[NSString stringWithCString:title]]
	    autorelease];
}

NSMenu *newsubmenu(NSMenu *parent, const char *title)
{
    NSMenuItem *item;
    NSMenu *child;

    item = [[[NSMenuItem allocWithZone:[NSMenu menuZone]]
	     initWithTitle:[NSString stringWithCString:title]
	     action:NULL
	     keyEquivalent:@""]
	    autorelease];
    child = newmenu(title);
    [item setEnabled:YES];
    [item setSubmenu:child];
    [parent addItem:item];
    return child;
}

id initnewitem(NSMenuItem *item, NSMenu *parent, const char *title,
	       const char *key, id target, SEL action)
{
    unsigned mask = NSCommandKeyMask;

    if (key[strcspn(key, "-")]) {
	while (*key && *key != '-') {
	    int c = tolower((unsigned char)*key);
	    if (c == 's') {
		mask |= NSShiftKeyMask;
	    } else if (c == 'o' || c == 'a') {
		mask |= NSAlternateKeyMask;
	    }
	    key++;
	}
	if (*key)
	    key++;
    }

    item = [[item initWithTitle:[NSString stringWithCString:title]
	     action:NULL
	     keyEquivalent:[NSString stringWithCString:key]]
	    autorelease];

    if (*key)
	[item setKeyEquivalentModifierMask: mask];

    [item setEnabled:YES];
    [item setTarget:target];
    [item setAction:action];

    [parent addItem:item];

    return item;
}

NSMenuItem *newitem(NSMenu *parent, char *title, char *key,
		    id target, SEL action)
{
    return initnewitem([NSMenuItem allocWithZone:[NSMenu menuZone]],
		       parent, title, key, target, action);
}

/* ----------------------------------------------------------------------
 * AppController: the object which receives the messages from all
 * menu selections that aren't standard OS X functions.
 */
@interface AppController : NSObject
{
}
- (IBAction)newGame:(id)sender;
@end

@implementation AppController

- (IBAction)newGame:(id)sender
{
    const game *g = [sender getPayload];
    id win;

    win = [[GameWindow alloc] initWithGame:g];
    [win makeKeyAndOrderFront:self];
}

@end

/* ----------------------------------------------------------------------
 * Main program. Constructs the menus and runs the application.
 */
int main(int argc, char **argv)
{
    NSAutoreleasePool *pool;
    NSMenu *menu;
    NSMenuItem *item;
    AppController *controller;

    pool = [[NSAutoreleasePool alloc] init];
    [NSApplication sharedApplication];

    controller = [[[AppController alloc] init] autorelease];

    [NSApp setMainMenu: newmenu("Main Menu")];

    menu = newsubmenu([NSApp mainMenu], "Apple Menu");
    [NSApp setServicesMenu:newsubmenu(menu, "Services")];
    [menu addItem:[NSMenuItem separatorItem]];
    item = newitem(menu, "Hide Puzzles", "h", NSApp, @selector(hide:));
    item = newitem(menu, "Hide Others", "o-h", NSApp, @selector(hideOtherApplications:));
    item = newitem(menu, "Show All", "", NSApp, @selector(unhideAllApplications:));
    [menu addItem:[NSMenuItem separatorItem]];
    item = newitem(menu, "Quit", "q", NSApp, @selector(terminate:));
    [NSApp setAppleMenu: menu];

    menu = newsubmenu([NSApp mainMenu], "Open");
    {
	int i;

	for (i = 0; i < gamecount; i++) {
	    id item =
		initnewitem([DataMenuItem allocWithZone:[NSMenu menuZone]],
			    menu, gamelist[i]->name, "", controller,
			    @selector(newGame:));
	    [item setPayload:(void *)gamelist[i]];
	}
    }

    menu = newsubmenu([NSApp mainMenu], "Game");
    item = newitem(menu, "New", "n", NULL, @selector(newGame:));
    item = newitem(menu, "Restart", "r", NULL, @selector(restartGame:));
    item = newitem(menu, "Specific", "", NULL, @selector(specificGame:));
    [menu addItem:[NSMenuItem separatorItem]];
    item = newitem(menu, "Undo", "z", NULL, @selector(undoMove:));
    item = newitem(menu, "Redo", "S-z", NULL, @selector(redoMove:));
    [menu addItem:[NSMenuItem separatorItem]];
    item = newitem(menu, "Close", "w", NULL, @selector(performClose:));

    menu = newsubmenu([NSApp mainMenu], "Type");
    typemenu = menu;
    item = newitem(menu, "Custom", "", NULL, @selector(customGameType:));

    menu = newsubmenu([NSApp mainMenu], "Window");
    [NSApp setWindowsMenu: menu];
    item = newitem(menu, "Minimise Window", "m", NULL, @selector(performMiniaturize:));

    [NSApp run];
    [pool release];
}
