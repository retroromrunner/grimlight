// GRIMLIGHT — The Hollow Bell. A turn-based gothic puzzle-strategy game for GBA.
// Bare-metal C: Mode 0 tiled BG + sprites, PSG music/SFX. No libc.
#include "assets.h"

typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
typedef signed char s8; typedef short s16; typedef int s32;
#define REG16(a) (*(volatile u16*)(a))
#define REG32(a) (*(volatile u32*)(a))

#define REG_DISPCNT REG16(0x04000000)
#define REG_VCOUNT  REG16(0x04000006)
#define REG_BG0CNT  REG16(0x04000008)
#define REG_BG1CNT  REG16(0x0400000A)
#define REG_BG0HOFS REG16(0x04000010)
#define REG_BG0VOFS REG16(0x04000012)
#define REG_KEY     REG16(0x04000130)
#define BG_PAL   ((volatile u16*)0x05000000)
#define OBJ_PAL  ((volatile u16*)0x05000200)
#define BG0_TILES ((volatile u32*)0x06000000)
#define BG1_TILES ((volatile u32*)0x06004000)
#define BG0_MAP   ((volatile u16*)0x06008000)
#define BG1_MAP   ((volatile u16*)0x06008800)
#define OBJ_TILES ((volatile u32*)0x06010000)
#define OAM       ((volatile u16*)0x07000000)

#define KEY_A 0x0001
#define KEY_B 0x0002
#define KEY_SELECT 0x0004
#define KEY_START 0x0008
#define KEY_RIGHT 0x0010
#define KEY_LEFT 0x0020
#define KEY_UP 0x0040
#define KEY_DOWN 0x0080

#define MW 15
#define MH 10
#define RGB15(r,g,b) ((r) | ((g)<<5) | ((b)<<10))

static u32 rng_state = 0x1A2B3C4D;
static u32 rnd(void){ rng_state = rng_state * 1664525u + 1013904223u; return rng_state >> 16; }
static int iabs(int v){ return v < 0 ? -v : v; }
static int clampi(int v, int lo, int hi){ return v < lo ? lo : (v > hi ? hi : v); }

static void vsync(void){
    while (REG_VCOUNT >= 160) { }
    while (REG_VCOUNT < 160) { }
}
static void copy32(volatile u32* dst, const u32* src, int words){
    for (int i = 0; i < words; i++) dst[i] = src[i];
}

// ---------------- palettes: banks 0..3 = bright, dim, dark, pitch ----------------
static u16 bgpal[4][16];
static u16 objpal[4][16];
static int pal_gen = 0;
static u16 shade_col(u16 c, int num, int den){
    int r = (c & 31) * num / den, g = ((c >> 5) & 31) * num / den, b = ((c >> 10) & 31) * num / den;
    return (u16)RGB15(r, g, b);
}
static void build_palettes(int chapter){
    static const int fac[4] = {16, 10, 5, 2};       // brightness denominators /16
    static const int glowfac[4] = {16, 14, 11, 8};  // palette idx 15 (amber glow) resists darkness
    static const int goldfac[4] = {16, 12, 8, 5};   // palette idx 12 (gold)
    for (int bank = 0; bank < 4; bank++){
        for (int i = 0; i < 16; i++){
            u16 base = TERRAIN_PALS[chapter][i];
            int f = (i == 15) ? glowfac[bank] : (i == 12) ? goldfac[bank] : fac[bank];
            bgpal[bank][i] = shade_col(base, f, 16);
            u16 ab = ACTOR_PAL[i];
            int af = fac[bank];
            if (bank > 0 && (i == 10 || i == 11)) af = 12; // eyes/lantern stay luminous
            objpal[bank][i] = (bank == 3) ? (u16)((i == 10) ? RGB15(31,6,6) : (i == 11) ? RGB15(31,20,5) : (i == 15) ? RGB15(28,27,24) : RGB15(0,0,0))
                                          : shade_col(ab, af, 16);
        }
    }
    pal_gen++;
}
static int fade_scale = 16;   // 16 = full brightness
static int fade_red = 0;      // death flash tint
static int flash_white = 0;   // exit flash (declared here; used by flush_palettes below)
static u16 flash_blend(u16 c){
    int r = (c & 31) + flash_white, g = ((c >> 5) & 31) + flash_white, b = ((c >> 10) & 31) + flash_white;
    if (r > 31) r = 31; if (g > 31) g = 31; if (b > 31) b = 31;
    return (u16)RGB15(r, g, b);
}
static void flush_palettes(void){
    // skipped unless a fade/flash value moved or the palettes were rebuilt
    static int last_fs = -1, last_fr = -1, last_fw = -1, last_pg = -1;
    if (fade_scale == last_fs && fade_red == last_fr && flash_white == last_fw && pal_gen == last_pg) return;
    last_fs = fade_scale; last_fr = fade_red; last_fw = flash_white; last_pg = pal_gen;
    for (int bank = 0; bank < 4; bank++)
        for (int i = 0; i < 16; i++){
            u16 c = shade_col(bgpal[bank][i], fade_scale, 16);
            if (fade_red){ int r = (c & 31) + fade_red; if (r > 31) r = 31; c = (u16)((c & ~31) | r); }
            BG_PAL[bank * 16 + i] = flash_white > 0 ? flash_blend(c) : c;
            u16 oc = shade_col(objpal[bank][i], fade_scale, 16);
            OBJ_PAL[bank * 16 + i] = flash_white > 0 ? flash_blend(oc) : oc;
        }
    // text banks: 4 bone, 5 amber, 6 red, 7 gray (index1 = ink, rest unused)
    BG_PAL[4 * 16 + 1] = shade_col(RGB15(27,25,23), fade_scale, 16);
    BG_PAL[5 * 16 + 1] = shade_col(RGB15(29,20,5), fade_scale, 16);
    BG_PAL[6 * 16 + 1] = shade_col(RGB15(28,7,7), fade_scale, 16);
    BG_PAL[7 * 16 + 1] = shade_col(RGB15(15,14,18), fade_scale, 16);
    BG_PAL[0] = RGB15(2,1,4); // backdrop
}

// ---------------- BG1 text layer ----------------
static int glyph_index(char c){
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    const char* s = FONT_CHARS_STR;
    for (int i = 0; s[i]; i++) if (s[i] == c) return i;
    return 50; // space
}
static void clear_text(void){
    int sp = glyph_index(' ');
    for (int i = 0; i < 32 * 32; i++) BG1_MAP[i] = (u16)(sp | (4 << 12));
}
static void put_text(int tx, int ty, const char* str, int bank){
    for (int i = 0; str[i]; i++){
        if (tx + i >= 30) break;
        BG1_MAP[ty * 32 + tx + i] = (u16)(glyph_index(str[i]) | (bank << 12));
    }
}
static void put_num(int tx, int ty, int val, int digits, int bank){
    char buf[12]; int n = 0;
    if (val < 0) val = 0;
    for (int i = 0; i < digits; i++){ buf[digits - 1 - i] = (char)('0' + (val % 10)); val /= 10; }
    buf[digits] = 0; n = digits;
    for (int i = 0; i < n; i++) BG1_MAP[ty * 32 + tx + i] = (u16)(glyph_index(buf[i]) | (bank << 12));
}
static void put_text_c(int ty, const char* str, int bank){ // centered (30 tiles wide)
    int len = 0; while (str[len]) len++;
    put_text((30 - len) / 2, ty, str, bank);
}

// ---------------- shared game state ----------------
enum { ST_TITLE, ST_INTRO, ST_PLAY, ST_ANIM, ST_CLEAR, ST_DEAD, ST_END, ST_PAUSE };
static int state = ST_TITLE;
static int state_t = 0;         // frames in current state
static int frame = 0;
static u16 keys = 0, keys_prev = 0;
static int pressed(u16 k){ return (keys & k) && !(keys_prev & k); }

static char grid[MH][MW];
static u8 light[MH][MW];
static u8 tile_cache[MH][MW];
static int level_idx = 0;
static int chapter_of_level = 0;
static int score = 0, deaths = 0, total_moves = 0;
static int moves_level = 0, relics_left = 0, relics_total = 0;
static int base_radius = 3, radius = 3, oil_budget = 0, oil_left = 0;
static int freeze_turns = 0, rune_used = 0, turn_count = 0;
static int shake = 0;
static char msg[48]; static int msg_t = 0; static int msg_ver = 0;
static void set_msg(const char* s){ int i = 0; while (s[i] && i < 47){ msg[i] = s[i]; i++; } msg[i] = 0; msg_t = 200; msg_ver++; }

typedef struct { int x, y, px, py, sx, sy, dir, frame; } Actor; // dir 0 down 1 up 2 left 3 right
static Actor player;
enum { EN_WRAITH, EN_SKEL, EN_SPIDER };
typedef struct { int type, x, y, px, py, sx, sy, dir, alive, moved; } Enemy;
static Enemy enemies[10]; static int num_enemies = 0;

typedef struct { int x, y, vx, vy, life, maxlife, tile, bank; } Particle;
static Particle parts[20];
static void spawn_particle(int x, int y, int vx, int vy, int life, int tile, int bank){
    for (int i = 0; i < 20; i++) if (parts[i].life <= 0){
        parts[i].x = x; parts[i].y = y; parts[i].vx = vx; parts[i].vy = vy;
        parts[i].life = life; parts[i].maxlife = life; parts[i].tile = tile; parts[i].bank = bank; return;
    }
}

// ---------------- audio: PSG music + SFX ----------------
#define SND(x) REG16(0x04000060 + (x))
#define REG_SOUNDCNT_L REG16(0x04000080)
#define REG_SOUNDCNT_H REG16(0x04000082)
#define REG_SOUNDCNT_X REG16(0x04000084)
// channel regs (offsets from 0x04000060): ch1 sweep/duty/freq = 0,2,4 ; ch2 = 8,0xC ; ch3 = 0x10,0x12,0x14 ; ch4 = 0x18,0x1C
static void audio_init(void){
    REG_SOUNDCNT_X = 0x0080;                    // master enable
    REG_SOUNDCNT_L = 0x0077 | (0x77 << 8);      // all channels L+R, vol 7
    REG_SOUNDCNT_H = 0x0002;                    // PSG full volume
    // wave RAM: soft sine-ish for bells (bank 0)
    static const u8 wave[16] = {0x89,0xBC,0xDE,0xFE,0xED,0xCB,0x98,0x76,0x65,0x43,0x21,0x01,0x12,0x34,0x56,0x67};
    for (int i = 0; i < 16; i++) REG16(0x04000030 + i * 2) = (u16)(wave[i] | (wave[i] << 8));
    SND(0x10) = 0; // ch3 bank select 0, stop
}
static const short NOTE_HZ[12] = {262,278,294,311,330,349,370,392,415,440,466,494}; // C4..B4
static int note_hz(int midi){
    int oct = (midi - 60) / 12, idx = (midi - 60) % 12;
    if (idx < 0){ idx += 12; oct -= 1; }
    int hz = NOTE_HZ[idx];
    while (oct > 0){ hz *= 2; oct--; }
    while (oct < 0){ hz = (hz + 1) / 2; oct++; }
    return hz;
}
static int psg_freq(int midi){ int hz = note_hz(midi); if (hz < 65) hz = 65; return 2048 - (131072 / hz); }
static void sq_note(int ch, int midi, int vol, int duty, int len_frames){
    int off = (ch == 1) ? 0 : 8;
    int env = (vol << 12) | (2 << 8);           // decreasing envelope
    if (len_frames > 40) env = (vol << 12);     // sustain for long notes
    SND(off + 2) = (u16)((duty << 6) | env);
    SND(off + 4) = (u16)(psg_freq(midi) | 0x8000);
    (void)len_frames;
}
static void bell_toll(int midi, int vol){
    SND(0x10) = 0x20;                            // ch3: bank 0, play mode
    SND(0x12) = (u16)(vol << 13);                // volume (1=100% 2=50% ...)
    SND(0x14) = (u16)(psg_freq(midi) | 0x8000);
}
static void noise_hit(int vol, int frames, int rough){
    SND(0x18) = (u16)((vol << 12) | (2 << 8));
    SND(0x1C) = (u16)((rough ? 0x0080 : 0) | 0x8000 | 8);
    (void)frames;
}
// music sequencer: 64 steps, chords Am F Dm E (Andalusian descent)
static const s8 CHORD_ROOT[4] = {45, 41, 38, 40};
static const s8 CHORD_TONES[4][4] = { {57,60,64,69}, {53,57,60,65}, {50,53,57,62}, {52,56,59,64} };
static const s8 LEAD_PAT[16] = {0,-1,1,-1,2,-1,3,-1, 2,-1,1,2,-1,-1,1,-1};
// exit fanfare: a bright square-wave victory figure on the lead channel, bells beneath
static const s8 FANF_NOTES[8] = {69, 73, 76, 81, 76, 81, 85, 88};
static int mus_step = 0, mus_timer = 0, sfx_duck = 0;
static int fanf = 0, fanf_t = 0;
static int doorfx = 0, doorfx_t = 0;
static int wind_vol = 1, wind_timer = 0;
static void audio_update(void){
    // door grind: six pulses of rough noise over a descending iron groan, ending in a boom
    if (doorfx > 0 && --doorfx_t <= 0){
        int stp = 6 - doorfx;
        noise_hit(13, 8, 1);
        sq_note(1, 41 - stp * 2, 12, 3, 26);
        if (doorfx == 1){ bell_toll(57, 1); sq_note(2, 33, 12, 3, 44); }
        doorfx--; doorfx_t = 6;
    }
    if (fanf > 0 && --fanf_t <= 0){
        int i = 8 - fanf;
        sq_note(1, FANF_NOTES[i], 13, 2, 30);
        if (i == 0 || i == 4) bell_toll(45, 1);
        if (i == 7) bell_toll(57, 1);
        fanf--; fanf_t = 7;
    }
    // wind: slow wandering noise on ch4 (only when no push/step sfx this frame)
    if (--wind_timer <= 0){
        wind_timer = 14 + (int)(rnd() % 20);
        wind_vol += (int)(rnd() % 3) - 1; wind_vol = clampi(wind_vol, 0, 3);
        if (wind_vol > 0 && sfx_duck <= 0){ SND(0x18) = (u16)((wind_vol << 12) | (1 << 8)); SND(0x1C) = 0x8000 | 0x0043; }
    }
    if (sfx_duck > 0) sfx_duck--;
    if (--mus_timer > 0) return;
    mus_timer = 7; // ~128bpm sixteenths
    int chord = (mus_step >> 4) & 3, st = mus_step & 15;
    if (st == 0){ bell_toll(CHORD_ROOT[chord] + 24, 1); sq_note(2, CHORD_ROOT[chord], 9, 2, 100); }
    if (st == 8) sq_note(2, CHORD_ROOT[chord] + 7, 7, 2, 50);
    if (st == 0 || st == 4 || st == 8 || st == 12) { /* harmony pulse on ch2 handled above */ }
    s8 li = LEAD_PAT[st];
    if (li >= 0 && sfx_duck <= 0) sq_note(1, CHORD_TONES[chord][li], 6, 1, 20);
    mus_step = (mus_step + 1) & 63;
}
// SFX (duck the lead briefly so they read clearly)
static void sfx_step(void){ noise_hit(4, 3, 0); }
static void sfx_push(void){ noise_hit(6, 8, 1); sfx_duck = 6; }
static void sfx_chime(void){ sq_note(1, 88, 11, 2, 12); sfx_duck = 10; }
static void sfx_dissolve(void){ noise_hit(8, 12, 0); sq_note(1, 40, 9, 0, 16); sfx_duck = 12; }
static void sfx_unlock(void){ sq_note(1, 76, 10, 2, 10); sfx_duck = 14; bell_toll(69, 2); }
static void sfx_sting(void){ sq_note(1, 46, 13, 3, 30); sq_note(2, 45, 13, 3, 30); noise_hit(10, 20, 1); sfx_duck = 30; }
static void sfx_bell(void){ bell_toll(57, 1); bell_toll(69, 2); noise_hit(3, 10, 0); sfx_duck = 8; }
static void sfx_toggle(void){ sq_note(1, 72, 8, 1, 8); sfx_duck = 6; }
static void sfx_gate(void){ noise_hit(7, 10, 1); sq_note(1, 41, 9, 3, 16); sfx_duck = 8; }
static void sfx_lever(void){ noise_hit(9, 8, 1); sq_note(1, 50, 11, 3, 12); bell_toll(81, 3); sfx_duck = 10; }
static void sfx_oilpick(void){ sq_note(1, 69, 10, 2, 10); bell_toll(76, 2); sfx_duck = 8; }
// exit fanfare: an 8-note victory figure sequenced in audio_update over ~56 frames
static void sfx_exit(void){ fanf = 8; fanf_t = 0; sfx_duck = 60; }
// a great door grinding open (sequenced in audio_update via doorfx)
static void sfx_door(void){ doorfx = 6; doorfx_t = 0; sfx_duck = 46; }
static void sfx_seal(void){ sq_note(1, 81, 11, 2, 10); bell_toll(88, 3); sfx_duck = 8; }
static void sfx_reset(void){ sq_note(1, 55, 11, 3, 14); sq_note(2, 49, 11, 3, 18); noise_hit(8, 10, 1); sfx_duck = 14; }

// libc-free memory helpers (freestanding environment still expects these symbols)
void* memcpy(void* d, const void* s, unsigned n){ volatile u8* dd = (volatile u8*)d; const volatile u8* ss = (const volatile u8*)s; for (unsigned i = 0; i < n; i++) dd[i] = ss[i]; return d; }
void* memset(void* d, int v, unsigned n){ volatile u8* dd = (volatile u8*)d; for (unsigned i = 0; i < n; i++) dd[i] = (u8)v; return d; }

// ---------------- levels ----------------
// Legend: # wall, I pillar, H portrait, T tombstone, . floor, , carpet, x cracked, (space) void,
// D door, G plate gate (1 plate), K twin gate (2 plates), A lever gate (open when lever ON),
// E lever gate (open when lever OFF), L lever, g plate, C candle unlit, c candle lit,
// B brazier, R bell rune, S start, r relic, o oil, w wraith, k skeleton(h), v skeleton(v), s spider
#define LF_CANDLES 1   // the door also needs every candle lit
#define LF_RUNE    2   // the door is webbed shut until the bell rune is tolled
typedef struct { const char* name; const char* chap; int chapter; int radius; int oil; int flags;
                 const char* f1; const char* f2; const char* hint; const char* map[MH]; } Level;
static const Level LEVELS[14] = {
{ "CANDLELIT ENTRY", "CHAPTER ONE - THE MANOR", 0, 3, 0, LF_CANDLES,
  "THE MANOR SLEEPS.", "TAKE BACK WHAT WAS STOLEN.",
  "LIGHT THE CANDLE. GATHER THE RELICS.",
  { "###############",
    "#S....#.......#",
    "#.....#...r...#",
    "#..C..#.......#",
    "#.....###.###.#",
    "#.............#",
    "#.r.....,.....#",
    "#.....###.....#",
    "#......D......#",
    "###############" } },
{ "THE LONG GALLERY", "CHAPTER ONE - THE MANOR", 0, 3, 0, 0,
  "SOMETHING DRIFTS IN THE DARK", "BETWEEN THE PORTRAITS.",
  "WRAITHS CANNOT ENTER THE LIGHT.",
  { "###############",
    "#S,,,,#.......#",
    "#,,,,,#...w...#",
    "#,,,,,#.......#",
    "##.####...##.##",
    "#.............#",
    "#....T...T....#",
    "#..r.......r..#",
    "#......D......#",
    "###############" } },
{ "OIL AND ASHES", "CHAPTER ONE - THE MANOR", 0, 3, 30, 0,
  "YOUR LAMP THIRSTS.", "ONE FLASK REMAINS. TWO ROADS CALL.",
  "CHOOSE YOUR ROAD WELL.",
  { "###############",
    "#S..#......#..#",
    "#...#......#r.#",
    "#...#...#..#..#",
    "#...#####..#..#",
    "###w#......#..#",
    "#...#...#.....#",
    "#.....o.###...#",
    "#.r.....#...Dr#",
    "###############" } },
{ "THE PLATE AND THE GATE", "CHAPTER TWO - THE CRYPT", 1, 3, 40, 0,
  "OLD STONE LISTENS FOR WEIGHT.", "",
  "A BRAZIER CAN HOLD A PLATE FOR YOU.",
  { "###############",
    "#S....#.......#",
    "#.....#..G..r.#",
    "#..B..#..G....#",
    "#.....#..G....#",
    "#..g..###.#####",
    "#.............#",
    "#.............#",
    "#......D......#",
    "###############" } },
{ "TWO WEIGHTS", "CHAPTER TWO - THE CRYPT", 1, 3, 60, 0,
  "ONE STONE IS NOT ENOUGH.", "",
  "SOME GATES NEED TWO PLATES HELD AT ONCE.",
  { "###############",
    "#S............#",
    "#.B.....g.....#",
    "#.............#",
    "#..B......w...#",
    "#.............#",
    "#..g..........#",
    "######KKK######",
    "#.r....D....r.#",
    "###############" } },
{ "WEAVER'S DARK", "CHAPTER TWO - THE CRYPT", 1, 2, 0, LF_RUNE,
  "THEY WEAVE WHERE NO LIGHT FALLS.", "",
  "THE GATE WANTS A WEIGHTED PLATE. THE DOOR, A BELL.",
  { "###############",
    "#S......#.....#",
    "#.......#..r..#",
    "#..c....#.....#",
    "#.......###G###",
    "#....R........#",
    "#..sB......s..#",
    "#.r...g....r..#",
    "#......D......#",
    "###############" } },
{ "THE TURNKEY", "CHAPTER THREE - THE BELFRY", 2, 3, 80, 0,
  "EVERY LOCK HAS ITS PRICE.", "",
  "BUMP THE LEVER. ONE GATE OPENS, ONE SHUTS.",
  { "###############",
    "#S....#....r..#",
    "#.....A.......#",
    "#..L..#.......#",
    "#.....#.......#",
    "###.######g####",
    "#....B.k......#",
    "###E##.....G###",
    "#..D..#####..r#",
    "###############" } },
{ "EVERY CANDLE", "CHAPTER THREE - THE BELFRY", 2, 3, 76, LF_CANDLES,
  "EVERY FLAME MUST BURN", "BEFORE THE DOOR WILL KNOW YOU.",
  "LIGHT EVERY CANDLE. GATHER THE RELICS.",
  { "###############",
    "#S....#.......#",
    "#.....#..C....#",
    "#..C..#..#r##.#",
    "#.....#..#....#",
    "##g####..#G.w.#",
    "#...B.......###",
    "#..s......C...#",
    "#.r...D.....r.#",
    "###############" } },
{ "THE HOLLOW BELL", "CHAPTER THREE - THE BELFRY", 2, 3, 80, LF_RUNE,
  "EVERY HAUNTING AT ONCE.", "RING THE DARK TO SLEEP.",
  "THREE SEALS KEEP THE VAULT. ONE TRUE ORDER.",
  { "###############",
    "#S..#..o..#...#",
    "#...#..r..#r..#",
    "#2B.#.....#...#",
    "#..#####K####M#",
    "#.......o...1.#",
    "#..w.B.k.R..s.#",
    "#..g..g......3#",
    "#.r....D....r.#",
    "###############" } },
{ "THE AIRLOCK", "CHAPTER FOUR - THE DEEP BELL", 3, 3, 40, 0,
  "TWO LEVERS BREATHE AS ONE.", "WHAT OPENS HERE SEALS THERE.",
  "CROSS, TURN, CROSS AGAIN.",
  { "###############",
    "#S...#....#...#",
    "#....#....#r..#",
    "#.L..A..k.#...#",
    "#....#....#...#",
    "#....#.L..E...#",
    "#....#....#.r.#",
    "#..o.#.r..#...#",
    "#....#....#D..#",
    "###############" } },
{ "THE HERDING", "CHAPTER FOUR - THE DEEP BELL", 3, 3, 40, 0,
  "THE DEAD FEAR YOUR LIGHT.", "DRIVE THEM BEFORE YOU.",
  "A CORNERED WRAITH IS A WALL.",
  { "###############",
    "#S....r......r#",
    "#.............#",
    "#..#########..#",
    "#.w#########w.#",
    "#..#########..#",
    "#.............#",
    "#..C......r...#",
    "#.....D....s..#",
    "###############" } },
{ "THE WEAVER'S CLOCK", "CHAPTER FOUR - THE DEEP BELL", 3, 3, 44, 0,
  "THE WEAVER WORKS THE LOOM.", "HER WEIGHT OPENS THE WAY.",
  "STEP CLOSE TO FREEZE HER HAND.",
  { "###############",
    "#S......#.....#",
    "#.......##g.s##",
    "#.o.....#######",
    "#..k..........#",
    "#.......##G####",
    "#.......#.....#",
    "#..r....#.r...#",
    "#r......#..D..#",
    "###############" } },
{ "THE FOUR SEALS", "CHAPTER FOUR - THE DEEP BELL", 3, 3, 84, LF_CANDLES,
  "FOUR SEALS SLEEP IN FOUR WINDS.", "ONE TRUE ORDER, OR ALL UNDONE.",
  "LIGHT EVERY CANDLE TOO.",
  { "###############",
    "#S...L#...#Lr.#",
    "#.3..C#....2..#",
    "#.....#.o....r#",
    "####A#####E####",
    "#...........C.#",
    "#.1..C..#..4..#",
    "#..L.r.M.r....#",
    "#.....#D#.....#",
    "###############" } },
{ "THE DEEP BELL", "CHAPTER FOUR - THE DEEP BELL", 3, 3, 88, LF_CANDLES | LF_RUNE,
  "THE DEEP BELL TOLLS FOR KEEPERS.", "SEALS, WEIGHTS, WEBS - ALL OF IT.",
  "SPEND YOUR LIGHT LIKE A MISER.",
  { "###############",
    "#S..#.C..#.k..#",
    "#3..#.w..#.1..#",
    "#.C.#....#.C.r#",
    "#...####.####.#",
    "#..B.g..K....o#",
    "#r.B.g..K..R.w#",
    "#..s....###MM##",
    "#.o...r...2D..#",
    "###############" } },
};
#define NUM_LEVELS 14

typedef struct { int x, y, px, py, sx, sy; } Brazier;
static Brazier braziers[4]; static int num_braziers = 0;
static u8 candle_scored[MH][MW];
static int gate_open = 0;
static int twin_open = 0;
static int lever_on = 0;
static int seq_prog = 0, seq_total = 0;   // seal-order levers: '1'..'3' flipped in order
static int door_x = 0, door_y = 0;
static int map_dirty = 1;
static int seq_done(void){ return seq_total > 0 && seq_prog >= seq_total; }
static int level_flags = 0;
static int door_ready_prev = 0;
static int reveal_t = 0;

static int brazier_at(int x, int y){ for (int i = 0; i < num_braziers; i++) if (braziers[i].x == x && braziers[i].y == y) return i; return -1; }
static int enemy_at(int x, int y){ for (int i = 0; i < num_enemies; i++) if (enemies[i].alive && enemies[i].x == x && enemies[i].y == y) return i; return -1; }
static int gate_char(char c){ return c == 'G' || c == 'K' || c == 'A' || c == 'E' || c == 'M'; }
static int base_solid(int x, int y){
    if (x < 0 || y < 0 || x >= MW || y >= MH) return 1;
    char c = grid[y][x];
    if (gate_char(c)){
        // a gate never blocks someone already standing in it
        if ((player.x == x && player.y == y) || brazier_at(x, y) >= 0 || enemy_at(x, y) >= 0) return 0;
        if (c == 'G') return !gate_open;
        if (c == 'K') return !twin_open;
        if (c == 'A') return !lever_on;
        if (c == 'M') return !seq_done();
        return lever_on; // 'E'
    }
    return c == '#' || c == 'I' || c == 'H' || c == 'T' || c == 'C' || c == 'c' || c == 'L' || (c >= '1' && c <= '4') || c == ' ';
}
static int plate_pressed_at(int x, int y){
    if (grid[y][x] != 'g') return 0;
    if (player.x == x && player.y == y) return 1;
    if (brazier_at(x, y) >= 0) return 1;
    if (enemy_at(x, y) >= 0) return 1;
    return 0;
}
static void update_gates(void){
    int pressed = 0;
    for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++)
        if (grid[y][x] == 'g' && plate_pressed_at(x, y)) pressed++;
    gate_open = pressed >= 1;
    twin_open = pressed >= 2;
}
static int door_ready(void){
    if (relics_left > 0) return 0;
    if (level_flags & LF_CANDLES)
        for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++)
            if (grid[y][x] == 'C') return 0;
    if ((level_flags & LF_RUNE) && !rune_used) return 0;
    return 1;
}

static void compute_light(void);
static void load_level(int idx){
    const Level* L = &LEVELS[idx];
    num_enemies = 0; num_braziers = 0; relics_left = 0;
    for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++){
        char c = L->map[y][x];
        candle_scored[y][x] = 0;
        grid[y][x] = c;
        if (c == 'S'){ player.x = x; player.y = y; grid[y][x] = '.'; }
        else if (c == 'B'){ braziers[num_braziers].x = x; braziers[num_braziers].y = y; num_braziers++; grid[y][x] = '.'; }
        else if (c == 'w' || c == 'k' || c == 'v' || c == 's'){
            Enemy* e = &enemies[num_enemies++];
            e->type = (c == 'w') ? EN_WRAITH : (c == 's') ? EN_SPIDER : EN_SKEL;
            e->x = x; e->y = y; e->dir = (c == 'v') ? 1 : 3; e->alive = 1; e->moved = 0;
            grid[y][x] = '.';
        }
        else if (c == 'r') relics_left++;
        else if (c == 'D'){ door_x = x; door_y = y; }
    }
    relics_total = relics_left;
    seq_total = 0; seq_prog = 0;
    for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++)
        if (grid[y][x] >= '1' && grid[y][x] <= '4') seq_total++;
    player.px = player.sx = player.x * 16; player.py = player.sy = player.y * 16;
    player.dir = 0; player.frame = 0;
    for (int i = 0; i < num_enemies; i++){ enemies[i].px = enemies[i].sx = enemies[i].x * 16; enemies[i].py = enemies[i].sy = enemies[i].y * 16; }
    for (int i = 0; i < num_braziers; i++){ braziers[i].px = braziers[i].sx = braziers[i].x * 16; braziers[i].py = braziers[i].sy = braziers[i].y * 16; }
    for (int i = 0; i < 20; i++) parts[i].life = 0;
    base_radius = radius = L->radius; oil_budget = L->oil; oil_left = oil_budget;
    level_flags = L->flags; lever_on = 0; flash_white = 0;
    freeze_turns = 0; rune_used = 0; reveal_t = 0; moves_level = 0; turn_count = 0;
    chapter_of_level = L->chapter;
    build_palettes(chapter_of_level); flush_palettes();
    update_gates();
    compute_light();
    door_ready_prev = door_ready();
    map_dirty = 1;
    set_msg("");
}

static void compute_light(void){
    // sources: player lantern (S = radius+2), lit candles & braziers (S = 3), rune reveal
    for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++){
        int best = 0;
        int d = iabs(x - player.x); if (iabs(y - player.y) > d) d = iabs(y - player.y);
        int v = (radius + 2) - d; if (v > best) best = v;
        light[y][x] = (u8)clampi(best, 0, 3);
    }
    for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++){
        int s = (grid[y][x] == 'c') ? 3 : 0;
        if (!s) continue;
        for (int yy = y - 3; yy <= y + 3; yy++) for (int xx = x - 3; xx <= x + 3; xx++){
            if (xx < 0 || yy < 0 || xx >= MW || yy >= MH) continue;
            int d = iabs(xx - x); if (iabs(yy - y) > d) d = iabs(yy - y);
            int v = s - d; if (v > light[yy][xx]) light[yy][xx] = (u8)clampi(v, 0, 3);
        }
    }
    for (int i = 0; i < num_braziers; i++){
        int x = braziers[i].x, y = braziers[i].y;
        for (int yy = y - 3; yy <= y + 3; yy++) for (int xx = x - 3; xx <= x + 3; xx++){
            if (xx < 0 || yy < 0 || xx >= MW || yy >= MH) continue;
            int d = iabs(xx - x); if (iabs(yy - y) > d) d = iabs(yy - y);
            int v = 3 - d; if (v > light[yy][xx]) light[yy][xx] = (u8)clampi(v, 0, 3);
        }
    }
}

static int tile_for_cell(int x, int y){
    char c = grid[y][x];
    switch (c){
        case '#': return 5; case 'I': return 6; case 'H': return 18; case 'T': return 16;
        case ' ': return 0; case ',': return 3; case 'x': return 4;
        case 'D': return door_ready() ? 8 : ((level_flags & LF_RUNE) && !rune_used) ? 22 : 7;
        case 'G': return gate_open ? 10 : 9;
        case 'K': return twin_open ? 10 : 9;
        case 'A': return lever_on ? 10 : 21;
        case 'E': return lever_on ? 21 : 10;
        case 'M': return seq_done() ? 10 : 21;
        case 'L': return lever_on ? 20 : 19;
        case '1': case '2': case '3': case '4': return (c - '0' <= seq_prog) ? 20 : 19;
        case 'g': return plate_pressed_at(x, y) ? 12 : 11;
        case 'C': return 13; case 'c': return 14;
        case 'R': return rune_used ? 1 : 17;
        default: return ((x * 3 + y * 7) % 5 == 0) ? 2 : 1;
    }
}
static void render_map(void){
    // full redraw from the CACHED light map; called only when the world changed
    for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++){
        int tile = tile_for_cell(x, y);
        tile_cache[y][x] = (u8)tile;
        int b = (reveal_t > 0) ? 3 : light[y][x]; // bell-rune reveal is a visual flash only
        int bank = 3 - b;
        for (int sy = 0; sy < 2; sy++) for (int sx = 0; sx < 2; sx++)
            BG0_MAP[(y * 2 + sy) * 32 + (x * 2 + sx)] = (u16)((tile * 4 + sy * 2 + sx) | (bank << 12));
    }
}
static void render_flicker(void){
    // cheap ambient pass: only dim (light==1) cells shimmer, using cached tiles
    if (reveal_t > 0) return;
    for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++){
        if (light[y][x] != 1) continue;
        int b = ((x * 31 + y * 17 + (frame >> 4)) % 7 == 0) ? 0 : 1;
        int bank = 3 - b, tile = tile_cache[y][x];
        for (int sy = 0; sy < 2; sy++) for (int sx = 0; sx < 2; sx++)
            BG0_MAP[(y * 2 + sy) * 32 + (x * 2 + sx)] = (u16)((tile * 4 + sy * 2 + sx) | (bank << 12));
    }
}

// ---------------- turn logic ----------------
static int pending_died = 0, pending_cleared = 0;
static const s8 DX[4] = {0, 0, -1, 1};
static const s8 DY[4] = {1, -1, 0, 0};
static int enemy_blocked(int x, int y){
    if (base_solid(x, y)) return 1;
    if (grid[y][x] == 'D') return 1;
    if (brazier_at(x, y) >= 0) return 1;
    if (enemy_at(x, y) >= 0) return 1;
    return 0;
}
static void wraith_move(Enemy* e){
    int md = iabs(e->x - player.x) + iabs(e->y - player.y);
    if (light[e->y][e->x] >= 2){ // caught in the light: repelled — it flees to a darker or
        // farther cell, and if no cell improves its lot it cowers where it stands,
        // plugging the way (touching it is still death). Light never kills it now.
        int cl = light[e->y][e->x];
        int best = -1, bestlight = 99, bestdist = -1;
        for (int d = 0; d < 4; d++){
            int nx = e->x + DX[d], ny = e->y + DY[d];
            if (nx < 0 || ny < 0 || nx >= MW || ny >= MH || enemy_blocked(nx, ny)) continue;
            if (nx == player.x && ny == player.y) continue;
            int l = light[ny][nx], dist = iabs(nx - player.x) + iabs(ny - player.y);
            if (l < bestlight || (l == bestlight && dist > bestdist)){ best = d; bestlight = l; bestdist = dist; }
        }
        if (best >= 0 && (bestlight < cl || (bestlight == cl && bestdist > md))){ e->x += DX[best]; e->y += DY[best]; }
        return;
    }
    if (md == 1){ e->x = player.x; e->y = player.y; pending_died = 1; return; }
    // BFS through darkness toward the player
    static s8 stepdir[MH][MW]; static u8 seen[MH][MW];
    for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++){ seen[y][x] = 0; stepdir[y][x] = -1; }
    int qx[150], qy[150], qh = 0, qt = 0;
    qx[qt] = e->x; qy[qt] = e->y; qt++; seen[e->y][e->x] = 1;
    int found = 0;
    while (qh < qt && !found){
        int cx = qx[qh], cy = qy[qh]; qh++;
        for (int d = 0; d < 4; d++){
            int nx = cx + DX[d], ny = cy + DY[d];
            if (nx < 0 || ny < 0 || nx >= MW || ny >= MH || seen[ny][nx]) continue;
            int goal = (nx == player.x && ny == player.y);
            if (!goal && (light[ny][nx] >= 2 || enemy_blocked(nx, ny))) continue;
            seen[ny][nx] = 1; stepdir[ny][nx] = (cx == e->x && cy == e->y) ? (s8)d : stepdir[cy][cx];
            if (goal){ found = 1; break; }
            qx[qt] = nx; qy[qt] = ny; qt++;
        }
    }
    if (found){ int d = stepdir[player.y][player.x]; if (d >= 0){ e->x += DX[d]; e->y += DY[d]; } return; }
    // no dark path: creep to the darkest reachable neighbour
    int best = -1, bestlight = 99, bestdist = 999;
    for (int d = 0; d < 4; d++){
        int nx = e->x + DX[d], ny = e->y + DY[d];
        if (nx < 0 || ny < 0 || nx >= MW || ny >= MH || enemy_blocked(nx, ny)) continue;
        int l = light[ny][nx], dist = iabs(nx - player.x) + iabs(ny - player.y);
        if (l < 2 && (l < bestlight || (l == bestlight && dist < bestdist))){ best = d; bestlight = l; bestdist = dist; }
    }
    if (best >= 0){ e->x += DX[best]; e->y += DY[best]; }
}
static void skeleton_move(Enemy* e){
    for (int tries = 0; tries < 2; tries++){
        int nx = e->x + DX[e->dir], ny = e->y + DY[e->dir];
        if (nx == player.x && ny == player.y){ e->x = nx; e->y = ny; pending_died = 1; return; }
        if (nx < 0 || ny < 0 || nx >= MW || ny >= MH || enemy_blocked(nx, ny)){
            e->dir = (e->dir == 0) ? 1 : (e->dir == 1) ? 0 : (e->dir == 2) ? 3 : 2;
            continue;
        }
        e->x = nx; e->y = ny; return;
    }
}
static void spider_move(Enemy* e, int idx){
    if (light[e->y][e->x] >= 3) return;              // frozen only in BRIGHT light; at the lantern's edge they still creep
    (void)idx;                                        // weavers move EVERY turn unless the light is bright
    int sx = (player.x > e->x) ? 1 : (player.x < e->x) ? -1 : 0;
    int sy = (player.y > e->y) ? 1 : (player.y < e->y) ? -1 : 0;
    int cand[3][2] = { {sx, sy}, {sx, 0}, {0, sy} };
    for (int i = 0; i < 3; i++){
        int nx = e->x + cand[i][0], ny = e->y + cand[i][1];
        if (cand[i][0] == 0 && cand[i][1] == 0) continue;
        if (nx == player.x && ny == player.y){ e->x = nx; e->y = ny; pending_died = 1; return; }
        if (nx < 0 || ny < 0 || nx >= MW || ny >= MH || enemy_blocked(nx, ny)) continue;
        e->x = nx; e->y = ny; return;
    }
}
static void resolve_turn(int dx, int dy, int is_wait){
    player.sx = player.px; player.sy = player.py;
    for (int i = 0; i < num_enemies; i++){ enemies[i].sx = enemies[i].px; enemies[i].sy = enemies[i].py; }
    for (int i = 0; i < num_braziers; i++){ braziers[i].sx = braziers[i].px; braziers[i].sy = braziers[i].py; }
    pending_died = 0; pending_cleared = 0;
    int go0 = gate_open, to0 = twin_open;
    int moved = is_wait, oil_got = 0;
    if (!is_wait){
        int nx = player.x + dx, ny = player.y + dy;
        player.dir = (dy == 1) ? 0 : (dy == -1) ? 1 : (dx == -1) ? 2 : 3;
        int bi = brazier_at(nx, ny);
        if (bi >= 0){
            int bx = nx + dx, by = ny + dy;
            char bc = (bx < 0 || by < 0 || bx >= MW || by >= MH) ? '#' : grid[by][bx];
            if (!base_solid(bx, by) && brazier_at(bx, by) < 0 && enemy_at(bx, by) < 0 &&
                (bc == '.' || bc == ',' || bc == 'x' || bc == 'g')){
                braziers[bi].x = bx; braziers[bi].y = by;
                player.x = nx; player.y = ny; moved = 1; sfx_push();
            }
        } else if (nx < 0 || ny < 0 || nx >= MW || ny >= MH || base_solid(nx, ny) || (grid[ny][nx] == 'D' && !door_ready())){
            char tc = (nx < 0 || ny < 0 || nx >= MW || ny >= MH) ? '#' : grid[ny][nx];
            if (tc == 'C' || tc == 'c'){
                grid[ny][nx] = (tc == 'C') ? 'c' : 'C';
                if (tc == 'C' && !candle_scored[ny][nx]){ score += 25; candle_scored[ny][nx] = 1; }
                sfx_toggle(); moved = 1;
            } else if (tc == 'L'){
                lever_on = !lever_on; sfx_lever(); moved = 1;
                set_msg(lever_on ? "THE LEVER CLANKS - ON" : "THE LEVER CLANKS - OFF");
            } else if (tc >= '1' && tc <= '4'){
                int d = tc - '0';
                if (d == seq_prog + 1){
                    seq_prog++; moved = 1;
                    if (seq_prog >= seq_total){ sfx_door(); set_msg("THE SEALED GATE OPENS"); }
                    else { sfx_seal(); set_msg(d == 1 ? "THE FIRST SEAL TURNS" : (d == 2 ? "THE SECOND SEAL TURNS" : "THE THIRD SEAL TURNS")); }
                } else if (d > seq_prog + 1){
                    seq_prog = 0; sfx_reset(); set_msg("THE SEALS RESET"); moved = 1;
                } else {
                    sfx_toggle(); set_msg("THAT SEAL ALREADY TURNS"); moved = 1;
                }
            } else if (tc == 'D'){
                if ((level_flags & LF_RUNE) && !rune_used && relics_left == 0) set_msg("WEBBED SHUT - TOLL THE BELL");
                else if ((level_flags & LF_CANDLES) && relics_left == 0) set_msg("LIGHT EVERY CANDLE FIRST");
                else if (relics_left > 0) set_msg("GATHER THE RELICS FIRST");
            }
        } else if (enemy_at(nx, ny) >= 0){
            player.x = nx; player.y = ny; moved = 1; pending_died = 1;
        } else {
            player.x = nx; player.y = ny; moved = 1; sfx_step();
        }
        if (!moved) return; // bonk: no turn passes
        // pickups & triggers on the player's new cell
        char c = grid[player.y][player.x];
        if (c == 'r'){
            grid[player.y][player.x] = '.'; relics_left--; score += 150; sfx_chime();
            static const s8 RING[8][2] = {{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}};
            for (int i = 0; i < 8; i++) spawn_particle(player.px + 4, player.py + 4, RING[i][0], RING[i][1], 22, 19, 3);
            spawn_particle(player.px + 4, player.py, 0, -1, 44, 21, 3); // the relic's soul rises
        } else if (c == 'o'){
            grid[player.y][player.x] = '.'; oil_left = oil_budget; radius = base_radius; oil_got = 1; sfx_oilpick();
            for (int i = 0; i < 5; i++) spawn_particle(player.px + 4, player.py + 6, (int)(rnd() % 3) - 1, -1, 26, 19, 0);
            set_msg("OIL - THE LIGHT SWELLS");
        } else if (c == 'R' && !rune_used){
            rune_used = 1; freeze_turns = 3; reveal_t = 110; shake = 8; sfx_bell();
            set_msg("THE BELL TOLLS - THEY FREEZE");
        } else if (c == 'D' && door_ready()){
            pending_cleared = 1; sfx_exit();
        }
    }
    if (moved && door_ready() && !door_ready_prev){ sfx_door(); set_msg("THE DOOR UNSEALS"); }
    turn_count++; moves_level++; total_moves++;
    if (oil_budget > 0 && !oil_got){
        oil_left--;
        if (oil_left <= 0){
            oil_left = 0; pending_died = 1; set_msg("YOUR LANTERN DIES");
        } else {
            int nr = (oil_left > oil_budget / 3) ? base_radius : (base_radius > 1 ? base_radius - 1 : 1);
            if (nr < radius) set_msg("YOUR LANTERN GROWS DIM");
            radius = nr;
        }
    }
    update_gates(); compute_light();
    if (freeze_turns > 0) freeze_turns--;
    else {
        for (int i = 0; i < num_enemies; i++){
            if (!enemies[i].alive) continue;
            if (enemies[i].type == EN_WRAITH) wraith_move(&enemies[i]);
            else if (enemies[i].type == EN_SKEL) skeleton_move(&enemies[i]);
            else spider_move(&enemies[i], i);
            if (enemies[i].x == player.x && enemies[i].y == player.y) pending_died = 1;
        }
    }
    update_gates();
    if ((!go0 && gate_open) || (!to0 && twin_open)) sfx_gate();
    door_ready_prev = door_ready();
    map_dirty = 1;
    state = ST_ANIM; state_t = 0;
}

// ---------------- sprites ----------------
static u16 oam_shadow[128 * 4];
static int oam_n = 0;
static void add_sprite(int x, int y, int tile, int bank, int flip){
    if (oam_n >= 128 || x < -16 || x >= 240 || y < -16 || y >= 160) return;
    oam_shadow[oam_n * 4 + 0] = (u16)(y & 0xFF);
    oam_shadow[oam_n * 4 + 1] = (u16)((x & 0x1FF) | (1 << 14) | (flip ? (1 << 12) : 0));
    oam_shadow[oam_n * 4 + 2] = (u16)((tile * 4) | (1 << 10) | (bank << 12));
    oam_shadow[oam_n * 4 + 3] = 0;
    oam_n++;
}
static void add_sprite_blend(int x, int y, int tile, int bank){ // semi-transparent, behind actors
    if (oam_n >= 128 || x < -16 || x >= 240 || y < -16 || y >= 160) return;
    oam_shadow[oam_n * 4 + 0] = (u16)((y & 0xFF) | (1 << 10)); // OBJ mode 01 = alpha blending
    oam_shadow[oam_n * 4 + 1] = (u16)((x & 0x1FF) | (1 << 14));
    oam_shadow[oam_n * 4 + 2] = (u16)((tile * 4) | (2 << 10) | (bank << 12));
    oam_shadow[oam_n * 4 + 3] = 0;
    oam_n++;
}
static void update_particles(void){
    for (int i = 0; i < 20; i++){
        if (parts[i].life <= 0) continue;
        parts[i].life--;
        parts[i].x += parts[i].vx; parts[i].y += parts[i].vy;
        if ((parts[i].life & 1) && parts[i].vy < 0) parts[i].y -= 0; // gentle rise handled by vy
    }
    // ambient soul wisps drifting through dark cells
    if ((frame % 97) == 0 && (state == ST_PLAY || state == ST_ANIM)){
        int x = (int)(rnd() % MW), y = (int)(rnd() % MH);
        if (light[y][x] == 0) spawn_particle(x * 16, y * 16, 0, -1, 130, 21, 2);
    }
}
static void compose_oam(void){
    oam_n = 0;
    if (state == ST_PLAY || state == ST_ANIM || state == ST_PAUSE){
        // items
        int relic_variant = 0;
        for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++){
            char c = grid[y][x];
            if (c != 'r' && c != 'o') continue;
            int b = (reveal_t > 0) ? 3 : light[y][x];
            int bob = ((frame >> 4) + x) & 1;
            if (c == 'r'){
                if (b == 0){ if ((frame % 200) < 8) add_sprite(x * 16, y * 16, 19, 3, 0); }
                else add_sprite(x * 16, y * 16 + bob, 12 + (relic_variant % 3), b >= 2 ? 0 : (b == 1 ? 1 : 2), 0);
                relic_variant++;
            } else if (b > 0) add_sprite(x * 16, y * 16 + bob, 15, b >= 2 ? 0 : (b == 1 ? 1 : 2), 0);
        }
        // flames over lit candles and braziers (glow bank), soft halo beneath each flame
        static const int FLSEQ[4] = {16, 17, 22, 17};
        int fl = FLSEQ[(frame >> 3) & 3];
        for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++)
            if (grid[y][x] == 'c'){ add_sprite_blend(x * 16, y * 16 - 2, 23, 3); add_sprite(x * 16, y * 16 - 2, fl, 3, 0); }
        for (int i = 0; i < num_braziers; i++){ add_sprite_blend(braziers[i].px, braziers[i].py - 2, 23, 3); add_sprite(braziers[i].px, braziers[i].py - 2, fl, 3, 0); }
        // brazier bodies (sprite 20), shaded by local light; flame above stays on the glow bank
        for (int i = 0; i < num_braziers; i++){
            int b = (reveal_t > 0) ? 3 : light[braziers[i].y][braziers[i].x];
            add_sprite(braziers[i].px, braziers[i].py, 20, b >= 3 ? 0 : (b == 2 ? 1 : 2), 0);
        }
        // enemies
        for (int i = 0; i < num_enemies; i++){
            Enemy* e = &enemies[i]; if (!e->alive) continue;
            int b = (reveal_t > 0) ? 3 : light[e->y][e->x];
            int bob = (e->type == EN_WRAITH) ? (((frame >> 4) & 1) ? -1 : 0) : 0;
            if (b == 0) add_sprite(e->px, e->py, 18, 3, 0); // only eyes in the pitch dark
            else {
                int tile = (e->type == EN_WRAITH) ? 6 + ((frame >> 5) & 1)
                         : (e->type == EN_SKEL) ? 8 + ((frame >> 5) & 1) : 10 + ((frame >> 5) & 1);
                add_sprite(e->px, e->py + bob, tile, b >= 3 ? 0 : (b == 2 ? 1 : 2), 0);
                if (e->type == EN_SPIDER && b <= 2) add_sprite(e->px, e->py, 18, 3, 0); // weaver eyes stay visible at the light's edge
            }
        }
        // player
        int ptile = (player.dir == 0) ? 0 : (player.dir == 1) ? 2 : 4;
        int pflip = (player.dir == 3);
        int pframe = (state == ST_ANIM) ? ((state_t / 6) & 1) : 0;
        int pbob = (state == ST_PLAY) ? ((frame >> 5) & 1) : 0;
        add_sprite(player.px, player.py + pbob, ptile + pframe, 0, pflip);
    }
    if (state == ST_TITLE){
        static const int FLSEQ2[4] = {16, 17, 22, 17};
        int fl = FLSEQ2[(frame >> 3) & 3];
        add_sprite_blend(2 * 16, 2 * 16 - 2, 23, 3);
        add_sprite_blend(12 * 16, 2 * 16 - 2, 23, 3);
        add_sprite(2 * 16, 2 * 16 - 2, fl, 3, 0);
        add_sprite(12 * 16, 2 * 16 - 2, fl, 3, 0);
        add_sprite(7 * 16, 5 * 16, 0, 0, 0);
    }
    if (state == ST_CLEAR && state_t <= 30){
        // the doorway blooms: a soft cross of light swelling outward from the door
        int o = state_t / 3;
        int cx = door_x * 16, cy = door_y * 16 - 2;
        add_sprite_blend(cx, cy, 23, 3);
        if (o > 0){
            add_sprite_blend(cx - o, cy, 23, 3);
            add_sprite_blend(cx + o, cy, 23, 3);
            add_sprite_blend(cx, cy - o, 23, 3);
            add_sprite_blend(cx, cy + o, 23, 3);
        }
    }
    for (int i = 0; i < 20; i++){
        if (parts[i].life <= 0) continue;
        if (parts[i].life < parts[i].maxlife / 4 && (frame & 2)) continue; // fade-out flicker
        add_sprite(parts[i].x, parts[i].y, parts[i].tile, parts[i].bank, 0);
    }
    for (int i = oam_n; i < 128; i++){ oam_shadow[i * 4] = 0x0200; oam_shadow[i * 4 + 1] = 0; oam_shadow[i * 4 + 2] = 0; oam_shadow[i * 4 + 3] = 0; }
    copy32((volatile u32*)OAM, (const u32*)oam_shadow, 256);
}

// ---------------- screens & state machine ----------------
static void set_state(int s){ state = s; state_t = 0; }
static void fill_backdrop(int border_tile, int fill_tile, int border_bank, int fill_bank){
    for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++){
        int edge = (x == 0 || y == 0 || x == MW - 1 || y == MH - 1);
        int tile = edge ? border_tile : fill_tile, bank = edge ? border_bank : fill_bank;
        for (int sy = 0; sy < 2; sy++) for (int sx = 0; sx < 2; sx++)
            BG0_MAP[(y * 2 + sy) * 32 + (x * 2 + sx)] = (u16)((tile * 4 + sy * 2 + sx) | (bank << 12));
    }
}
static void draw_title_bg(void){
    for (int y = 0; y < MH; y++) for (int x = 0; x < MW; x++){
        int tile, bank;
        if (x == 0 || y == 0 || x == MW - 1 || y == MH - 1){ tile = (x == 7 && y == 0) ? 18 : 5; bank = 1; }
        else if ((x == 2 && y == 2) || (x == 12 && y == 2)){ tile = 14; bank = 0; }
        else if ((x == 2 && y == 7) || (x == 12 && y == 7)){ tile = 16; bank = 1; }
        else if (x == 7){ tile = 3; bank = 1; }
        else { tile = ((x * 3 + y * 7) % 5 == 0) ? 2 : 1; bank = 2; }
        for (int sy = 0; sy < 2; sy++) for (int sx = 0; sx < 2; sx++)
            BG0_MAP[(y * 2 + sy) * 32 + (x * 2 + sx)] = (u16)((tile * 4 + sy * 2 + sx) | (bank << 12));
    }
}
static void enter_title(void){
    score = 0; deaths = 0; total_moves = 0; level_idx = 0;
    fade_scale = 16; fade_red = 0;
    draw_title_bg();
    clear_text();
    put_text_c(2, "GRIMLIGHT", 5);
    put_text_c(3, "THE HOLLOW BELL", 4);
    put_text_c(11, "A TURN-BASED GOTHIC PUZZLE", 7);
    put_text_c(12, "OF LIGHT AND SHADOW", 7);
    put_text_c(17, "DPAD MOVE - A WAIT", 7);
    put_text_c(18, "B RESTART - START PAUSE", 7);
    set_state(ST_TITLE);
}
static void draw_intro(void){
    const Level* L = &LEVELS[level_idx];
    clear_text();
    put_text_c(2, L->chap, 7);
    char lvbuf[10] = "LEVEL 0";
    if (level_idx < 9) lvbuf[6] = (char)('1' + level_idx);
    else { lvbuf[6] = '1'; lvbuf[7] = (char)('0' + (level_idx - 9)); lvbuf[8] = 0; }
    put_text_c(4, lvbuf, 5);
    put_text_c(6, L->name, 4);
    put_text_c(9, L->f1, 4);
    if (L->f2[0]) put_text_c(10, L->f2, 4);
    // hint wrapped over two lines if long
    int len = 0; while (L->hint[len]) len++;
    if (len <= 30) put_text_c(13, L->hint, 7);
    else {
        int split = 28; while (split > 0 && L->hint[split] != ' ') split--;
        char a[32], b[32]; int i;
        for (i = 0; i < split; i++) a[i] = L->hint[i]; a[split] = 0;
        int j = 0; for (i = split + 1; L->hint[i]; i++) b[j++] = L->hint[i]; b[j] = 0;
        put_text_c(13, a, 7); put_text_c(14, b, 7);
    }
    put_text_c(17, "PRESS A", 5);
}
static void enter_intro(int idx){
    level_idx = idx;
    load_level(idx);
    for (int i = 0; i < 32 * 32; i++) BG0_MAP[i] = 0; // opaque black behind the briefing text
    fade_scale = 16; fade_red = 0; flush_palettes();
    draw_intro();
    set_state(ST_INTRO);
}
static void draw_hud(void){
    // redrawn only when something it shows actually changed
    static int last_sig = -1;
    int sig = score * 3 + total_moves * 7 + level_idx * 131 + relics_left * 17 + oil_left * 19
            + (msg_t > 0 ? (1 << 20) : 0) + msg_ver * 4096;
    if (sig == last_sig) return;
    last_sig = sig;
    // row 0: SC 000000   LV 1   R 0/2   OIL 14
    char line[31]; for (int i = 0; i < 30; i++) line[i] = ' '; line[30] = 0;
    line[1] = 'S'; line[2] = 'C';
    int v = score; for (int i = 9; i >= 4; i--){ line[i] = (char)('0' + (v % 10)); v /= 10; }
    line[12] = 'L'; line[13] = 'V';
    if (level_idx < 9) line[15] = (char)('1' + level_idx);
    else { line[15] = '1'; line[16] = (char)('0' + (level_idx - 9)); }
    line[18] = 'R'; line[20] = (char)('0' + (relics_total - relics_left)); line[21] = '/'; line[22] = (char)('0' + relics_total);
    line[24] = 'O'; line[25] = 'I'; line[26] = 'L';
    if (oil_budget > 0){ line[28] = (char)('0' + (oil_left / 10)); line[29] = (char)('0' + (oil_left % 10)); }
    else { line[28] = '-'; line[29] = '-'; }
    for (int i = 0; i < 30; i++) BG1_MAP[i] = (u16)(glyph_index(line[i]) | (4 << 12));
    if (oil_budget > 0 && oil_left <= oil_budget / 4)
        for (int i = 28; i < 30; i++) BG1_MAP[i] = (u16)(glyph_index(line[i]) | (6 << 12));
    // bottom message row
    for (int i = 0; i < 30; i++) BG1_MAP[19 * 32 + i] = (u16)(glyph_index(' ') | (4 << 12));
    if (msg_t > 0){
        int len = 0; while (msg[len]) len++;
        int tx = (30 - len) / 2; if (tx < 0) tx = 0;
        for (int i = 0; msg[i] && tx + i < 30; i++) BG1_MAP[19 * 32 + tx + i] = (u16)(glyph_index(msg[i]) | (5 << 12));
    }
}
static void start_death(void){
    deaths++; score = score > 200 ? score - 200 : 0;
    sfx_sting(); fade_red = 12;
    set_state(ST_DEAD);
}
static void update(void){
    state_t++;
    if (msg_t > 0) msg_t--;
    if (reveal_t > 0 && --reveal_t == 0) map_dirty = 1;
    if (flash_white > 0) flash_white--;
    switch (state){
    case ST_TITLE:
        if (pressed(KEY_START)){ enter_intro(0); }
        break;
    case ST_INTRO:
        if ((frame >> 5) & 1) put_text_c(17, "PRESS A", 5);
        else put_text_c(17, "", 5);
        if (pressed(KEY_A) || pressed(KEY_START)){ clear_text(); set_state(ST_PLAY); }
        break;
    case ST_PLAY: {
        if (pressed(KEY_START)){ set_state(ST_PAUSE); put_text_c(9, "PAUSED", 5); put_text_c(11, "START RESUMES", 7); break; }
        if (pressed(KEY_B)){ load_level(level_idx); set_msg("THE ROOM RESETS"); break; }
        int dx = 0, dy = 0;
        if (pressed(KEY_RIGHT)) dx = 1; else if (pressed(KEY_LEFT)) dx = -1;
        else if (pressed(KEY_DOWN)) dy = 1; else if (pressed(KEY_UP)) dy = -1;
        if (dx || dy) resolve_turn(dx, dy, 0);
        else if (pressed(KEY_A)) resolve_turn(0, 0, 1);
        break; }
    case ST_ANIM: {
        int t = state_t; if (t > 12) t = 12;
        player.px = player.sx + (player.x * 16 - player.sx) * t / 12;
        player.py = player.sy + (player.y * 16 - player.sy) * t / 12;
        for (int i = 0; i < num_enemies; i++){
            enemies[i].px = enemies[i].sx + (enemies[i].x * 16 - enemies[i].sx) * t / 12;
            enemies[i].py = enemies[i].sy + (enemies[i].y * 16 - enemies[i].sy) * t / 12;
        }
        for (int i = 0; i < num_braziers; i++){
            braziers[i].px = braziers[i].sx + (braziers[i].x * 16 - braziers[i].sx) * t / 12;
            braziers[i].py = braziers[i].sy + (braziers[i].y * 16 - braziers[i].sy) * t / 12;
        }
        if (state_t >= 12){
            player.px = player.x * 16; player.py = player.y * 16;
            for (int i = 0; i < num_enemies; i++){ enemies[i].px = enemies[i].x * 16; enemies[i].py = enemies[i].y * 16; }
            for (int i = 0; i < num_braziers; i++){ braziers[i].px = braziers[i].x * 16; braziers[i].py = braziers[i].y * 16; }
            if (pending_died) start_death();
            else if (pending_cleared){
                int bonus = 600 - 15 * moves_level; if (bonus < 0) bonus = 0;
                score += bonus;
                flash_white = 0; // the bloom ramps smoothly during ST_CLEAR instead
                set_state(ST_CLEAR);
            } else set_state(ST_PLAY);
            map_dirty = 1;
        }
        break; }
    case ST_CLEAR:
        if (state_t <= 26) flash_white = state_t * 12 / 26; // light swells smoothly, then ebbs
        if (state_t > 40) fade_scale = 16 - (state_t - 40) * 16 / 30;
        if (state_t >= 70){
            fade_scale = 16;
            if (level_idx + 1 >= NUM_LEVELS){
                fill_backdrop(5, 1, 2, 3);
                clear_text();
                put_text_c(4, "THE BELL TOLLS AT DAWN", 5);
                put_text_c(6, "THE HAUNTING RESTS", 4);
                put_text_c(9, "THE MANOR IS QUIET AGAIN", 7);
                char buf[31]; for (int i = 0; i < 30; i++) buf[i] = ' '; buf[30] = 0;
                buf[8] = 'S'; buf[9] = 'C'; buf[10] = 'O'; buf[11] = 'R'; buf[12] = 'E';
                int v = score; for (int i = 19; i >= 14; i--){ buf[i] = (char)('0' + (v % 10)); v /= 10; }
                put_text_c(11, buf, 4);
                for (int i = 0; i < 30; i++) buf[i] = ' ';
                buf[8] = 'D'; buf[9] = 'E'; buf[10] = 'A'; buf[11] = 'T'; buf[12] = 'H'; buf[13] = 'S';
                v = deaths; buf[16] = (char)('0' + (v / 10)); buf[17] = (char)('0' + (v % 10));
                buf[20] = 'M'; buf[21] = 'O'; buf[22] = 'V'; buf[23] = 'E'; buf[24] = 'S';
                v = total_moves; buf[27] = (char)('0' + ((v / 100) % 10)); buf[28] = (char)('0' + ((v / 10) % 10)); buf[29] = (char)('0' + (v % 10));
                put_text_c(12, buf, 4);
                put_text_c(16, "PRESS START", 5);
                set_state(ST_END);
            } else enter_intro(level_idx + 1);
        }
        break;
    case ST_DEAD:
        if (fade_red > 0) fade_red--;
        if (state_t > 12) fade_scale = 16 - (state_t - 12) * 16 / 26;
        if (state_t >= 42){
            fade_scale = 16; fade_red = 0;
            load_level(level_idx);
            set_msg("THE DARK TAKES YOU - AGAIN");
            set_state(ST_PLAY);
        }
        break;
    case ST_END:
        if (pressed(KEY_START)) enter_title();
        break;
    case ST_PAUSE:
        if (pressed(KEY_START)){ clear_text(); set_state(ST_PLAY); }
        break;
    }
    if (state == ST_PLAY || state == ST_ANIM) draw_hud();
    if (map_dirty && (state == ST_PLAY || state == ST_ANIM || state == ST_CLEAR)){ render_map(); map_dirty = 0; }
    else if (state == ST_PLAY || state == ST_ANIM) render_flicker();
    update_particles();
}

#ifdef QA_BUILD
#include "qa_script.h"
static int qa_idx = 0, qa_phase = 0, qa_timer = 0, qa_from = -1;
static u16 qa_drive(void){
    if (qa_idx >= QA_NACT) return 0;
    if (qa_phase == 0){
        if (state != ST_TITLE && state != ST_INTRO && state != ST_PLAY && state != ST_END) return 0;
        qa_phase = 1; qa_timer = 0; qa_from = state;
        return (u16)QA_ACTIONS[qa_idx++];
    }
    qa_timer++;
    if (state != qa_from || qa_timer > 40) qa_phase = 0;
    return 0;
}
static int qa_done_mask = 0, qa_end_seen = 0, qa_end_snap = 0;
static void qa_report(void){
    if (state == ST_CLEAR) qa_done_mask |= (1 << level_idx);
    if (state == ST_END){ qa_end_seen = 1; qa_done_mask = (1 << NUM_LEVELS) - 1; }
    volatile unsigned char* s = (volatile unsigned char*)0x0E000000;
    if (state == ST_END && !qa_end_snap){
        qa_end_snap = 1;
        s[16] = (unsigned char)(score & 0xFF); s[17] = (unsigned char)((score >> 8) & 0xFF);
        s[18] = (unsigned char)deaths;
        s[19] = (unsigned char)(total_moves & 0xFF); s[20] = (unsigned char)((total_moves >> 8) & 0xFF);
        s[21] = (unsigned char)level_idx; s[22] = 1;
    }
    s[0] = 'G'; s[1] = 'R';
    s[2] = (unsigned char)state;
    s[3] = (unsigned char)level_idx;
    s[4] = (unsigned char)(score & 0xFF); s[5] = (unsigned char)((score >> 8) & 0xFF);
    s[6] = (unsigned char)deaths;
    s[7] = (unsigned char)(total_moves & 0xFF); s[8] = (unsigned char)((total_moves >> 8) & 0xFF);
    s[9] = (unsigned char)(qa_done_mask & 0xFF); s[10] = (unsigned char)((qa_done_mask >> 8) & 0xFF);
    s[11] = (unsigned char)qa_end_seen;
    s[12] = (unsigned char)(qa_idx & 0xFF); s[13] = (unsigned char)((qa_idx >> 8) & 0xFF);
    unsigned int sum = 0; for (int i = 2; i <= 13; i++) sum += s[i];
    s[14] = (unsigned char)(sum & 0xFF);
    s[15] = 0xA5;
}
#endif

int main(void){
    copy32(BG0_TILES, (const u32*)TERRAIN_TILES, NUM_TERRAIN_CELLS * 4 * 8);
    copy32(BG1_TILES, (const u32*)FONT_TILES, 51 * 8);
    copy32(OBJ_TILES, (const u32*)SPRITE_TILES, NUM_SPRITES * 4 * 8);
    REG_BG0CNT = (u16)(2 | (16 << 8));        // prio 2, char block 0, screen block 16
    REG_BG1CNT = (u16)(0 | (1 << 2) | (17 << 8)); // prio 0, char block 1, screen block 17
    REG_DISPCNT = (u16)(0x1000 | 0x0200 | 0x0100 | 0x0040); // OBJ on, BG1 on, BG0 on, 1D sprites
    REG16(0x04000050) = (u16)((1 << 4) | (1 << 6) | (1 << 1) | (1 << 0)); // BLDCNT: OBJ alpha over BG0/BG1
    REG16(0x04000052) = (u16)(10 | (6 << 8));                              // BLDALPHA: 10/16 sprite, 6/16 bg
    build_palettes(0); flush_palettes();
    audio_init();
    enter_title();
    while (1){
        vsync();
        keys_prev = keys;
#ifdef QA_BUILD
        keys = qa_drive();
#else
        keys = (u16)(~REG_KEY) & 0x03FF;
#endif
        update();
        audio_update();
        compose_oam();
        flush_palettes();
        if (shake > 0){ shake--; REG_BG0HOFS = (u16)((int)(rnd() % 3) - 1); REG_BG0VOFS = (u16)((int)(rnd() % 3) - 1); }
        else { REG_BG0HOFS = 0; REG_BG0VOFS = 0; }
        frame++;
#ifdef QA_BUILD
        qa_report();
#endif
    }
    return 0;
}
