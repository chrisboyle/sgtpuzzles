/*
 * misc.c: Miscellaneous helpful functions.
 */

#include <assert.h>
#include <stdlib.h>

#include "puzzles.h"

void free_cfg(config_item *cfg)
{
    config_item *i;

    for (i = cfg; i->type != C_END; i++)
	if (i->type == C_STRING)
	    sfree(i->sval);
    sfree(cfg);
}
