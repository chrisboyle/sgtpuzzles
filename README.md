Android port of Simon Tatham's Puzzles
======================================

This is [Simon Tatham's Portable Puzzle Collection](https://www.chiark.greenend.org.uk/~sgtatham/puzzles/),
ported to Android.

The easiest way to install it is [from Google Play](https://play.google.com/store/apps/details?id=name.boyle.chris.sgtpuzzles).

If you'd like to get involved, read on:

Project goals
-------------

* Run in Android environments where puzzles make sense: phones, tablets,
  Chromebooks, maybe TVs, not watches or cars.
* Provide the same puzzle collection that the upstream project provides for
  other platforms.
* Provide appropriate controls for Android environments, e.g. on-screen buttons
  and the use of long-press instead of right-click.
* Provide appropriate display adjustments for mobile use-cases, e.g. night mode,
  and minor changes to the recommended sizes of puzzles.
* Run completely offline, including the documentation, and with minimum required
  permissions.
* Follow Android conventions: implicit saving of games in progress, standard
  settings screen with immediate apply, etc.
* Make it easy to exchange saved puzzle states with other users and other
  platforms.

Potential/experimental goals, that could exist on Android before potentially
being included upstream later:

* Accessibility improvements relative to upstream.
* Translate the Puzzles into more languages: **first fix issue #1 properly** to
  allow translating strings that come from C, which is most of them. I don't yet
  know how to fix that, and I don't recommend starting work on any translations
  until it's fixed.
* Make it easier to have multiple states of the same game in progress, without
  having to manage files.

Non-goals for this project
--------------------------

I do not want to make certain kinds of changes that I think would make the Android
port too different from upstream:

* New games that don't exist upstream.
* Changes to game rules (implemented or documented) versus upstream.
* Timers, scoring, or high-score tables that differ from upstream.

Any suggestions in this category should be sent upstream to Simon, as per the
[Feedback section of his page](https://www.chiark.greenend.org.uk/~sgtatham/puzzles/#:~:text=Feedback),
and if any such improvements are accepted upstream then I'll merge them into the
Android port if at all possible.

You're welcome to discuss changes of this kind on this project's issue tracker,
applying the tag `upstream` to issues to reflect that they won't be implemented
here directly.

How to help without writing any code
------------------------------------

Good bug reports and well-thought-out feature suggestions (bearing in mind the above)
are always helpful; here's the
[issue tracker](https://github.com/chrisboyle/sgtpuzzles/issues).
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
Android-specific code is in Kotlin, and JNI (Java Native Interface) is the
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

You'll also find a branch called `upstream`, which is Simon's code whenever
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
Kotlin. The Kotlin classes providing the UI layer, game chooser, etc. are in
`app/src/main/kotlin`.  The main class `GamePlay` has the native methods that
`android.c` implements. The game area on screen is a `GameView`, which basically
just has a bitmap for the puzzle to draw on. Note that almost no native code is
run until the user has chosen a game (unless there's a previous game to resume).

Files for other platforms etc. that are not currently usable in this fork are
in the `not-in-use` directory.

If your change is relevant to other platforms, you should definitely ping Simon
about it as well as me, but test on at least one other platform first in a
separate checkout (see above).

Happy hacking! :-)

--
Chris Boyle
