/*
 * ============================================================
 * ST5004CEM - Operating Systems and Security
 * Task 1: Process Management and Threading
 * Author  : [Ronan Chhetri]
 * ID      : [240587]
 *
 * Compatible with: Linux AND macOS (Apple Silicon / Intel)
 * ============================================================
 *
 * Demonstrates:
 *   1. Multiple threads performing concurrent tasks (3+ threads)
 *   2. Synchronisation via POSIX mutexes and condition variables
 *   3. Round-Robin scheduler simulation
 *   4. Race condition demo (unsafe vs safe)
 *   5. Deadlock prevention via resource ordering
 *
 * Compile (macOS):
 *   gcc -Wall -Wextra -o task1 task1_process_threading.c -lpthread
 *
 * Compile (Linux):
 *   gcc -Wall -Wextra -o task1 task1_process_threading.c -lpthread
 *
 * Run:
 *   ./task1
 * ============================================================
 */
/*
 * ================================================================
 * ST5004CEM — Operating Systems and Security
 * Task 1: Process Management and Threading
 *
 * Author : [Your Name]
 * ID     : [Your Student ID]
 * ================================================================
 *
 * WHAT THIS PROGRAM DEMONSTRATES
 * ─────────────────────────────────────────────────────────────
 *  Section 1 │ 3 worker threads scheduled round-robin
 *  Section 2 │ Mutex-protected shared counter (race condition fix)
 *  Section 3 │ Producer/Consumer with bounded buffer
 *  Section 4 │ Round-robin CPU scheduler stats (Gantt-style)
 *  Section 5 │ Deadlock prevention via resource ordering
 *
 * NOTE ON macOS COMPATIBILITY
 *   macOS does not support sem_init() for unnamed semaphores.
 *   This program uses pthread_cond_t + pthread_mutex_t everywhere,
 *   so it compiles and runs on both macOS and Linux without changes.
 *
 * COMPILE:
 *   gcc -Wall -Wextra -o task1 task1_process_threading.c -lpthread
 *
 * RUN:
 *   ./task1
 * ================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

/* ── ANSI colours ───────────────────────────────────────────── */
#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define DIM     "\033[2m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define CYAN    "\033[36m"

/* One colour label per worker thread */
static const char *T_COLOR[3] = {CYAN, YELLOW, "\033[35m"};  /* cyan/yellow/magenta */
static const char *T_NAME[3]  = {"Thread-0", "Thread-1", "Thread-2"};

/* ── Timestamp helper ───────────────────────────────────────── */
static void print_ts(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    time_t raw = ts.tv_sec;
    struct tm *t = localtime(&raw);
    printf(DIM "%02d:%02d:%02d.%03ld  " RESET,
           t->tm_hour, t->tm_min, t->tm_sec,
           ts.tv_nsec / 1000000L);
}

/* ================================================================
 *  SECTION 1 — ROUND-ROBIN WORKER THREADS
 *
 *  Three threads share a single CPU turn-token (current_turn).
 *  Only the thread whose ID matches current_turn may run.
 *  After finishing its time-slice it increments current_turn
 *  and broadcasts to wake the others. This is the classic
 *  cooperative round-robin pattern.
 *
 *  Primitives used:
 *    pthread_mutex_t scheduler_mutex — protects current_turn
 *    pthread_cond_t  turn_cond       — threads sleep here while
 *                                      waiting for their turn
 *
 *  Deadlock prevention: resource_A and resource_B are always
 *  acquired in the SAME ORDER (A first, then B) by every thread.
 *  This breaks the "circular wait" Coffman condition so deadlock
 *  can never form — no matter how threads are interleaved.
 * ================================================================ */

#define NUM_THREADS  3
#define TIME_SLICE   1    /* seconds each thread works per round */
#define TOTAL_ROUNDS 3    /* rounds each thread must complete    */

/* Shared counter — accessed by all threads */
static int             shared_counter = 0;
static pthread_mutex_t counter_mutex  = PTHREAD_MUTEX_INITIALIZER;

/* Round-robin scheduler state */
static int             current_turn   = 0;
static pthread_mutex_t scheduler_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  turn_cond;      /* initialised in main() */

/* Resources for deadlock-prevention demo */
static pthread_mutex_t resource_A = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t resource_B = PTHREAD_MUTEX_INITIALIZER;

/* Per-thread argument + live state (used by display) */
typedef struct {
    int  thread_id;
    int  rounds_done;
    char state[16];   /* WAITING | RUNNING | COUNTER | RES-A | RES-B | DONE */
} ThreadArgs;

static ThreadArgs targs[NUM_THREADS];

/* ── Gantt-style progress bar ───────────────────────────────── */
static void print_bar(int done, int total, const char *color)
{
    int filled = (total > 0) ? (done * 20) / total : 0;
    printf("%s[", color);
    for (int i = 0; i < 20; i++)
        printf(i < filled ? "\xe2\x96\x88" : "\xe2\x96\x91");  /* █ or ░ */
    printf("]" RESET);
}

/* ── Live status display ────────────────────────────────────── */
static pthread_mutex_t ui_mutex = PTHREAD_MUTEX_INITIALIZER;

static void draw_status(void)
{
    printf("\n" BOLD BLUE "  ── Thread Status ──────────────────────────────────" RESET "\n");
    printf(BOLD "  %-12s  %-8s  %-22s  %s\n" RESET,
           "Thread", "Rounds", "Progress", "State");
    printf(DIM "  ────────────  ────────  ──────────────────────  ─────────\n" RESET);

    for (int i = 0; i < NUM_THREADS; i++) {
        /* choose state colour */
        const char *sc =
            strcmp(targs[i].state, "DONE")    == 0 ? GREEN  :
            strcmp(targs[i].state, "RUNNING") == 0 ? T_COLOR[i] :
            strcmp(targs[i].state, "COUNTER") == 0 ? YELLOW :
            strcmp(targs[i].state, "RES-A")   == 0 ? YELLOW :
            strcmp(targs[i].state, "RES-B")   == 0 ? YELLOW : DIM;

        printf("  %s%-12s" RESET "  " BOLD "%d / %d" RESET "     ",
               T_COLOR[i], T_NAME[i], targs[i].rounds_done, TOTAL_ROUNDS);
        print_bar(targs[i].rounds_done, TOTAL_ROUNDS, T_COLOR[i]);
        printf("  %s%-9s" RESET "\n", sc, targs[i].state);
    }
    printf(BOLD "  Shared counter : " RESET GREEN "%d" RESET
           DIM " / %d\n" RESET, shared_counter, NUM_THREADS * TOTAL_ROUNDS);
    printf(BOLD BLUE "  ─────────────────────────────────────────────────────" RESET "\n\n");
    fflush(stdout);
}

/* ── Worker thread function ─────────────────────────────────── */
static void *thread_task(void *arg)
{
    ThreadArgs *t  = (ThreadArgs *)arg;
    int         id = t->thread_id;

    for (int round = 0; round < TOTAL_ROUNDS; round++) {

        /* ── Step 1: wait for this thread's turn (round-robin) ── */
        pthread_mutex_lock(&ui_mutex);
        strcpy(t->state, "WAITING");
        pthread_mutex_unlock(&ui_mutex);

        pthread_mutex_lock(&scheduler_mutex);
        while (current_turn != id)
            pthread_cond_wait(&turn_cond, &scheduler_mutex);
        pthread_mutex_unlock(&scheduler_mutex);

        /* ── Step 2: simulate CPU work for one time-slice ─────── */
        pthread_mutex_lock(&ui_mutex);
        strcpy(t->state, "RUNNING");
        print_ts();
        printf("%s[%s]" RESET " Round %d — running for %ds\n",
               T_COLOR[id], T_NAME[id], round + 1, TIME_SLICE);
        draw_status();
        pthread_mutex_unlock(&ui_mutex);

        sleep(TIME_SLICE);

        /* ── Step 3: increment shared counter (critical section) ─ */
        pthread_mutex_lock(&ui_mutex);
        strcpy(t->state, "COUNTER");
        pthread_mutex_unlock(&ui_mutex);

        pthread_mutex_lock(&counter_mutex);   /* ← acquire mutex */
        shared_counter++;
        t->rounds_done++;
        pthread_mutex_unlock(&counter_mutex); /* ← release mutex */

        pthread_mutex_lock(&ui_mutex);
        print_ts();
        printf("%s[%s]" RESET " counter → %d\n",
               T_COLOR[id], T_NAME[id], shared_counter);
        pthread_mutex_unlock(&ui_mutex);

        /* ── Step 4: deadlock-safe two-resource access ─────────
         *
         *  All threads lock resource_A BEFORE resource_B.
         *  This fixed global order eliminates circular wait —
         *  no thread can hold B while waiting for A — so
         *  deadlock is structurally impossible.
         */
        pthread_mutex_lock(&ui_mutex);
        strcpy(t->state, "RES-A");
        print_ts();
        printf("%s[%s]" RESET " requesting resource_A → resource_B (ordered)\n",
               T_COLOR[id], T_NAME[id]);
        pthread_mutex_unlock(&ui_mutex);

        pthread_mutex_lock(&resource_A);      /* acquire A first  */
        pthread_mutex_lock(&ui_mutex);
        strcpy(t->state, "RES-B");
        pthread_mutex_unlock(&ui_mutex);

        pthread_mutex_lock(&resource_B);      /* then acquire B   */
        pthread_mutex_lock(&ui_mutex);
        print_ts();
        printf("%s[%s]" RESET " holding both resources — working safely\n",
               T_COLOR[id], T_NAME[id]);
        pthread_mutex_unlock(&ui_mutex);

        /* ... work that needs both resources would happen here ... */

        pthread_mutex_unlock(&resource_B);    /* release B first  */
        pthread_mutex_unlock(&resource_A);    /* then release A   */

        pthread_mutex_lock(&ui_mutex);
        print_ts();
        printf("%s[%s]" RESET " released resource_A and resource_B\n",
               T_COLOR[id], T_NAME[id]);
        pthread_mutex_unlock(&ui_mutex);

        /* ── Step 5: pass turn to next thread (round-robin) ────── */
        pthread_mutex_lock(&scheduler_mutex);
        current_turn = (current_turn + 1) % NUM_THREADS;  /* 0→1→2→0→… */
        pthread_cond_broadcast(&turn_cond);  /* wake all; each checks if it's their turn */
        pthread_mutex_unlock(&scheduler_mutex);
    }

    /* All rounds done */
    pthread_mutex_lock(&ui_mutex);
    strcpy(t->state, "DONE");
    print_ts();
    printf("%s[%s]" RESET GREEN " all rounds done!\n" RESET,
           T_COLOR[id], T_NAME[id]);
    pthread_mutex_unlock(&ui_mutex);

    return NULL;
}

/* ── Run the round-robin thread demo ───────────────────────── */
static void run_rr_threads(void)
{
    pthread_t threads[NUM_THREADS];

    /* Initialise per-thread state */
    for (int i = 0; i < NUM_THREADS; i++) {
        targs[i].thread_id   = i;
        targs[i].rounds_done = 0;
        strcpy(targs[i].state, "WAITING");
    }

    printf(BOLD BLUE
           "\n╔══════════════════════════════════════════════════════╗\n"
           "║  ST5004CEM — Task 1: Process Management & Threading ║\n"
           "╚══════════════════════════════════════════════════════╝\n" RESET);
    printf(BOLD "\n  Threads: %d  |  Time-slice: %ds  |  Rounds each: %d\n\n" RESET,
           NUM_THREADS, TIME_SLICE, TOTAL_ROUNDS);

    draw_status();

    /* Spawn all threads */
    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, thread_task, &targs[i]) != 0) {
            fprintf(stderr, RED "Error: could not create thread %d\n" RESET, i);
            exit(EXIT_FAILURE);
        }
        printf(DIM "  [Main] Thread-%d spawned\n" RESET, i);
    }

    /* Wait for all to finish */
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i], NULL);

    draw_status();
    printf(GREEN BOLD
           "  ✓ All %d threads completed. No deadlock occurred.\n"
           "  Final shared counter = %d  (expected %d = %d × %d)\n\n" RESET,
           NUM_THREADS, shared_counter,
           NUM_THREADS * TOTAL_ROUNDS, NUM_THREADS, TOTAL_ROUNDS);
}


/* ================================================================
 *  SECTION 2 — RACE CONDITION DEMONSTRATION
 *
 *  Runs the same increment (4 threads × 100,000 iterations) twice:
 *    UNSAFE — no lock  → increments are silently lost
 *    SAFE   — mutex    → always produces exactly 400,000
 *
 *  This concretely shows WHY mutex synchronisation is essential.
 * ================================================================ */

#define RACE_THREADS 4
#define RACE_ITERS   100000

static long            rc_unsafe = 0;
static long            rc_safe   = 0;
static pthread_mutex_t rc_lock   = PTHREAD_MUTEX_INITIALIZER;

static void *unsafe_inc(void *arg) {
    (void)arg;
    for (int i = 0; i < RACE_ITERS; i++) {
        long tmp = rc_unsafe;   /* read  ─┐ not atomic: another */
        rc_unsafe = tmp + 1;    /* write ─┘ thread can wedge in */
    }
    return NULL;
}

static void *safe_inc(void *arg) {
    (void)arg;
    for (int i = 0; i < RACE_ITERS; i++) {
        pthread_mutex_lock(&rc_lock);
            rc_safe++;           /* only one thread here at a time */
        pthread_mutex_unlock(&rc_lock);
    }
    return NULL;
}

static void demo_race_condition(void)
{
    pthread_t t[RACE_THREADS];
    long expected = (long)RACE_THREADS * RACE_ITERS;

    printf(BOLD BLUE "\n── SECTION 2: Race Condition Demo ─────────────────────\n" RESET);

    /* Unsafe run */
    rc_unsafe = 0;
    for (int i = 0; i < RACE_THREADS; i++) pthread_create(&t[i], NULL, unsafe_inc, NULL);
    for (int i = 0; i < RACE_THREADS; i++) pthread_join(t[i], NULL);
    printf(RED "  [UNSAFE]" RESET " expected %ld, got %ld  "
           RED "(lost %ld increments — race condition!)\n" RESET,
           expected, rc_unsafe, expected - rc_unsafe);

    /* Safe run */
    rc_safe = 0;
    for (int i = 0; i < RACE_THREADS; i++) pthread_create(&t[i], NULL, safe_inc, NULL);
    for (int i = 0; i < RACE_THREADS; i++) pthread_join(t[i], NULL);
    printf(GREEN "  [SAFE]  " RESET " expected %ld, got %ld  "
           GREEN "(no lost increments — mutex works!)\n\n" RESET,
           expected, rc_safe);
}


/* ================================================================
 *  SECTION 3 — PRODUCER / CONSUMER WITH BOUNDED BUFFER
 *
 *  A classic OS synchronisation problem.
 *  - 1 producer generates items into a fixed-size buffer.
 *  - 3 consumers take items and process them.
 *
 *  Bounded buffer is managed with:
 *    buf_mutex   — protects head/tail/count fields
 *    not_full    — producer waits here when buffer is full
 *    not_empty   — consumers wait here when buffer is empty
 *
 *  A second semaphore (db_cond / db_count) limits the number
 *  of consumers doing "database work" to MAX_DB_CONN at once,
 *  simulating a real connection pool.
 * ================================================================ */

#define BUF_SIZE     5
#define PC_CONSUMERS 3
#define MAX_DB_CONN  2
#define NUM_ITEMS    9
#define SENTINEL    -1

static int             pc_buf[BUF_SIZE];
static int             pc_head = 0, pc_tail = 0, pc_count = 0;
static pthread_mutex_t buf_mutex    = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  not_full;    /* initialised in main() */
static pthread_cond_t  not_empty;   /* initialised in main() */

/* DB connection pool (counts available slots) */
static int             db_count = MAX_DB_CONN;
static pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  db_avail;    /* initialised in main() */

/* --- Buffer operations --- */
static void buf_put(int item)
{
    pthread_mutex_lock(&buf_mutex);
    while (pc_count == BUF_SIZE)          /* wait if full */
        pthread_cond_wait(&not_full, &buf_mutex);
    pc_buf[pc_head] = item;
    pc_head = (pc_head + 1) % BUF_SIZE;
    pc_count++;
    pthread_cond_signal(&not_empty);      /* tell consumers */
    pthread_mutex_unlock(&buf_mutex);
}

static int buf_get(void)
{
    pthread_mutex_lock(&buf_mutex);
    while (pc_count == 0)                  /* wait if empty */
        pthread_cond_wait(&not_empty, &buf_mutex);
    int item = pc_buf[pc_tail];
    pc_tail = (pc_tail + 1) % BUF_SIZE;
    pc_count--;
    pthread_cond_signal(&not_full);        /* tell producer */
    pthread_mutex_unlock(&buf_mutex);
    return item;
}

static void db_acquire(void)
{
    pthread_mutex_lock(&db_mutex);
    while (db_count == 0)                 /* wait if pool exhausted */
        pthread_cond_wait(&db_avail, &db_mutex);
    db_count--;
    pthread_mutex_unlock(&db_mutex);
}

static void db_release(void)
{
    pthread_mutex_lock(&db_mutex);
    db_count++;
    pthread_cond_signal(&db_avail);
    pthread_mutex_unlock(&db_mutex);
}

/* --- Threads --- */
static void *producer(void *arg)
{
    (void)arg;
    for (int i = 0; i < NUM_ITEMS; i++) {
        usleep(80000);
        buf_put(i);
        pthread_mutex_lock(&ui_mutex);
        print_ts();
        printf(CYAN "  [Producer]" RESET " produced item-%d\n", i);
        pthread_mutex_unlock(&ui_mutex);
    }
    /* Send one sentinel per consumer so every consumer can exit */
    for (int i = 0; i < PC_CONSUMERS; i++) buf_put(SENTINEL);
    pthread_mutex_lock(&ui_mutex);
    print_ts();
    printf(CYAN "  [Producer]" RESET " all items sent, sentinels dispatched\n");
    pthread_mutex_unlock(&ui_mutex);
    return NULL;
}

static void *consumer(void *arg)
{
    int id = *(int *)arg;
    char name[16];
    snprintf(name, sizeof(name), "Consumer-%d", id);

    while (1) {
        int item = buf_get();
        if (item == SENTINEL) {
            pthread_mutex_lock(&ui_mutex);
            print_ts();
            printf(YELLOW "  [%s]" RESET " sentinel — exiting\n", name);
            pthread_mutex_unlock(&ui_mutex);
            break;
        }

        /* At most MAX_DB_CONN consumers do DB work at once */
        db_acquire();
        pthread_mutex_lock(&ui_mutex);
        print_ts();
        printf(YELLOW "  [%s]" RESET " processing item-%d  "
               DIM "(DB connection held, pool: %d/%d)\n" RESET,
               name, item, db_count, MAX_DB_CONN);
        pthread_mutex_unlock(&ui_mutex);

        usleep(150000);
        db_release();

        pthread_mutex_lock(&ui_mutex);
        print_ts();
        printf(YELLOW "  [%s]" RESET " done with item-%d\n", name, item);
        pthread_mutex_unlock(&ui_mutex);
    }
    return NULL;
}

static void run_producer_consumer(void)
{
    printf(BOLD BLUE "\n── SECTION 3: Producer / Consumer ─────────────────────\n" RESET);
    printf("   Buffer size: %d  |  Max concurrent DB connections: %d\n\n",
           BUF_SIZE, MAX_DB_CONN);

    pc_head = pc_tail = pc_count = 0;
    db_count = MAX_DB_CONN;

    pthread_t prod_t;
    pthread_t cons_t[PC_CONSUMERS];
    int ids[PC_CONSUMERS];

    pthread_create(&prod_t, NULL, producer, NULL);
    for (int i = 0; i < PC_CONSUMERS; i++) {
        ids[i] = i;
        pthread_create(&cons_t[i], NULL, consumer, &ids[i]);
    }
    pthread_join(prod_t, NULL);
    for (int i = 0; i < PC_CONSUMERS; i++) pthread_join(cons_t[i], NULL);

    printf(GREEN "\n  ✓ Producer/Consumer complete.\n\n" RESET);
}


/* ================================================================
 *  SECTION 4 — SCHEDULING STATS TABLE
 *
 *  Simulates a CPU scheduler (no threads needed — pure arithmetic).
 *  Prints a Gantt-style per-quantum table showing which process
 *  runs each tick, then computes waiting time and turnaround time
 *  for each process. These are the standard OS scheduler metrics.
 *
 *  Round-Robin rule: if a process is not finished after QUANTUM
 *  ticks, it goes back to the end of the ready queue.
 * ================================================================ */

#define MAX_PROCS 10
#define QUANTUM    3

typedef struct {
    int  pid;
    char name[20];
    int  burst;       /* total CPU time needed */
    int  remaining;
    int  wait;        /* waiting time (computed) */
    int  turnaround;  /* turnaround time (computed) */
} Proc;

typedef struct {
    Proc *q[MAX_PROCS];
    int   front, rear, size;
} Queue;

static void q_init(Queue *q) { q->front=0; q->rear=0; q->size=0; }
static int  q_empty(Queue *q){ return q->size==0; }
static void q_push(Queue *q, Proc *p) {
    q->q[q->rear] = p; q->rear=(q->rear+1)%MAX_PROCS; q->size++;
}
static Proc *q_pop(Queue *q) {
    Proc *p = q->q[q->front]; q->front=(q->front+1)%MAX_PROCS; q->size--; return p;
}

static void run_scheduler_stats(void)
{
    printf(BOLD BLUE "\n── SECTION 4: Round-Robin Scheduler Stats ─────────────\n" RESET);
    printf("   Quantum = %d ms\n\n", QUANTUM);

    Proc procs[5] = {
        {1, "BrowserTab",   10, 0, 0, 0},
        {2, "VideoEncode",   7, 0, 0, 0},
        {3, "FileIndexer",   4, 0, 0, 0},
        {4, "SpellCheck",    9, 0, 0, 0},
        {5, "SyncService",   3, 0, 0, 0},
    };
    int n = 5;
    for (int i = 0; i < n; i++) procs[i].remaining = procs[i].burst;

    Queue rq; q_init(&rq);
    for (int i = 0; i < n; i++) q_push(&rq, &procs[i]);

    printf("  %-6s  %-16s  %-8s  %-10s\n", "Clock", "Process", "Ran(ms)", "Remaining");
    printf("  " DIM "──────  ────────────────  ────────  ──────────\n" RESET);

    int clock=0;
    Proc *done[MAX_PROCS]; int ndone=0;

    while (!q_empty(&rq)) {
        Proc *p = q_pop(&rq);
        int run = (p->remaining < QUANTUM) ? p->remaining : QUANTUM;
        p->remaining -= run;
        clock += run;

        printf("  %-6d  %-16s  %-8d  %-10d\n",
               clock, p->name, run, p->remaining);

        if (p->remaining == 0) {
            p->turnaround = clock;
            p->wait       = clock - p->burst;
            done[ndone++] = p;
            printf("  " GREEN "✓ %-16s FINISHED  (turnaround=%dms, waiting=%dms)\n" RESET,
                   p->name, p->turnaround, p->wait);
        } else {
            q_push(&rq, p);
        }
    }

    printf("\n  " BOLD "%-16s  %-10s  %-12s  %s\n" RESET,
           "Process", "Burst(ms)", "Waiting(ms)", "Turnaround(ms)");
    double tw=0, tta=0;
    for (int i=0; i<ndone; i++) {
        printf("  %-16s  %-10d  %-12d  %d\n",
               done[i]->name, done[i]->burst, done[i]->wait, done[i]->turnaround);
        tw += done[i]->wait; tta += done[i]->turnaround;
    }
    printf("\n  " BOLD "Average waiting time   : " RESET "%.1f ms\n", tw/ndone);
    printf("  " BOLD "Average turnaround time: " RESET "%.1f ms\n\n", tta/ndone);
}


/* ================================================================
 *  MAIN
 * ================================================================ */
int main(void)
{
    /* Initialise condition variables (cannot use static initialisers) */
    pthread_cond_init(&turn_cond, NULL);
    pthread_cond_init(&not_full,  NULL);
    pthread_cond_init(&not_empty, NULL);
    pthread_cond_init(&db_avail,  NULL);

    /* Section 1: round-robin worker threads */
    run_rr_threads();

    /* Section 2: race condition demo */
    demo_race_condition();

    /* Section 3: producer / consumer */
    run_producer_consumer();

    /* Section 4: scheduler stats (no threads — pure simulation) */
    run_scheduler_stats();

    printf(GREEN BOLD
           "═══════════════════════════════════════════════════════\n"
           "  All Task 1 demonstrations completed successfully.\n"
           "═══════════════════════════════════════════════════════\n\n" RESET);

    /* Destroy dynamically initialised condition variables */
    pthread_cond_destroy(&turn_cond);
    pthread_cond_destroy(&not_full);
    pthread_cond_destroy(&not_empty);
    pthread_cond_destroy(&db_avail);

    /* Destroy all mutexes */
    pthread_mutex_destroy(&counter_mutex);
    pthread_mutex_destroy(&scheduler_mutex);
    pthread_mutex_destroy(&resource_A);
    pthread_mutex_destroy(&resource_B);
    pthread_mutex_destroy(&ui_mutex);
    pthread_mutex_destroy(&rc_lock);
    pthread_mutex_destroy(&buf_mutex);
    pthread_mutex_destroy(&db_mutex);

    return 0;
}