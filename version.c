/*
 * Puzzles version numbering.
 */

#define STR1(x) #x
#define STR(x) STR1(x)

/* cmb: No idea how to wrap these in _(), so have done it in android.c for Android. */

#if defined REVISION

char ver[] = "Revision: r" STR(REVISION);
char vernum[] = "r" STR(REVISION);

#else

char ver[] = "Unidentified build, " __DATE__ " " __TIME__;
char vernum[] = "?";

#endif
