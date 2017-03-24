#include <stdio.h>
#include <sys/time.h>
#include <signal.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

#include "RRF.h"

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
        printf("#####\tDEBUG: enter init_mythreadlib()\n"); //DEBUG
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
        printf("#####\tDEBUG: i = %d\n", i); //DEBUG
        enqueue(queues[priority], &t_state[i]);
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

        printf("Thread %d finished\n ***************\n", tid);
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
int mythread_getpriority() { //ASK: why the parameter?
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
        //printf("##### DEBUG: Tick on thread %d. Remaining ticks: %d\n", running->tid, running->ticks);
        if(mythread_getpriority() == LOW_PRIORITY && --running->ticks == 0) {
                TCB* next = scheduler();
                if(next!=NULL) activator(next);
        }
}



/* Scheduler: returns the next thread to be executed */
TCB* scheduler(){

        running->ticks = QUANTUM_TICKS;
        if( (mythread_getpriority() == LOW_PRIORITY || running->state == FREE) && queue_empty(queues[HIGH_PRIORITY])) {
                if(!queue_empty(queues[LOW_PRIORITY])) {
                        disable_interrupt();
                        if(running->state == INIT) {
                                printf("##### DEBUG: (scheduler)Thread %d has run out of time and will be added again to the queue\n", running->tid);
                                enqueue(queues[LOW_PRIORITY], running);
                        }
                        TCB* next = dequeue(queues[LOW_PRIORITY]);
                        enable_interrupt();

                        printf("##### DEBUG: Dequeued thread %d \n", next->tid); //DEBUG

                        return next;
                }else if(running->state == INIT) return NULL;
                else printf("##### DEBUG: Queue is empty\n");  //DEBUG
        }else{
                disable_interrupt();
                TCB* next = dequeue(queues[HIGH_PRIORITY]);
                enable_interrupt();
                return next;
        }
        printf("mythread_free: No thread in the system\nExiting...\n");
        exit(1);
}

/* Activator */
void activator(TCB* next){

        ucontext_t* curContext = &running->run_env;

        running = next;
        current = next->tid;

        swapcontext(curContext, &(next->run_env));
}