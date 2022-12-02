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
var timer;
var timer_reference;

// void timer_callback(double tplus);
//
// Called every 20ms while timing is active.
var timer_callback;

// The status bar object, if we have one.
var statusbar = null;

// Currently live blitters. We keep an integer id for each one on the
// JS side; the C side, which expects a blitter to look like a struct,
// simply defines the struct to contain that integer id.
var blittercount = 0;
var blitters = [];

// State for the dialog-box mechanism. dlg_dimmer and dlg_form are the
// page-darkening overlay and the actual dialog box respectively;
// dlg_return_funcs is a list of JS functions to be called when the OK
// button is pressed, to pass the results back to C.
var dlg_dimmer = null, dlg_form = null;
var dlg_return_funcs = null;

// void dlg_return_sval(int index, const char *val);
// void dlg_return_ival(int index, int val);
//
// C-side entry points called by functions in dlg_return_funcs, to
// pass back the final value in each dialog control.
var dlg_return_sval, dlg_return_ival;

// The <ul> object implementing the game-type drop-down, and a list of
// the sub-lists inside it. Used by js_add_preset().
var gametypelist = null;
var gametypesubmenus = [];

// C entry point for miscellaneous events.
var command;

// The <form> encapsulating the menus.  Used by
// js_get_selected_preset() and js_select_preset().
var menuform = null;

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

// Higher-level mouse helper function to specifically map mouse
// coordinates into the coordinates on a canvas that appear under it.
// This depends on the details of how a canvas gets scaled by CSS.
function canvas_mouse_coords(event, element) {
    var rcoords = relative_mouse_coords(event, element);
    // Assume that the CSS object-fit property is "fill" (the default).
    var xscale = element.width / element.offsetWidth;
    var yscale = element.height / element.offsetHeight;
    return {x: rcoords.x * xscale, y: rcoords.y * yscale}
}

// Enable and disable items in the CSS menus.
function disable_menu_item(item, disabledFlag) {
    item.disabled = disabledFlag;
}

// Dialog-box functions called from both C and JS.
function dialog_init(titletext) {
    // Forward compatibility: Delete form and dimmer if they already
    // exist.
    dlg_dimmer = document.getElementById("dlgdimmer");
    if (dlg_dimmer) dlg_dimmer.parentElement.removeChild(dlg_dimmer);
    dlg_form = document.getElementById("dlgform");
    if (dlg_form) dlg_form.parentElement.removeChild(dlg_form);

    // Create an overlay on the page which darkens everything
    // beneath it.
    dlg_dimmer = document.createElement("div");
    dlg_dimmer.id = "dlgdimmer";

    // Now create a form which sits on top of that in turn.
    dlg_form = document.createElement("form");
    dlg_form.id = "dlgform";

    var title = document.createElement("h2");
    title.appendChild(document.createTextNode(titletext));
    dlg_form.appendChild(title);

    dlg_return_funcs = [];
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
    var mousedown = Module.cwrap('mousedown', 'boolean',
                                 ['number', 'number', 'number']);

    var button_phys2log = [null, null, null];
    var buttons_down = function() {
        var i, toret = 0;
        for (i = 0; i < 3; i++)
            if (button_phys2log[i] !== null)
                toret |= 1 << button_phys2log[i];
        return toret;
    };

    onscreen_canvas.onmousedown = function(event) {
        if (event.button >= 3)
            return;

        var xy = canvas_mouse_coords(event, onscreen_canvas);
        var logbutton = event.button;
        if (event.shiftKey)
            logbutton = 1;   // Shift-click overrides to middle button
        else if (event.ctrlKey)
            logbutton = 2;   // Ctrl-click overrides to right button

        if (mousedown(xy.x, xy.y, logbutton))
            event.preventDefault();
        button_phys2log[event.button] = logbutton;

        onscreen_canvas.setCapture(true);
    };
    var mousemove = Module.cwrap('mousemove', 'boolean',
                                 ['number', 'number', 'number']);
    onscreen_canvas.onmousemove = function(event) {
        var down = buttons_down();
        if (down) {
            var xy = canvas_mouse_coords(event, onscreen_canvas);
            if (mousemove(xy.x, xy.y, down))
                event.preventDefault();
        }
    };
    var mouseup = Module.cwrap('mouseup', 'boolean',
                               ['number', 'number', 'number']);
    onscreen_canvas.onmouseup = function(event) {
        if (event.button >= 3)
            return;

        if (button_phys2log[event.button] !== null) {
            var xy = canvas_mouse_coords(event, onscreen_canvas);
            if (mouseup(xy.x, xy.y, button_phys2log[event.button]))
                event.preventDefault();
            button_phys2log[event.button] = null;
        }
    };

    // Set up keyboard handlers. We call event.preventDefault()
    // in the keydown handler if it looks like we might have
    // done something with the key.  This means that users
    // of this puzzle collection in other media
    // can indulge their instinct to press ^R for redo, for example,
    // without accidentally reloading the page.
    var key = Module.cwrap('key', 'boolean', ['number', 'string', 'string',
                                              'number', 'number', 'number']);
    onscreen_canvas.onkeydown = function(event) {
        if (key(event.keyCode, event.key, event.char, event.location,
                event.shiftKey ? 1 : 0, event.ctrlKey ? 1 : 0))
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
    var get_save_file = Module.cwrap('get_save_file', 'number', []);
    var free_save_file = Module.cwrap('free_save_file', 'void', ['number']);
    var load_game = Module.cwrap('load_game', 'void', ['string', 'number']);

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
            a.href = "data:application/octet-stream," +
                encodeURIComponent(savefile_text);
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
                    reader.readAsText(file);
                }
                dialog_cleanup();
            }, function(event) {
                dialog_cleanup();
            });
        }
    };

    gametypelist = document.getElementById("gametype");
    gametypesubmenus.push(gametypelist);
    menuform = document.getElementById("gamemenu");

    // Find the next or previous item in a menu, or null if there
    // isn't one.  Skip list items that don't have a child (i.e.
    // separators) or whose child is disabled.
    function isuseful(item) {
        return item.querySelector(":scope > :not(:disabled)");
    }
    function nextmenuitem(item) {
        do item = item.nextElementSibling;
        while (item !== null && !isuseful(item));
        return item;
    }
    function prevmenuitem(item) {
        do item = item.previousElementSibling;
        while (item !== null && !isuseful(item));
        return item;
    }
    function firstmenuitem(menu) {
        var item = menu && menu.firstElementChild;
        while (item !== null && !isuseful(item))
            item = item.nextElementSibling;
        return item;
    }
    function lastmenuitem(menu) {
        var item = menu && menu.lastElementChild;
        while (item !== null && !isuseful(item))
            item = item.previousElementSibling;
        return item;
    }
    // Keyboard handlers for the menus.
    function menukey(event) {
        var thisitem = event.target.closest("li");
        var thismenu = thisitem.closest("ul");
        var targetitem = null;
        var parentitem;
        var parentitem_up = null;
        var parentitem_sideways = null;
        var submenu;
        function ishorizontal(menu) {
            // Which direction does this menu go in?
            var cs = window.getComputedStyle(menu);
            return cs.display == "flex" && cs.flexDirection == "row";
        }
        if (dlg_dimmer !== null)
            return;
        if (!["ArrowLeft", "ArrowRight", "ArrowUp", "ArrowDown", "Enter",
              "Escape", "Backspace", "SoftRight"]
            .includes(event.key))
            return;
        if (ishorizontal(thismenu)) {
            // Top-level menu bar.
            if (event.key == "ArrowLeft")
                targetitem = prevmenuitem(thisitem) || lastmenuitem(thismenu);
            else if (event.key == "ArrowRight")
                targetitem = nextmenuitem(thisitem) || firstmenuitem(thismenu);
            else if (event.key == "ArrowUp")
                targetitem = lastmenuitem(thisitem.querySelector("ul"));
            else if (event.key == "ArrowDown" || event.key == "Enter")
                targetitem = firstmenuitem(thisitem.querySelector("ul"));
        } else {
            // Ordinary vertical menu.
            parentitem = thismenu.closest("li");
            if (parentitem) {
                if (ishorizontal(parentitem.closest("ul")))
                    parentitem_up = parentitem;
                else
                    parentitem_sideways = parentitem;
            }
            if (event.key == "ArrowUp")
                targetitem = prevmenuitem(thisitem) || parentitem_up ||
                    lastmenuitem(thismenu);
            else if (event.key == "ArrowDown")
                targetitem = nextmenuitem(thisitem) || parentitem_up ||
                    firstmenuitem(thismenu);
            else if (event.key == "ArrowRight")
                targetitem = thisitem.querySelector("li") ||
                    (parentitem_up && nextmenuitem(parentitem_up));
            else if (event.key == "Enter")
                targetitem = thisitem.querySelector("li");
            else if (event.key == "ArrowLeft")
                targetitem = parentitem_sideways ||
                    (parentitem_up && prevmenuitem(parentitem_up));
            else if (event.key == "Backspace")
                targetitem = parentitem;
        }
        if (targetitem)
            targetitem.firstElementChild.focus();
        else if (event.key == "Enter")
            event.target.click();
        else if (event.key == "Escape" || event.key == "SoftRight" ||
                 event.key == "Backspace")
            // Leave the menu entirely.
            onscreen_canvas.focus();
        // Prevent default even if we didn't do anything, as long as this
        // was an interesting key.
        event.preventDefault();
    }
    menuform.addEventListener("keydown", menukey);

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
            event.preventDefault();
            event.stopPropagation();
        }

        if (dlg_dimmer !== null && event.keyCode == 27) {
            command(4);
            event.preventDefault();
            event.stopPropagation();
        }
    }, true);

    // Event handler to fake :focus-within on browsers too old for
    // it (like KaiOS 2.5).  Browsers without :focus-within are also
    // too old for focusin/out events, so we have to use focus events
    // which don't bubble but can be captured.
    //
    // A button losing focus because it was disabled doesn't generate
    // a blur event, so we do this entirely in the focus handler.
    document.documentElement.addEventListener("focus", function(event) {
        for (var elem = event.target; elem; elem = elem.parentElement)
            elem.classList.add("focus-within");
        for (elem of
             Array.from(document.getElementsByClassName("focus-within")))
            if (!elem.contains(event.target))
                elem.classList.remove("focus-within");
    }, true);

    // Set up the function pointers we haven't already grabbed. 
    dlg_return_sval = Module.cwrap('dlg_return_sval', 'void',
                                   ['number','string']);
    dlg_return_ival = Module.cwrap('dlg_return_ival', 'void',
                                   ['number','number']);
    timer_callback = Module.cwrap('timer_callback', 'void', ['number']);

    // Save references to the two permalinks and the status bar.
    permalink_desc = document.getElementById("permalink-desc");
    permalink_seed = document.getElementById("permalink-seed");
    statusbar = document.getElementById("statusbar");

    resizable_div = document.getElementById("resizable");
    if (resizable_div !== null) {
        // Delete any existing resize handle.
        var resize_handle = document.getElementById("resizehandle");
        if (resize_handle)
            resize_handle.parentNode.removeChild(resize_handle);
        // Create the resize handle.
        resize_handle = document.createElement("canvas");
        resize_handle.width = 10;
        resize_handle.height = 10;

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

        resizable_div.appendChild(resize_handle);
        resize_handle.id = "resizehandle";
        resize_handle.title = "Drag to resize the puzzle. Right-click to restore the default size.";
        var resize_xbase = null, resize_ybase = null, restore_pending = false;
        var resize_xoffset = null, resize_yoffset = null;
        var resize_puzzle = Module.cwrap('resize_puzzle',
                                         'void', ['number', 'number']);
        var restore_puzzle_size = Module.cwrap('restore_puzzle_size',
                                               'void', []);
        resize_handle.oncontextmenu = function(event) { return false; }
        resize_handle.onmousedown = function(event) {
            if (event.button == 0) {
                var xy = element_coords(onscreen_canvas);
                resize_xbase = xy.x + onscreen_canvas.offsetWidth / 2;
                resize_ybase = xy.y;
                resize_xoffset =
                    xy.x + onscreen_canvas.offsetWidth - event.pageX;
                resize_yoffset =
                    xy.y + onscreen_canvas.offsetHeight - event.pageY;
            } else {
                restore_pending = true;
            }
            resize_handle.setCapture(true);
            event.preventDefault();
        };
        window.addEventListener("mousemove", function(event) {
            if (resize_xbase !== null && resize_ybase !== null) {
                var dpr = window.devicePixelRatio || 1;
                resize_puzzle(
                    (event.pageX + resize_xoffset - resize_xbase) * dpr * 2,
                    (event.pageY + resize_yoffset - resize_ybase) * dpr);
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
    }

    /*
     * Arrange to detect changes of device pixel ratio.  Adapted from
     * <https://developer.mozilla.org/en-US/docs/Web/API/Window/
     * devicePixelRatio> (CC0) to work on older browsers.
     */
    var rescale_puzzle = Module.cwrap('rescale_puzzle', 'void', []);
    var mql = null;
    var update_pixel_ratio = function() {
        var dpr = window.devicePixelRatio;
        if (mql !== null)
            mql.removeListener(update_pixel_ratio);
        mql = window.matchMedia(`(resolution: ${dpr}dppx)`);
        mql.addListener(update_pixel_ratio);
        rescale_puzzle();
    }

    Module.preRun = function() {
        // Merge environment variables from HTML script element.
        // This means you can add something like this to the HTML:
        // <script id="environment" type="application/json">
        //   { "LOOPY_DEFAULT": "20x10t11dh" }
        // </script>
        var envscript = document.getElementById("environment");
        var k, v;
        if (envscript !== null)
            for ([k, v] of
                 Object.entries(JSON.parse(envscript.textContent)))
                ENV[k] = v;
    };

    Module.onRuntimeInitialized = function() {
        // Run the C setup function, passing argv[1] as the fragment
        // identifier (so that permalinks of the form puzzle.html#game-id
        // can launch the specified id).
        Module.callMain([decodeURIComponent(location.hash)]);

        update_pixel_ratio();
        // And if we get here with everything having gone smoothly, i.e.
        // we haven't crashed for one reason or another during setup, then
        // it's probably safe to hide the 'sorry, no puzzle here' div and
        // show the div containing the actual puzzle.
        var apology = document.getElementById("apology");
        if (apology !== null) apology.style.display = "none";
        document.getElementById("puzzle").style.display = "";

        // Default to giving keyboard focus to the puzzle.
        onscreen_canvas.focus();
    };
}
