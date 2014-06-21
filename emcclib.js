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
     * void js_debug(const char *message);
     *
     * A function to write a diagnostic to the Javascript console.
     * Unused in production, but handy in development.
     */
    js_debug: function(ptr) {
        console.log(Pointer_stringify(ptr));
    },

    /*
     * void js_error_box(const char *message);
     *
     * A wrapper around Javascript's alert(), so the C code can print
     * simple error message boxes (e.g. when invalid data is entered
     * in a configuration dialog).
     */
    js_error_box: function(ptr) {
        alert(Pointer_stringify(ptr));
    },

    /*
     * void js_remove_type_dropdown(void);
     *
     * Get rid of the drop-down list on the web page for selecting
     * game presets. Called at setup time if the game back end
     * provides neither presets nor configurability.
     */
    js_remove_type_dropdown: function() {
        document.getElementById("gametype").style.display = "none";
    },

    /*
     * void js_remove_solve_button(void);
     *
     * Get rid of the Solve button on the web page. Called at setup
     * time if the game doesn't support an in-game solve function.
     */
    js_remove_solve_button: function() {
        document.getElementById("solve").style.display = "none";
    },

    /*
     * void js_add_preset(const char *name);
     *
     * Add a preset to the drop-down types menu. The provided text is
     * the name of the preset. (The corresponding game_params stays on
     * the C side and never comes out this far; we just pass a numeric
     * index back to the C code when a selection is made.)
     *
     * The special 'Custom' preset is requested by passing NULL to
     * this function, rather than the string "Custom", since in that
     * case we need to do something special - see below.
     */
    js_add_preset: function(ptr) {
        var name = (ptr == 0 ? "Customise..." : Pointer_stringify(ptr));
        var value = gametypeoptions.length;

        var option = document.createElement("option");
        option.value = value;
        option.appendChild(document.createTextNode(name));
        gametypeselector.appendChild(option);
        gametypeoptions.push(option);

        if (ptr == 0) {
            // The option we've just created is the one for inventing
            // a new custom setup.
            gametypenewcustom = option;
            option.value = -1;

            // Now create another element called 'Custom', which will
            // be auto-selected by us to indicate the custom settings
            // you've previously selected. However, we don't add it to
            // the game type selector; it will only appear when the
            // user actually has custom settings selected.
            option = document.createElement("option");
            option.value = -2;
            option.appendChild(document.createTextNode("Custom"));
            gametypethiscustom = option;
        }
    },

    /*
     * int js_get_selected_preset(void);
     *
     * Return the index of the currently selected value in the type
     * dropdown.
     */
    js_get_selected_preset: function() {
        for (var i in gametypeoptions) {
            if (gametypeoptions[i].selected) {
                return gametypeoptions[i].value;
            }
        }
        return 0;
    },

    /*
     * void js_select_preset(int n);
     *
     * Cause a different value to be selected in the type dropdown
     * (for when the user selects values from the Custom configurer
     * which turn out to exactly match a preset).
     */
    js_select_preset: function(n) {
        if (gametypethiscustom !== null) {
            // Fiddle with the Custom/Customise options. If we're
            // about to select the Custom option, then it should be in
            // the menu, and the other one should read "Re-customise";
            // if we're about to select another one, then the static
            // Custom option should disappear and the other one should
            // read "Customise".

            if (gametypethiscustom.parentNode == gametypeselector)
                gametypeselector.removeChild(gametypethiscustom);
            if (gametypenewcustom.parentNode == gametypeselector)
                gametypeselector.removeChild(gametypenewcustom);

            if (n < 0) {
                gametypeselector.appendChild(gametypethiscustom);
                gametypenewcustom.lastChild.data = "Re-customise...";
            } else {
                gametypenewcustom.lastChild.data = "Customise...";
            }
            gametypeselector.appendChild(gametypenewcustom);
            gametypenewcustom.selected = false;
        }

        if (n < 0) {
            gametypethiscustom.selected = true;
        } else {
            gametypeoptions[n].selected = true;
        }
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
        desc = Pointer_stringify(desc);
        permalink_desc.href = "#" + desc;

        if (seed == 0) {
            permalink_seed.style.display = "none";
        } else {
            seed = Pointer_stringify(seed);
            permalink_seed.href = "#" + seed;
            permalink_seed.style.display = "inline";
        }
    },

    /*
     * void js_enable_undo_redo(int undo, int redo);
     *
     * Set the enabled/disabled states of the undo and redo buttons,
     * after a move.
     */
    js_enable_undo_redo: function(undo, redo) {
        undo_button.disabled = (undo == 0);
        redo_button.disabled = (redo == 0);
    },

    /*
     * void js_activate_timer();
     *
     * Start calling the C timer_callback() function every 20ms.
     */
    js_activate_timer: function() {
        if (timer === null) {
            timer_reference_date = (new Date()).valueOf();
            timer = setInterval(function() {
                var now = (new Date()).valueOf();
                timer_callback((now - timer_reference_date) / 1000.0);
                timer_reference_date = now;
                return true;
            }, 20);
        }
    },

    /*
     * void js_deactivate_timer();
     *
     * Stop calling the C timer_callback() function every 20ms.
     */
    js_deactivate_timer: function() {
        if (timer !== null) {
            clearInterval(timer);
            timer = null;
        }
    },

    /*
     * void js_canvas_start_draw(void);
     *
     * Prepare to do some drawing on the canvas.
     */
    js_canvas_start_draw: function() {
        ctx = offscreen_canvas.getContext('2d');
        update_xmin = update_xmax = update_ymin = update_ymax = undefined;
    },

    /*
     * void js_canvas_draw_update(int x, int y, int w, int h);
     *
     * Mark a rectangle of the off-screen canvas as needing to be
     * copied to the on-screen one.
     */
    js_canvas_draw_update: function(x, y, w, h) {
        /*
         * Currently we do this in a really simple way, just by taking
         * the smallest rectangle containing all updates so far. We
         * could instead keep the data in a richer form (e.g. retain
         * multiple smaller rectangles needing update, and only redraw
         * the whole thing beyond a certain threshold) but this will
         * do for now.
         */
        if (update_xmin === undefined || update_xmin > x) update_xmin = x;
        if (update_ymin === undefined || update_ymin > y) update_ymin = y;
        if (update_xmax === undefined || update_xmax < x+w) update_xmax = x+w;
        if (update_ymax === undefined || update_ymax < y+h) update_ymax = y+h;
    },

    /*
     * void js_canvas_end_draw(void);
     *
     * Finish the drawing, by actually copying the newly drawn stuff
     * to the on-screen canvas.
     */
    js_canvas_end_draw: function() {
        if (update_xmin !== undefined) {
            var onscreen_ctx = onscreen_canvas.getContext('2d');
            onscreen_ctx.drawImage(offscreen_canvas,
                                   update_xmin, update_ymin,
                                   update_xmax - update_xmin,
                                   update_ymax - update_ymin,
                                   update_xmin, update_ymin,
                                   update_xmax - update_xmin,
                                   update_ymax - update_ymin);
        }
        ctx = null;
    },

    /*
     * void js_canvas_draw_rect(int x, int y, int w, int h,
     *                          const char *colour);
     * 
     * Draw a rectangle.
     */
    js_canvas_draw_rect: function(x, y, w, h, colptr) {
        ctx.fillStyle = Pointer_stringify(colptr);
        ctx.fillRect(x, y, w, h);
    },

    /*
     * void js_canvas_clip_rect(int x, int y, int w, int h);
     * 
     * Set a clipping rectangle.
     */
    js_canvas_clip_rect: function(x, y, w, h) {
        ctx.save();
        ctx.beginPath();
        ctx.rect(x, y, w, h);
        ctx.clip();
    },

    /*
     * void js_canvas_unclip(void);
     * 
     * Reset to no clipping.
     */
    js_canvas_unclip: function() {
        ctx.restore();
    },

    /*
     * void js_canvas_draw_line(float x1, float y1, float x2, float y2,
     *                          int width, const char *colour);
     * 
     * Draw a line. We must adjust the coordinates by 0.5 because
     * Javascript's canvas coordinates appear to be pixel corners,
     * whereas we want pixel centres. Also, we manually draw the pixel
     * at each end of the line, which our clients will expect but
     * Javascript won't reliably do by default (in common with other
     * Postscriptish drawing frameworks).
     */
    js_canvas_draw_line: function(x1, y1, x2, y2, width, colour) {
        colour = Pointer_stringify(colour);

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
     * void js_canvas_draw_poly(int *points, int npoints,
     *                          const char *fillcolour,
     *                          const char *outlinecolour);
     * 
     * Draw a polygon.
     */
    js_canvas_draw_poly: function(pointptr, npoints, fill, outline) {
        ctx.beginPath();
        ctx.moveTo(getValue(pointptr  , 'i32') + 0.5,
                   getValue(pointptr+4, 'i32') + 0.5);
        for (var i = 1; i < npoints; i++)
            ctx.lineTo(getValue(pointptr+8*i  , 'i32') + 0.5,
                       getValue(pointptr+8*i+4, 'i32') + 0.5);
        ctx.closePath();
        if (fill != 0) {
            ctx.fillStyle = Pointer_stringify(fill);
            ctx.fill();
        }
        ctx.lineWidth = '1';
        ctx.lineCap = 'round';
        ctx.lineJoin = 'round';
        ctx.strokeStyle = Pointer_stringify(outline);
        ctx.stroke();
    },

    /*
     * void js_canvas_draw_circle(int x, int y, int r,
     *                            const char *fillcolour,
     *                            const char *outlinecolour);
     * 
     * Draw a circle.
     */
    js_canvas_draw_circle: function(x, y, r, fill, outline) {
        ctx.beginPath();
        ctx.arc(x + 0.5, y + 0.5, r, 0, 2*Math.PI);
        if (fill != 0) {
            ctx.fillStyle = Pointer_stringify(fill);
            ctx.fill();
        }
        ctx.lineWidth = '1';
        ctx.lineCap = 'round';
        ctx.lineJoin = 'round';
        ctx.strokeStyle = Pointer_stringify(outline);
        ctx.stroke();
    },

    /*
     * int js_canvas_find_font_midpoint(int height, const char *fontptr);
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
    js_canvas_find_font_midpoint: function(height, font) {
        font = Pointer_stringify(font);

        // Reuse cached value if possible
        if (midpoint_cache[font] !== undefined)
            return midpoint_cache[font];

        // Find the width of the string
        var ctx1 = onscreen_canvas.getContext('2d');
        ctx1.font = font;
        var width = (ctx1.measureText(midpoint_test_str).width + 1) | 0;

        // Construct a test canvas of appropriate size, initialise it to
        // black, and draw the string on it in white
        var measure_canvas = document.createElement('canvas');
        var ctx2 = measure_canvas.getContext('2d');
        ctx2.canvas.width = width;
        ctx2.canvas.height = 2*height;
        ctx2.fillStyle = "#000000";
        ctx2.fillRect(0, 0, width, 2*height);
        var baseline = (1.5*height) | 0;
        ctx2.fillStyle = "#ffffff";
        ctx2.font = font;
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
        midpoint_cache[font] = ret;
        return ret;
    },

    /*
     * void js_canvas_draw_text(int x, int y, int halign,
     *                          const char *colptr, const char *fontptr,
     *                          const char *text);
     * 
     * Draw text. Vertical alignment has been taken care of on the C
     * side, by optionally calling the above function. Horizontal
     * alignment is handled here, since we can get the canvas draw
     * function to do it for us with almost no extra effort.
     */
    js_canvas_draw_text: function(x, y, halign, colptr, fontptr, text) {
        ctx.font = Pointer_stringify(fontptr);
        ctx.fillStyle = Pointer_stringify(colptr);
        ctx.textAlign = (halign == 0 ? 'left' :
                         halign == 1 ? 'center' : 'right');
        ctx.textBaseline = 'alphabetic';
        ctx.fillText(Pointer_stringify(text), x, y);
    },

    /*
     * int js_canvas_new_blitter(int w, int h);
     * 
     * Create a new blitter object, which is just an offscreen canvas
     * of the specified size.
     */
    js_canvas_new_blitter: function(w, h) {
        var id = blittercount++;
        blitters[id] = document.createElement("canvas");
        blitters[id].width = w;
        blitters[id].height = h;
        return id;
    },

    /*
     * void js_canvas_free_blitter(int id);
     * 
     * Free a blitter (or rather, destroy our reference to it so JS
     * can garbage-collect it, and also enforce that we don't
     * accidentally use it again afterwards).
     */
    js_canvas_free_blitter: function(id) {
        blitters[id] = null;
    },

    /*
     * void js_canvas_copy_to_blitter(int id, int x, int y, int w, int h);
     * 
     * Copy from the puzzle image to a blitter. The size is passed to
     * us, partly so we don't have to remember the size of each
     * blitter, but mostly so that the C side can adjust the copy
     * rectangle in the case where it partially overlaps the edge of
     * the screen.
     */
    js_canvas_copy_to_blitter: function(id, x, y, w, h) {
        var blitter_ctx = blitters[id].getContext('2d');
        blitter_ctx.drawImage(offscreen_canvas,
                              x, y, w, h,
                              0, 0, w, h);
    },

    /*
     * void js_canvas_copy_from_blitter(int id, int x, int y, int w, int h);
     * 
     * Copy from a blitter back to the puzzle image. As above, the
     * size of the copied rectangle is passed to us from the C side
     * and may already have been modified.
     */
    js_canvas_copy_from_blitter: function(id, x, y, w, h) {
        ctx.drawImage(blitters[id],
                      0, 0, w, h,
                      x, y, w, h);
    },

    /*
     * void js_canvas_make_statusbar(void);
     * 
     * Cause a status bar to exist. Called at setup time if the puzzle
     * back end turns out to want one.
     */
    js_canvas_make_statusbar: function() {
        var statusholder = document.getElementById("statusbarholder");
        statusbar = document.createElement("div");
        statusbar.style.overflow = "hidden";
        statusbar.style.width = (onscreen_canvas.width - 4) + "px";
        statusholder.style.width = onscreen_canvas.width + "px";
        statusbar.style.height = "1.2em";
        statusbar.style.textAlign = "left";
        statusbar.style.background = "#d8d8d8";
        statusbar.style.borderLeft = '2px solid #c8c8c8';
        statusbar.style.borderTop = '2px solid #c8c8c8';
        statusbar.style.borderRight = '2px solid #e8e8e8';
        statusbar.style.borderBottom = '2px solid #e8e8e8';
        statusbar.appendChild(document.createTextNode(" "));
        statusholder.appendChild(statusbar);
    },

    /*
     * void js_canvas_set_statusbar(const char *text);
     * 
     * Set the text in the status bar.
     */
    js_canvas_set_statusbar: function(ptr) {
        var text = Pointer_stringify(ptr);
        statusbar.replaceChild(document.createTextNode(text),
                               statusbar.lastChild);
    },

    /*
     * void js_canvas_set_size(int w, int h);
     * 
     * Set the size of the puzzle canvas. Called at setup, and every
     * time the user picks new puzzle settings requiring a different
     * size.
     */
    js_canvas_set_size: function(w, h) {
        onscreen_canvas.width = w;
        offscreen_canvas.width = w;
        if (statusbar !== null) {
            statusbar.style.width = (w - 4) + "px";
            document.getElementById("statusbarholder").style.width = w + "px";
        }
        resizable_div.style.width = w + "px";

        onscreen_canvas.height = h;
        offscreen_canvas.height = h;
    },

    /*
     * void js_dialog_init(const char *title);
     * 
     * Begin constructing a 'dialog box' which will be popped up in an
     * overlay on top of the rest of the puzzle web page.
     */
    js_dialog_init: function(titletext) {
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
        title.appendChild(document.createTextNode
                          (Pointer_stringify(titletext)));
        dlg_form.appendChild(title);

        dlg_return_funcs = [];
        dlg_next_id = 0;
    },

    /*
     * void js_dialog_string(int i, const char *title, const char *initvalue);
     * 
     * Add a string control (that is, an edit box) to the dialog under
     * construction.
     */
    js_dialog_string: function(index, title, initialtext) {
        dlg_form.appendChild(document.createTextNode(Pointer_stringify(title)));
        var editbox = document.createElement("input");
        editbox.type = "text";
        editbox.value = Pointer_stringify(initialtext);
        dlg_form.appendChild(editbox);
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
        dlg_form.appendChild(document.createTextNode(Pointer_stringify(title)));
        var dropdown = document.createElement("select");
        var choicestr = Pointer_stringify(choicelist);
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
        dlg_form.appendChild(dropdown);
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
        checkbox.id = "cb" + String(dlg_next_id++);
        checkbox.checked = (initvalue != 0);
        dlg_form.appendChild(checkbox);
        var checkboxlabel = document.createElement("label");
        checkboxlabel.setAttribute("for", checkbox.id);
        checkboxlabel.textContent = Pointer_stringify(title);
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
        // Put in the OK and Cancel buttons at the bottom.
        var button;

        button = document.createElement("input");
        button.type = "button";
        button.value = "OK";
        button.onclick = function(event) {
            for (var i in dlg_return_funcs)
                dlg_return_funcs[i]();
            command(3);
        }
        dlg_form.appendChild(button);

        button = document.createElement("input");
        button.type = "button";
        button.value = "Cancel";
        button.onclick = function(event) {
            command(4);
        }
        dlg_form.appendChild(button);

        document.body.appendChild(dlg_dimmer);
        document.body.appendChild(dlg_form);
    },

    /*
     * void js_dialog_cleanup(void);
     * 
     * Stop displaying a dialog, and clean up the internal state
     * associated with it.
     */
    js_dialog_cleanup: function() {
        document.body.removeChild(dlg_dimmer);
        document.body.removeChild(dlg_form);
        dlg_dimmer = dlg_form = null;
        onscreen_canvas.focus();
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
    }
});
