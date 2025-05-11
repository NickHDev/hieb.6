/*
Author: Nicholas Hieb
Date: 04/10/2025
This File is the main file to be executed and runs child processes.
*/
#include "oss.h"

int shm_id;
int msqid;
int semid_resource_table;
int semid_process_table;
FILE *log_file;
shared_data *shm;
int main(int argc, char *argv[])
{
    int proc = 1;
    int simul = 1;
    unsigned int intervalOfChilds = 100;
    int opt = 0;
    // Signal handler closing program after 60 sec
    signal(SIGALRM, signal_handler);
    alarm(5);
    system("touch oss.log");
    // Open log file
    log_file = fopen("oss.log", "w");
    if (log_file == NULL)
    {
        perror("Failed to open log file");
        cleanup();
        exit(EXIT_FAILURE);
    }
    // Initialize shared memory and message queue
    initializeSharedMemory();
    initializeQueue();

    // Parse command-line
    while ((opt = getopt(argc, argv, "hn:s:i:f:")) != -1)
    {
        switch (opt)
        {
        case 'n':
            proc = atoi(optarg);
            break;
        case 's':
            simul = atoi(optarg);
            break;
        case 'i':
            intervalOfChilds = atoi(optarg);
            break;
        case 'f':
            log_file = fopen(optarg, "w");
            break;
        case 'h':
        default:
            printf("Usage: %s [-h] -n <number> -s <number> -t <number> -i <number> -f <file>\n", argv[0]);
            printf("Options:\n");
            printf("  -h\tPrints this help message.\n");
            printf("  -n\tTotal number of processes to create.\n");
            printf("  -s\tMaximum number of simultaneous processes.\n");
            printf("  -i\tInterval in milliseconds to launch Child Processes\n");
            printf("  -f\tOutput file\n");
            return EXIT_FAILURE;
        }
    }
    if (intervalOfChilds <= 0)
    {
        perror("Invalid interval");
        exit(EXIT_FAILURE);
    }
    intervalOfChilds = intervalOfChilds * 1000000;
    // Main loop of our OSS
    while (shm->completedProcesses < proc || shm->runningProcesses > 0)
    {
        // Non-Blocking waitpid to see if a child process has terminated
        handle_termination();

        if (shm->completedProcesses >= proc && shm->runningProcesses == 0)
        {
            break;
        }

        if ((shm->completedProcesses + shm->runningProcesses) < proc && shm->runningProcesses < simul)
        {
            // Check if it's time to launch a new process
            if (shm->runningProcesses < 18)
            {
                launchProcess();
                shm->runningProcesses++;
            }
        }

        // Determine if we can grant any outstanding requests
        check_blocked_queue();

        // Check for a message from child process
        message receivemsg;
        if (msgrcv(msqid, &receivemsg, sizeof(message), getpid(), 0) == -1)
        {
            perror("msgrcv in oss");
            cleanup();
            exit(1);
        }
        message sendmsg;
        sendmsg.mtype = receivemsg.childPid;

        receivemsg.pageRequest = floor(receivemsg.pageRequest / 1024);
        int sim_pid = -1;
        bool pageFault = true;
        // get sim pid
        for (int i = 0; i < 18; i++)
        {
            if (shm->processTable[i].pid == receivemsg.childPid)
            {
                sim_pid = i;
                break;
            }
        }

        // Check if Terminating
        if (strcmp(receivemsg.mtext, "Terminating") == 0)
        {
            // Release all of frames
            for (int i = 0; i < 256; i++)
            {
                if (shm->frameTable[i].processID == sim_pid)
                {
                    shm->frameTable[i].LRU_TimeStampSecond = 0;
                    shm->frameTable[i].LRU_TimeStampNano = 0;
                    shm->frameTable[i].dirtyBit = -1;
                    shm->frameTable[i].occupied = 0;
                    shm->frameTable[i].processID = -1;
                    shm->frameTable[i].pagenumber = -1;
                }
            }

            strcpy(sendmsg.mtext, "Terminating");
            if (msgsnd(msqid, &sendmsg, sizeof(message) - sizeof(long), 0) == -1)
            {
                perror("msgsnd in oss in terminating");
                cleanup();
                exit(1);
            }
        }
        else // Check for page fault (request already in frame table)
        {
            for (int i = 0; i < 256; i++)
            {
                if (shm->frameTable[i].processID == sim_pid && shm->frameTable[i].pagenumber == receivemsg.pageRequest)
                {
                    pageFault = false;
                    shm->frameTable[i].LRU_TimeStampSecond = shm->clock.seconds;
                    shm->frameTable[i].LRU_TimeStampNano = shm->clock.nano;
                    if (strcmp(receivemsg.mtext, "Write") == 0)
                    {
                        shm->frameTable[i].dirtyBit = 1;
                    }
                    incrementClock(0);
                    strcpy(sendmsg.mtext, "Granted");
                    if (msgsnd(msqid, &sendmsg, sizeof(message) - sizeof(long), 0) == -1)
                    {
                        perror("msgsnd in oss in granted");
                        cleanup();
                        exit(1);
                    }
                    break;
                }
                else
                    pageFault = true;
            }
        }

        if (pageFault) // put it in blocked queue until event time
        {
        }

        // Every second check print frame table and page tables
        if (shm->clock.nano >= LOG_INTERVAL_FULL || (shm->clock.nano == 0 && shm->clock.seconds > 0))
        {
            printStatus(log_file);
        }
    }
    // Print final report
    print_final_report();
    // Detach and remove shared memory
    cleanup();
    return EXIT_SUCCESS;
}
// Functions to handle logic of main while loop
void signal_handler(int sig)
{
    for (int i = 0; i < 20; i++)
    {
        if (shm->processTable[i].occupied)
        {
            kill(shm->processTable[i].pid, SIGTERM);
        }
    }
    // Detach shared memory
    if (shmdt(shm) == -1)
    {
        perror("shmdt failed");
    }
    // Remove shared memory
    if (shmctl(shm_id, IPC_RMID, NULL) == -1)
    {
        perror("shmctl failed");
    }
    // Remove message queue
    if (msgctl(msqid, IPC_RMID, NULL) == -1)
    {
        perror("msgctl failed");
    }
    // Close log file
    if (log_file != NULL)
    {
        fclose(log_file);
    }

    exit(EXIT_FAILURE);
}
void incrementClock(int timeToIncrement)
{
    if (timeToIncrement == 0)
        shm->clock.nano += CLOCK_INCREMENT;
    else
        shm->clock.nano += timeToIncrement;

    if (shm->clock.nano >= 1000000000)
    {
        shm->clock.seconds += 1;
        shm->clock.nano -= 1000000000;
    }
}
void launchProcess(void)
{
    pid_t childPid = fork();
    if (childPid == 0)
    {
        char sim_pid_str[10];
        snprintf(sim_pid_str, sizeof(sim_pid_str), "%d", 1);
        char *args[] = {"./user_proc", sim_pid_str, NULL};
        execv(args[0], args);
    }
    else if (childPid > 0)
    {
        for (int i = 0; i < 18; i++)
        {
            if (shm->processTable[i].occupied == 0)
            {
                shm->processTable[i].occupied = 1;
                shm->processTable[i].pid = childPid;
                shm->processTable[i].sim_pid = i;
                shm->processTable[i].startSeconds = shm->clock.seconds;
                shm->processTable[i].startNano = shm->clock.nano;
                shm->processTable[i].isBlocked = 0;
                shm->processTable[i].eventTimeNano = 0;
                // Set page table to -1
                for (int j = 0; j < 32; j++)
                {
                    shm->processTable[i].pageTable[j] = -1;
                }
                fprintf(log_file, "OSS: Generating process with PID %d at time %d:%d\n", childPid, shm->clock.seconds, shm->clock.nano);
                break;
            }
        }
    }
    else
    {
        perror("Fork Failed");
    }
}
void initializeQueue(void)
{
    key_t key;
    if ((key = ftok("oss.log", 1)) == -1)
    {
        perror("ftok");
        exit(1);
    }

    if ((msqid = msgget(key, 0644 | IPC_CREAT)) == -1)
    {
        perror("msgget");
        exit(1);
    }
    printf("Message queue set up\n");
}
void initializeSharedMemory(void)
{
    // Shared memory
    shm_id = shmget(SHM_KEY, BUFF_SZ, IPC_CREAT | 0666);
    if (shm_id <= 0)
    {
        perror("Shared memory get failed in parent\n");
        exit(EXIT_FAILURE);
    }

    // Init shared data
    shm = (shared_data *)shmat(shm_id, NULL, 0);
    if (shm == NULL)
    {
        perror("Failed to allocate shared data");
        exit(EXIT_FAILURE);
    }

    shm->clock.seconds = 0;
    shm->clock.nano = 0;
    shm->runningProcesses = 0;
    shm->completedProcesses = 0;
    shm->log_lines = 0;

    // Initialize ready queue
    shm->ready_queue.count = 0;
    for (int j = 0; j < 18; j++)
    {
        shm->ready_queue.processes[j] = -1;
    }
    // Initialize blocked queue
    shm->blocked_queue.count = 0;
    for (int i = 0; i < 18; i++)
    {
        shm->blocked_queue.processes[i] = -1;
    }
    // Init frame table
    for (int i = 0; i < 256; i++)
    {
        shm->frameTable[i].LRU_TimeStampSecond = 0;
        shm->frameTable[i].LRU_TimeStampNano = 0;
        shm->frameTable[i].dirtyBit = -1;
        shm->frameTable[i].occupied = 0;
        shm->frameTable[i].processID = -1;
        shm->frameTable[i].pagenumber = -1;
    }
    // Init process table
    for (int i = 0; i < 18; i++)
    {
        shm->processTable[i].occupied = 0;
        shm->processTable[i].sim_pid = -1;
    }
    printf("Shared memory set up\n");
}
void cleanup(void)
{
    for (int i = 0; i < 20; i++)
    {
        if (shm->processTable[i].occupied)
        {
            kill(shm->processTable[i].pid, SIGTERM);
        }
    }
    // Detach shared memory
    if (shmdt(shm) == -1)
    {
        perror("shmdt failed");
    }
    // Remove shared memory
    if (shmctl(shm_id, IPC_RMID, NULL) == -1)
    {
        perror("shmctl failed");
    }
    // Remove message queue
    if (msgctl(msqid, IPC_RMID, NULL) == -1)
    {
        perror("msgctl failed");
    }
    // Close file
    if (log_file != NULL)
    {
        fclose(log_file);
    }
    // Exit the program
    exit(EXIT_FAILURE);
}
void printStatus(FILE *fp)
{
    printf("OSS PID: %d SysClockS: %d SysclockNano: %d\n", getpid(), shm->clock.seconds, shm->clock.nano);
    fprintf(fp, "OSS PID: %d SysClockS: %d SysclockNano: %d\n", getpid(), shm->clock.seconds, shm->clock.nano);

    // print frame table

    // print page tables

    fprintf(log_file, "\n");
    shm->log_lines += 50; // Approximate line count for this log entry
}
void print_final_report(void)
{
    printf("Final Report:\n");
    printf("Total dispatch time: %d.%d\n", shm->clock.seconds, shm->clock.nano);
    printf("Processes created: %d\n", (shm->runningProcesses + shm->completedProcesses));
    printf("Processes completed: %d\n", shm->completedProcesses);

    fprintf(log_file, "Final Report:\n");
    fprintf(log_file, "Total dispatch time: %d.%d\n", shm->clock.seconds, shm->clock.nano);
    fprintf(log_file, "Processes created: %d\n", (shm->runningProcesses + shm->completedProcesses));
    fprintf(log_file, "Processes completed: %d\n", shm->completedProcesses);
}
void check_blocked_queue(void)
{
    // Checking if there is a process in blocked queue
    if (shm->blocked_queue.count <= 0)
        return; // No processes waiting for resources

    /*
    see if the event wait has come up for any process
    if so make it ready
    give it either an empty frame if it exists
    if no free frame, give it the frame LRU
    set its LRU bit to current time
    */

   
    // Check if any process in the blocked queue event wait time has come up
    for (int i = 0; i < shm->blocked_queue.count + 1; i++)
    {
        int processId = shm->blocked_queue.processes[i];
        for (int j = 0; j < 18; j++)
        {
            if (shm->processTable[j].sim_pid == processId)
            {
            }
        }
    }
}
void handle_termination(void)
{
    int status = 0;
    int completedPid = waitpid(-1, &status, WNOHANG);
    if (completedPid > 0)
    {
        shm->completedProcesses++;
        shm->runningProcesses--;
        for (int i = 0; i < 18; i++)
        {
            if (shm->processTable[i].pid == completedPid)
            {
                // Clear Resources
                for (int j = 0; j < 32; j++)
                {
                    shm->processTable[i].pageTable[j] = -1;
                }
                shm->processTable[i].occupied = 0;
                shm->processTable[i].pid = 0;
                shm->processTable[i].sim_pid = -1;
                shm->processTable[i].startSeconds = 0;
                shm->processTable[i].startNano = 0;
                shm->processTable[i].isBlocked = 0;
                break;
            }
        }
    }
}