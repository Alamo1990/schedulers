#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

#include "mythread.h"

long hungry = 0L;

struct queue* queues[2];

TCB* scheduler();
void activator(TCB* next);
void timer_interrupt(int sig);

/* Array of state thread control blocks: the process allows a maximum of N threads */
static TCB t_state[N];
/* Current running thread */
static TCB* running;
static int current = 0;
/* Variable indicating if the library is initialized (init == 1) or not (init == 0) */
static int init=0;

/* Initialize the thread library */
void init_mythreadlib() {
        queues[HIGH_PRIORITY] = queue_new();
        queues[LOW_PRIORITY] = queue_new();
        int i;
        t_state[0].state = INIT;
        t_state[0].priority = LOW_PRIORITY;
        t_state[0].ticks = QUANTUM_TICKS;
        if(getcontext(&t_state[0].run_env) == -1) {
                perror("getcontext in my_thread_create");
                exit(5);
        }
        for(i=1; i<N; i++) {
                t_state[i].state = FREE;
        }
        t_state[0].tid = 0;
        running = &t_state[0];
        init_interrupt();
}


/* Create and intialize a new thread with body fun_addr and one integer argument */
int mythread_create (void (*fun_addr)(),int priority){
        int i;

        if (!init) { init_mythreadlib(); init=1; }
        for (i=0; i<N; i++)
                if (t_state[i].state == FREE) break;
        if (i == N) return(-1);
        if(getcontext(&t_state[i].run_env) == -1) {
                perror("getcontext in my_thread_create");
                exit(-1);
        }

        t_state[i].state = INIT;
        t_state[i].priority = priority;
        t_state[i].function = fun_addr;
        t_state[i].ticks = QUANTUM_TICKS;
        t_state[i].run_env.uc_stack.ss_sp = (void *)(malloc(STACKSIZE));
        if(t_state[i].run_env.uc_stack.ss_sp == NULL) {
                printf("thread failed to get stack space\n");
                exit(-1);
        }
        t_state[i].tid = i;
        t_state[i].run_env.uc_stack.ss_size = STACKSIZE;
        t_state[i].run_env.uc_stack.ss_flags = 0;
        makecontext(&t_state[i].run_env, fun_addr, 1);
        disable_interrupt();
        enqueue(queues[priority], &t_state[i]);
        enable_interrupt();
        if(priority == HIGH_PRIORITY && running->priority == LOW_PRIORITY)
                activator(scheduler());

        return i;
} /****** End my_thread_create() ******/


/* Free terminated thread and exits */
void mythread_exit() {
        int tid = mythread_gettid();

        printf("Thread %d FINISHED\n ***************\n", tid);
        t_state[tid].state = FREE;
        free(t_state[tid].run_env.uc_stack.ss_sp);

        TCB* next = scheduler();
        activator(next);
}

/* Sets the priority of the calling thread */
void mythread_setpriority(int priority) {
        int tid = mythread_gettid();
        t_state[tid].priority = priority;
}

/* Returns the priority of the calling thread */
int mythread_getpriority(int priority) {
        int tid = mythread_gettid();
        return t_state[tid].priority;
}


/* Get the current thread id.  */
int mythread_gettid(){
        if (!init) { init_mythreadlib(); init=1; }
        return current;
}

/* Timer interrupt  */
void timer_interrupt(int sig){
        if(!queue_empty(queues[LOW_PRIORITY]) && !queue_empty(queues[HIGH_PRIORITY])) hungry++;
        else if((queue_empty(queues[LOW_PRIORITY]) && queue_empty(queues[HIGH_PRIORITY]))) hungry = 0L;
        if(hungry >= STARVATION) {
                TCB* promoted;
                disable_interrupt();
                while ((promoted = dequeue(queues[LOW_PRIORITY])) != NULL) {
                        enqueue(queues[HIGH_PRIORITY], promoted);
                        printf("*** THREAD %d PROMOTED TO HIGH PRIORITY QUEUE\n", promoted->tid);
                }
                enable_interrupt();
                hungry = 0L;
        }

        if(running->priority == LOW_PRIORITY && --running->ticks == 0) {
                TCB* next = scheduler();
                if(next!=NULL) activator(next);
        }
}



/* Scheduler: returns the next thread to be executed */
TCB* scheduler(){
        if( (running->priority == LOW_PRIORITY || running->state == FREE) && queue_empty(queues[HIGH_PRIORITY])) { //Get thread from low priority queue
                if(!queue_empty(queues[LOW_PRIORITY])) {

                        disable_interrupt();
                        TCB* next = dequeue(queues[LOW_PRIORITY]);
                        enable_interrupt();

                        if(running->state == INIT) {
                                disable_interrupt();
                                enqueue(queues[LOW_PRIORITY], running);
                                enable_interrupt();

                                running->ticks = QUANTUM_TICKS;
                                printf("*** SWAPCONTEXT FROM %d to %d\n", current, next->tid);
                        }else printf("*** THREAD %d FINISHED: SET CONTEXT OF %d\n", current, next->tid);


                        return next;
                }else if(running->state == INIT) return NULL;
        }else if(running->priority == LOW_PRIORITY) { //Get thread from high priority queue, and enqueue the current one to the low priority queue
                disable_interrupt();
                if(running->state == INIT) {
                        enqueue(queues[LOW_PRIORITY], running);
                }
                TCB* next = dequeue(queues[HIGH_PRIORITY]);
                enable_interrupt();

                if(running->ticks == 0) {
                        printf("*** SWAPCONTEXT FROM %d to %d\n", current, next->tid);
                        running->ticks = QUANTUM_TICKS;
                }else printf("*** THREAD %d PREEMPTED : SET CONTEXT OF %d\n", current, next->tid);

                return next;
        }else{ //Get thread from high priority queue
                disable_interrupt();
                TCB* next = dequeue(queues[HIGH_PRIORITY]);
                enable_interrupt();

                printf("*** THREAD %d FINISHED: SET CONTEXT OF %d\n", current, next->tid);

                return next;
        }
        printf("FINISH\n");
        exit(1);
}

/* Activator */
void activator(TCB* next){
        ucontext_t* curContext = &running->run_env;

        running = next;
        current = next->tid;

        swapcontext(curContext, &(next->run_env));
}
