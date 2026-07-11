/*
 * ================================================================
 * ST5004CEM — Operating Systems and Security
 * Task 2: Memory Management Simulation
 *
 * Author  : [Your Name]
 * ID      : [Your Student ID]
 * College : Softwarica College of IT & E-Commerce
 * ================================================================
 *
 * Compile:  gcc -Wall -Wextra -o task2 task2.c
 * Run:      ./task2
 * ================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── ANSI colour codes — output lai ramro dekhaunko lagi ── */
#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define DIM     "\033[2m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define CYAN    "\033[36m"

/* ================================================================
 *  CONFIGURATION — yo values change garera test garna milxa
 *  PAGE_SIZE: ek page kati bytes ko hunxa (virtual memory concept)
 *  FRAME_COUNT: physical RAM ma kati frames xa
 *  REF_LENGTH: reference string kati long xa
 * ================================================================ */
#define PAGE_SIZE    4    /* bytes per page — yo chai virtual memory ma ek page ko size */
#define FRAME_COUNT  4    /* RAM ma kati frames xa — physical memory ko limit */
#define REF_LENGTH   12   /* CPU le kati palta pages access garxa */

/* ================================================================
 *  PAGE FRAME STRUCT
 *
 *  Physical memory ko ek slot. Jab ek page disk bata RAM ma
 *  aauxa, it occupies one frame. Frame khali bhayo bhane
 *  page_id = -1 hunxa.
 *
 *  loaded_at: kati number ko tick ma load bhako — FIFO ko lagi
 *  last_used: last kati number ko tick ma access bhako — LRU ko lagi
 *  data:      page ko actual content (simulate garieko)
 * ================================================================ */
typedef struct {
    int  page_id;             /* kun page xa yahaa (-1 = khali) */
    int  loaded_at;           /* kati baje load bhako (FIFO use garxa yo) */
    int  last_used;           /* last kati baje use bhako (LRU use garxa yo) */
    char data[PAGE_SIZE];     /* page ko simulated content */
} Frame;

/* ================================================================
 *  STATS STRUCT — ek algorithm run ko result
 * ================================================================ */
typedef struct {
    int hits;    /* page already RAM ma thyo — disk access chaina */
    int faults;  /* page RAM ma thiyena — disk bata load garnu paro */
} Stats;


/* ================================================================
 *  HELPER FUNCTIONS
 * ================================================================ */

/* Frame array ma page_id xa ki xaina check garxa — xa bhane index return garxa */
static int find_page(Frame *frames, int page_id)
{
    for (int i = 0; i < FRAME_COUNT; i++)
        if (frames[i].page_id == page_id) return i;
    return -1; /* chaina */
}

/* Koi frame khali xa ki xaina */
static int has_empty(Frame *frames)
{
    for (int i = 0; i < FRAME_COUNT; i++)
        if (frames[i].page_id == -1) return 1;
    return 0;
}

/* Pahilo khali frame ko index return garxa */
static int first_empty(Frame *frames)
{
    for (int i = 0; i < FRAME_COUNT; i++)
        if (frames[i].page_id == -1) return i;
    return -1;
}

/* ================================================================
 *  PRINT FRAME STATE — visual table jastai dekhauxa
 *  Khal tick ma sabai frames ko state show garxa.
 *  Cyan = newly accessed page, Green = other pages, Dim = empty
 * ================================================================ */
static void print_frames(Frame *frames, int ref, int is_fault)
{
    printf("  Ref P%-2d │ ", ref);
    for (int i = 0; i < FRAME_COUNT; i++) {
        if (frames[i].page_id == -1)
            printf(DIM " [  —  ]" RESET);          /* khali frame */
        else if (frames[i].page_id == ref)
            printf(CYAN " [ P%-2d ]" RESET, frames[i].page_id);  /* yo nai ref garieko page */
        else
            printf(GREEN " [ P%-2d ]" RESET, frames[i].page_id); /* aru pages */
    }

    /* Fault bhayo ki hit — RED ma FAULT, GREEN ma HIT */
    if (is_fault)
        printf("  " RED "✗ PAGE FAULT" RESET "\n");
    else
        printf("  " GREEN "✓ HIT" RESET "\n");
}

/* ================================================================
 *  LOAD PAGE — disk bata RAM ma page load garxa (simulate)
 *  Real OS ma yaha actual disk I/O hunthyo — time laagthyo.
 *  Hami yahaa dummy data bhar dinchhau.
 * ================================================================ */
static void load_page(Frame *f, int page_id, int tick)
{
    f->page_id   = page_id;
    f->loaded_at = tick;   /* FIFO ko lagi — kahile load bhayo record */
    f->last_used = tick;   /* LRU ko lagi — last use time set */

    /* Dummy content — 'A'=0, 'B'=1, 'C'=2... jastai page identity dekhauxa */
    for (int b = 0; b < PAGE_SIZE; b++)
        f->data[b] = (char)('A' + page_id);

    printf(YELLOW);
    printf("    -> Disk bata P%d load garyo frame ma (data: ", page_id);
    for (int b = 0; b < PAGE_SIZE; b++)
        printf("%c ", f->data[b]);
    printf(")\n" RESET);
}

/* ================================================================
 *  PRINT HEADER — algorithm start huda info print garxa
 * ================================================================ */
static void print_header(const char *algo_name, int *ref_string)
{
    printf(BOLD BLUE
           "\n╔══════════════════════════════════════════════════════╗\n"
           "║     ST5004CEM Task 2 — Memory Management Sim        ║\n"
           "╚══════════════════════════════════════════════════════╝\n"
           RESET);
    printf(BOLD "\n  Algorithm    : " RESET "%s\n", algo_name);
    printf(BOLD "  Page size    : " RESET "%d bytes\n", PAGE_SIZE);
    printf(BOLD "  Frame count  : " RESET "%d  (physical RAM capacity)\n", FRAME_COUNT);
    printf(BOLD "  Ref string   : " RESET);
    for (int i = 0; i < REF_LENGTH; i++)
        printf("P%d ", ref_string[i]);
    printf("\n\n");

    /* Table header */
    printf(BOLD "  Ref Page │ Frames%-*s│ Result\n" RESET,
           FRAME_COUNT * 8 - 6, "");
    printf(DIM "  ─────────┼");
    for (int i = 0; i < FRAME_COUNT * 8; i++) printf("─");
    printf("┼─────────────\n" RESET);
}


/* ================================================================
 *  FIFO PAGE REPLACEMENT ALGORITHM
 *
 *  "First In First Out" — sabai bhandaa puraano page lai nikaalxa.
 *  loaded_at field use garera kaile load bhako tha paauxa.
 *  Smallest loaded_at = oldest = evict yo.
 *
 *  Simplest algorithm — implement garna easy.
 *  Tara Belady's Anomaly suffer garxa: sometimes more frames
 *  le bढi faults dinxa — FIFO ko weakness ho yo.
 * ================================================================ */
static Stats run_fifo(int *ref_string)
{
    /* Sabai frames initialize garxa — sab khali suru ma */
    Frame frames[FRAME_COUNT];
    for (int i = 0; i < FRAME_COUNT; i++) {
        frames[i].page_id   = -1;
        frames[i].loaded_at =  0;
        frames[i].last_used =  0;
        memset(frames[i].data, 0, PAGE_SIZE);
    }

    Stats s = {0, 0};
    int tick = 0;

    print_header("FIFO — First In First Out", ref_string);

    for (int i = 0; i < REF_LENGTH; i++) {
        int ref = ref_string[i];
        tick++;

        int idx = find_page(frames, ref);

        if (idx != -1) {
            /* HIT — page already RAM ma xa, disk janu pardaina */
            frames[idx].last_used = tick; /* access time update garxa */
            s.hits++;
            print_frames(frames, ref, 0);
        } else {
            /* PAGE FAULT — disk bata load garnu parcha */
            s.faults++;

            if (has_empty(frames)) {
                /* Khali frame xa — seedha load */
                int slot = first_empty(frames);
                load_page(&frames[slot], ref, tick);
            } else {
                /* Sabai frames full — FIFO: oldest page lai nikaal */
                int oldest = 0;
                for (int j = 1; j < FRAME_COUNT; j++)
                    if (frames[j].loaded_at < frames[oldest].loaded_at)
                        oldest = j;
                printf(RED "    ✕ Evict: P%d (tick %d ma load bhako thyo — sabai bhandaa puraano)\n" RESET,
                       frames[oldest].page_id, frames[oldest].loaded_at);
                load_page(&frames[oldest], ref, tick);
            }
            print_frames(frames, ref, 1);
        }
    }
    return s;
}


/* ================================================================
 *  LRU PAGE REPLACEMENT ALGORITHM
 *
 *  "Least Recently Used" — sabai bhandaa kam use bhako page nikaalxa.
 *  last_used field track garera kaile use bhako tha paauxa.
 *  Smallest last_used = longest time since used = evict yo.
 *
 *  LRU is generally better than FIFO because it uses
 *  temporal locality — recently used pages are likely to be
 *  used again soon (kaam garirako page feri kaam lagxa).
 *  Tara overhead bढi hunxa — last_used track garna parcha.
 * ================================================================ */
static Stats run_lru(int *ref_string)
{
    /* Sabai frames initialize */
    Frame frames[FRAME_COUNT];
    for (int i = 0; i < FRAME_COUNT; i++) {
        frames[i].page_id   = -1;
        frames[i].loaded_at =  0;
        frames[i].last_used =  0;
        memset(frames[i].data, 0, PAGE_SIZE);
    }

    Stats s = {0, 0};
    int tick = 0;

    print_header("LRU — Least Recently Used", ref_string);

    for (int i = 0; i < REF_LENGTH; i++) {
        int ref = ref_string[i];
        tick++;

        int idx = find_page(frames, ref);

        if (idx != -1) {
            /* HIT — page RAM ma xa, important: last_used update garnu parcha
             * yo nai LRU ko key idea — use garyo bhane "fresh" mark gara */
            frames[idx].last_used = tick;
            s.hits++;
            print_frames(frames, ref, 0);
        } else {
            /* PAGE FAULT — disk bata load */
            s.faults++;

            if (has_empty(frames)) {
                int slot = first_empty(frames);
                load_page(&frames[slot], ref, tick);
            } else {
                /* Sabai full — LRU: sabai bhandaa puraano use bhako page nikaal */
                int lru_idx = 0;
                for (int j = 1; j < FRAME_COUNT; j++)
                    if (frames[j].last_used < frames[lru_idx].last_used)
                        lru_idx = j;
                printf(YELLOW "    ✕ Evict: P%d (tick %d ma last use bhako thyo — sabai bhandaa stale)\n" RESET,
                       frames[lru_idx].page_id, frames[lru_idx].last_used);
                load_page(&frames[lru_idx], ref, tick);
            }
            print_frames(frames, ref, 1);
        }
    }
    return s;
}


/* ================================================================
 *  OPTIMAL PAGE REPLACEMENT ALGORITHM (BONUS)
 *
 *  "Optimal / MIN" — future ma sabai bhandaa kaminai use
 *  hunne page nikaalxa. Theoretical best — real OS ma
 *  implement garna mildaina (future known huna parcha).
 *  Tara benchmark ko rupa ma use garxa — other algorithms
 *  sita compare garna milxa.
 * ================================================================ */
static Stats run_optimal(int *ref_string)
{
    Frame frames[FRAME_COUNT];
    for (int i = 0; i < FRAME_COUNT; i++) {
        frames[i].page_id   = -1;
        frames[i].loaded_at =  0;
        frames[i].last_used =  0;
        memset(frames[i].data, 0, PAGE_SIZE);
    }

    Stats s = {0, 0};
    int tick = 0;

    print_header("OPT — Optimal (Belady's Algorithm)", ref_string);

    for (int i = 0; i < REF_LENGTH; i++) {
        int ref = ref_string[i];
        tick++;

        int idx = find_page(frames, ref);

        if (idx != -1) {
            /* HIT */
            frames[idx].last_used = tick;
            s.hits++;
            print_frames(frames, ref, 0);
        } else {
            /* PAGE FAULT */
            s.faults++;

            if (has_empty(frames)) {
                int slot = first_empty(frames);
                load_page(&frames[slot], ref, tick);
            } else {
                /* Future ma kun page sabai pachi use hunxa tyo nikaal
                 * Future reference herna parcha — real OS ma impossible */
                int evict_idx = -1;
                int farthest  = -1;

                for (int f = 0; f < FRAME_COUNT; f++) {
                    int next_use = REF_LENGTH; /* assume never used again */
                    /* Future ma yo page kab use hunxa khoji */
                    for (int k = i + 1; k < REF_LENGTH; k++) {
                        if (ref_string[k] == frames[f].page_id) {
                            next_use = k;
                            break;
                        }
                    }
                    /* Sabai bhandaa pachi use hunne page lai evict garxa */
                    if (next_use > farthest) {
                        farthest  = next_use;
                        evict_idx = f;
                    }
                }
                printf(CYAN "    ✕ Evict: P%d (future ma sabai kaminai use hunxa)\n" RESET,
                       frames[evict_idx].page_id);
                load_page(&frames[evict_idx], ref, tick);
            }
            print_frames(frames, ref, 1);
        }
    }
    return s;
}


/* ================================================================
 *  PRINT INDIVIDUAL STATS — ek algorithm ko result
 * ================================================================ */
static void print_stats(const char *name, Stats s)
{
    float hit_rate   = (float)s.hits   / REF_LENGTH * 100.0f;
    float fault_rate = (float)s.faults / REF_LENGTH * 100.0f;

    printf(BOLD "\n  ── %s Results ──────────────────\n" RESET, name);
    printf("  Total refs   : %d\n",   REF_LENGTH);
    printf("  Hits         : " GREEN "%d  (%.1f%%)\n" RESET, s.hits,   hit_rate);
    printf("  Page Faults  : " RED   "%d  (%.1f%%)\n" RESET, s.faults, fault_rate);
}


/* ================================================================
 *  COMPARISON TABLE — teen wota algorithm haru ko head-to-head
 *
 *  Real assignment ma yahi dekhauxa ki kun algorithm ramro cha
 *  ra kina — FIFO vs LRU vs OPT.
 * ================================================================ */
static void print_comparison(Stats fifo, Stats lru, Stats opt)
{
    printf(BOLD BLUE
           "\n╔══════════════════════════════════════════════════════╗\n"
           "║            Algorithm Comparison Table               ║\n"
           "╚══════════════════════════════════════════════════════╝\n"
           RESET);

    printf(BOLD "\n  %-22s  %-8s  %-10s  %-10s  %s\n" RESET,
           "Algorithm", "Hits", "Faults", "Hit Rate", "Miss Rate");
    printf(DIM "  ──────────────────────  ────────  ──────────  ──────────  ──────────\n" RESET);

    float fifo_hr = (float)fifo.hits / REF_LENGTH * 100.0f;
    float lru_hr  = (float)lru.hits  / REF_LENGTH * 100.0f;
    float opt_hr  = (float)opt.hits  / REF_LENGTH * 100.0f;

    printf("  %-22s  " GREEN "%-8d" RESET "  " RED "%-10d" RESET "  %-10.1f%%  %.1f%%\n",
           "FIFO (First-In-First-Out)", fifo.hits, fifo.faults, fifo_hr, 100.0f - fifo_hr);
    printf("  %-22s  " GREEN "%-8d" RESET "  " RED "%-10d" RESET "  %-10.1f%%  %.1f%%\n",
           "LRU (Least-Recently-Used)", lru.hits, lru.faults, lru_hr, 100.0f - lru_hr);
    printf("  %-22s  " GREEN "%-8d" RESET "  " RED "%-10d" RESET "  %-10.1f%%  %.1f%%\n",
           "OPT (Optimal/Belady's)",   opt.hits, opt.faults, opt_hr, 100.0f - opt_hr);

    /* Verdict — kun algorithm jityo */
    printf("\n" BOLD "  Verdict:\n" RESET);

    if (lru.faults < fifo.faults)
        printf(GREEN "  ✓ LRU le FIFO bhandaa %d faults kam garyo (%d vs %d)\n" RESET,
               fifo.faults - lru.faults, lru.faults, fifo.faults);
    else if (fifo.faults < lru.faults)
        printf(YELLOW "  ✓ FIFO le LRU bhandaa %d faults kam garyo (%d vs %d)\n" RESET,
               lru.faults - fifo.faults, fifo.faults, lru.faults);
    else
        printf(CYAN "  ✓ FIFO ra LRU ko faults baraber xa (%d each)\n" RESET, fifo.faults);

    printf(GREEN "  ✓ OPT (Optimal) le sabai bhandaa kam faults garyo — theoretical minimum\n" RESET);

    /* Analysis notes */
    printf(BOLD "\n  Analysis Notes:\n" RESET);
    printf(DIM
           "  • FIFO:  Implement garna easy, tara Belady's Anomaly possible —\n"
           "           more frames le sometimes bढi faults dinxa.\n"
           "  • LRU:   Temporal locality use garxa, generally FIFO bhandaa ramro.\n"
           "           Tara last_used track garna overhead bढi hunxa.\n"
           "  • OPT:   Theoretical best — real OS ma future known huna parcha\n"
           "           so implement garna mildaina. Benchmark ko rupa ma use hunxa.\n"
           RESET);
}


/* ================================================================
 *  TEST CASE 2 — Different reference string sita test garna
 *  Belady's Anomaly dekhauxa: FIFO ma frames bढayo bhane
 *  faults bढna sakxa (weird behaviour)
 * ================================================================ */
static void run_test_case_2(void)
{
    printf(BOLD BLUE
           "\n╔══════════════════════════════════════════════════════╗\n"
           "║     Test Case 2: Belady's Anomaly Demo (FIFO)       ║\n"
           "╚══════════════════════════════════════════════════════╝\n"
           RESET);
    printf(DIM "  Classic Belady's Anomaly reference string:\n"
               "  3 frames vs 4 frames — FIFO ma faults bढxa!\n\n" RESET);

    /* Belady's Anomaly ko classic example */
    int belady_ref[] = {1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5};
    int len = 12;

    /* 3 frames sita FIFO */
    Frame f3[3];
    for (int i = 0; i < 3; i++) {
        f3[i].page_id = -1; f3[i].loaded_at = 0;
        f3[i].last_used = 0; memset(f3[i].data, 0, PAGE_SIZE);
    }
    int faults3 = 0, tick = 0;
    printf(BOLD "  [3 Frames FIFO]\n" RESET);
    printf("  Ref  │");
    for (int i = 0; i < 3; i++) printf(" Frame%d │", i);
    printf(" Result\n");
    printf(DIM "  ─────┼─────────┼─────────┼─────────┼─────────\n" RESET);

    for (int i = 0; i < len; i++) {
        int ref = belady_ref[i]; tick++;
        int hit = 0;
        for (int j = 0; j < 3; j++)
            if (f3[j].page_id == ref) { f3[j].last_used = tick; hit = 1; break; }

        if (!hit) {
            faults3++;
            int slot = -1;
            for (int j = 0; j < 3; j++) if (f3[j].page_id == -1) { slot = j; break; }
            if (slot == -1) {
                int oldest = 0;
                for (int j = 1; j < 3; j++)
                    if (f3[j].loaded_at < f3[oldest].loaded_at) oldest = j;
                slot = oldest;
            }
            f3[slot].page_id = ref; f3[slot].loaded_at = tick; f3[slot].last_used = tick;
        }
        printf("  P%-3d │", ref);
        for (int j = 0; j < 3; j++) {
            if (f3[j].page_id == -1) printf(DIM " [ — ]  │" RESET);
            else if (f3[j].page_id == ref) printf(CYAN " [ P%d ]  │" RESET, f3[j].page_id);
            else printf(GREEN " [ P%d ]  │" RESET, f3[j].page_id);
        }
        printf(hit ? GREEN " HIT\n" RESET : RED " FAULT\n" RESET);
    }
    printf(BOLD "  3 Frames: %d faults\n" RESET, faults3);

    /* 4 frames sita FIFO */
    Frame f4[4];
    for (int i = 0; i < 4; i++) {
        f4[i].page_id = -1; f4[i].loaded_at = 0;
        f4[i].last_used = 0; memset(f4[i].data, 0, PAGE_SIZE);
    }
    int faults4 = 0; tick = 0;
    printf(BOLD "\n  [4 Frames FIFO]\n" RESET);
    printf("  Ref  │");
    for (int i = 0; i < 4; i++) printf(" Frame%d │", i);
    printf(" Result\n");
    printf(DIM "  ─────┼─────────┼─────────┼─────────┼─────────┼─────────\n" RESET);

    for (int i = 0; i < len; i++) {
        int ref = belady_ref[i]; tick++;
        int hit = 0;
        for (int j = 0; j < 4; j++)
            if (f4[j].page_id == ref) { f4[j].last_used = tick; hit = 1; break; }

        if (!hit) {
            faults4++;
            int slot = -1;
            for (int j = 0; j < 4; j++) if (f4[j].page_id == -1) { slot = j; break; }
            if (slot == -1) {
                int oldest = 0;
                for (int j = 1; j < 4; j++)
                    if (f4[j].loaded_at < f4[oldest].loaded_at) oldest = j;
                slot = oldest;
            }
            f4[slot].page_id = ref; f4[slot].loaded_at = tick; f4[slot].last_used = tick;
        }
        printf("  P%-3d │", ref);
        for (int j = 0; j < 4; j++) {
            if (f4[j].page_id == -1) printf(DIM " [ — ]  │" RESET);
            else if (f4[j].page_id == ref) printf(CYAN " [ P%d ]  │" RESET, f4[j].page_id);
            else printf(GREEN " [ P%d ]  │" RESET, f4[j].page_id);
        }
        printf(hit ? GREEN " HIT\n" RESET : RED " FAULT\n" RESET);
    }
    printf(BOLD "  4 Frames: %d faults\n" RESET, faults4);

    /* Anomaly reveal */
    if (faults4 > faults3)
        printf(RED BOLD
               "\n  !! BELADY'S ANOMALY DEKHIYO !!\n"
               "  4 frames sita (%d faults) > 3 frames sita (%d faults)\n"
               "  FIFO ma more memory = more faults — counterintuitive!\n" RESET,
               faults4, faults3);
    else
        printf(CYAN "\n  Is ref string ma Belady's anomaly dekhiena.\n"
               "  Classic anomaly string sita FIFO ma hunxa yo.\n" RESET);
}


/* ================================================================
 *  MAIN — sabai algorithms chalauxa, comparison print garxa
 * ================================================================ */
int main(void)
{
    /*
     * Reference string: CPU le yo order ma pages access garxa.
     * Pages 0–4 xa (5 distinct virtual pages).
     * FRAME_COUNT = 4 frames matra, tara 5 wota pages xa —
     * so thrashing hunchha! Yo choose gareko chai FIFO vs LRU
     * difference clearly dekhauxa.
     */
    int ref_string[REF_LENGTH] = {0, 1, 2, 3, 0, 1, 4, 0, 1, 2, 3, 4};

    printf(BOLD BLUE
           "\n╔══════════════════════════════════════════════════════╗\n"
           "║  ST5004CEM — Task 2: Memory Management Simulation  ║\n"
           "╚══════════════════════════════════════════════════════╝\n"
           RESET);
    printf(DIM
           "  Virtual memory simulator with page replacement algorithms.\n"
           "  Testing: FIFO, LRU, and Optimal on same reference string.\n\n" RESET);

    /* Algorithm 1: FIFO */
    Stats fifo_stats = run_fifo(ref_string);
    print_stats("FIFO", fifo_stats);

    printf("\n\n");

    /* Algorithm 2: LRU */
    Stats lru_stats = run_lru(ref_string);
    print_stats("LRU", lru_stats);

    printf("\n\n");

    /* Algorithm 3: Optimal (bonus) */
    Stats opt_stats = run_optimal(ref_string);
    print_stats("OPT", opt_stats);

    /* Head-to-head comparison */
    print_comparison(fifo_stats, lru_stats, opt_stats);

    /* Test Case 2: Belady's Anomaly demo */
    run_test_case_2();

    printf(GREEN BOLD
           "\n═══════════════════════════════════════════════════════\n"
           "  Task 2 ka sabai algorithms successfully complete bhayo!\n"
           "═══════════════════════════════════════════════════════\n\n" RESET);

    return 0;
}
