/*
Author: Nicholas Hieb
Date: 04/10/2025
This File is the secondary file and is the child process to be run.
*/
#include "oss.h"
shared_data *shm;
int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: worker <maxTimeToRun>\n");
        exit(EXIT_FAILURE);
    }
    int maxTimeToRun = atoi(argv[1]);
    // Getting shared memory and setting var values
    int shm_id = shmget(SHM_KEY, BUFF_SZ, IPC_CREAT | 0666);
    int msqid = 0;
    int time_used = maxTimeToRun;
    bool terminate = false;
    char message_str[100];
    // Seed random number generator with unique sequences
    srand(getpid() * 100);

    key_t key;
    // get a key for our message queue
    if ((key = ftok("oss.log", 1)) == -1)
    {
        perror("ftok");
        exit(1);
    }
    // create our message queue
    if ((msqid = msgget(key, 0644)) == -1)
    {
        perror("msgget in child");
        exit(1);
    }

    if (shm_id == -1)
    {
        perror("Shared memory get failed in childn");
        exit(EXIT_FAILURE);
    }
    shm = (shared_data *)shmat(shm_id, NULL, 0);
    if (shm == NULL)
    {
        perror("Failed to allocate shared data");
        exit(EXIT_FAILURE);
    }
    // We need all of the resources
    bool needsResource = true;
    bool isWaiting = false;
    // We are running the child until we need to stop at the max time limit
    while (terminate == false)
    {
        if (isWaiting == false)
        {
            int decision = rand() % 100;
            int byteRequest = rand() % 32000;
            if (decision > 30) // Request a resource if we dont have all of them
            {
                strcpy(message_str, "Read");
                isWaiting = true;
            }
            else if (decision < 30 && decision > 1) // Release a resource
            {
                strcpy(message_str, "Write");
                isWaiting = true;
            }
            else // Terminate
            {
                terminate = true;
                strcpy(message_str, "Terminating");
            }
            // Send decision to parent
            message sendmsg;
            sendmsg.mtype = getppid();
            sendmsg.childPid = getpid();
            sendmsg.pageRequest = byteRequest;
            strcpy(sendmsg.mtext, message_str);
            if (msgsnd(msqid, &sendmsg, sizeof(message) - sizeof(long), 0) == -1)
            {
                perror("msgsnd in child");
                shmdt(shm);
                exit(1);
            }
        }

        // Check for a message from parent process and wait until we get granted a resource
        message receivemsg;
        if (msgrcv(msqid, &receivemsg, sizeof(message), getpid(), 0) == -1)
        {
            perror("msgrcv in oss");
            shmdt(shm);
            exit(1);
        }
        if (strcmp(receivemsg.mtext, "Granted") == 0)
        {
            isWaiting = false;
        }
        else if (strcmp(receivemsg.mtext, "Terminating") == 0)
        {
            printf("Process: %d is terminating\n", getpid());
            terminate = true;
        }
        //usleep(1000); // Sleep for 1ms
    }
    shmdt(shm);
    return 0;
}
