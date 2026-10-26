/* M8: dispatcher from original code addresses to the ported screen handlers. */
#include <stdio.h>
#include <stdlib.h>
#include "satmenu.h"

typedef struct { u32 addr; MenuFn fn; } Reg;
static Reg regs[1024];
static int nregs;
static int inited;

void menu_mode_register(void);
void menu_start_register(void);
void menu_race_register(void);
void menu_arcade_register(void);
void menu_setting_register(void);
void menu_option_register(void);
void menu_timeattack_register(void);
void menu_records_register(void);
void menu_name_register(void);
void menu_post_register(void);
void menu_result_register(void);
void menu_photo_register(void);
void menu_ending_register(void);
void menu_adv_register(void);

void menu_init(void)
{
    if (inited) return;
    inited = 1;
    menu_mode_register();
    menu_start_register();
    menu_race_register();
    menu_arcade_register();
    menu_setting_register();
    menu_option_register();
    menu_timeattack_register();
    menu_records_register();
    menu_name_register();
    menu_post_register();
    menu_result_register();
    menu_photo_register();
    menu_ending_register();
    menu_adv_register();
}

void menu_reg(u32 addr, MenuFn fn)
{
    regs[nregs].addr = addr;
    regs[nregs].fn = fn;
    nregs++;
}

void (*menu_call_hook)(u32 addr);           /* the test sets it: called with every dispatch first */

void menu_call(u32 addr)
{
    int i;
    menu_init();
    if (menu_call_hook) menu_call_hook(addr);
    for (i = 0; i < nregs; i++)
        if (regs[i].addr == addr) { regs[i].fn(); return; }
    menu_unported(addr);
}

void (*menu_unported_hook)(u32 addr);       /* the test sets it: a scenario may end where the screen hands over to a screen that is not part of the package */

int menu_option_ntypes = 4;
int menu_no_battle;
int (*menu_option_diy_hook)(void);
int (*menu_stub_hook)(u32 addr);           /* the host sets it: returns 1 when it does what the routine would do (nothing the menus read), and the caller goes on */

void menu_unported(u32 addr)
{
    if (menu_stub_hook && menu_stub_hook(addr)) return;
    if (menu_unported_hook) menu_unported_hook(addr);
    fprintf(stderr, "menu: no port for %08x yet\n", (unsigned)addr);
    exit(2);
}

Menu3D menu3d_req[MENU3D_MAX];
int menu3d_n;
unsigned menu3d_tick;

void menu3d_car(u32 block, u32 offset, int car2)
{
    Menu3D *m;
    if (menu3d_tick != sat_ticks) { menu3d_tick = sat_ticks; menu3d_n = 0; }
    if (menu3d_n >= MENU3D_MAX) return;
    m = &menu3d_req[menu3d_n++];
    m->kind = 0; m->block = block; m->offset = offset; m->car2 = car2; m->noshadow = R32(block + 900) != 0;
    m->model = (int)R8(block + 92); m->still = R8(block + 94) == 0;
    m->ang[0] = RS32(block + 12); m->ang[1] = RS32(block + 16); m->ang[2] = RS32(block + 20);
    m->pos[0] = RS32(block + 24); m->pos[1] = RS32(block + 28); m->pos[2] = RS32(block + 32);
}

void menu3d_model(u32 model_ptr, u32 offset)
{
    Menu3D *m;
    if (menu3d_tick != sat_ticks) { menu3d_tick = sat_ticks; menu3d_n = 0; }
    if (menu3d_n >= MENU3D_MAX) return;
    m = &menu3d_req[menu3d_n++];
    m->kind = 1; m->block = model_ptr; m->offset = offset; m->model = 0; m->still = 0; m->noshadow = 0;
    m->pos[0] = RS32(0x060E5100 + 12); m->pos[1] = RS32(0x060E5100 + 16); m->pos[2] = RS32(0x060E5100 + 20);
    m->ang[0] = RS32(0x060E5100 + 24); m->ang[1] = RS32(0x060E5100 + 28); m->ang[2] = 0;
}

M3Op m3_ops[M3_MAX];
int m3_n;
unsigned m3_tick;

void m3(int op, s32 a, s32 b, s32 c)
{
    if (m3_tick != sat_ticks) { m3_tick = sat_ticks; m3_n = 0; }
    if (m3_n >= M3_MAX) return;
    m3_ops[m3_n].op = op; m3_ops[m3_n].a = a; m3_ops[m3_n].b = b; m3_ops[m3_n].c = c;
    m3_n++;
}

void m3_translate_v(u32 vec)
{
    m3(M3_TRANSLATE, RS32(vec), RS32(vec + 4), RS32(vec + 8));
}

