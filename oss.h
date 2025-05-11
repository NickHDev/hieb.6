#ifndef oss_h 
#define oss_h
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <signal.h>
#include <getopt.h>
#include <sys/msg.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#define SHM_KEY 284742
#define SEM_KEY 284743
#undef BUFF_SZ
#define BUFF_SZ sizeof(shared_data)
#define MAX_LOG_LINES 10000
#define LOG_INTERVAL_HALF 500000000  // 0.5 seconds
#define LOG_INTERVAL_FULL 1000000000 // 1 seconds
#define CLOCK_INCREMENT 100000000
#define PROCESS_TIME_DECISION 250000000   // 2.5ms

// Message queue
typedef struct msgbuf
{
    long mtype;
    pid_t childPid;
    char mtext[100];
    int pageRequest;
} message;
// Process control block
typedef struct PCB
{
    int occupied;
    pid_t pid;
    int sim_pid;
    int startSeconds;
    int startNano;
    int eventTimeNano;
    int pageTable[32]; // what frame each page is in
    int isBlocked;
} PCB;
// System clock
typedef struct
{
    unsigned int seconds;
    unsigned int nano;
} system_clock;
// Queue structure for MLFQ
typedef struct
{
    int processes[18];
    int count;
} queue_t;
typedef struct
{
    int LRU_TimeStampSecond;
    int LRU_TimeStampNano;
    int dirtyBit;
    int occupied;
    int processID;
    int pagenumber;
} frame_table;

// Shared memory
typedef struct
{
    system_clock clock;
    queue_t ready_queue;
    queue_t blocked_queue;
    PCB processTable[20];
    frame_table frameTable[256];
    int runningProcesses;
    int completedProcesses;
    int log_lines;
} shared_data;

// Function prototypes
void incrementClock(int);
void signal_handler(int sig);
void launchProcess(void);
void initializeQueue(void);
void initializeSharedMemory(void);
void cleanup(void);
void printStatus(FILE *);
void print_final_report(void);
void check_blocked_queue(void);
void handle_termination(void);
#endif
