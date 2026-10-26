/* M8: software renderer of the Saturn video chips on the memory model (src/sat.h): VDP2 scroll layers + VDP1 sprite commands -> a 352 x 224 (or 320 x 224) picture.
 * Used by the PSP host to show the menu screens (the result is uploaded as a texture) and by tools/render_mtr.c to look at oracle traces. */
#ifndef VDPR_H
#define VDPR_H
#include "sat.h"

#define VDPR_MAXW 352
#define VDPR_MAXH 224

typedef struct {
    int w, h;                       /* 320 / 352 x 224 / 240 */
    int same;                       /* vdp_render_frame: 1 = nothing that shapes the picture changed since the last call, pix is still the picture (the host need not upload it again) */
    u32 pix[VDPR_MAXW * VDPR_MAXH] __attribute__((aligned(64)));      /* ABGR8888 (red in the low byte, as the PSP GU wants it); aligned: the GE's copy to the texture (sceGuCopyImage) rounds the source address down to 16 bytes (at offset 12 the picture came 3 dots to the right with garbage at its left edge) */
} VdpFrame;

/* the VDP1 draw list: command indexes (addresses 0x060C0000 + 8 * index) in the order they are drawn */
int vdp1_sorted_list(u16 *list, int max);               /* the sprite commands of the z-buckets of the master pipeline, far to near, insertion order inside a bucket */
void vdp1_draw(u16 *fb, int w, int h, const u16 *list, int n);   /* fb: w * h RGB555 words with bit 15 set where something was drawn */
void vdp2_render(VdpFrame *f, const u16 *vdp1fb);      /* VDP2 layers + the VDP1 frame buffer (may be NULL) */
void vdp_render_frame(VdpFrame *f);                    /* the whole picture of the current memory state */
/* the host's 3D split (the photo scene): see vdpr.c.  A caller sets these, renders (vdp_render_frame), and sets them back to NULL / 0. */
extern VdpFrame *vdpr_hi;
extern int vdpr_hi_prio, vdpr_no_offset;
extern int vdpr_noskip;
extern int vdpr_rbg_half;                               /* the host: RBG0 in 2 x 2 dot blocks (the 3D scenes' sky) */
#define VDPR_NPARTS 7
extern unsigned long long (*vdpr_clock)(void);          /* optional timing: vdpr_t[] sums the clock's ticks of (0) the VRAM change scan, (1) colour RAM, (2) the layers, (3) the composite, (4) the picture's dots, (5) the VDP1 sprites, (6) the VDP1 list */
extern unsigned long long vdpr_t[VDPR_NPARTS];
void vdpr_forget(void);                                 /* the next vdp_render_frame composes the picture again even if nothing changed (the host wrote into a VdpFrame) */

#endif
