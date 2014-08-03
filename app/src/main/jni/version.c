/*
 * Puzzles version numbering.
 */

#define STR1(x) #x
#define STR(x) STR1(x)

#ifdef INCLUDE_EMPTY_H
/*
 * Horrible hack to force version.o to be rebuilt unconditionally in
 * the automake world: empty.h is an empty header file, created by the
 * makefile and forcibly updated every time make is run. Including it
 * here causes automake to track it as a dependency, which will cause
 * version.o to be rebuilt too.
 *
 * The space between # and include causes mkfiles.pl's dependency
 * scanner (for all other makefile types) to ignore this include,
 * which is correct because only the automake makefile passes
 * -DINCLUDE_EMPTY_H to enable it.
 */
# include "empty.h"
#endif

#if defined REVISION

char ver[] = "Revision: r" STR(REVISION);

#else

char ver[] = "Unidentified build, " __DATE__ " " __TIME__;

#endif
