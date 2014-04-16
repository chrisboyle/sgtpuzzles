/*
 * Puzzles version numbering.
 */

#define STR1(x) #x
#define STR(x) STR1(x)

#if defined REVISION

char ver[] = "Revision: r" STR(REVISION);

#else

char ver[] = "Unidentified build, " __DATE__ " " __TIME__;

#endif
