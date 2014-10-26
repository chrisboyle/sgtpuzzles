#ifdef EXECUTABLE
#include <stdio.h>
#include "puzzles.h"

#define USAGE "Usage: puzzles-gen gamename [params | --seed seed | --desc desc]\n"

struct frontend {
	midend *me;
};

void serialise_write(void *ctx, void *buf, int len) {
	write(1, buf, len);
}


const struct drawing_api null_drawing = {
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL,
};

int main(int argc, const char *argv[]) {
	if (argc < 2 || argc > 4) {
		fprintf(stderr, USAGE);
		exit(1);
	}
	int defmode = DEF_PARAMS;
	if (argc >= 4) {
		if (!strcmp(argv[2], "--seed")) {
			defmode = DEF_SEED;
		} else if (!strcmp(argv[2], "--desc")) {
			defmode = DEF_DESC;
		} else {
			fprintf(stderr, USAGE);
			exit(1);
		}
	}

	const game *thegame = game_by_name(argv[1]);

	if (!thegame) {
		fprintf(stderr, "Game name not recognised\n");
		exit(1);
	}

	frontend *fe = snew(frontend);
	fe->me = midend_new(fe, thegame, &null_drawing, fe);

	char* error = NULL;
	game_params *params = NULL;
	if (defmode == DEF_PARAMS) {
		params = oriented_params_from_str(thegame, (argc >= 3 && strlen(argv[2]) > 0) ? argv[2] : NULL, &error);
	} else {
		char *tmp = dupstr(argv[3]);
		error = midend_game_id_int(fe->me, tmp, defmode, FALSE);
		sfree(tmp);
	}
	if (error) {
		fprintf(stderr, "%s\n", error);
		exit(1);
	}
	if (defmode == DEF_PARAMS) midend_set_params(fe->me, params);
	midend_new_game(fe->me);

	// We need a save not just a desc: aux info contains solution
	midend_serialise(fe->me, serialise_write, NULL);
	exit(0);
}
#endif
