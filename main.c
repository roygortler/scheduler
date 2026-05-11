#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>

#define NUM_LEVELS 6
#define MAX_TASKS_PER_QUEUE 10

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
    jmp_buf context;    
    int is_started;                  
    void (*task_func)(void);       
} TCB;


typedef struct {
    TCB* tasks[MAX_TASKS_PER_QUEUE];
    int head;
    int tail;
    int size;
} Queue;
Queue priority_queues[NUM_LEVELS];
TCB* current_running_task = NULL;

void init_queues() {
    for (int i = 0; i < NUM_LEVELS; i++) {
        priority_queues[i].head = 0;
        priority_queues[i].tail = 0;
        priority_queues[i].size = 0;
    }
}
int enqueue(TCB* task) {
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
        return NULL; // התור ריק
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

int scheduler_ticks = 0;


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
    void switch_context(TCB* next_task) {
   
    if (current_running_task != NULL && current_running_task->state == RUNNING) {
        current_running_task->state = READY;
        
        if (setjmp(current_running_task->context) == 0) {
            enqueue(current_running_task); 
        } else {
            return;
        }
    }
    current_running_task = next_task;
    current_running_task->state = RUNNING;
    
    printf("\n[Context Switch] ---> Running Task %d (Priority %d)\n", 
           current_running_task->id, current_running_task->priority);

    if (current_running_task->is_started == 0) {
        current_running_task->is_started = 1;
        current_running_task->task_func();   
    } else {
        longjmp(current_running_task->context, 1); 
    }
}
}