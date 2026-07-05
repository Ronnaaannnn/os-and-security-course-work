# Task 1 — Process Management and Threading
## ST5004CEM | Operating Systems and Security
**Language:** C | **Library:** POSIX pthreads (`-lpthread`)

## What This Program Covers

| Section | Feature | Marks Criteria |
|---|---|---|
| 1 | 3 threads scheduled round-robin with live colour progress display | Threading + scheduling |
| 2 | Race condition: unsafe vs safe counter increment (4 threads × 100k) | Synchronisation |
| 3 | Producer/Consumer: bounded buffer + DB connection pool limiter | Mutex + cond vars |
| 4 | Scheduler stats table: per-quantum Gantt + waiting/turnaround times | Process scheduling |

Deadlock prevention (resource ordering A→B) is woven into Section 1 — every thread acquires `resource_A` before `resource_B`, eliminating circular wait.

```
╔══════════════════════════════════════════════════════╗
║  ST5004CEM — Task 1: Process Management & Threading ║
╚══════════════════════════════════════════════════════╝

── Thread Status ────────────────────────────────────
  Thread        Rounds    Progress                State
  Thread-0      1 / 3     [██████░░░░░░░░░░░░░░]  RUNNING
  Thread-1      0 / 3     [░░░░░░░░░░░░░░░░░░░░]  WAITING
  Thread-2      0 / 3     [░░░░░░░░░░░░░░░░░░░░]  WAITING
  Shared counter : 1 / 9

── SECTION 2: Race Condition Demo ──────────────────
  [UNSAFE]  expected 400000, got ~200000  (lost increments!)
  [SAFE]    expected 400000, got 400000   (mutex works!)

── SECTION 3: Producer / Consumer ──────────────────
  [Producer]    produced item-0
  [Consumer-0]  processing item-0  (DB connection held, pool: 1/2)
  ...

── SECTION 4: Round-Robin Scheduler Stats ──────────
  Clock   Process         Ran(ms)   Remaining
  3       BrowserTab      3         7
  ...
  Average waiting time:    19.6 ms
  Average turnaround time: 26.2 ms
```


