if(NOT build_icons)
  # This entire subdirectory does nothing on platforms where we can't
  # build the icons in any case.
  return()
endif()

include(FindPerl)
if(NOT PERL_EXECUTABLE)
  message(WARNING "Puzzle icons cannot be rebuilt (did not find Perl)")
  set(build_icons FALSE)
  return()
endif()

find_program(CONVERT convert)
find_program(IDENTIFY identify)
if(NOT CONVERT OR NOT IDENTIFY)
  message(WARNING "Puzzle icons cannot be rebuilt (did not find ImageMagick)")
  set(build_icons FALSE)
  return()
endif()

# For puzzles which have animated moves, it's nice to show the sample
# image part way through the animation of a move. This setting will
# cause a 'redo' action immediately after loading the save file,
# causing the first undone move in the undo chain to be redone, and
# then it will stop this far through the move animation to take the
# screenshot.
set(cube_redo 0.15)
set(fifteen_redo 0.3)
set(flip_redo 0.3)
set(netslide_redo 0.3)
set(sixteen_redo 0.3)
set(twiddle_redo 0.3)

# For many puzzles, we'd prefer that the icon zooms in on a couple of
# squares of the playing area rather than trying to show the whole of
# a game. These settings configure that. Each one indicates the
# expected full size of the screenshot image, followed by the area we
# want to crop to.
#
# (The expected full size is a safety precaution: if a puzzle changes
# its default display size, then that won't match, and we'll get a
# build error here rather than silently continuing to take the wrong
# subrectangle of the resized puzzle display.)
set(blackbox_crop 352x352 144x144+0+208)
set(bridges_crop 264x264 107x107+157+157)
set(dominosa_crop 304x272 152x152+152+0)
set(fifteen_crop 240x240 120x120+0+120)
set(filling_crop 256x256 133x133+14+78)
set(flip_crop 288x288 145x145+120+72)
set(galaxies_crop 288x288 165x165+0+0)
set(guess_crop 263x420 178x178+75+17)
set(inertia_crop 321x321 128x128+193+0)
set(keen_crop 288x288 96x96+24+120)
set(lightup_crop 256x256 112x112+144+0)
set(loopy_crop 257x257 113x113+0+0)
set(magnets_crop 264x232 96x96+36+100)
set(mines_crop 240x240 110x110+130+130)
set(mosaic_crop 288x288 97x97+142+78)
set(net_crop 193x193 113x113+0+80)
set(netslide_crop 289x289 144x144+0+0)
set(palisade_crop 288x288 192x192+0+0)
set(pattern_crop 384x384 223x223+0+0)
set(pearl_crop 216x216 94x94+108+15)
set(pegs_crop 263x263 147x147+116+0)
set(range_crop 256x256 98x98+111+15)
set(rect_crop 205x205 115x115+90+0)
set(signpost_crop 240x240 98x98+23+23)
set(singles_crop 224x224 98x98+15+15)
set(sixteen_crop 288x288 144x144+144+144)
set(slant_crop 321x321 160x160+160+160)
set(solo_crop 481x481 145x145+24+24)
set(tents_crop 320x320 165x165+142+0)
set(towers_crop 300x300 102x102+151+6)
set(tracks_crop 246x246 118x118+6+6)
set(twiddle_crop 192x192 102x102+69+21)
set(undead_crop 416x480 192x192+16+80)
set(unequal_crop 208x208 104x104+104+104)
set(untangle_crop 320x320 164x164+3+116)

add_custom_target(icons)

# All sizes of icon we make for any purpose.
set(all_icon_sizes 96 88 48 44 32 16)

# Sizes of icon we put into the Windows .ico files.
set(win_icon_sizes 48 32 16)

# Border thickness for each icon size.
set(border_96 4)
set(border_88 4)
set(border_48 4)
set(border_44 4)
set(border_32 2)
set(border_16 1)

set(icon_srcdir ${CMAKE_SOURCE_DIR}/icons)
set(icon_bindir ${CMAKE_BINARY_DIR}/icons)

function(build_icon name)
  set(output_icon_files)

  # Compile the GTK puzzle binary without an icon, so that we can run
  # it to generate a screenshot to make the icon out of.
  add_executable(${NAME}-icon-maker ${NAME}.c
    ${CMAKE_SOURCE_DIR}/no-icon.c)
  target_link_libraries(${NAME}-icon-maker
    common ${platform_gui_libs} ${platform_libs})
  set_target_properties(${NAME}-icon-maker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${icon_bindir})

  # Now run that binary to generate a screenshot of the puzzle in
  # play, which will be the base image we make everything else out
  # out.
  if(DEFINED ${name}_redo)
    set(redo_arg --redo ${${name}_redo})
  else()
    set(redo_arg)
  endif()
  add_custom_command(OUTPUT ${icon_bindir}/${name}-base.png
    COMMAND ${icon_bindir}/${name}-icon-maker
      ${redo_arg}
      --screenshot ${icon_bindir}/${name}-base.png
      --load ${icon_srcdir}/${name}.sav
    DEPENDS
      ${name}-icon-maker ${icon_srcdir}/${name}.sav)

  # Shrink it to a fixed-size square image for the web page,
  # trimming boring border parts of the original image in the
  # process. Done by square.pl.
  add_custom_command(OUTPUT ${icon_bindir}/${name}-web.png
    COMMAND ${PERL_EXECUTABLE} ${icon_srcdir}/square.pl
      ${CONVERT} 150 5
      ${icon_bindir}/${name}-base.png
      ${icon_bindir}/${name}-web.png
    DEPENDS
      ${icon_srcdir}/square.pl
      ${icon_bindir}/${name}-base.png)
  list(APPEND output_icon_files ${icon_bindir}/${name}-web.png)

  # Shrink differently to an oblong for the KaiStore marketing
  # banner.  This is dimmed behind the name of the application, so put
  # it at a jaunty angle to avoid unfortunate interactions with the
  # text.
  add_custom_command(OUTPUT ${icon_bindir}/${name}-banner.jpg
    COMMAND ${CONVERT} ${icon_bindir}/${name}-base.png
      -crop 1:1+0+0 -rotate -10 +repage -shave 13% -resize 240 -crop x130+0+0
      ${icon_bindir}/${name}-banner.jpg
    DEPENDS ${icon_bindir}/${name}-base.png)
  list(APPEND output_icon_files ${icon_bindir}/${name}-banner.jpg)

  # Make the base image for all the icons, by cropping out the most
  # interesting part of the whole screenshot.
  add_custom_command(OUTPUT ${icon_bindir}/${name}-ibase.png
    COMMAND ${icon_srcdir}/crop.sh
      ${IDENTIFY} ${CONVERT}
      ${icon_bindir}/${name}-base.png
      ${icon_bindir}/${name}-ibase.png
      ${${name}_crop}
    DEPENDS
      ${icon_srcdir}/crop.sh
      ${icon_bindir}/${name}-base.png)

  # Coerce that base image down to colour depth of 4 bits, using the
  # fixed 16-colour Windows palette. We do this before shrinking the
  # image, because I've found that gives better results than just
  # doing it after.
  add_custom_command(OUTPUT ${icon_bindir}/${name}-ibase4.png
    COMMAND ${CONVERT}
      -colors 16
      +dither
      -set colorspace RGB
      -map ${icon_srcdir}/win16pal.xpm
      ${icon_bindir}/${name}-ibase.png
      ${icon_bindir}/${name}-ibase4.png
    DEPENDS
      ${icon_srcdir}/win16pal.xpm
      ${icon_bindir}/${name}-ibase.png)

  foreach(size ${all_icon_sizes})
    # Make a 24-bit icon image at each size, by shrinking the base
    # icon image.
    add_custom_command(OUTPUT ${icon_bindir}/${name}-${size}d24.png
      COMMAND ${PERL_EXECUTABLE} ${icon_srcdir}/square.pl
        ${CONVERT} ${size} ${border_${size}}
        ${icon_bindir}/${name}-ibase.png
        ${icon_bindir}/${name}-${size}d24.png
      DEPENDS
        ${icon_srcdir}/square.pl
        ${icon_bindir}/${name}-ibase.png)
    list(APPEND output_icon_files ${icon_bindir}/${name}-${size}d24.png)

    # And reduce the colour depth of that one to make an 8-bit
    # version.
    add_custom_command(OUTPUT ${icon_bindir}/${name}-${size}d8.png
      COMMAND ${CONVERT}
        -colors 256
        ${icon_bindir}/${name}-${size}d24.png
        ${icon_bindir}/${name}-${size}d8.png
      DEPENDS ${icon_bindir}/${name}-${size}d24.png)
    list(APPEND output_icon_files ${icon_bindir}/${name}-${size}d8.png)
  endforeach()

  foreach(size ${win_icon_sizes})
    # 4-bit icons are only needed for Windows. We make each one by
    # first shrinking the large 4-bit image we made above ...
    add_custom_command(OUTPUT ${icon_bindir}/${name}-${size}d4pre.png
      COMMAND ${PERL_EXECUTABLE} ${icon_srcdir}/square.pl
        ${CONVERT} ${size} ${border_${size}}
        ${icon_bindir}/${name}-ibase4.png
        ${icon_bindir}/${name}-${size}d4pre.png
      DEPENDS
        ${icon_srcdir}/square.pl
        ${icon_bindir}/${name}-ibase4.png)

    # ... and then re-coercing the output back to 16 colours, since
    # that shrink operation will have introduced intermediate colour
    # values again.
    add_custom_command(OUTPUT ${icon_bindir}/${name}-${size}d4.png
      COMMAND ${CONVERT}
        -colors 16
        +dither
        -set colorspace RGB
        -map ${icon_srcdir}/win16pal.xpm
        ${icon_bindir}/${name}-${size}d4pre.png
        ${icon_bindir}/${name}-${size}d4.png
      DEPENDS ${icon_bindir}/${name}-${size}d4pre.png)
    list(APPEND output_icon_files ${icon_bindir}/${name}-${size}d4.png)
  endforeach()

  # Make the Windows icon.
  set(icon_pl_args)
  set(icon_pl_deps)
  foreach(depth 24 8 4)
    list(APPEND icon_pl_args -${depth})
    foreach(size ${win_icon_sizes})
      list(APPEND icon_pl_args ${icon_bindir}/${name}-${size}d${depth}.png)
      list(APPEND icon_pl_deps ${icon_bindir}/${name}-${size}d${depth}.png)
    endforeach()
  endforeach()
  add_custom_command(OUTPUT ${icon_bindir}/${name}.ico
    COMMAND ${PERL_EXECUTABLE} ${icon_srcdir}/icon.pl
      --convert=${CONVERT}
      ${icon_pl_args} > ${icon_bindir}/${name}.ico
    DEPENDS
      ${icon_srcdir}/icon.pl
      ${icon_pl_deps})
  list(APPEND output_icon_files ${icon_bindir}/${name}.ico)

  # Make a C source file containing XPMs of all the 24-bit images.
  set(cicon_pl_infiles)
  foreach(size ${all_icon_sizes})
    list(APPEND cicon_pl_infiles ${icon_bindir}/${name}-${size}d24.png)
  endforeach()
  add_custom_command(OUTPUT ${icon_bindir}/${name}-icon.c
    COMMAND ${PERL_EXECUTABLE} ${icon_srcdir}/cicon.pl
      ${CONVERT} ${cicon_pl_infiles} > ${icon_bindir}/${name}-icon.c
    DEPENDS
      ${icon_srcdir}/cicon.pl
      ${cicon_pl_infiles})
  list(APPEND output_icon_files ${icon_bindir}/${name}-icon.c)

  # Make the KaiOS icons, which have rounded corners and shadows
  # https://developer.kaiostech.com/docs/design-guide/launcher-icon
  foreach(size 56 112)
    math(EXPR srciconsize "${size} * 44 / 56")
    math(EXPR borderwidth "(${size} - ${srciconsize}) / 2")
    math(EXPR cornerradius "${size} * 5 / 56")
    math(EXPR sizeminusone "${srciconsize} - 1")
    math(EXPR shadowspread "${size} * 4 / 56")
    math(EXPR shadowoffset "${size} * 2 / 56")
    add_custom_command(OUTPUT ${icon_bindir}/${name}-${size}kai.png
      COMMAND ${CONVERT}
        ${icon_bindir}/${name}-${srciconsize}d24.png
        -alpha Opaque
        "\\(" -size ${srciconsize}x${srciconsize} -depth 8 canvas:none
           -draw "roundRectangle 0,0,${sizeminusone},${sizeminusone},${cornerradius},${cornerradius}" "\\)"
        -compose dst-in -composite
        -compose over -bordercolor transparent -border ${borderwidth}
        "\\(" +clone -background black
           -shadow 30x${shadowspread}+0+${shadowoffset} "\\)"
        +swap -background none -flatten -crop '${size}x${size}+0+0!' -depth 8
        ${icon_bindir}/${name}-${size}kai.png
      DEPENDS ${icon_bindir}/${name}-${srciconsize}d24.png)
    list(APPEND output_icon_files ${icon_bindir}/${name}-${size}kai.png)
  endforeach()

  add_custom_target(${name}-icons DEPENDS ${output_icon_files})
  add_dependencies(icons ${name}-icons)
endfunction()
