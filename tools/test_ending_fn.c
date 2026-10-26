/* M8: replays tools/fn_ending.py's vectors on get_camera_angle (src/menu_ending.c): tools/test_ending_fn.sh */
#include "../src/menu_ending.c"
#include <stdio.h>
int main(void)
{
    FILE *f = fopen("extracted/oracle/fn_ending.txt", "r");
    unsigned v[8];
    int n = 0, bad = 0;
    if (sat_init("extracted/psp")) return 1;
    sat_boot();
    while (fscanf(f, "%x %x %x %x %x %x %x %x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]) == 8) {
        s32 a[3] = { (s32)v[0], (s32)v[1], (s32)v[2] }, b[3] = { (s32)v[3], (s32)v[4], (s32)v[5] }, r0, r1;
        get_camera_angle(a, b, &r0, &r1);
        if ((u32)r0 != v[6] || (u32)r1 != v[7]) { if (bad++ < 5) printf("case %d: expected %08x %08x got %08x %08x\n", n, v[6], v[7], (u32)r0, (u32)r1); }
        n++;
    }
    printf("%d cases, %d differ\n", n, bad);
    return 0;
}
