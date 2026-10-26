/* M9: the song player (src/bgm.c) on the PC against the render the stream was made from:
 *   gcc -O2 -I src tools/test_bgm.c src/bgm.c -lm -o build/host/test_bgm
 *   build/host/test_bgm dir set value name variant ref.raw offset_frames [frames]
 * ref.raw = float32 stereo of tools/bgm_render.c; the stream starts `offset_frames` into it (the loop's start); prints the SNR of the decoded stream, the step at the loop point, the fade and the
 * switch of the stereo option. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bgm.h"

static unsigned char flag;

static void mixn(short *out, long frames)
{
    long done = 0;
    while (done < frames) {
        int n = frames - done > 1024 ? 1024 : (int)(frames - done);
        bgm_mix(out + 2 * done, n);
        done += n;
    }
}

int main(int argc, char **argv)
{
    unsigned set, value;
    int variant;
    long off, frames, i;
    float *ref;
    short *out;
    FILE *f;
    long nref;
    double sse = 0, sig = 0;
    if (argc < 8) { fprintf(stderr, "usage: test_bgm dir set value name variant ref.raw offset [frames]\n"); return 2; }
    set = (unsigned)atoi(argv[2]); value = (unsigned)strtoul(argv[3], NULL, 16); variant = atoi(argv[5]);
    off = atol(argv[7]); frames = argc > 8 ? atol(argv[8]) : 44100 * 30;
    flag = variant ? 0x80 : 0;
    bgm_init(argv[1], &flag);
    f = fopen(argv[6], "rb");
    if (!f) { perror(argv[6]); return 1; }
    fseek(f, 0, SEEK_END); nref = ftell(f) / 8; fseek(f, 0, SEEK_SET);
    ref = (float *)malloc((size_t)nref * 8);
    if (fread(ref, 8, (size_t)nref, f) != (size_t)nref) return 1;
    fclose(f);
    out = (short *)calloc((size_t)frames * 2, sizeof(short));
    if (!bgm_start(set, value)) { fprintf(stderr, "no stream for set %u value %x\n", set, value); return 1; }
    mixn(out, frames);
    for (i = 0; i < frames && off + i < nref; i++) {
        double a = out[2 * i] / 32768.0, r = ref[2 * (off + i)];
        sse += (a - r) * (a - r); sig += r * r;
    }
    printf("decoded %ld frames: SNR against the render %.1f dB  (playing %d)\n", frames, 10.0 * log10(sig / sse), bgm_playing());
    if (getenv("LOOPLEN")) {                            /* the loop: play past the end, look at the step between the last and the first frame of the stream */
        long len = atol(getenv("LOOPLEN")), total = len + 44100 * 3, at;
        short *o2 = (short *)calloc((size_t)total * 2, sizeof(short));
        double d1 = 0, d2 = 0;
        bgm_stop();
        mixn(o2, 1);
        bgm_start(set, value);
        mixn(o2, total);
        for (i = 1; i < 20000; i++) { d1 += fabs((double)o2[2 * (len + i)] - o2[2 * (len + i - 1)]); }
        for (i = 1; i < 20000; i++) { d2 += fabs((double)o2[2 * (len / 2 + i)] - o2[2 * (len / 2 + i - 1)]); }
        at = len;
        printf("loop: step across the loop point %d -> %d (mean step elsewhere %.0f, after the loop %.0f); playing %d\n", o2[2 * (at - 1)], o2[2 * at], d2 / 20000, d1 / 20000, bgm_playing());
    }
    if (getenv("FADE")) {                               /* a fade to 0 over time units: the level over the next seconds, then the stream stops */
        int t = atoi(getenv("FADE"));
        short *o3 = (short *)calloc((size_t)44100 * 30 * 2, sizeof(short));
        long k;
        bgm_start(set, value);
        mixn(o3, 44100);
        bgm_fade(0, t);
        memset(o3, 0, 44100 * 30 * 2 * sizeof(short));
        mixn(o3, 44100 * 30);
        for (k = 0; k < 30; k++) {
            double e = 0;
            for (i = 0; i < 44100; i++) e += (double)o3[2 * (k * 44100 + i)] * o3[2 * (k * 44100 + i)];
            printf("fade second %ld: rms %.0f\n", k, sqrt(e / 44100));
            if (e == 0) break;
        }
        printf("playing %d after the fade\n", bgm_playing());
    }
    if (getenv("SWITCH")) {                             /* the stereo option changes while the song plays: it goes on at the same place */
        short *o4 = (short *)calloc((size_t)44100 * 6 * 2, sizeof(short));
        bgm_start(set, value);
        mixn(o4, 44100 * 2);
        flag ^= 0x80;
        mixn(o4 + 2 * 44100 * 2, 44100 * 4);
        printf("switch: playing %d; sample around the switch %d %d %d | %d %d\n", bgm_playing(), o4[2 * 88198], o4[2 * 88199], o4[2 * 88200], o4[2 * 88201], o4[2 * 88202]);
    }
    return 0;
}
