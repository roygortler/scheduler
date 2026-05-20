#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <signal.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>

#define NUM_LEVELS 6
#define MAX_TASKS_PER_QUEUE 64
#define STACK_SIZE 8192

void task_yield();

typedef enum {
    READY,
    RUNNING,
    BLOCKED,
    TERMINATED
} TaskState;

typedef struct {
    ucontext_t context;
    int id;
    int priority;
    TaskState state;
    bool is_active;
    uint8_t stack[];
} TaskControlBlock;

typedef TaskControlBlock TCB;

typedef struct {
    void *pool_start;
    size_t task_block_size;
    int max_tasks;
} TaskPool;

typedef struct {
    TCB* tasks[MAX_TASKS_PER_QUEUE];
    int head;
    int tail;
    int size;
} Queue;

Queue priority_queues[NUM_LEVELS];
TCB* current_running_task = NULL;
ucontext_t scheduler_context;
int scheduler_ticks = 0;

void init_queues() {
    for (int i = 0; i < NUM_LEVELS; i++) {
        priority_queues[i].head = 0;
        priority_queues[i].tail = 0;
        priority_queues[i].size = 0;
    }
}

int enqueue(TCB* task) {
    if (task->state == TERMINATED) {
        return -1;
    }

    int prio = task->priority;
    Queue* q = &priority_queues[prio];

    if (q->size >= MAX_TASKS_PER_QUEUE) {
        printf("Error: Queue for priority %d is full!\n", prio);
        return -1;
    }

    q->tasks[q->tail] = task;
    q->tail = (q->tail + 1) % MAX_TASKS_PER_QUEUE;
    q->size++;
    task->state = READY;
    return 0;
}

TCB* dequeue(int prio) {
    Queue* q = &priority_queues[prio];

    if (q->size == 0) {
        return NULL;
    }

    TCB* task = q->tasks[q->head];
    q->head = (q->head + 1) % MAX_TASKS_PER_QUEUE;
    q->size--;
    return task;
}

TCB* get_next_task() {
    for (int i = NUM_LEVELS - 1; i >= 0; i--) {
        if (priority_queues[i].size > 0) {
            return dequeue(i);
        }
    }
    return NULL;
}

void prevent_starvation() {
    printf("\n--- [System] Preventing Starvation: Boosting older tasks ---\n");

    for (int i = NUM_LEVELS - 2; i >= 0; i--) {
        Queue* current_q = &priority_queues[i];
        int size = current_q->size;

        for (int j = 0; j < size; j++) {
            TCB* task = dequeue(i);
            if (task != NULL) {
                task->priority++;
                enqueue(task);
                printf("Task %d boosted from priority %d to %d\n", task->id, i, task->priority);
            }
        }
    }
}

void task_high_prio() {
    int count = 0;
    while (count < 5) {
        printf("Task HIGH (ID: %d) processing... Step %d\n", current_running_task->id, ++count);
        usleep(50000);
        task_yield();
    }
}

void task_low_prio() {
    int count = 0;
    while (count < 3) {
        printf("Task LOW  (ID: %d) processing... Step %d\n", current_running_task->id, ++count);
        usleep(50000);
        task_yield();
    }
}

void task_yield() {
    if (current_running_task != NULL) {
        scheduler_ticks += 50;

        if (scheduler_ticks >= 400) {
            prevent_starvation();
            scheduler_ticks = 0;
        }

        enqueue(current_running_task);
        swapcontext(&current_running_task->context, &scheduler_context);
    }
}

TaskPool* initialize_scheduler_pool(int num_tasks, size_t stack_size) {
    TaskPool *tp = (TaskPool*) malloc(sizeof(TaskPool));
    if (!tp) return NULL;

    tp->task_block_size = sizeof(TaskControlBlock) + stack_size;
    tp->max_tasks = num_tasks;

    tp->pool_start = malloc(tp->task_block_size * num_tasks);
    if (!tp->pool_start) {
        free(tp);
        return NULL;
    }

    for (int i = 0; i < num_tasks; i++) {
        TaskControlBlock *tcb = (TaskControlBlock*)((uint8_t*)tp->pool_start + (i * tp->task_block_size));
        tcb->is_active = false;
        tcb->state = TERMINATED;
        tcb->id = -1;
    }

    printf("Scheduler Pool initialized: %d tasks, %zu bytes each. Total: %zu bytes\n",
           num_tasks, tp->task_block_size, tp->task_block_size * num_tasks);

    return tp;
}

TaskControlBlock* spawn_task(TaskPool *tp, void (*func)(void), int priority) {
    for (int i = 0; i < tp->max_tasks; i++) {
        TaskControlBlock *tcb = (TaskControlBlock*)((uint8_t*)tp->pool_start + (i * tp->task_block_size));

        if (!tcb->is_active) {
            tcb->is_active = true;
            tcb->id = i;
            tcb->priority = priority;
            tcb->state = READY;

            if (getcontext(&tcb->context) == -1) {
                tcb->is_active = false;
                return NULL;
            }

            tcb->context.uc_stack.ss_sp = tcb->stack;
            tcb->context.uc_stack.ss_size = tp->task_block_size - sizeof(TaskControlBlock);
            tcb->context.uc_stack.ss_flags = 0;
            tcb->context.uc_link = &scheduler_context;

            makecontext(&tcb->context, func, 0);

            return tcb;
        }
    }

    return NULL;
}

int main() {
    init_queues();

    int num_tasks = 100;
    TaskPool* pool = initialize_scheduler_pool(num_tasks, STACK_SIZE);
    if (!pool) {
        printf("Failed to initialize task pool\n");
        return 1;
    }

    for (int i = 0; i < num_tasks; i++) {
        void (*func)(void) = (i % 3 == 0) ? task_low_prio : task_high_prio;
        int priority = (i % NUM_LEVELS);
        TCB* t = spawn_task(pool, func, priority);
        if (t == NULL) {
            printf("Failed to spawn task %d\n", i);
            break;
        }
        enqueue(t);
    }

    printf("Starting User-Mode CPU Scheduler...\n");

    while (1) {
        TCB* next = get_next_task();
        if (next == NULL) {
            printf("\n--- [System] All tasks finished or queue empty. Shutting down. ---\n");
            break;
        }

        current_running_task = next;
        current_running_task->state = RUNNING;

        printf("\n[Context Switch] ---> Running Task %d (Priority %d)\n",
               current_running_task->id, current_running_task->priority);

        swapcontext(&scheduler_context, &current_running_task->context);

        if (current_running_task->state == RUNNING) {
            current_running_task->state = TERMINATED;
            current_running_task->is_active = false;
            printf("[Scheduler] Cleaned up completed Task %d\n", current_running_task->id);
        }
    }

    free(pool->pool_start);
    free(pool);

    return 0;
}
