/*
 * Header for spectre-help.c
 */

/* Dummy value indicating no specific hexagon, used in some diagrams
 * for the accompanying article. */
#define NO_HEX (Hex)9

/*
 * String constants for the hex names, including an extra entry
 * mapping NO_HEX to the empty string.
 */
extern const char *hex_names[10];

typedef struct Graphics {
    FILE *fp;
    bool close_file; /* if it's not stdout */
    bool started;    /* have we written the header yet? */
    double xoff, xscale, yoff, yscale, absscale, linewidth;
    bool jigsaw_mode; /* draw protrusions on hex edges */
    bool vertex_blobs; /* draw blobs marking hex vertices */
    bool hex_arrows; /* draw arrows orienting each hex */
    bool number_edges; /* number the edges of everything */
    bool number_cells; /* number the things themselves */
    bool four_colour;  /* four-colour Spectres instead of semantically */
    bool arcs; /* draw Spectre edges as arcs */
} Graphics;

typedef struct GrCoords {
    double x, y;
} GrCoords;

Graphics *gr_new(const char *filename, double xmin, double xmax,
                 double ymin, double ymax, double scale);
void gr_free(Graphics *gr);
GrCoords gr_logcoords(Point p);
GrCoords gr_log2phys(Graphics *gr, GrCoords c);
GrCoords gr_physcoords(Graphics *gr, Point p);
void gr_draw_text(Graphics *gr, GrCoords logpos, double logheight,
                  const char *text);
void gr_draw_path(Graphics *gr, const char *classes, const GrCoords *phys,
                  size_t n, bool closed);
void gr_draw_blob(Graphics *gr, const char *classes, GrCoords log,
                  double logradius);
void gr_draw_hex(Graphics *gr, unsigned index, Hex htype,
                 const Point *vertices);
void gr_draw_spectre(Graphics *gr, Hex container, unsigned index,
                     const Point *vertices);
void gr_draw_spectre_from_coords(Graphics *gr, SpectreCoords *sc,
                                 const Point *vertices);
void gr_draw_extra_edge(Graphics *gr, Point a, Point b);
