Android port of Simon Tatham's Puzzles
======================================

This is [Simon Tatham's Portable Puzzle Collection](https://www.chiark.greenend.org.uk/~sgtatham/puzzles/),
ported to Android.

The easiest way to install it is [from Google Play](https://play.google.com/store/apps/details?id=name.boyle.chris.sgtpuzzles).

If you'd like to get involved, read on:

How to help without writing any code
------------------------------------

Good bug reports and well-thought-out feature suggestions are always helpful,
here's the [issue tracker](https://github.com/chrisboyle/sgtpuzzles/issues).
It's always worth a quick search to see if your bug/idea has already been reported.

Simon has an excellent page on [how to write a good bug report](https://www.chiark.greenend.org.uk/~sgtatham/bugs.html).

Need to add/change some graphics? The sources live in
`app/src/main/graphics-sources` and were converted to icons with
[Android Asset Studio](https://romannurik.github.io/AndroidAssetStudio/)

Coders should read on...

Prerequisites
-------------

Some knowledge of Android Development and/or C, and ideally JNI, depending
on what you want to work on: the puzzles themselves are in C, most
Android-specific code is in Java, and JNI (Java Native Interface) is the
API bridge between the two.

If you have the option, a Linux-based development machine, as I haven't
tried to build on Windows or OSX, and it will need some tweaks.

[Android Studio](https://developer.android.com/studio)

[Android NDK (Native Development Kit)](https://developer.android.com/ndk)

If using gradle directly, create `local.properties` with `sdk.dir=/your_path_here`
(I think Android Studio writes this automatically)

I probably missed a few things here. File a bug when you find them. :-)

Getting/configuring the source
------------------------------

The source lives at https://github.com/chrisboyle/sgtpuzzles - you can
either clone/download it from there, or make your own fork on github
(the Fork button near the top right). A fork means you can easily send
me a "pull request" of your change, and I can review and integrate it,
all within github.

You'll also find a branch called "upstream", which is Simon's code whenever
I last synced. Handy for diffs, to see what I broke on Android. :-)

This repository includes old `git-svn` commits that predate upstream's move
to git. History will look slightly nicer if, after cloning, you do:

    git fetch origin 'refs/replace/*:refs/replace/*'

Sadly github itself does not appear to support replacement so you will still
see duplicates there.

You should now be able to edit, build and launch the app like any other Android
project (except a lot of it is in C). Don't forget that you'll be signing with
a dev key, so to test on a device that already has the Google Play version,
uninstall that first.

Architecture / where to find stuff
----------------------------------

Simon has some excellent [developer documentation](https://www.chiark.greenend.org.uk/~sgtatham/puzzles/devel/)
which is definitely worth reading first, at least the Introduction.

The Android front-end (`android.c`) is basically just glue, passing everything to
Java. The Java classes providing the UI layer, game chooser, etc. are in
`app/src/main/java`.  The main class `GamePlay` has the native methods that
`android.c` implements. The game area on screen is a `GameView`, which basically
just has a bitmap for the puzzle to draw on. Note that almost no native code is
run until the user has chosen a game (unless there's a previous game to resume).

Files for other platforms etc. that are not currently usable in this fork are
in the not-in-use directory.

If your change is relevant to other platforms, you should definitely ping Simon
about it as well as me, but test on at least one other platform first in a
separate checkout (see above).

Major changes e.g. adding a game
--------------------------------

At this level you especially need to think carefully about other platforms.
One of the best features of this collection is its relative consistency across
different environments, and the Android port should continue that.

If adding a game, definitely read the relevant chapter (6) of Simon's
development docs, and add the new game to the places I've gone and duplicated
the list just for Android...

 * `app/src/main/res/values/strings.xml`
 * `app/src/main/res/values/game_props.xml`
 * ...and possibly others.

Happy hacking! :-)

Chris Boyle
