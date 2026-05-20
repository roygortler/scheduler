# User-Mode CPU Scheduler

A cooperative, priority-based task scheduler implemented in C using POSIX `ucontext`. It runs entirely in user space — no kernel threads, no OS scheduling — and manually switches between tasks using CPU context swapping.

## How it works

The scheduler maintains 6 priority queues (0 = lowest, 5 = highest). Tasks are picked from the highest non-empty queue and run until they voluntarily yield via `task_yield()`. When a task yields, it goes back into the queue and the scheduler picks the next highest-priority task.

To prevent low-priority tasks from waiting forever, a starvation prevention mechanism triggers every 400 ticks and bumps all waiting tasks up one priority level.

## Key concepts

| Concept | Description |
|--------|-------------|
| `TaskControlBlock (TCB)` | Stores a task's CPU context, stack, priority, and state |
| `TaskPool` | A pre-allocated memory pool — one big `malloc` split into fixed-size slots, avoids repeated allocations |
| `spawn_task()` | Finds a free slot in the pool and sets up a new task's context and stack |
| `task_yield()` | A task calls this to voluntarily give up the CPU and go back into the queue |
| `swapcontext()` | The actual context switch — saves current CPU state and jumps to another task |
| `prevent_starvation()` | Every 400 ticks, boosts all lower-priority tasks up one level |

## Task lifecycle

```
spawn_task()  →  enqueue()  →  swapcontext()  →  task_yield()  →  swapcontext()  →  TERMINATED
   created        queued         starts running      pauses           resumes           done
```

## Configuration

Edit these defines at the top of `main.c`:

```c
#define NUM_LEVELS          6      // number of priority levels
#define MAX_TASKS_PER_QUEUE 64     // max tasks per priority level
#define STACK_SIZE          8192   // stack size per task in bytes
```

## Build and run

```bash
gcc -o scheduler main.c
./scheduler
```

## Requirements

- Linux (uses POSIX `ucontext.h`)
- GCC

```bash
sudo apt install gcc   # if not installed
```
