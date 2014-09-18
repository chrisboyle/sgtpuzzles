#ifdef EXECUTABLE
#include <stdio.h>
#include "puzzles.h"

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
	if (argc < 2 || argc > 3) {
		fprintf(stderr, "Usage: puzzles-gen gamename [params]\n");
		exit(1);
	}

	const game *thegame = NULL;
	int i;
	for (i = 0; i<gamecount; i++) {
		if (!strcmp(argv[1], gamenames[i])) {
			thegame = gamelist[i];
			break;
		}
	}

	if (!thegame) {
		fprintf(stderr, "Game name not recognised\n");
		exit(1);
	}

	frontend *fe = snew(frontend);
	fe->me = midend_new(fe, thegame, &null_drawing, fe);

	game_params *params = thegame->default_params();
	if (argc >= 3 && strlen(argv[2]) > 0) {
		if (!strcmp(argv[2], "--portrait") || !strcmp(argv[2], "--landscape")) {
			unsigned int w, h;
			int pos;
			char * encoded = thegame->encode_params(params, TRUE);
			if (sscanf(encoded, "%ux%u%n", &w, &h, &pos) >= 2) {
				if ((w > h) != (argv[2][2] == 'l')) {
					sprintf(encoded, "%ux%u%s", h, w, encoded + pos);
					thegame->decode_params(params, encoded);
				}
			}
			sfree(encoded);
		} else {
			thegame->decode_params(params, argv[2]);
		}
		char *error = thegame->validate_params(params, TRUE);
		if (error) {
			thegame->free_params(params);
			fprintf(stderr, "%s\n", error);
			exit(1);
		}
	}
	midend_set_params(fe->me, params);
	midend_new_game(fe->me);

	// We need a save not just a desc: aux info contains solution
	midend_serialise(fe->me, serialise_write, NULL);
	exit(0);
}
#endif
