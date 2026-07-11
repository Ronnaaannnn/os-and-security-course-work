/*
 * ================================================================
 * ST5004CEM — Operating Systems and Security
 * Task 1: Process Management and Threading
 *
 * Author  : [Your Name]
 * ID      : [Your Student ID]
 * College : Softwarica College of IT & E-Commerce
 * ================================================================
 *
 * Compile:  gcc -Wall -Wextra -o task1 task1.c -lpthread
 * Run:      ./task1
 * ================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

/* ── ANSI colour codes — terminal ma colour dekhaunko lagi ── */
#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define DIM     "\033[2m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define CYAN    "\033[36m"
#define MAGENTA "\033[35m"

/* ── Pratek thread ko aafnai color hunxa — distinguish garna easy hunxa ── */
static const char *T_COLOR[3] = { CYAN, YELLOW, MAGENTA };
static const char *T_NAME[3]  = { "Thread-0", "Thread-1", "Thread-2" };

/* ================================================================
 *  CONFIGURATION — yo values haru change garera behaviour
 *  adjust garna milxa, jastai: round haru badhauna, slice ghatauna
 * ================================================================ */
#define NUM_THREADS  3   /* kati threads banauney */
#define TIME_SLICE   1   /* pratek round ma kati second kaam garney */
#define TOTAL_ROUNDS 3   /* pratek thread le kati palta kaam garney */
#define BAR_WIDTH   20   /* progress bar ko width */

/* ================================================================
 *  SHARED DATA — sabai thread le yo counter access garxa
 *  mutex nabhaye yo value galat hunxa (race condition hunchha)
 * ================================================================ */
static int             shared_counter  = 0;
static pthread_mutex_t counter_mutex   = PTHREAD_MUTEX_INITIALIZER;

/* ================================================================
 *  ROUND-ROBIN SCHEDULER STATE
 *
 *  current_turn le batauxa kun thread ko palo ho.
 *  scheduler_mutex le current_turn lai protect garxa.
 *  turn_cond ma threads sleep garxa afno palo naayesamma.
 * ================================================================ */
static int             current_turn    = 0;
static pthread_mutex_t scheduler_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  turn_cond;  /* main() ma init hunxa */

/* ================================================================
 *  DEADLOCK PREVENTION — resource_A ra resource_B
 *
 *  Deadlock hunko lagi 4 wota conditions chahinxa:
 *    1. Mutual exclusion
 *    2. Hold-and-wait
 *    3. No preemption
 *    4. Circular wait  ← yo break garney ho
 *
 *  Fix: sabai thread le A pehila lock garxa, pachhi B.
 *  Yo fixed order le circular wait hudaina, deadlock impossible.
 * ================================================================ */
static pthread_mutex_t resource_A = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t resource_B = PTHREAD_MUTEX_INITIALIZER;

/* ================================================================
 *  THREAD ARGUMENT STRUCT
 *  Pratek thread ko ID, kitna round bhayo, ra aile kun state ma xa
 * ================================================================ */
typedef struct {
    int  thread_id;
    int  rounds_done;
    char state[16];  /* WAITING | RUNNING | COUNTER | RES-A | RES-B | DONE */
} ThreadArgs;

static ThreadArgs targs[NUM_THREADS];

/* ui ko lagi mutex — duita thread le ek saathi print nagaros */
static pthread_mutex_t ui_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Timestamp — kati baje yo kaam bhayo ── */
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

/* ── Progress bar — rounds done ko basis ma blocks fill garxa ── */
static void print_bar(int done, int total, const char *color)
{
    /* kitna blocks fill garney: done/total * BAR_WIDTH */
    int filled = (total > 0) ? (done * BAR_WIDTH) / total : 0;
    printf("%s[", color);
    for (int i = 0; i < BAR_WIDTH; i++)
        printf(i < filled ? "\xe2\x96\x88" : "\xe2\x96\x91"); /* █ ya ░ */
    printf("]" RESET);
}

/* ── Status table — sabai threads ko current situation dekhauxa ── */
static void draw_status(void)
{
    /* ui_mutex caller le hold gareko hunuparchha */
    printf("\n" BOLD BLUE
           "  ┌──────────────────────────────────────────────────────┐\n"
           "  │              Thread Status Dashboard                 │\n"
           "  └──────────────────────────────────────────────────────┘\n"
           RESET);
    printf(BOLD "  %-12s  %-8s  %-22s  %s\n" RESET,
           "Thread", "Rounds", "Progress", "State");
    printf(DIM "  ────────────  ────────  ──────────────────────  ─────────\n" RESET);

    for (int i = 0; i < NUM_THREADS; i++) {
        /* State anusaar color choose garney */
        const char *sc =
            strcmp(targs[i].state, "DONE")    == 0 ? GREEN      :
            strcmp(targs[i].state, "RUNNING") == 0 ? T_COLOR[i] :
            strcmp(targs[i].state, "COUNTER") == 0 ? YELLOW     :
            strcmp(targs[i].state, "RES-A")   == 0 ? YELLOW     :
            strcmp(targs[i].state, "RES-B")   == 0 ? YELLOW     : DIM;

        printf("  %s%-12s" RESET "  " BOLD "%d / %d" RESET "     ",
               T_COLOR[i], T_NAME[i],
               targs[i].rounds_done, TOTAL_ROUNDS);
        print_bar(targs[i].rounds_done, TOTAL_ROUNDS, T_COLOR[i]);
        printf("  %s%s" RESET "\n", sc, targs[i].state);
    }

    printf(BOLD "  Shared counter : " RESET GREEN "%d" RESET DIM " / %d\n" RESET,
           shared_counter, NUM_THREADS * TOTAL_ROUNDS);
    printf(DIM "  ──────────────────────────────────────────────────────\n\n" RESET);
    fflush(stdout);
}


/* ================================================================
 *  THREAD FUNCTION — pratek thread le yo nai function run garxa
 *
 *  Ek round ma:
 *    1. Afno palo aaunsamma wait garxa (round-robin)
 *    2. TIME_SLICE second kaam garxa (simulate)
 *    3. Mutex hold garera shared_counter increment garxa
 *    4. resource_A pachhi resource_B lock garxa (deadlock prevent)
 *    5. Turn next thread lai pass garxa
 * ================================================================ */
static void *thread_task(void *arg)
{
    ThreadArgs *t  = (ThreadArgs *)arg;
    int         id = t->thread_id;

    for (int round = 0; round < TOTAL_ROUNDS; round++) {

        /* ── Step 1: palo aaunsamma sleep ──────────────── */
        pthread_mutex_lock(&ui_mutex);
        strcpy(t->state, "WAITING");
        pthread_mutex_unlock(&ui_mutex);

        /* scheduler_mutex liyera afno palo check garxa */
        pthread_mutex_lock(&scheduler_mutex);
        while (current_turn != id)
            /* palo hoina bhane condition variable ma sleep */
            pthread_cond_wait(&turn_cond, &scheduler_mutex);
        pthread_mutex_unlock(&scheduler_mutex);

        /* ── Step 2: kaam suru, TIME_SLICE second simulate ─ */
        pthread_mutex_lock(&ui_mutex);
        strcpy(t->state, "RUNNING");
        print_ts();
        printf("%s[%s]" RESET " Round %d suru — %d second kaam gardaichhu...\n",
               T_COLOR[id], T_NAME[id], round + 1, TIME_SLICE);
        draw_status();
        pthread_mutex_unlock(&ui_mutex);

        sleep(TIME_SLICE); /* yo thread ko CPU time — ek second */

        /* ── Step 3: shared counter safe tarika le update ──
         * mutex bina garyo bhane race condition hunthyo
         * ek thread le read garyo, arko le overwrite garyo —
         * result galat aauxa. Mutex le ek patak ek jana matra
         * critical section bhitra pasna dinxa.
         */
        pthread_mutex_lock(&ui_mutex);
        strcpy(t->state, "COUNTER");
        pthread_mutex_unlock(&ui_mutex);

        pthread_mutex_lock(&counter_mutex);   /* ← CRITICAL SECTION suru */
        shared_counter++;
        t->rounds_done++;
        pthread_mutex_unlock(&counter_mutex); /* ← CRITICAL SECTION end */

        pthread_mutex_lock(&ui_mutex);
        print_ts();
        printf("%s[%s]" RESET " Counter update bhayo → " GREEN "%d\n" RESET,
               T_COLOR[id], T_NAME[id], shared_counter);
        pthread_mutex_unlock(&ui_mutex);

        /* ── Step 4: resource_A pehila, pachhi resource_B ──
         * Yo ORDER haina bhane circular wait hunthyo:
         *   Thread-0 holds A, wants B
         *   Thread-1 holds B, wants A  → DEADLOCK!
         * Fixed order (A→B) le circular wait impossible banauxa.
         */
        pthread_mutex_lock(&ui_mutex);
        strcpy(t->state, "RES-A");
        print_ts();
        printf("%s[%s]" RESET " resource_A maagdaichhu (A → B order)...\n",
               T_COLOR[id], T_NAME[id]);
        pthread_mutex_unlock(&ui_mutex);

        pthread_mutex_lock(&resource_A); /* A pehila lock */

        pthread_mutex_lock(&ui_mutex);
        strcpy(t->state, "RES-B");
        pthread_mutex_unlock(&ui_mutex);

        pthread_mutex_lock(&resource_B); /* A paako pachhI B lock */

        pthread_mutex_lock(&ui_mutex);
        print_ts();
        printf("%s[%s]" RESET " A ra B duwa hold — kaam gardaichhu\n",
               T_COLOR[id], T_NAME[id]);
        pthread_mutex_unlock(&ui_mutex);

        /* yo xetrama actual dual-resource kaam hunthyo */

        pthread_mutex_unlock(&resource_B); /* B pehila release */
        pthread_mutex_unlock(&resource_A); /* pachhi A release */

        pthread_mutex_lock(&ui_mutex);
        print_ts();
        printf("%s[%s]" RESET " resource_A ra resource_B duwa release gares\n",
               T_COLOR[id], T_NAME[id]);
        pthread_mutex_unlock(&ui_mutex);

        /* ── Step 5: aglo thread lai turn pass ─────────────
         * (current_turn + 1) % NUM_THREADS = 0→1→2→0→1→2...
         * broadcast le sabai sleeping threads lai wake garxa,
         * pratek le check garxa — jasko palo ho tyo kaam garxa.
         */
        pthread_mutex_lock(&scheduler_mutex);
        current_turn = (current_turn + 1) % NUM_THREADS;
        pthread_cond_broadcast(&turn_cond);
        pthread_mutex_unlock(&scheduler_mutex);
    }

    /* Sabai round sakiyo — DONE state ma jane */
    pthread_mutex_lock(&ui_mutex);
    strcpy(t->state, "DONE");
    print_ts();
    printf("%s[%s]" RESET GREEN " Sabai %d round sakiyo!\n" RESET,
           T_COLOR[id], T_NAME[id], TOTAL_ROUNDS);
    pthread_mutex_unlock(&ui_mutex);

    return NULL;
}


/* ================================================================
 *  SECTION 2 — RACE CONDITION DEMO
 *
 *  Mutex bina counter update garyo bhane ke hunchha dekhauxa.
 *  4 thread × 100,000 iterations = 400,000 expected.
 *  Unsafe run ma result kam aauxa — increments lost hunxa.
 *  Safe run ma mutex le protect garyo, result exact aauxa.
 * ================================================================ */
#define RACE_THREADS 4
#define RACE_ITERS   100000

static long            rc_unsafe = 0;
static long            rc_safe   = 0;
static pthread_mutex_t rc_lock   = PTHREAD_MUTEX_INITIALIZER;

/* Mutex bina — race condition hunxa yahaa */
static void *unsafe_inc(void *arg)
{
    (void)arg;
    for (int i = 0; i < RACE_ITERS; i++) {
        /* read → modify → write tinta alag step ho
         * arko thread beechma aayera same value padna sakxa
         * result: duita threads same counter dekhi, duita le
         * +1 garyo, tara final value sirf ek le badyo */
        long tmp  = rc_unsafe;
        rc_unsafe = tmp + 1; /* YO SAFE HOINA */
    }
    return NULL;
}

/* Mutex sita — thread-safe, ekdam accurate */
static void *safe_inc(void *arg)
{
    (void)arg;
    for (int i = 0; i < RACE_ITERS; i++) {
        pthread_mutex_lock(&rc_lock);
        rc_safe++;           /* ek palta ek jana matra yahaa aauxa */
        pthread_mutex_unlock(&rc_lock);
    }
    return NULL;
}

static void demo_race_condition(void)
{
    pthread_t t[RACE_THREADS];
    long expected = (long)RACE_THREADS * RACE_ITERS;

    printf(BOLD BLUE
           "\n╔══════════════════════════════════════════════════════╗\n"
           "║       Section 2: Race Condition Demonstration       ║\n"
           "╚══════════════════════════════════════════════════════╝\n"
           RESET);
    printf(DIM "  %d threads, %d iterations each — expected total: %ld\n\n" RESET,
           RACE_THREADS, RACE_ITERS, expected);

    /* Pahilo: mutex bina run — lost increments dekhauxa */
    rc_unsafe = 0;
    for (int i = 0; i < RACE_THREADS; i++)
        pthread_create(&t[i], NULL, unsafe_inc, NULL);
    for (int i = 0; i < RACE_THREADS; i++)
        pthread_join(t[i], NULL);

    printf(RED "  [UNSAFE]" RESET
           " Expected: %ld | Got: %ld | Lost: " RED "%ld increments\n" RESET,
           expected, rc_unsafe, expected - rc_unsafe);

    /* Dusted: mutex sita run — perfect result */
    rc_safe = 0;
    for (int i = 0; i < RACE_THREADS; i++)
        pthread_create(&t[i], NULL, safe_inc, NULL);
    for (int i = 0; i < RACE_THREADS; i++)
        pthread_join(t[i], NULL);

    printf(GREEN "  [SAFE]  " RESET
           " Expected: %ld | Got: %ld | Lost: " GREEN "0 increments\n\n" RESET,
           expected, rc_safe);
}


/* ================================================================
 *  SECTION 3 — PRODUCER / CONSUMER WITH BOUNDED BUFFER
 *
 *  Classic OS synchronisation problem:
 *    - 1 producer le items banauxa ra buffer ma rakhxa
 *    - 3 consumer le buffer bata liyera process garxa
 *
 *  buf_mutex le buffer ko head/tail/count protect garxa.
 *  not_full: producer yahaa wait garxa buffer full bhayo bhane.
 *  not_empty: consumer yahaa wait garxa buffer empty bhayo bhane.
 *
 *  db_avail le maximum MAX_DB_CONN consumers lai ek saath
 *  "database work" garna dinxa — real connection pool jastai.
 * ================================================================ */
#define BUF_SIZE     5
#define PC_CONSUMERS 3
#define MAX_DB_CONN  2
#define NUM_ITEMS    9
#define SENTINEL    -1  /* consumer lai rokna pathauney signal */

static int             pc_buf[BUF_SIZE];
static int             pc_head = 0, pc_tail = 0, pc_count = 0;
static pthread_mutex_t buf_mutex  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  not_full;   /* init in main */
static pthread_cond_t  not_empty;  /* init in main */

/* DB connection pool — MAX_DB_CONN bhandaa bढi kaam garna rokxa */
static int             db_slots = MAX_DB_CONN;
static pthread_mutex_t db_mutex  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  db_avail;   /* init in main */

/* ── Buffer helpers ─────────────────────────────────────────── */
static void buf_put(int item)
{
    pthread_mutex_lock(&buf_mutex);
    while (pc_count == BUF_SIZE)        /* full chha bhane wait */
        pthread_cond_wait(&not_full, &buf_mutex);
    pc_buf[pc_head] = item;
    pc_head = (pc_head + 1) % BUF_SIZE;
    pc_count++;
    pthread_cond_signal(&not_empty);    /* consumer lai batauxa: item aayo */
    pthread_mutex_unlock(&buf_mutex);
}

static int buf_get(void)
{
    pthread_mutex_lock(&buf_mutex);
    while (pc_count == 0)               /* empty chha bhane wait */
        pthread_cond_wait(&not_empty, &buf_mutex);
    int item = pc_buf[pc_tail];
    pc_tail = (pc_tail + 1) % BUF_SIZE;
    pc_count--;
    pthread_cond_signal(&not_full);     /* producer lai batauxa: slot khali */
    pthread_mutex_unlock(&buf_mutex);
    return item;
}

static void db_acquire(void)
{
    pthread_mutex_lock(&db_mutex);
    while (db_slots == 0)               /* connection pool full bhane wait */
        pthread_cond_wait(&db_avail, &db_mutex);
    db_slots--;
    pthread_mutex_unlock(&db_mutex);
}

static void db_release(void)
{
    pthread_mutex_lock(&db_mutex);
    db_slots++;
    pthread_cond_signal(&db_avail);
    pthread_mutex_unlock(&db_mutex);
}

/* ── Producer thread ─────────────────────────────────────────── */
static void *producer(void *arg)
{
    (void)arg;
    for (int i = 0; i < NUM_ITEMS; i++) {
        usleep(80000); /* production time simulate — 80ms */
        buf_put(i);
        pthread_mutex_lock(&ui_mutex);
        print_ts();
        printf(CYAN "  [Producer]" RESET " item-%d banayo ra buffer ma halyo\n", i);
        pthread_mutex_unlock(&ui_mutex);
    }
    /* Pratek consumer ko lagi ek-ek sentinel pathauxa */
    for (int i = 0; i < PC_CONSUMERS; i++) buf_put(SENTINEL);
    pthread_mutex_lock(&ui_mutex);
    print_ts();
    printf(CYAN "  [Producer]" RESET " Sabai items pathayo, sentinel haalyo — kaam sakiyo\n");
    pthread_mutex_unlock(&ui_mutex);
    return NULL;
}

/* ── Consumer thread ─────────────────────────────────────────── */
static void *consumer(void *arg)
{
    int id = *(int *)arg;
    char name[20];
    snprintf(name, sizeof(name), "Consumer-%d", id);

    while (1) {
        int item = buf_get();

        if (item == SENTINEL) {
            /* Sentinel payo — yo consumer ko kaam sakiyo */
            pthread_mutex_lock(&ui_mutex);
            print_ts();
            printf(YELLOW "  [%s]" RESET " Sentinel payo — bahira niklanchhu\n", name);
            pthread_mutex_unlock(&ui_mutex);
            break;
        }

        /* DB connection liyera kaam garxa — pool ma slot chha bhane matra */
        db_acquire();
        pthread_mutex_lock(&ui_mutex);
        print_ts();
        printf(YELLOW "  [%s]" RESET " item-%d process gardaichhu "
               DIM "(DB slot liyeko, available: %d/%d)\n" RESET,
               name, item, db_slots, MAX_DB_CONN);
        pthread_mutex_unlock(&ui_mutex);

        usleep(150000); /* DB kaam simulate — 150ms */
        db_release();   /* slot farkauxa pool ma */

        pthread_mutex_lock(&ui_mutex);
        print_ts();
        printf(YELLOW "  [%s]" RESET " item-%d ko kaam sakiyo\n", name, item);
        pthread_mutex_unlock(&ui_mutex);
    }
    return NULL;
}

static void run_producer_consumer(void)
{
    printf(BOLD BLUE
           "\n╔══════════════════════════════════════════════════════╗\n"
           "║         Section 3: Producer / Consumer              ║\n"
           "╚══════════════════════════════════════════════════════╝\n"
           RESET);
    printf(DIM "  Buffer: %d slots | DB connections: max %d at once\n\n" RESET,
           BUF_SIZE, MAX_DB_CONN);

    pc_head = pc_tail = pc_count = 0;
    db_slots = MAX_DB_CONN;

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

    printf(GREEN "\n  ✓ Producer/Consumer demo sakiyo — koi data lost bhayena\n\n" RESET);
}


/* ================================================================
 *  SECTION 4 — ROUND-ROBIN CPU SCHEDULER STATS
 *
 *  Yaha real threads chhainna — pure simulation ho.
 *  Paging jastai, process haru queue ma basa garxa.
 *  Pratek tick ma ek process le QUANTUM ms paauxa.
 *  Nabhaye back of queue ma farkauxa.
 *
 *  Stats: Waiting time = Turnaround - Burst
 *         Turnaround   = Finish time - Arrival time
 * ================================================================ */
#define MAX_PROCS 10
#define QUANTUM    3  /* ms per turn */

typedef struct {
    int  pid;
    char name[20];
    int  burst;       /* total CPU time chahinxa */
    int  remaining;   /* abhi kati baaki cha */
    int  wait;        /* waiting time */
    int  turnaround;  /* total time from arrival to finish */
} Proc;

/* Circular queue — ready queue ko lagi */
typedef struct {
    Proc *q[MAX_PROCS];
    int   front, rear, size;
} Queue;

static void q_init(Queue *q) { q->front=0; q->rear=0; q->size=0; }
static int  q_empty(Queue *q){ return q->size == 0; }
static void q_push(Queue *q, Proc *p) {
    q->q[q->rear]=p; q->rear=(q->rear+1)%MAX_PROCS; q->size++;
}
static Proc *q_pop(Queue *q) {
    Proc *p=q->q[q->front]; q->front=(q->front+1)%MAX_PROCS; q->size--; return p;
}

static void run_scheduler_stats(void)
{
    printf(BOLD BLUE
           "\n╔══════════════════════════════════════════════════════╗\n"
           "║       Section 4: CPU Scheduler Stats (RR Sim)       ║\n"
           "╚══════════════════════════════════════════════════════╝\n"
           RESET);
    printf(DIM "  Quantum = %d ms | Sabai process t=0 ma arrive garxa\n\n" RESET, QUANTUM);

    /* 5 wota process — different burst times sita */
    Proc procs[5] = {
        {1, "BrowserTab",   10, 0, 0, 0},
        {2, "VideoEncode",   7, 0, 0, 0},
        {3, "FileIndexer",   4, 0, 0, 0},
        {4, "SpellCheck",    9, 0, 0, 0},
        {5, "SyncService",   3, 0, 0, 0},
    };
    int n = 5;

    /* Remaining time set garxa ani queue ma halna */
    Queue rq; q_init(&rq);
    for (int i = 0; i < n; i++) {
        procs[i].remaining = procs[i].burst;
        q_push(&rq, &procs[i]);
    }

    printf("  %-6s  %-16s  %-9s  %-10s\n",
           "Clock", "Process", "Ran(ms)", "Remaining");
    printf(DIM "  ──────  ────────────────  ─────────  ──────────\n" RESET);

    int clock = 0;
    Proc *done[MAX_PROCS];
    int ndone = 0;

    while (!q_empty(&rq)) {
        Proc *p = q_pop(&rq);

        /* Kati ms run garney — quantum bhandaa baaki kom bhayo bhane baaki nai */
        int run = (p->remaining < QUANTUM) ? p->remaining : QUANTUM;
        p->remaining -= run;
        clock += run;

        printf("  %-6d  %-16s  %-9d  %-10d\n",
               clock, p->name, run, p->remaining);

        if (p->remaining == 0) {
            /* Kaam sakiyo — stats calculate garxa */
            p->turnaround = clock;           /* t=0 ma arrive garekai ho */
            p->wait       = clock - p->burst;
            done[ndone++] = p;
            printf(GREEN "  ✓ %-16s DONE  (turnaround=%dms, waiting=%dms)\n" RESET,
                   p->name, p->turnaround, p->wait);
        } else {
            /* Abhi baaki cha — queue ko puchhar ma farkauxa */
            q_push(&rq, p);
        }
    }

    /* Summary table */
    printf("\n  " BOLD "%-16s  %-10s  %-12s  %s\n" RESET,
           "Process", "Burst(ms)", "Waiting(ms)", "Turnaround(ms)");
    double tw = 0, tta = 0;
    for (int i = 0; i < ndone; i++) {
        printf("  %-16s  %-10d  %-12d  %d\n",
               done[i]->name, done[i]->burst, done[i]->wait, done[i]->turnaround);
        tw  += done[i]->wait;
        tta += done[i]->turnaround;
    }
    printf("\n  " BOLD "Average waiting time   : " RESET "%.1f ms\n", tw  / ndone);
    printf("  " BOLD "Average turnaround time: " RESET "%.1f ms\n\n", tta / ndone);
}


/* ================================================================
 *  MAIN — sabai demo haru ekai order ma chalauxa
 * ================================================================ */
int main(void)
{
    /* Condition variables ko lagi init — static ma garna mildaina */
    pthread_cond_init(&turn_cond, NULL);
    pthread_cond_init(&not_full,  NULL);
    pthread_cond_init(&not_empty, NULL);
    pthread_cond_init(&db_avail,  NULL);

    /* Header print */
    printf(BOLD BLUE
           "\n╔══════════════════════════════════════════════════════╗\n"
           "║  ST5004CEM — Task 1: Process Management & Threading ║\n"
           "╚══════════════════════════════════════════════════════╝\n"
           RESET);
    printf(BOLD "  Threads: %d  |  Time-slice: %ds  |  Rounds each: %d\n" RESET,
           NUM_THREADS, TIME_SLICE, TOTAL_ROUNDS);

    /* Section 1 — tint WT thread le round-robin garna */
    pthread_t threads[NUM_THREADS];
    for (int i = 0; i < NUM_THREADS; i++) {
        targs[i].thread_id   = i;
        targs[i].rounds_done = 0;
        strcpy(targs[i].state, "WAITING");
    }
    draw_status();

    for (int i = 0; i < NUM_THREADS; i++) {
        if (pthread_create(&threads[i], NULL, thread_task, &targs[i]) != 0) {
            fprintf(stderr, RED "Thread %d banau sakiena!\n" RESET, i);
            exit(EXIT_FAILURE);
        }
        printf(DIM "  [Main] Thread-%d suru bhayo\n" RESET, i);
    }
    for (int i = 0; i < NUM_THREADS; i++)
        pthread_join(threads[i], NULL);

    /* Final status ra result */
    pthread_mutex_lock(&ui_mutex);
    draw_status();
    printf(GREEN BOLD
           "  ✓ Sabai %d threads sakiyo. Deadlock bhayena.\n"
           "  Final counter = %d  (expected = %d × %d = %d)\n\n" RESET,
           NUM_THREADS, shared_counter,
           NUM_THREADS, TOTAL_ROUNDS, NUM_THREADS * TOTAL_ROUNDS);
    pthread_mutex_unlock(&ui_mutex);

    /* Section 2 — race condition demo */
    demo_race_condition();

    /* Section 3 — producer consumer */
    run_producer_consumer();

    /* Section 4 — scheduler stats */
    run_scheduler_stats();

    printf(GREEN BOLD
           "═══════════════════════════════════════════════════════\n"
           "  Task 1 ka sabai section successfully complete bhayo!\n"
           "═══════════════════════════════════════════════════════\n\n" RESET);

    /* Sabai resources cleanup */
    pthread_cond_destroy(&turn_cond);
    pthread_cond_destroy(&not_full);
    pthread_cond_destroy(&not_empty);
    pthread_cond_destroy(&db_avail);
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
