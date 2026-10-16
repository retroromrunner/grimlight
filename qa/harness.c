// Host QA harness: maps fake GBA hardware regions at their real addresses,
// runs the actual game (main.c, QA build) in a thread, simulates VCOUNT,
// and reads the game's SRAM proof block directly. Also dumps VRAM/palette/OAM
// in mGBA x/4 text format for render.py at three moments.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

extern int grimlight_main(void);

static volatile unsigned short* vcount_reg(void){
    return (volatile unsigned short*)0x04000006;
}
static unsigned char* sram(void){ return (unsigned char*)0x0E000000; }

static void map_region(void* addr, size_t len){
    void* p = mmap(addr, len, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p != addr){ perror("mmap"); exit(1); }
}

static void* vcount_thread(void* arg){
    (void)arg;
    unsigned int v = 0;
    for (;;){
        *vcount_reg() = (unsigned short)v;
        v = (v + 1) % 228;
        // small delay so the game thread can observe both vsync phases
        for (volatile int i = 0; i < 60000; i++) { }
    }
    return 0;
}

static void* game_thread(void* arg){
    (void)arg;
    grimlight_main();
    return 0;
}

static void dump(const char* path){
    FILE* f = fopen(path, "w");
    if (!f) return;
    struct { unsigned int base; int words; } regions[] = {
        { 0x05000000, 256 }, { 0x06000000, 24576 }, { 0x07000000, 256 },
    };
    for (int r = 0; r < 3; r++){
        volatile unsigned int* m = (volatile unsigned int*)regions[r].base;
        for (int i = 0; i < regions[r].words; i += 8){
            fprintf(f, "0x%08X:", regions[r].base + i * 4);
            for (int j = 0; j < 8; j++) fprintf(f, " %08X", m[i + j]);
            fprintf(f, "\n");
        }
    }
    fclose(f);
}

int main(void){
    map_region((void*)0x04000000, 0x10000);   // IO registers
    map_region((void*)0x05000000, 0x1000);    // palettes
    map_region((void*)0x06000000, 0x20000);   // VRAM
    map_region((void*)0x07000000, 0x1000);    // OAM
    map_region((void*)0x0E000000, 0x10000);   // SRAM

    pthread_t gt, vt;
    pthread_create(&vt, 0, vcount_thread, 0);
    pthread_create(&gt, 0, game_thread, 0);

    int dumped_title = 0, dumped_play = 0, dumped_intro = 0, done = 0;
    int base_moves[9]; for (int i = 0; i < 9; i++) base_moves[i] = -1;
    int dumped_lv[9] = {0}; int last_level = 0;
    for (int t = 0; t < 24000; t++){          // up to ~120 s
        unsigned char* s = sram();
        int state = s[2], level = s[3];
        int moves = s[7] | (s[8] << 8);
        if (level != last_level){ base_moves[level] = moves; last_level = level; }
        if (!dumped_title && s[0] == 'G' && state == 0){
            dump("qa/dump_title.txt"); dumped_title = 1;
        }
        if (!dumped_intro && state == 1){ dump("qa/dump_intro.txt"); dumped_intro = 1; }
        if (!dumped_play && (state == 2 || state == 3) && level == 0 && moves >= 3){
            dump("qa/dump_l1.txt"); dumped_play = 1;
        }
        if ((level == 4 || level == 6 || level == 8) && !dumped_lv[level] &&
            (state == 2 || state == 3) && base_moves[level] >= 0 && moves >= base_moves[level] + 4){
            char path[32]; snprintf(path, sizeof path, "qa/dump_l%d.txt", level + 1);
            dump(path); dumped_lv[level] = 1;
        }
        if (s[11] == 1 && state == 0){ done = 1; break; }  // end seen, back at title
        if (s[11] == 1 && state == 6) { /* at END screen; final START comes next */ }
        usleep(5000);
    }

    unsigned char* s = sram();
    unsigned int sum = 0; for (int i = 2; i <= 13; i++) sum += s[i];
    int score = s[4] | (s[5] << 8);
    int moves = s[7] | (s[8] << 8);
    int mask = s[9] | (s[10] << 8);
    int qaidx = s[12] | (s[13] << 8);
    printf("SRAM: magic=%c%c state=%d level=%d score=%d deaths=%d moves=%d "
           "mask=0x%03X end_seen=%d qa_idx=%d checksum_ok=%d sentinel=0x%02X\n",
           s[0], s[1], s[2], s[3], score, s[6], moves, mask, s[11], qaidx,
           (int)((sum & 0xFF) == s[14]), s[15]);
    if (s[22] == 1)
        printf("AT_END: score=%d deaths=%d moves=%d level=%d\n",
               s[16] | (s[17] << 8), s[18], s[19] | (s[20] << 8), s[21]);
    else printf("AT_END: (no snapshot)\n");
    dump("qa/dump_final.txt");
    printf(done ? "RESULT: COMPLETE\n" : "RESULT: TIMEOUT (state above is where it stalled)\n");
    fflush(stdout);
    _exit(done ? 0 : 2);
}
