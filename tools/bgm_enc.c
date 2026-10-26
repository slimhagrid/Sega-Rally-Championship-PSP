/* M9: turns a rendered song (float32 stereo, tools/bgm_render.c) into a .bgm file for the PSP player (src/bgm.c): 4 bits per sample, blocks of 28 samples with one of five predictors and
 * a scale chosen per block by trying all of them (the scheme of the PlayStation's sound chip: the quantiser follows the signal, ~10 dB better than IMA ADPCM at the same size).
 *   bgm_enc in.raw out.bgm start length [xfade] [mono]
 * start / length in frames (samples per channel) of the part of the render that is kept; with xfade > 0 the song loops: the last xfade frames are an equal-power cross-fade into the
 * xfade frames before `start`, so the end continues into the start without a click.  "mono": one channel (the render has L == R: the driver's mono setting).
 * File: "BGM2", u32 frames, u32 loop (1 / 0), u32 channels, u32 rate, u32 frames per superblock (1008); then the superblocks, per channel: int16 the two samples before it (newest first)
 * and 36 blocks of 15 bytes (byte 0 = shift | filter << 4, then 14 bytes = 28 nibbles, low nibble first).  Every superblock can be decoded on its own (loop / variant switch). */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SUPER 1008
#define NBLK 36
static const int c1[5] = { 0, 60, 115, 98, 122 };
static const int c2[5] = { 0, 0, -52, -55, -60 };

static int clamp16(int v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }

/* encodes 28 samples x[] with history h1 (newest), h2: the best filter / shift; returns the error, fills code[28], *h1 / *h2 are the decoded history afterwards */
static double enc_block(const int16_t *x, int *h1, int *h2, unsigned char *out)
{
    double best = 1e300;
    int f, sh, i;
    int bf = 0, bs = 0, bq[28], b1 = 0, b2 = 0;
    for (f = 0; f < 5; f++) {
        for (sh = 0; sh <= 12; sh++) {
            int step = 1 << (12 - sh), p1 = *h1, p2 = *h2, q[28];
            double e = 0;
            for (i = 0; i < 28; i++) {
                int pred = (p1 * c1[f] + p2 * c2[f]) >> 6, r = x[i] - pred, qq = r >= 0 ? (r + step / 2) / step : -((-r + step / 2) / step), s;
                if (qq > 7) qq = 7;
                if (qq < -8) qq = -8;
                s = clamp16(pred + qq * step);
                e += (double)(s - x[i]) * (s - x[i]);
                q[i] = qq; p2 = p1; p1 = s;
                if (e >= best) break;
            }
            if (i == 28 && e < best) { best = e; bf = f; bs = sh; memcpy(bq, q, sizeof q); b1 = p1; b2 = p2; }
        }
    }
    out[0] = (unsigned char)(bs | (bf << 4));
    for (i = 0; i < 14; i++) out[1 + i] = (unsigned char)((bq[2 * i] & 15) | ((bq[2 * i + 1] & 15) << 4));
    *h1 = b1; *h2 = b2;
    return best;
}

int main(int argc, char **argv)
{
    FILE *f;
    long start, length, xfade = 0, total, n, nsup, s;
    int mono = 0, ch, nch, k;
    float *raw;
    int16_t *pcm[2];
    unsigned char hdr[24];
    int h1[2] = { 0, 0 }, h2[2] = { 0, 0 };
    double sse = 0, sig = 0;
    if (argc < 5) { fprintf(stderr, "usage: bgm_enc in.raw out.bgm start length [xfade] [mono]\n"); return 2; }
    start = atol(argv[3]); length = atol(argv[4]);
    if (argc > 5) xfade = atol(argv[5]);
    if (argc > 6) mono = 1;
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); total = ftell(f) / (2 * (long)sizeof(float)); fseek(f, 0, SEEK_SET);
    raw = (float *)malloc((size_t)total * 2 * sizeof(float));
    if (fread(raw, sizeof(float), (size_t)total * 2, f) != (size_t)total * 2) return 1;
    fclose(f);
    if (start + length > total || (xfade && start < xfade)) { fprintf(stderr, "range: start %ld length %ld xfade %ld of %ld\n", start, length, xfade, total); return 1; }
    nch = mono ? 1 : 2;
    nsup = (length + SUPER - 1) / SUPER;
    for (ch = 0; ch < nch; ch++) {
        pcm[ch] = (int16_t *)calloc((size_t)(nsup * SUPER + 28), sizeof(int16_t));
        for (n = 0; n < length; n++) {
            double v = raw[2 * (start + n) + ch];
            if (xfade && n >= length - xfade) {
                double w = (double)(n - (length - xfade)) / (double)xfade, u = raw[2 * (start - xfade + (n - (length - xfade))) + ch];
                v = v * cos(w * 1.5707963267948966) + u * sin(w * 1.5707963267948966);
            }
            pcm[ch][n] = (int16_t)clamp16((int)lrint(v * 32767.0));
        }
        for (; n < nsup * SUPER + 28; n++) pcm[ch][n] = pcm[ch][length - 1];
    }
    f = fopen(argv[2], "wb");
    memcpy(hdr, "BGM2", 4);
    { uint32_t v[5] = { (uint32_t)length, xfade ? 1u : 0u, (uint32_t)nch, 44100u, SUPER }; memcpy(hdr + 4, v, 20); }
    fwrite(hdr, 1, 24, f);
    for (s = 0; s < nsup; s++) {
        for (ch = 0; ch < nch; ch++) {
            unsigned char blk[4 + NBLK * 15];
            int16_t hh[2];
            hh[0] = (int16_t)h1[ch]; hh[1] = (int16_t)h2[ch];
            memcpy(blk, hh, 4);
            for (k = 0; k < NBLK; k++) {
                long at = s * SUPER + 28 * k;
                double e = enc_block(pcm[ch] + at, &h1[ch], &h2[ch], blk + 4 + 15 * k);
                if (at + 28 <= length) { sse += e; for (n = 0; n < 28; n++) sig += (double)pcm[ch][at + n] * pcm[ch][at + n]; }
            }
            fwrite(blk, 1, sizeof blk, f);
        }
    }
    fclose(f);
    printf("%s: %ld frames, %d channel(s), %ld superblocks, %ld bytes, SNR %.1f dB%s\n", argv[2], length, nch, nsup, 24 + nsup * nch * (4 + NBLK * 15), 10.0 * log10(sig / (sse > 0 ? sse : 1)), xfade ? ", looped" : "");
    return 0;
}
