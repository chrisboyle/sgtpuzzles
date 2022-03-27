/*
 * emccpre.js: one of the Javascript components of an Emscripten-based
 * web/Javascript front end for Puzzles.
 *
 * The other parts of this system live in emcc.c and emcclib.js. It
 * also depends on being run in the context of a web page containing
 * an appropriate collection of bits and pieces (a canvas, some
 * buttons and links etc), which is generated for each puzzle by the
 * script html/jspage.pl.
 *
 * This file contains the Javascript code which is prefixed unmodified
 * to Emscripten's output via the --pre-js option. It declares all our
 * global variables, and provides the puzzle init function and a
 * couple of other helper functions.
 */

// To avoid flicker while doing complicated drawing, we use two
// canvases, the same size. One is actually on the web page, and the
// other is off-screen. We do all our drawing on the off-screen one
// first, and then copy rectangles of it to the on-screen canvas in
// response to draw_update() calls by the game backend.
var onscreen_canvas, offscreen_canvas;

// A persistent drawing context for the offscreen canvas, to save
// constructing one per individual graphics operation.
var ctx;

// Bounding rectangle for the copy to the onscreen canvas that will be
// done at drawing end time. Updated by js_canvas_draw_update and used
// by js_canvas_end_draw.
var update_xmin, update_xmax, update_ymin, update_ymax;

// Module object for Emscripten. We fill in these parameters to ensure
// that Module.run() won't be called until we're ready (we want to do
// our own init stuff first), and that when main() returns nothing
// will get cleaned up so we remain able to call the puzzle's various
// callbacks.
//
//
// Page loading order:
//
// 1. The browser starts reading *.html (which comes from jspage.pl)
// 2. It finds the <script> tag.  This is marked defer, so the
//    browser will start fetching and parsing it, but not execute it
//    until the page has loaded.
//
// Now the browser is loading *.html and *.js in parallel.  The
// html is rendered as we go, and the js is deferred.
//
// 3. The HTML finishes loading.  The browser is about to fire the
//    `DOMContentLoaded` event (ie `onload`) but before that, it
//    actually runs the deferred JS.  THis consists of
//
//    (i) emccpre.js (this file).  This sets up various JS variables
//      including the emscripten Module object.
//
//    (ii) emscripten's JS.  This starts the WASM loading.
//
//    (iii) emccpost.js.  This calls initPuzzle, which is defined here
//      in this file.  initPuzzle:
//
//      (a) finds various DOM elements and bind them to variables,
//      which depend on the HTML having loaded (it has).
//
//      (b) makes various `cwrap` calls into the emscripten module to
//      set up hooks; this depends on the emscripten JS having been
//      loaded (it has).
//
//      (c) Makes the call to emscripten's
//      Module.onRuntimeInitialized, which sets the callback for when
//      the WASM has finished loading and initialising.  This has to
//      come before the WASM finishes loading, or we'll miss the
//      callback.  We are executing synchronously here in the same JS
//      file as started the WASM loading, so that is guaranteed.
//
// When this JS execution is complete, the browser fires the `onload`
// event.  This is ignored.  It continues loading the WASM.
//
// 4. The WASM loading and initialisation completes.  The
//    onRuntimeInitialised callback calls into emscripten-generated
//    WASM to call the C `main`, to actually start the puzzle.

var Module = {
    'noInitialRun': true,
    'noExitRuntime': true
};

// Variables used by js_canvas_find_font_midpoint().
var midpoint_test_str = "ABCDEFGHIKLMNOPRSTUVWXYZ0123456789";
var midpoint_cache = [];

// Variables used by js_activate_timer() and js_deactivate_timer().
var timer = null;
var timer_reference_date;

// void timer_callback(double tplus);
//
// Called every 20ms while timing is active.
var timer_callback;

// The status bar object, if we create one.
var statusbar = null;

// Currently live blitters. We keep an integer id for each one on the
// JS side; the C side, which expects a blitter to look like a struct,
// simply defines the struct to contain that integer id.
var blittercount = 0;
var blitters = [];

// State for the dialog-box mechanism. dlg_dimmer and dlg_form are the
// page-darkening overlay and the actual dialog box respectively;
// dlg_next_id is used to allocate each checkbox a unique id to use
// for linking its label to it (see js_dialog_boolean);
// dlg_return_funcs is a list of JS functions to be called when the OK
// button is pressed, to pass the results back to C.
var dlg_dimmer = null, dlg_form = null;
var dlg_next_id = 0;
var dlg_return_funcs = null;

// void dlg_return_sval(int index, const char *val);
// void dlg_return_ival(int index, int val);
//
// C-side entry points called by functions in dlg_return_funcs, to
// pass back the final value in each dialog control.
var dlg_return_sval, dlg_return_ival;

// The <ul> object implementing the game-type drop-down, and a list of
// the <li> objects inside it. Used by js_add_preset(),
// js_get_selected_preset() and js_select_preset().
var gametypelist = null, gametypeitems = [];
var gametypeselectedindex = null;
var gametypesubmenus = [];

// The two anchors used to give permalinks to the current puzzle. Used
// by js_update_permalinks().
var permalink_seed, permalink_desc;

// The undo and redo buttons. Used by js_enable_undo_redo().
var undo_button, redo_button;

// A div element enclosing both the puzzle and its status bar, used
// for positioning the resize handle.
var resizable_div;

// Helper function to find the absolute position of a given DOM
// element on a page, by iterating upwards through the DOM finding
// each element's offset from its parent, and thus calculating the
// page-relative position of the target element.
function element_coords(element) {
    var ex = 0, ey = 0;
    while (element.offsetParent) {
        ex += element.offsetLeft;
        ey += element.offsetTop;
        element = element.offsetParent;
    }
    return {x: ex, y:ey};
}

// Helper function which is passed a mouse event object and a DOM
// element, and returns the coordinates of the mouse event relative to
// the top left corner of the element by subtracting element_coords
// from event.page{X,Y}.
function relative_mouse_coords(event, element) {
    var ecoords = element_coords(element);
    return {x: event.pageX - ecoords.x,
            y: event.pageY - ecoords.y};
}

// Enable and disable items in the CSS menus.
function disable_menu_item(item, disabledFlag) {
    if (disabledFlag)
        item.className = "disabled";
    else
        item.className = "";
}

// Dialog-box functions called from both C and JS.
function dialog_init(titletext) {
    // Create an overlay on the page which darkens everything
    // beneath it.
    dlg_dimmer = document.createElement("div");
    dlg_dimmer.style.width = "100%";
    dlg_dimmer.style.height = "100%";
    dlg_dimmer.style.background = '#000000';
    dlg_dimmer.style.position = 'fixed';
    dlg_dimmer.style.opacity = 0.3;
    dlg_dimmer.style.top = dlg_dimmer.style.left = 0;
    dlg_dimmer.style["z-index"] = 99;

    // Now create a form which sits on top of that in turn.
    dlg_form = document.createElement("form");
    dlg_form.style.width = (window.innerWidth * 2 / 3) + "px";
    dlg_form.style.opacity = 1;
    dlg_form.style.background = '#ffffff';
    dlg_form.style.color = '#000000';
    dlg_form.style.position = 'absolute';
    dlg_form.style.border = "2px solid black";
    dlg_form.style.padding = "20px";
    dlg_form.style.top = (window.innerHeight / 10) + "px";
    dlg_form.style.left = (window.innerWidth / 6) + "px";
    dlg_form.style["z-index"] = 100;

    var title = document.createElement("p");
    title.style.marginTop = "0px";
    title.appendChild(document.createTextNode(titletext));
    dlg_form.appendChild(title);

    dlg_return_funcs = [];
    dlg_next_id = 0;
}

function dialog_launch(ok_function, cancel_function) {
    // Put in the OK and Cancel buttons at the bottom.
    var button;

    if (ok_function) {
        button = document.createElement("input");
        button.type = "button";
        button.value = "OK";
        button.onclick = ok_function;
        dlg_form.appendChild(button);
    }

    if (cancel_function) {
        button = document.createElement("input");
        button.type = "button";
        button.value = "Cancel";
        button.onclick = cancel_function;
        dlg_form.appendChild(button);
    }

    document.body.appendChild(dlg_dimmer);
    document.body.appendChild(dlg_form);
}

function dialog_cleanup() {
    document.body.removeChild(dlg_dimmer);
    document.body.removeChild(dlg_form);
    dlg_dimmer = dlg_form = null;
    onscreen_canvas.focus();
}

// Init function called from body.onload.
function initPuzzle() {
    // Construct the off-screen canvas used for double buffering.
    onscreen_canvas = document.getElementById("puzzlecanvas");
    offscreen_canvas = document.createElement("canvas");
    offscreen_canvas.width = onscreen_canvas.width;
    offscreen_canvas.height = onscreen_canvas.height;

    // Stop right-clicks on the puzzle from popping up a context menu.
    // We need those right-clicks!
    onscreen_canvas.oncontextmenu = function(event) { return false; }

    // Set up mouse handlers. We do a bit of tracking of the currently
    // pressed mouse buttons, to avoid sending mousemoves with no
    // button down (our puzzles don't want those events).
    mousedown = Module.cwrap('mousedown', 'void',
                             ['number', 'number', 'number']);

    button_phys2log = [null, null, null];
    buttons_down = function() {
        var i, toret = 0;
        for (i = 0; i < 3; i++)
            if (button_phys2log[i] !== null)
                toret |= 1 << button_phys2log[i];
        return toret;
    };

    onscreen_canvas.onmousedown = function(event) {
        if (event.button >= 3)
            return;

        var xy = relative_mouse_coords(event, onscreen_canvas);
        var logbutton = event.button;
        if (event.shiftKey)
            logbutton = 1;   // Shift-click overrides to middle button
        else if (event.ctrlKey)
            logbutton = 2;   // Ctrl-click overrides to right button

        mousedown(xy.x, xy.y, logbutton);
        button_phys2log[event.button] = logbutton;

        onscreen_canvas.setCapture(true);
    };
    mousemove = Module.cwrap('mousemove', 'void',
                             ['number', 'number', 'number']);
    onscreen_canvas.onmousemove = function(event) {
        var down = buttons_down();
        if (down) {
            var xy = relative_mouse_coords(event, onscreen_canvas);
            mousemove(xy.x, xy.y, down);
        }
    };
    mouseup = Module.cwrap('mouseup', 'void',
                           ['number', 'number', 'number']);
    onscreen_canvas.onmouseup = function(event) {
        if (event.button >= 3)
            return;

        if (button_phys2log[event.button] !== null) {
            var xy = relative_mouse_coords(event, onscreen_canvas);
            mouseup(xy.x, xy.y, button_phys2log[event.button]);
            button_phys2log[event.button] = null;
        }
    };

    // Set up keyboard handlers. We do all the actual keyboard
    // handling in onkeydown; but we also call event.preventDefault()
    // in both the keydown and keypress handlers. This means that
    // while the canvas itself has focus, _all_ keypresses go only to
    // the puzzle - so users of this puzzle collection in other media
    // can indulge their instinct to press ^R for redo, for example,
    // without accidentally reloading the page.
    key = Module.cwrap('key', 'void', ['number', 'number', 'string',
                                       'string', 'number', 'number']);
    onscreen_canvas.onkeydown = function(event) {
        key(event.keyCode, event.charCode, event.key, event.char,
            event.shiftKey ? 1 : 0, event.ctrlKey ? 1 : 0);
        event.preventDefault();
    };
    onscreen_canvas.onkeypress = function(event) {
        event.preventDefault();
    };

    // command() is a C function called to pass back events which
    // don't fall into other categories like mouse and key events.
    // Mostly those are button presses, but there's also one for the
    // game-type dropdown having been changed.
    command = Module.cwrap('command', 'void', ['number']);

    // Event handlers for buttons and things, which call command().
    document.getElementById("specific").onclick = function(event) {
        // Ensure we don't accidentally process these events when a
        // dialog is actually active, e.g. because the button still
        // has keyboard focus
        if (dlg_dimmer === null)
            command(0);
    };
    document.getElementById("random").onclick = function(event) {
        if (dlg_dimmer === null)
            command(1);
    };
    document.getElementById("new").onclick = function(event) {
        if (dlg_dimmer === null)
            command(5);
    };
    document.getElementById("restart").onclick = function(event) {
        if (dlg_dimmer === null)
            command(6);
    };
    undo_button = document.getElementById("undo");
    undo_button.onclick = function(event) {
        if (dlg_dimmer === null)
            command(7);
    };
    redo_button = document.getElementById("redo");
    redo_button.onclick = function(event) {
        if (dlg_dimmer === null)
            command(8);
    };
    document.getElementById("solve").onclick = function(event) {
        if (dlg_dimmer === null)
            command(9);
    };

    // 'number' is used for C pointers
    get_save_file = Module.cwrap('get_save_file', 'number', []);
    free_save_file = Module.cwrap('free_save_file', 'void', ['number']);
    load_game = Module.cwrap('load_game', 'void', ['string', 'number']);

    document.getElementById("save").onclick = function(event) {
        if (dlg_dimmer === null) {
            var savefile_ptr = get_save_file();
            var savefile_text = UTF8ToString(savefile_ptr);
            free_save_file(savefile_ptr);
            dialog_init("Download saved-game file");
            dlg_form.appendChild(document.createTextNode(
                "Click to download the "));
            var a = document.createElement("a");
            a.download = "puzzle.sav";
            a.href = "data:application/octet-stream," + savefile_text;
            a.appendChild(document.createTextNode("saved-game file"));
            dlg_form.appendChild(a);
            dlg_form.appendChild(document.createTextNode("."));
            dlg_form.appendChild(document.createElement("br"));
            dialog_launch(function(event) {
                dialog_cleanup();
            });
        }
    };

    document.getElementById("load").onclick = function(event) {
        if (dlg_dimmer === null) {
            dialog_init("Upload saved-game file");
            var input = document.createElement("input");
            input.type = "file";
            input.multiple = false;
            dlg_form.appendChild(input);
            dlg_form.appendChild(document.createElement("br"));
            dialog_launch(function(event) {
                if (input.files.length == 1) {
                    var file = input.files.item(0);
                    var reader = new FileReader();
                    reader.addEventListener("loadend", function() {
                        var string = reader.result;
                        load_game(string, string.length);
                    });
                    reader.readAsBinaryString(file);
                }
                dialog_cleanup();
            }, function(event) {
                dialog_cleanup();
            });
        }
    };

    gametypelist = document.getElementById("gametype");
    gametypesubmenus.push(gametypelist);

    // In IE, the canvas doesn't automatically gain focus on a mouse
    // click, so make sure it does
    onscreen_canvas.addEventListener("mousedown", function(event) {
        onscreen_canvas.focus();
    });

    // In our dialog boxes, Return and Escape should be like pressing
    // OK and Cancel respectively
    document.addEventListener("keydown", function(event) {

        if (dlg_dimmer !== null && event.keyCode == 13) {
            for (var i in dlg_return_funcs)
                dlg_return_funcs[i]();
            command(3);
        }

        if (dlg_dimmer !== null && event.keyCode == 27)
            command(4);
    });

    // Set up the function pointers we haven't already grabbed. 
    dlg_return_sval = Module.cwrap('dlg_return_sval', 'void',
                                   ['number','string']);
    dlg_return_ival = Module.cwrap('dlg_return_ival', 'void',
                                   ['number','number']);
    timer_callback = Module.cwrap('timer_callback', 'void', ['number']);

    // Save references to the two permalinks.
    permalink_desc = document.getElementById("permalink-desc");
    permalink_seed = document.getElementById("permalink-seed");

    // Default to giving keyboard focus to the puzzle.
    onscreen_canvas.focus();

    // Create the resize handle.
    var resize_handle = document.createElement("canvas");
    resize_handle.width = 10;
    resize_handle.height = 10;
    {
        var ctx = resize_handle.getContext("2d");
        ctx.beginPath();
        for (var i = 1; i <= 7; i += 3) {
            ctx.moveTo(8.5, i + 0.5);
            ctx.lineTo(i + 0.5, 8.5);
        }
        ctx.lineWidth = '1px';
        ctx.lineCap = 'round';
        ctx.lineJoin = 'round';
        ctx.strokeStyle = '#000000';
        ctx.stroke();
    }
    resizable_div = document.getElementById("resizable");
    resizable_div.appendChild(resize_handle);
    resize_handle.style.position = 'absolute';
    resize_handle.style.zIndex = 98;
    resize_handle.style.bottom = "0";
    resize_handle.style.right = "0";
    resize_handle.style.cursor = "se-resize";
    resize_handle.title = "Drag to resize the puzzle. Right-click to restore the default size.";
    var resize_xbase = null, resize_ybase = null, restore_pending = false;
    var resize_xoffset = null, resize_yoffset = null;
    var resize_puzzle = Module.cwrap('resize_puzzle',
                                     'void', ['number', 'number']);
    var restore_puzzle_size = Module.cwrap('restore_puzzle_size', 'void', []);
    resize_handle.oncontextmenu = function(event) { return false; }
    resize_handle.onmousedown = function(event) {
        if (event.button == 0) {
            var xy = element_coords(onscreen_canvas);
            resize_xbase = xy.x + onscreen_canvas.width / 2;
            resize_ybase = xy.y;
            resize_xoffset = xy.x + onscreen_canvas.width - event.pageX;
            resize_yoffset = xy.y + onscreen_canvas.height - event.pageY;
        } else {
            restore_pending = true;
        }
        resize_handle.setCapture(true);
        event.preventDefault();
    };
    window.addEventListener("mousemove", function(event) {
        if (resize_xbase !== null && resize_ybase !== null) {
            resize_puzzle((event.pageX + resize_xoffset - resize_xbase) * 2,
                          (event.pageY + resize_yoffset - resize_ybase));
            event.preventDefault();
            // Chrome insists on selecting text during a resize drag
            // no matter what I do
            if (window.getSelection)
                window.getSelection().removeAllRanges();
            else
                document.selection.empty();        }
    });
    window.addEventListener("mouseup", function(event) {
        if (resize_xbase !== null && resize_ybase !== null) {
            resize_xbase = null;
            resize_ybase = null;
            onscreen_canvas.focus(); // return focus to the puzzle
            event.preventDefault();
        } else if (restore_pending) {
            // If you have the puzzle at larger than normal size and
            // then right-click to restore, I haven't found any way to
            // stop Chrome and IE popping up a context menu on the
            // revealed piece of document when you release the button
            // except by putting the actual restore into a setTimeout.
            // Gah.
            setTimeout(function() {
                restore_pending = false;
                restore_puzzle_size();
                onscreen_canvas.focus();
            }, 20);
            event.preventDefault();
        }
    });

    Module.onRuntimeInitialized = function() {
        // Run the C setup function, passing argv[1] as the fragment
        // identifier (so that permalinks of the form puzzle.html#game-id
        // can launch the specified id).
        Module.callMain([location.hash]);

        // And if we get here with everything having gone smoothly, i.e.
        // we haven't crashed for one reason or another during setup, then
        // it's probably safe to hide the 'sorry, no puzzle here' div and
        // show the div containing the actual puzzle.
        document.getElementById("apology").style.display = "none";
        document.getElementById("puzzle").style.display = "inline";
    };
}
