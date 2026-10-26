/* Sega Rally PSP - course renderer (M5).
 *
 * Draws a course the way the original polygon pipeline does (docs/render_pipeline.md): a list of parts (nearest first), every polygon gets a depth key
 * (mean / min / max / max+4 of its vertices' view-space z), polygons are bucket-sorted far to near (bucket = z * 32) and painted back to front, no depth buffer.
 * Data: `.rcp` packs of tools/conv_parts.py.  The view matrix is the game's own 3x4 camera matrix (row-major, p' = R*p + t, +z forward, 1.0625 x-scale included).
 */
#ifndef RENDER_H
#define RENDER_H

typedef struct { unsigned int first_vert, first_poly; unsigned short nverts, npolys; unsigned int pad; } RcpPart;
typedef struct { unsigned short w, h, tw, th; unsigned int po, co; } RcpTex;
typedef struct { unsigned short flags, tex, colr, kv[4], cv[4], pad; } RcpPoly;

typedef struct {
    unsigned char *file;
    unsigned int nparts, ntex, npoly, nverts;
    const RcpPart *part;
    const RcpTex *tex;
    const RcpPoly *poly;
    const float *vert;                  /* x, y, z per vertex, metres */
    const unsigned char *vis;           /* the 16 get_poly_map lists (see conv_parts.py) */
    unsigned int vis_size;
    unsigned int kind;                  /* 0 = course (near 1.25, polygons clipped), 1 = object pack (near 0.5, polygons dropped when any vertex is behind), 2 = the menus' shaded models (select.rcp: like 1, per-vertex normals in `vis`) */
} Course;

typedef struct {
    int x0, y0, w, h;                   /* where the 352x224 game picture goes on the 480x272 screen */
} RenderRect;

typedef struct {                        /* statistics of the last rcp_draw */
    int polys_in, polys_drawn, tris, draws;
    int skipped, vused;                 /* parts left out because the vertex pool was full / too many vertices, vertices used */
} RenderStats;

/* VDP2 race background: one 1024x512 256-colour panorama scrolled by the camera yaw, rotated by half the camera roll (tools/conv_bg.py, docs/vdp2_background.md) */
typedef struct {
    unsigned char *file;
    float base_x, base_y, yaw_ppu, roll_scale;
    unsigned int back;                  /* RGBA5551 back colour */
    unsigned int clut_off, tex_off[2];
} Sky;

int  sky_load(Sky *s, const char *path);
void sky_free(Sky *s);
/* yaw16: the camera yaw word (65536 = one turn); roll16: signed camera roll word */
void sky_draw(const Sky *s, unsigned int yaw16, int roll16, const RenderRect *rect);
void sky_draw_table(const Sky *s, int mx, int a, int b, const RenderRect *rect);       /* from the console's rotation parameter table (smoothed, camera.c) */

/* The game picture is always drawn at 352 x 224 pixels into an off-screen buffer (known good, sharp), then copied to the screen without stretching one axis more than the other:
 * view_begin() redirects drawing there (rect = VIEW_RECT for sky_draw / scene_draw / hud_draw), view_end() copies it to the draw buffer at `draw_off` (VRAM byte offset).
 * mode 0: 1:1 centred, mode 1: scaled to the screen height (428 x 272, nearest), mode 2: the same, bilinear. */
extern const RenderRect VIEW_RECT;
void view_begin(void);
void view_fade(int offset);                             /* the VDP2 colour offset of the fades: add `offset` (-255 .. 255) to every colour channel of the picture (call after the last layer, before view_end) */
void view_end(int draw_off, int mode);

int  rcp_load(Course *c, const char *path);
void rcp_free(Course *c);

/* One drawn model part: part `part` of pack `c`, model space (metres) -> view space by the 3x4 row-major matrix m (p' = R*p + t, +z forward).
 * scene_draw sorts the polygons of all items together (depth key per polygon, 8192 buckets) and paints far to near.  Items are given in the order the game
 * enters them (cars / objects first, then the course parts): polygons of one bucket are painted in the reverse of that order. */
typedef struct { const Course *c; int part; int clip; float m[12]; } SceneItem;      /* clip: object drawn with draw_clip_polygon (near plane clipping, gates / poles) */
void scene_set_pers(int p);                        /* the pipeline's perspective distance (224 in a race; a replay's camera: [0x060D008A], 0x200 at its start) */
void scene_draw(const SceneItem *items, int nitems, const RenderRect *rect, RenderStats *st);
void scene_set_light(float x, float y, float z);   /* the light of the kind 2 models (menus): a unit vector in view space (rotate_light) */
void scene_set_colors(const short *cols);        /* kind 2: per item of the next scene_draw a colour (RGB555) that replaces the colours of its polygons (-1: keep), NULL = none (the letters of the name entry) */
void scene_set_coloff(int r, int g, int b);       /* a colour offset added to every colour of the next scene_draw calls (the menus' fades: VDP2 colour offset), 0, 0, 0 = none */
void scene_set_offset(float dx, float dy);        /* shifts the projection centre of the next scene_draw by (dx, dy) dots of the 352 x 224 picture (the menus' set_poly_offset); the clip rectangle stays; 0, 0 = normal */

/* view: 12 floats (3 rows x 4 columns, row-major); parts: part indices in draw-list order */
void rcp_draw(const Course *c, const float *view, const unsigned char *parts, int nparts, const RenderRect *rect, RenderStats *st);

#endif
