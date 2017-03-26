#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

#include "RR.h"

long hungry = 0L;

struct queue* q;

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
        q = queue_new();
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
        enqueue(q, &t_state[i]);
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
        if(--running->ticks == 0) {
                TCB* next = scheduler();
                if(next!=NULL) activator(next);
        }
}



/* Scheduler: returns the next thread to be executed */
TCB* scheduler(){

        running->ticks = QUANTUM_TICKS;

        if(!queue_empty(q)) {

                disable_interrupt();
                TCB* next = dequeue(q);
                enable_interrupt();

                if(running->state == INIT) {
                        disable_interrupt();
                        enqueue(queues[LOW_PRIORITY], running);
                        enable_interrupt();

                        printf("*** SWAPCONTEXT FROM %d to %d\n", current, next->tid);
                }else printf("*** THREAD %d FINISHED: SET CONTEXT OF %d\n", current, next->tid);

                return next;
        }else if(running->state == INIT) return NULL;

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
