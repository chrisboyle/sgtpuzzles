/*
 * emcclib.js: one of the Javascript components of an Emscripten-based
 * web/Javascript front end for Puzzles.
 *
 * The other parts of this system live in emcc.c and emccpre.js. It
 * also depends on being run in the context of a web page containing
 * an appropriate collection of bits and pieces (a canvas, some
 * buttons and links etc), which is generated for each puzzle by the
 * script html/jspage.pl.
 *
 * This file contains a set of Javascript functions which we insert
 * into Emscripten's library object via the --js-library option; this
 * allows us to provide JS code which can be called from the
 * Emscripten-compiled C, mostly dealing with UI interaction of
 * various kinds.
 */

mergeInto(LibraryManager.library, {
    /*
     * void js_init_puzzle(void);
     *
     * Called at the start of main() to set up event handlers.
     */
    js_init_puzzle: function() {
        initPuzzle();
    },
    /*
     * void js_post_init(void);
     *
     * Called at the end of main() once the initial puzzle has been
     * started.
     */
    js_post_init: function() {
        post_init();
    },
    /*
     * void js_debug(const char *message);
     *
     * A function to write a diagnostic to the Javascript console.
     * Unused in production, but handy in development.
     */
    js_debug: function(ptr) {
        console.log(UTF8ToString(ptr));
    },

    /*
     * void js_error_box(const char *message);
     *
     * A wrapper around Javascript's alert(), so the C code can print
     * simple error message boxes (e.g. when invalid data is entered
     * in a configuration dialog).
     */
    js_error_box: function(ptr) {
        alert(UTF8ToString(ptr));
    },

    /*
     * void js_remove_type_dropdown(void);
     *
     * Get rid of the drop-down list on the web page for selecting
     * game presets. Called at setup time if the game back end
     * provides neither presets nor configurability.
     */
    js_remove_type_dropdown: function() {
        if (gametypelist === null) return;
        var gametypeitem = gametypelist.closest("li");
        if (gametypeitem === null) return;
        gametypeitem.parentNode.removeChild(gametypeitem);
        gametypelist = null;
    },

    /*
     * void js_remove_solve_button(void);
     *
     * Get rid of the Solve button on the web page. Called at setup
     * time if the game doesn't support an in-game solve function.
     */
    js_remove_solve_button: function() {
        if (solve_button === null) return;
        var solve_item = solve_button.closest("li");
        if (solve_item === null) return;
        solve_item.parentNode.removeChild(solve_item);
        solve_button = null;
    },

    /*
     * void js_add_preset(int menuid, const char *name, int value);
     *
     * Add a preset to the drop-down types menu, or to a submenu of
     * it. 'menuid' specifies an index into our array of submenus
     * where the item might be placed; 'value' specifies the number
     * that js_get_selected_preset() will return when this item is
     * clicked.
     */
    js_add_preset: function(menuid, ptr, value) {
        var name = UTF8ToString(ptr);
        var item = document.createElement("li");
        var label = document.createElement("label");
        var tick = document.createElement("input");
        tick.type = "radio";
        tick.className = "tick";
        tick.name = "preset";
        tick.value = value;
        label.appendChild(tick);
        label.appendChild(document.createTextNode(" " + name));
        item.appendChild(label);
        gametypesubmenus[menuid].appendChild(item);

        tick.onclick = function(event) {
            if (dlg_dimmer === null) {
                command(2);
            }
        }
    },

    /*
     * int js_add_preset_submenu(int menuid, const char *name);
     *
     * Add a submenu in the presets menu hierarchy. Returns its index,
     * for passing as the 'menuid' argument in further calls to
     * js_add_preset or this function.
     */
    js_add_preset_submenu: function(menuid, ptr, value) {
        var name = UTF8ToString(ptr);
        var item = document.createElement("li");
        // We still create a transparent tick element, even though it
        // won't ever be selected, to make submenu titles line up
        // nicely with their neighbours.
        var label = document.createElement("div");
        var tick = document.createElement("span");
        tick.className = "tick";
        label.appendChild(tick);
        label.tabIndex = 0;
        label.appendChild(document.createTextNode(" " + name));
        item.appendChild(label);
        var submenu = document.createElement("ul");
        label.appendChild(submenu);
        gametypesubmenus[menuid].appendChild(item);
        var toret = gametypesubmenus.length;
        gametypesubmenus.push(submenu);
        return toret;
    },

    /*
     * int js_get_selected_preset(void);
     *
     * Return the index of the currently selected value in the type
     * dropdown.
     */
    js_get_selected_preset: function() {
        return menuform.elements["preset"].value;
    },

    /*
     * void js_select_preset(int n);
     *
     * Cause a different value to be selected in the type dropdown
     * (for when the user selects values from the Custom configurer
     * which turn out to exactly match a preset).
     */
    js_select_preset: function(n) {
        menuform.elements["preset"].value = n;
    },

    /*
     * void js_default_colour(float *output);
     *
     * Try to extract a default colour from the CSS computed
     * background colour of the canvas element.
     */
    js_default_colour: function(output) {
        var col = window.getComputedStyle(onscreen_canvas).backgroundColor;
        /* We only support opaque sRGB colours. */
        var m = col.match(
            /^rgb\((\d+(?:\.\d+)?), (\d+(?:\.\d+)?), (\d+(?:\.\d+)?)\)$/);
        if (m) {
            setValue(output,     +m[1] / 255, "float");
            setValue(output + 4, +m[2] / 255, "float");
            setValue(output + 8, +m[3] / 255, "float");
        }
    },

    /*
     * void js_set_colour(int colour_number, char const *colour_string);
     *
     * Record a colour string used by the puzzle.
     */
    js_set_colour: function(colour_number, colour_string) {
        colours[colour_number] = UTF8ToString(colour_string);
        if (colour_number == 0)
            document.documentElement.style.setProperty("--puzzle-background",
                                                       colours[colour_number]);
    },

    /*
     * void js_get_date_64(unsigned *p);
     *
     * Return the current date, in milliseconds since the epoch
     * (Javascript's native format), as a 64-bit integer. Used to
     * invent an initial random seed for puzzle generation.
     */
    js_get_date_64: function(ptr) {
        var d = (new Date()).valueOf();
        setValue(ptr, d, 'i64');
    },

    /*
     * void js_update_permalinks(const char *desc, const char *seed);
     *
     * Update the permalinks on the web page for a new game
     * description and optional random seed. desc can never be NULL,
     * but seed might be (if the game was generated by entering a
     * descriptive id by hand), in which case we suppress display of
     * the random seed permalink.
     */
    js_update_permalinks: function(desc, seed) {
        desc = encodeURI(UTF8ToString(desc)).replace(/#/g, "%23");
        if (permalink_desc !== null)
            permalink_desc.href = "#" + desc;

        if (permalink_seed !== null) {
            if (seed == 0) {
                permalink_seed.style.display = "none";
            } else {
                seed = encodeURI(UTF8ToString(seed)).replace(/#/g, "%23");;
                permalink_seed.href = "#" + seed;
                permalink_seed.style.display = "";
            }
        }
    },

    /*
     * void js_enable_undo_redo(int undo, int redo);
     *
     * Set the enabled/disabled states of the undo and redo buttons,
     * after a move.
     */
    js_enable_undo_redo: function(undo, redo) {
        disable_menu_item(undo_button, (undo == 0));
        disable_menu_item(redo_button, (redo == 0));
    },

    /*
     * void js_enable_undo_redo(bool undo, bool redo);
     *
     * Update any labels for the SoftLeft and Enter keys.
     */
    js_update_key_labels: function(lsk_ptr, csk_ptr) {
        var elem;
        var lsk_text = UTF8ToString(lsk_ptr);
        var csk_text = UTF8ToString(csk_ptr);
        for (elem of document.querySelectorAll("#puzzle .lsk"))
            elem.textContent = lsk_text == csk_text ? "" : lsk_text;
        for (elem of document.querySelectorAll("#puzzle .csk"))
            elem.textContent = csk_text;
    },

    /*
     * void js_activate_timer();
     *
     * Start calling the C timer_callback() function every frame.
     * The C code ensures that the activate and deactivate functions
     * are called in a sensible order.
     */
    js_activate_timer: function() {
        timer_reference = performance.now();
        var frame = function(now) {
            timer = window.requestAnimationFrame(frame);
            // The callback might call js_deactivate_timer() below.
            timer_callback((now - timer_reference) / 1000.0);
            timer_reference = now;
        };
        timer = window.requestAnimationFrame(frame);
    },

    /*
     * void js_deactivate_timer();
     *
     * Stop calling the C timer_callback() function every frame.
     */
    js_deactivate_timer: function() {
        window.cancelAnimationFrame(timer);
    },

    /* ------------------------------------------------------------------
     * Implementation of the drawing API by calling Javascript canvas
     * drawing functions.  Functions that don't take a drawing * as the
     * first argument have C wrappers in emcc.c.
     */
    /*
     * void js_canvas_start_draw(drawing *dr);
     *
     * Prepare to do some drawing on the canvas.
     */
    js_canvas_start_draw: function(dr) {
        update_xmin = update_xmax = update_ymin = update_ymax = undefined;
    },

    /*
     * void js_canvas_draw_update(drawing *dr, int x, int y, int w, int h);
     *
     * Mark a rectangle of the off-screen canvas as needing to be
     * copied to the on-screen one.
     */
    js_canvas_draw_update: function(dr, x, y, w, h) {
        /*
         * Currently we do this in a really simple way, just by taking
         * the smallest rectangle containing all updates so far. We
         * could instead keep the data in a richer form (e.g. retain
         * multiple smaller rectangles needing update, and only redraw
         * the whole thing beyond a certain threshold) but this will
         * do for now.
         */
        x *= fe_scale; y *= fe_scale; w *= fe_scale; h *= fe_scale;
        if (update_xmin === undefined || update_xmin > x) update_xmin = x;
        if (update_ymin === undefined || update_ymin > y) update_ymin = y;
        if (update_xmax === undefined || update_xmax < x+w) update_xmax = x+w;
        if (update_ymax === undefined || update_ymax < y+h) update_ymax = y+h;
    },

    /*
     * void js_canvas_end_draw(drawing *dr);
     *
     * Finish the drawing, by actually copying the newly drawn stuff
     * to the on-screen canvas.
     */
    js_canvas_end_draw: function(dr) {
        if (update_xmin !== undefined) {
            var onscreen_ctx =
                onscreen_canvas.getContext('2d', { alpha: false });
            onscreen_ctx.drawImage(offscreen_canvas,
                                   update_xmin, update_ymin,
                                   update_xmax - update_xmin,
                                   update_ymax - update_ymin,
                                   update_xmin, update_ymin,
                                   update_xmax - update_xmin,
                                   update_ymax - update_ymin);
        }
    },

    /*
     * void js_canvas_draw_rect(drawing *dr,
     *                          int x, int y, int w, int h, int colour);
     * 
     * Draw a rectangle.
     */
    js_canvas_draw_rect: function(dr, x, y, w, h, colour) {
        ctx.fillStyle = colours[colour];
        ctx.fillRect(x, y, w, h);
    },

    /*
     * void js_canvas_clip_rect(drawing *dr, int x, int y, int w, int h);
     * 
     * Set a clipping rectangle.
     */
    js_canvas_clip: function(dr, x, y, w, h) {
        ctx.save();
        ctx.beginPath();
        ctx.rect(x, y, w, h);
        ctx.clip();
    },

    /*
     * void js_canvas_unclip(drawing *dr);
     * 
     * Reset to no clipping.
     */
    js_canvas_unclip: function(dr) {
        ctx.restore();
    },

    /*
     * void js_canvas_draw_line(float x1, float y1, float x2, float y2,
     *                          int width, int colour);
     * 
     * Draw a line. We must adjust the coordinates by 0.5 because
     * Javascript's canvas coordinates appear to be pixel corners,
     * whereas we want pixel centres. Also, we manually draw the pixel
     * at each end of the line, which our clients will expect but
     * Javascript won't reliably do by default (in common with other
     * Postscriptish drawing frameworks).
     *
     * This is used to implement both draw_line() and
     * draw_thick_line().
     */
    js_canvas_draw_line: function(x1, y1, x2, y2, width, colour) {
        colour = colours[colour];

        ctx.beginPath();
        ctx.moveTo(x1 + 0.5, y1 + 0.5);
        ctx.lineTo(x2 + 0.5, y2 + 0.5);
        ctx.lineWidth = width;
        ctx.lineCap = 'round';
        ctx.lineJoin = 'round';
        ctx.strokeStyle = colour;
        ctx.stroke();
        ctx.fillStyle = colour;
        ctx.fillRect(x1, y1, 1, 1);
        ctx.fillRect(x2, y2, 1, 1);
    },

    /*
     * void js_canvas_draw_poly(drawing *dr, int *points, int npoints,
     *                          int fillcolour, int outlinecolour);
     * 
     * Draw a polygon.
     */
    js_canvas_draw_poly: function(dr, pointptr, npoints, fill, outline) {
        ctx.beginPath();
        ctx.moveTo(getValue(pointptr  , 'i32') + 0.5,
                   getValue(pointptr+4, 'i32') + 0.5);
        for (var i = 1; i < npoints; i++)
            ctx.lineTo(getValue(pointptr+8*i  , 'i32') + 0.5,
                       getValue(pointptr+8*i+4, 'i32') + 0.5);
        ctx.closePath();
        if (fill >= 0) {
            ctx.fillStyle = colours[fill];
            ctx.fill();
        }
        ctx.lineWidth = '1';
        ctx.lineCap = 'round';
        ctx.lineJoin = 'round';
        ctx.strokeStyle = colours[outline];
        ctx.stroke();
    },

    /*
     * void js_canvas_draw_circle(drawing *dr, int x, int y, int r,
     *                            int fillcolour, int outlinecolour);
     * 
     * Draw a circle.
     */
    js_canvas_draw_circle: function(dr, x, y, r, fill, outline) {
        ctx.beginPath();
        ctx.arc(x + 0.5, y + 0.5, r, 0, 2*Math.PI);
        if (fill >= 0) {
            ctx.fillStyle = colours[fill];
            ctx.fill();
        }
        ctx.lineWidth = '1';
        ctx.lineCap = 'round';
        ctx.lineJoin = 'round';
        ctx.strokeStyle = colours[outline];
        ctx.stroke();
    },

    /*
     * int js_canvas_find_font_midpoint(int height, bool monospaced);
     * 
     * Return the adjustment required for text displayed using
     * ALIGN_VCENTRE. We want to place the midpoint between the
     * baseline and the cap-height at the specified position; so this
     * function returns the adjustment which, when added to the
     * desired centre point, returns the y-coordinate at which you
     * should put the baseline.
     *
     * There is no sensible method of querying this kind of font
     * metric in Javascript, so instead we render a piece of test text
     * to a throwaway offscreen canvas and then read the pixel data
     * back out to find the highest and lowest pixels. That's good
     * _enough_ (in that we only needed the answer to the nearest
     * pixel anyway), but rather disgusting!
     *
     * Since this is a very expensive operation, we cache the results
     * per (font,height) pair.
     */
    js_canvas_find_font_midpoint: function(height, monospaced) {
        if (height == 0) {
            // Handle this degenerate case by hand. Otherwise we end
            // up passing height=0 to the getImageData call below,
            // causing browsers to report errors.
            return 0;
        }

        // Resolve the font into a string.
        var ctx1 = onscreen_canvas.getContext('2d', { alpha: false });
        canvas_set_font(ctx1, height, monospaced);

        // Reuse cached value if possible
        if (midpoint_cache[ctx1.font] !== undefined)
            return midpoint_cache[ctx1.font];

        // Find the width of the string
        var width = (ctx1.measureText(midpoint_test_str).width + 1) | 0;

        // Construct a test canvas of appropriate size, initialise it to
        // black, and draw the string on it in white
        var measure_canvas = document.createElement('canvas');
        var ctx2 = measure_canvas.getContext('2d', { alpha: false });
        ctx2.canvas.width = width;
        ctx2.canvas.height = 2*height;
        ctx2.fillStyle = "#000000";
        ctx2.fillRect(0, 0, width, 2*height);
        var baseline = (1.5*height) | 0;
        ctx2.fillStyle = "#ffffff";
        canvas_set_font(ctx2, height, monospaced);
        ctx2.fillText(midpoint_test_str, 0, baseline);

        // Scan the contents of the test canvas to find the top and bottom
        // set pixels.
        var pixels = ctx2.getImageData(0, 0, width, 2*height).data;
        var ymin = 2*height, ymax = -1;
        for (var y = 0; y < 2*height; y++) {
            for (var x = 0; x < width; x++) {
                if (pixels[4*(y*width+x)] != 0) {
                    if (ymin > y) ymin = y;
                    if (ymax < y) ymax = y;
                    break;
                }
            }
        }

        var ret = (baseline - (ymin + ymax) / 2) | 0;
        midpoint_cache[ctx1.font] = ret;
        return ret;
    },

    /*
     * void js_canvas_draw_text(int x, int y, int halign,
     *                          const char *colptr, int height,
     *                          bool monospaced, const char *text);
     * 
     * Draw text. Vertical alignment has been taken care of on the C
     * side, by optionally calling the above function. Horizontal
     * alignment is handled here, since we can get the canvas draw
     * function to do it for us with almost no extra effort.
     */
    js_canvas_draw_text: function(x, y, halign, colour, fontsize, monospaced,
                                  text) {
        canvas_set_font(ctx, fontsize, monospaced);
        ctx.fillStyle = colours[colour];
        ctx.textAlign = (halign == 0 ? 'left' :
                         halign == 1 ? 'center' : 'right');
        ctx.textBaseline = 'alphabetic';
        ctx.fillText(UTF8ToString(text), x, y);
    },

    /*
     * void js_canvas_new_blitter(blitter *bl, int w, int h);
     * 
     * Create a new blitter object, which is just an offscreen canvas
     * of the specified size.  The pointer (which JavaScript sees as
     * a number) is used to allow us to look it up again later.
     */
    js_canvas_new_blitter: function(bl, w, h) {
        w *= fe_scale; h *= fe_scale;
        var new_blitter = document.createElement("canvas");
        new_blitter.width = w;
        new_blitter.height = h;
        blitters.set(bl, new_blitter);
    },

    /*
     * void js_canvas_free_blitter(blitter *bl);
     * 
     * Free a blitter (or rather, destroy our reference to it so JS
     * can garbage-collect it, and also enforce that we don't
     * accidentally use it again afterwards).
     */
    js_canvas_free_blitter: function(bl) {
        blitters.delete(bl);
    },

    /*
     * void js_canvas_blitter_save(drawing *dr, blitter *bl, int x, int y);
     * 
     * Copy from the puzzle image to a blitter. The size is passed to
     * us, partly so we don't have to remember the size of each
     * blitter, but mostly so that the C side can adjust the copy
     * rectangle in the case where it partially overlaps the edge of
     * the screen.
     */
    js_canvas_blitter_save: function(dr, bl, x, y) {
        var blitter_ctx = blitters.get(bl).getContext('2d', { alpha: false });
        x *= fe_scale; y *= fe_scale;
        blitter_ctx.drawImage(offscreen_canvas, -x, -y);
    },

    /*
     * void js_canvas_blitter_load(drawing *dr, blitter *bl, int x, int y);
     * 
     * Copy from a blitter back to the puzzle image. As above, the
     * size of the copied rectangle is passed to us from the C side
     * and may already have been modified.
     */
    js_canvas_blitter_load: function(dr, bl, x, y) {
        // Temporarily turn off the effects of fe_scale so we can do
        // it ourselves.
        ctx.save();
        ctx.resetTransform();
        x *= fe_scale; y *= fe_scale;
        ctx.drawImage(blitters.get(bl), x, y);
        ctx.restore();
    },

    /*
     * void js_canvas_remove_statusbar(void);
     *
     * Cause a status bar not to exist. Called at setup time if the
     * puzzle back end turns out not to want one.
     */
    js_canvas_remove_statusbar: function() {
        if (statusbar !== null)
            statusbar.parentNode.removeChild(statusbar);
        statusbar = null;
    },

    /*
     * void js_canvas_status_bar(drawing *dr, const char *text);
     * 
     * Set the text in the status bar.
     */
    js_canvas_status_bar: function(dr, ptr) {
        statusbar.textContent = UTF8ToString(ptr);
    },

    /*
     * bool js_canvas_get_preferred_size(int *wp, int *hp);
     *
     * This is called before calling midend_size() to set a puzzle to
     * the default size.  If the JavaScript layer has an opinion about
     * how big the puzzle should be, it can overwrite *wp and *hp with
     * its preferred size, and return true if the "user" parameter to
     * midend_size() should be true.  Otherwise it should leave them
     * alone and return false.
     */
    js_canvas_get_preferred_size: function(wp, hp) {
        if (containing_div !== null) {
            var dpr = window.devicePixelRatio || 1;
            setValue(wp, containing_div.clientWidth * dpr, "i32");
            setValue(hp, containing_div.clientHeight * dpr, "i32");
            return true;
        }
        return false;
    },

    /*
     * void js_canvas_set_size(int w, int h, int fe_scale);
     * 
     * Set the size of the puzzle canvas. Called whenever the size of
     * the canvas needs to change.  That might be because of a change
     * of configuration, because the user has resized the puzzle, or
     * because the device pixel ratio has changed.
     */
    js_canvas_set_size: function(w, h, new_fe_scale) {
        onscreen_canvas.width = w;
        offscreen_canvas.width = w;
        if (resizable_div !== null)
            resizable_div.style.width =
                w / (window.devicePixelRatio || 1) + "px";
        else {
            onscreen_canvas.style.width =
                w / (window.devicePixelRatio || 1) + "px";
            onscreen_canvas.style.height =
                h / (window.devicePixelRatio || 1) + "px";
        }

        onscreen_canvas.height = h;
        offscreen_canvas.height = h;
        fe_scale = new_fe_scale;
        ctx.resetTransform();
        ctx.scale(fe_scale, fe_scale);
    },

    /*
     * double js_get_device_pixel_ratio();
     *
     * Return the current device pixel ratio.
     */
    js_get_device_pixel_ratio: function() {
        return window.devicePixelRatio || 1;
    },

    /*
     * void js_dialog_init(const char *title);
     * 
     * Begin constructing a 'dialog box' which will be popped up in an
     * overlay on top of the rest of the puzzle web page.
     */
    js_dialog_init: function(titletext) {
        dialog_init(UTF8ToString(titletext));
    },

    /*
     * void js_dialog_string(int i, const char *title, const char *initvalue);
     * 
     * Add a string control (that is, an edit box) to the dialog under
     * construction.
     */
    js_dialog_string: function(index, title, initialtext) {
        var label = document.createElement("label");
        label.textContent = UTF8ToString(title);
        dlg_form.appendChild(label);
        var editbox = document.createElement("input");
        editbox.type = "text";
        editbox.value = UTF8ToString(initialtext);
        label.appendChild(editbox);
        dlg_form.appendChild(document.createElement("br"));

        dlg_return_funcs.push(function() {
            dlg_return_sval(index, editbox.value);
        });
    },

    /*
     * void js_dialog_choices(int i, const char *title, const char *choicelist,
     *                        int initvalue);
     * 
     * Add a choices control (i.e. a drop-down list) to the dialog
     * under construction. The 'choicelist' parameter is unchanged
     * from the way the puzzle back end will have supplied it: i.e.
     * it's still encoded as a single string whose first character
     * gives the separator.
     */
    js_dialog_choices: function(index, title, choicelist, initvalue) {
        var label = document.createElement("label");
        label.textContent = UTF8ToString(title);
        dlg_form.appendChild(label);
        var dropdown = document.createElement("select");
        var choicestr = UTF8ToString(choicelist);
        var items = choicestr.slice(1).split(choicestr[0]);
        var options = [];
        for (var i in items) {
            var option = document.createElement("option");
            option.value = i;
            option.appendChild(document.createTextNode(items[i]));
            if (i == initvalue) option.selected = true;
            dropdown.appendChild(option);
            options.push(option);
        }
        label.appendChild(dropdown);
        dlg_form.appendChild(document.createElement("br"));

        dlg_return_funcs.push(function() {
            var val = 0;
            for (var i in options) {
                if (options[i].selected) {
                    val = options[i].value;
                    break;
                }
            }
            dlg_return_ival(index, val);
        });
    },

    /*
     * void js_dialog_boolean(int i, const char *title, int initvalue);
     * 
     * Add a boolean control (a checkbox) to the dialog under
     * construction. Checkboxes are generally expected to be sensitive
     * on their label text as well as the box itself, so for this
     * control we create an actual label rather than merely a text
     * node (and hence we must allocate an id to the checkbox so that
     * the label can refer to it).
     */
    js_dialog_boolean: function(index, title, initvalue) {
        var checkbox = document.createElement("input");
        checkbox.type = "checkbox";
        checkbox.checked = (initvalue != 0);
        var checkboxlabel = document.createElement("label");
        checkboxlabel.appendChild(checkbox);
        checkboxlabel.appendChild(document.createTextNode(UTF8ToString(title)));
        dlg_form.appendChild(checkboxlabel);
        dlg_form.appendChild(document.createElement("br"));

        dlg_return_funcs.push(function() {
            dlg_return_ival(index, checkbox.checked ? 1 : 0);
        });
    },

    /*
     * void js_dialog_launch(void);
     * 
     * Finish constructing a dialog, and actually display it, dimming
     * everything else on the page.
     */
    js_dialog_launch: function() {
        dialog_launch(function(event) {
            for (var i in dlg_return_funcs)
                dlg_return_funcs[i]();
            command(3);         // OK
        }, function(event) {
            command(4);         // Cancel
        });
    },

    /*
     * void js_dialog_cleanup(void);
     * 
     * Stop displaying a dialog, and clean up the internal state
     * associated with it.
     */
    js_dialog_cleanup: function() {
        dialog_cleanup();
    },

    /*
     * void js_focus_canvas(void);
     * 
     * Return keyboard focus to the puzzle canvas. Called after a
     * puzzle-control button is pressed, which tends to have the side
     * effect of taking focus away from the canvas.
     */
    js_focus_canvas: function() {
        onscreen_canvas.focus();
    },

    /*
     * bool js_savefile_read(void *buf, int len);
     *
     * Read len bytes from the save file that we're currently loading.
     */
    js_savefile_read: function(buf, len) {
        return savefile_read_callback(buf, len);
    },

    /*
     * void js_save_prefs(const char *);
     *
     * Write a buffer of serialised preferences data into localStorage.
     */
    js_save_prefs: function(buf) {
        var prefsdata = UTF8ToString(buf);
        try {
            localStorage.setItem(location.pathname + " preferences", prefsdata);
        } catch (error) {
            // Tell the user their preferences have not been saved.
            console.error(error);
            alert("Saving of preferences failed: " + error.message);
        }
    },

    /*
     * void js_load_prefs(midend *);
     *
     * Retrieve preferences data from localStorage. If there is any,
     * pass it back in as a string, via prefs_load_callback.
     */
    js_load_prefs: function(me) {
        function load_prefs_from_string(prefsdata) {
            if (prefsdata !== undefined && prefsdata !== null) {
                var lenbytes = lengthBytesUTF8(prefsdata) + 1;
                var dest = _malloc(lenbytes);
                if (dest != 0) {
                    stringToUTF8(prefsdata, dest, lenbytes);
                    prefs_load_callback(me, dest);
                    _free(dest);
                }
            }
        }
        // Load any default preferences embedded in HTML.
        for (var prefsscript of
             document.querySelectorAll("script.preferences"))
            load_prefs_from_string(prefsscript.textContent);
        // Load saved preferences if they exist.
        try {
            load_prefs_from_string(
                localStorage.getItem(location.pathname + " preferences"));
        } catch (error) {
            // Log the error but otherwise pretend the settings were
            // absent.
            console.warn(error);
        }
    }
});
