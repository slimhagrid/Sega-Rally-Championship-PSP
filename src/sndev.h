/* M9: the race's sound events - gamesd::game_se_set and what it calls (0x0601633C .. 0x06016B74), ported from the SH-2 code, checked against the oracle's sound log (docs/sound.md,
 * tools/test_drive.c -snd).  The code only decides *what* to play: every call of the original's sound library goes out through SndEv.call with the library's own entry address, the
 * 68000's status bytes come in through SndEv.status.  The PSP host connects both to snd_lib.c, the host test logs them. */
#ifndef SNDEV_H
#define SNDEV_H

/* the entry points of the sound library the game code calls (the original's addresses) */
enum {
    SND_PLAY = 0x06016FBC,              /* r4 = sound id */
    SND_WORD = 0x0601704C,              /* r4 = the word for the 68000 (volume / pan update) */
    SND_CMD = 0x06016F10,               /* r4 = command row */
    SND_F24 = 0x06016F24,               /* r4 */
    SND_F30 = 0x06016F30,               /* r4 */
    SND_F3C = 0x06016F3C,               /* r4, r5, r6 */
    SND_F50 = 0x06016F50,               /* r4, r5 */
    SND_F60 = 0x06016F60,               /* r4, r5 */
    SND_FA4 = 0x06016FA4,               /* r4 */
    SND_FLUSH_WAIT = 0x060175A4,
    SND_CDPAUSE = 0x0600F7BE,           /* the pause menu holds the CD music */
    SND_CDRESUME = 0x0600F7E4,
    SND_CDPLAY = 0x0600F756             /* r4 = CD track, r5 = 15: starts the music (the host plays the track) */
};

typedef struct SndEv {
    void (*call)(void *ud, unsigned fn, int r4, int r5, int r6);      /* the library; NULL = nothing happens */
    int  (*status)(void *ud, int id);                                 /* FUN_06016F04: the status byte of the 68000 for the class of sound id `id` (0 = not playing) */
    void *ud;
    /* the module's work area [0x060AA012 ..] and the globals it reads (all set by the host, see sndev_init) */
    int w0, w2, w4;                     /* tyre noise: surface class, state (byte offset into the state table), last slip value */
    int b6, b7;                         /* skid noise delay, road noise start flag ([0x060AA018], [0x060AA019]) */
    int timer;                          /* [0x060AA030] */
    int b2d;                            /* [0x060AA02D] */
    int rp_state, rp_timer;             /* [0x060AFE00] / [0x060AFE02]: the replay's sound timeline (replay_sdctrl) */
    unsigned char wall[4];              /* [0x060AFE68 + course]: wall contact counter */
    int msg44;                          /* [0x060AAA44]: a co-driver message is being played */
    int msg_req;                        /* [0x060AAA45] as the sound system sees it: a phrase asked for and not taken yet (0 = none) */
    unsigned frame;                     /* [0x060AA168]: the pass counter */
    int course, game_type, mode810;     /* [0x060AA87E], [0x060AA860], [0x060AA810] */
    int stereo;                         /* bit 5 of [0x06040062] */
    int bgm_sel;                        /* [0x060AA828]: the music chosen in the practice / time attack menus */
    int opt_vol;                        /* [0x0604000C]: the music volume option */
    unsigned eng[4];                    /* the engine longs 0x25A007C0, +4, 0x25A007E0, +4: pitch index and volume of player 1 / 2 */
} SndEv;

/* what the host connects to the race: the sound library (snd_lib.c) on the PSP */
typedef struct SndHost {
    void (*call)(void *ud, unsigned fn, int r4, int r5, int r6);
    int  (*status)(void *ud, int id);
    void (*engine)(void *ud, const unsigned eng[4]);                  /* the engine longs into the sound RAM (0x25A007C0 ..) */
    void (*flush)(void *ud);                                          /* FUN_06017580: the commands of the pass to the 68000 */
    void (*vint)(void *ud);                                           /* the V-blank handler's sound work (the voice stream): twice per 30 Hz pass */
    int  (*msg_busy)(void *ud);                                       /* [0x060AAA44]: a phrase is playing */
    void (*msg_put)(void *ud, int msg);                               /* [0x060AAA45] = msg: the next flush starts the phrase */
    void *ud;
} SndHost;

static inline void se_call(SndEv *s, unsigned fn, int r4, int r5, int r6) { if (s->call) s->call(s->ud, fn, r4, r5, r6); }
static inline int  se_status(SndEv *s, int id) { return s->status ? s->status(s->ud, id) : 0; }

void sndev_init(SndEv *s);                                            /* gamesd::init_game_se (0x06016A20): what the race start-up does for the sound */
void sndev_game_se(SndEv *s, unsigned char *car, int *msg45);
void sndev_torque(SndEv *s, unsigned char *car, int torque, unsigned *rng);   /* FUN_06008BCC: the throttle-lift noises of the engine, called with the torque of the pass (rng = the game's random number state 0x060D0050, NULL = the oracle's stub) */
void sndev_kansei(SndEv *s);                                          /* gamesd::kansei_se_set (0x06016A10): the crowd / start sound of the course */
void sndev_eng_idle(SndEv *s);                                        /* 0x06017548: both engine longs to the idle note */
void sndev_go(SndEv *s);                                              /* the end of the GO display (0x06013226 ..): the stage music starts */
void sndev_replay_start(SndEv *s);                                    /* count_down_start_replay's first pass (0x06013F50): the replay's music of the first stage */
void sndev_replay_go(SndEv *s);                                       /* its last pass (0x0601406C): the music of the other stages, the sound timeline of replay_sdctrl */

#endif
