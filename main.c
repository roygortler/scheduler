#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <signal.h>
#include <unistd.h>

#define NUM_LEVELS 6
#define MAX_TASKS_PER_QUEUE 10
#define STACK_SIZE 8192 

void task_yield();

typedef enum {
    READY,
    RUNNING,
    BLOCKED,
    TERMINATED
} TaskState;

typedef struct {
    int id;
    int priority;       
    int quantum;        
    TaskState state;
    ucontext_t context;       
    void (*task_func)(void);  
    char stack[STACK_SIZE];   
} TCB;

TCB t1, t2, t3;

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
    // FIX: Clean safety check. If a task is terminated, do not queue it.
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
        printf("Task HIGH_1 (ID: %d) processing... Step %d\n", current_running_task->id, ++count);
        usleep(50000);
        task_yield();
    }

}


void task_low_prio() {
    int count = 0;
    while (count < 3) {
        printf("Task LOW_2 (ID: %d) processing... Step %d\n", current_running_task->id, ++count);
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

void init_task_context(TCB* task) {
    getcontext(&task->context);
    task->context.uc_stack.ss_sp = task->stack;
    task->context.uc_stack.ss_size = sizeof(task->stack);
    task->context.uc_stack.ss_flags = 0;
    task->context.uc_link = &scheduler_context; 
    makecontext(&task->context, task->task_func, 0);
}

int main() {
    init_queues();

    t1.id = 1; t1.priority = 5; t1.state = READY; t1.task_func = task_high_prio;
    t2.id = 2; t2.priority = 1; t2.state = READY; t2.task_func = task_low_prio;
    t3.id = 3; t3.priority = 5; t3.state = READY; t3.task_func = task_high_prio;

    init_task_context(&t1);
    init_task_context(&t2);
    init_task_context(&t3);

    enqueue(&t1);
    enqueue(&t2);
    enqueue(&t3);

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
            printf("[Scheduler] Cleaned up completed Task %d\n", current_running_task->id);
        }
    }

    return 0;
}