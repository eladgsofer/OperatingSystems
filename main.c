#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <sys/errno.h>
#include<signal.h>



#define TRUE 1
#define FALSE 0
#define READ 0
#define WRITE 1
#define SEND_FAILED -2
#define RECV_FAILED -3
#define MUTEX_LOCK_FAILED -4

/// Probabilities
#define HIT_RATE 					0.9
#define HIT_RATE_IN_PERCENTS 		HIT_RATE*100
#define WR_RATE 					0.7
#define WR_RATE_IN_PERCENTS			WR_RATE*100

/// Times
#define SIM_T 					6			//in seconds
#define TIME_BETWEEN_SNAPSHOTS 	200000000 	//in ns
#define MEM_WR_T 				10			//in ns
#define HD_ACCS_T 				10000000	//in ns
#define INTER_MEM_ACCS_T 		500000000	//in ns

/// Other
#define N 				10
#define USED_SLOTS_TH 	5
#define TRUE			1
#define FALSE			0

/// MMU State Machine
// Pages state
#define INVALID			0
#define VALID			1
#define DIRTY			2
// MMU state
#define HIT				1
#define MISS			0

/// Messages
#define MSG_BUFFER_SIZE 256
#define PROC_WRITE_REQ	0
#define PROC_READ_REQ	1
#define PROC_REQ 		1
#define MMU_ACK		 	2
#define HD_REQ 			3
#define HD_ACK 			4

# define BASE_KEY 1200


#define PROC_NUMBER 5
#define THR_NUM 3

# define MMU_IDX 0
# define PROC1_IDX 1
# define PROC2_IDX 2
# define HD_IDX 3
# define TIMER_IDX 4
# define MAIN_IDX 5
# define PRINTER_IDX 6
# define EVICTOR_IDX 7





typedef struct msgbuf{
    long mtype;
    int  srcMbx;
    char mtext;
} msgbuf;

//full == all pages are valid
// empty == all the pages are invalid
pthread_mutex_t memMutex;
typedef struct memory{
    int validArr[N];
    int dirtyArr[N];
    int start;
    int size;
} memory;
memory mmuMemory;

//                                   S + size
//                         | UNUSED  |   USED
//                    ---->|         |------------------------->
//                    ------------------------------------------

// condition variable and mutex for switching control between the mmu and the evictor.
pthread_mutex_t evictCondMut;
enum { MMU_STATE, EVICT_STATE } state = MMU_STATE;
pthread_cond_t      condMmu = PTHREAD_COND_INITIALIZER;
pthread_cond_t      condEvict = PTHREAD_COND_INITIALIZER;

//////////////////////////////// prototypes ////////////////////////////////
int myMsgReceive(int mailBoxId, msgbuf* rxMsg, int msgType);
void user_proc(int id);

int myMsgSend(int mailBoxId, const msgbuf* msgp);
int myMutexLock(pthread_mutex_t* mutex,int self_id);
int init_mailbox(int key);

int sendStopSim(int id,int reason);
void user_proc(int id);
int MMU();
void timer();
int HD();
void closeSystem();
void initSystem();
void killAllProc();
void printerThr();
void evictorThr();

void printfunc();
void constructAMessage(msgbuf* msg, int srcMbx, int mType, char mText);
//////////////////////////////// prototypes ////////////////////////////////

key_t mailBoxes[PROC_NUMBER+1];
pid_t processLst[PROC_NUMBER];
pthread_t threadLst[THR_NUM];


void sig_handler(int signum){
    printf("caught signal %d", signum);
    closeSystem();
}

void closeSystem() {
    // Kill all processes and queues
    killAllProc();

    // DESTROY THREADS
    for (int i=0;i<THR_NUM;i++)
    {
        pthread_cancel(threadLst[i]);
    }
    // DESTROY MUTEX
    pthread_mutex_destroy(&evictCondMut);
    pthread_mutex_destroy(&memMutex);

}

void killAllProc(){
    int i;
    // we do not kill the main process here.
    for(i=0;i<PROC_NUMBER;i++){
        msgctl(mailBoxes[i],IPC_RMID,NULL);
        kill(processLst[i], SIGKILL);
    }
    // close the main mailbox
    msgctl(mailBoxes[MAIN_IDX],IPC_RMID,NULL);
}

void initSystem() {
    int i, j;

    // open a mailbox for every process including main
    for (i=0;i<PROC_NUMBER+1;i++){
        if ((mailBoxes[i] = init_mailbox(BASE_KEY+i)) == -1)
        {
            // Roll back
            for(j=i;j>=0;j--)
                msgctl(mailBoxes[j],IPC_RMID,NULL);
        }
    }

    // open the processes
    for (i=0;i<PROC_NUMBER;i++){
        processLst[i] = fork();
        if (processLst[i]<0)
            // Roll back
            for(j=i;j>=0;j--){
                kill(processLst[j], SIGKILL);
            }
        else if (processLst[i]==0){
            switch (i) {
                case HD_IDX:
                    HD();
                    break;
                case MMU_IDX:
                    MMU();
                    break;
                case TIMER_IDX:
                    timer();
                case PROC1_IDX:
                    while(1){;}
                    user_proc(i);
                    break;
                case PROC2_IDX:
//                    while(1){;}
                    user_proc(i);
                    break;
            }
        }
    }
    // Init mutex
    if (pthread_mutex_init(&memMutex, NULL)){
        perror("memMutex mutex failed to open!\n");
        killAllProc();
        exit(1);
    }

    if (pthread_mutex_init(&evictCondMut, NULL)){
        perror("evictCondMut mutex failed to open!\n");
        pthread_mutex_destroy(&memMutex);
        killAllProc();
        exit(1);
    }
//    if (pthread_mutex_init(&pagesInUseMutex, NULL)){
//        puts("Error in initializing counter's mutex!\n");
//        pthread_mutex_destroy(&pageMutex);
//        killAllProc();
//        exit(1);
//    }
//    if (pthread_mutex_init(&indexFIFOMutex, NULL)){
//        puts("Error in initializing index' mutex!\n");
//        pthread_mutex_destroy(&pageMutex);
//        pthread_mutex_destroy(&pagesInUseMutex);
//        killAllProc();
//        exit(1);
//    }


    // Create threads
    if((pthread_create(&threadLst[0], NULL, (void*)MMU, NULL))){
        printf("MMU main thread failed to open\n");
        pthread_mutex_destroy(&evictCondMut);
        pthread_mutex_destroy(&memMutex);
        killAllProc();
        exit(1);
    }
    if((pthread_create(&threadLst[1], NULL, (void*)evictorThr, NULL))){
        printf("Evictor Thread failed to open!\n");
        pthread_cancel(threadLst[0]);
        pthread_mutex_destroy(&evictCondMut);
        pthread_mutex_destroy(&memMutex);
        killAllProc();
        exit(1);
    }
//    if((pthread_create(&threadLst[2], NULL, (void*)printerThr, NULL))){
//        printf("Printer Thread Failed to open!\n");
//        pthread_cancel(threadLst[0]);
//        pthread_cancel(threadLst[1]);
//        pthread_mutex_destroy(&evictCondMut);
//        pthread_mutex_destroy(&memMutex);
//        killAllProc();
//        exit(1);
//    }
}
// The starting point of the code and also the main proccess responsible for opening all other procs and threads
// and in the case one of them failes or the time runs out he also closes the system.
int main() {
    msgbuf return_msg;
    signal(SIGKILL, sig_handler); // Register signal handler
    signal(SIGTERM, sig_handler); // Register signal handler
    initSystem();
    msgrcv(mailBoxes[MAIN_IDX], &return_msg,sizeof(msgbuf) - sizeof(long), 1,0);
    closeSystem();
    printf("closing system\n");
    printf("closed by: %d for reason: %d", return_msg.srcMbx,return_msg.mtext);
    return 0;
}


// the function responsible for the consumer processes
void user_proc(int id){
    printf("Hi I'm User %d\n", id);
    float writeProb = 0;
    msgbuf tx,rx;
    tx.mtype = 1; // general type
    tx.srcMbx = id;
    while(TRUE){
        usleep(INTER_MEM_ACCS_T/(double)1000);
        writeProb = (rand()%1000)/(double)1000;
        if (writeProb<WR_RATE){ // write
            tx.mtext = WRITE;
        } else { // read
            tx.mtext = READ;
        }
        printf("user id = %d->MMU\n",id);
        // send request to the MMU
        myMsgSend(MMU_IDX,&tx);
        // wait for ack
        myMsgReceive(id, &rx, 1);
    }
}
void constructAMessage(msgbuf* msg, int srcMbx, int mType, char mText){
    msg->mtext=mText;
    msg->mtype = mType;
    msg->srcMbx=srcMbx;
}
// The Hard Disk processes responsible for simulating the mechanisms of a hard disk by wasting time

int HD() {

    msgbuf tx, rx;
/*    tx.mtype = 1;
    tx.srcMbx = HD_IDX;
    tx.mtext = 'A';*/
    constructAMessage(&tx, HD_IDX, HD_IDX, 'A');

    while (TRUE) {
        // RECEIVE REQUEST
        myMsgReceive(HD_IDX, &rx, HD_IDX);
        // PROCESS
        printf("HD received messages\n");
        fflush(stdout);
        usleep(HD_ACCS_T/(double)1000);
        // SEND ACK
        myMsgSend(rx.srcMbx, &tx);
    }
}

// Responsible for the managment of the memory unit
int MMU(){
    printf("hi Im MMU!\n");
    msgbuf rxMsg, txMsg;
    int randPage, nextPage;
    int currMemSize;
    int currProcessId;
    int missHitStatus, ReadWriteType;


    while (TRUE)
    {
        printf("waiting for messages\n");
        myMutexLock(&memMutex, MMU_IDX);
        printf("MMU begining size %d\n",mmuMemory.size);
        pthread_mutex_unlock(&memMutex);
        // type is 1 == from a process

        myMsgReceive(MMU_IDX, &rxMsg, 1);

        myMutexLock(&memMutex, MMU_IDX);
        printf("MMU after receive size %d\n",mmuMemory.size);
        pthread_mutex_unlock(&memMutex);

        ReadWriteType = rxMsg.mtext;

//        printf("MMU received message from %d type %s\n",rxMsg.srcMbx,(rxMsg.mtext == WRITE) ? ("write"):("read"));
//
//        fflush(stdout);

        myMutexLock(&memMutex, MMU_IDX);

        printf("MMU starting memory size=%d\n",mmuMemory.size);
        currProcessId = rxMsg.srcMbx;

        if (mmuMemory.size==0)
            missHitStatus = MISS;
        else
            missHitStatus = rand() % 100 < HIT_RATE_IN_PERCENTS? HIT : MISS;
        pthread_mutex_unlock(&memMutex);
        printf("MMU %s\n",(missHitStatus)?("HIT"):("MISS"));
        if (missHitStatus==HIT){
            constructAMessage(&txMsg, MMU_IDX, 1, 'A');

            if (ReadWriteType == WRITE)
            {
                usleep(MEM_WR_T/(double)1000);

                myMutexLock(&memMutex, MMU_IDX);
                randPage = (mmuMemory.start + ((int)rand()%mmuMemory.size)) % N;
                mmuMemory.dirtyArr[randPage] = TRUE;
                pthread_mutex_unlock(&memMutex);
            }

            // SEND ACK TO THE PROCESS
            myMsgSend(currProcessId, &txMsg);
        }
        else // ITS A MISS
        {
            myMutexLock(&memMutex, MMU_IDX);
            currMemSize = mmuMemory.size;
            pthread_mutex_unlock(&memMutex);

            if (currMemSize==N) {
                printf("MMU calling the evictor\n");
                // RAISE THE EVICTOR FROM HELL
                myMutexLock(&evictCondMut, MMU_IDX);
                state = EVICT_STATE;
                pthread_cond_signal(&condEvict);
                pthread_mutex_unlock(&evictCondMut);

                // WAIT FOR THE EVICTOR TO FINISH
                myMutexLock(&evictCondMut, MMU_IDX);
                while (state != MMU_STATE)
                    pthread_cond_wait(&condMmu, &evictCondMut);

                pthread_mutex_unlock(&evictCondMut);
            }
            printf("MMU calling the HD\n");
            constructAMessage(&txMsg, MMU_IDX, HD_IDX, 'A');
            // Send a request to HD
            myMsgSend(HD_IDX, &txMsg);
            // Wait for an ACK from the HD to retrieve the page
            myMsgReceive(MMU_IDX, &rxMsg, HD_IDX);

            // The memory is not full, and now it's a hit since the page is inside.
            usleep(MEM_WR_T/(double)1000);

            /// Memory access - now its a hit
            myMutexLock(&memMutex, MMU_IDX);

            nextPage = (mmuMemory.start + mmuMemory.size) % N;
            printf("MMU next page %d\n",nextPage);
            if (ReadWriteType == WRITE){
                printf("MMU setting dirty\n");
                mmuMemory.dirtyArr[nextPage] = TRUE;
            }
            printf("MMU setting valid\n");
            printf("MMU before size %d\n",mmuMemory.size);
            mmuMemory.validArr[nextPage] = TRUE;
            mmuMemory.size = mmuMemory.size+1;
            printf("MMU after size %d\n",mmuMemory.size);
            pthread_mutex_unlock(&memMutex);
            printfunc();
            fflush(stdout);
            // SEND ACK TO THE PROCESS
            constructAMessage(&txMsg, MMU_IDX, 1, 'A');
            myMsgSend(currProcessId, &txMsg);
            myMutexLock(&memMutex, MMU_IDX);
            printf("MMU after ack size %d\n",mmuMemory.size);
            pthread_mutex_unlock(&memMutex);
        }
    }

    exit(1);
}

// The evictor Thread wakes up to clean and free the memory and then sends back the control to the MMU
void evictorThr(){
    msgbuf hdRxMsg,hdTxMsg;
    hdTxMsg.mtype  = HD_IDX; // we have a special type for the HD
    hdTxMsg.srcMbx = EVICTOR_IDX;
    hdTxMsg.mtext = 'A';
    int first_eviction = TRUE;
    while (1){
        // wait for your turn
        myMutexLock(&evictCondMut, EVICTOR_IDX);
        while (state != EVICT_STATE)
            pthread_cond_wait(&condEvict, &evictCondMut);
        pthread_mutex_unlock(&evictCondMut);
        printf("EVICTOR IS ACTIVE");
        fflush(stdout);

        first_eviction = TRUE;

        // we have awoken! start the cleansing!
        myMutexLock(&memMutex,EVICTOR_IDX);
        while(mmuMemory.size > USED_SLOTS_TH){
            // we need to synchronize the HD with the dirty page
            if (mmuMemory.dirtyArr[mmuMemory.start] == 1){
                // free the mutex to allow the mmu to work on the memory while we wait for the HD
                pthread_mutex_unlock(&memMutex);
                // send a request for the hard disk and wait for ack
                myMsgSend(HD_IDX,&hdTxMsg);
                myMsgReceive(HD_IDX, &hdRxMsg, HD_IDX);
                // hard disk finished take back the control
                myMutexLock(&memMutex,EVICTOR_IDX);
                mmuMemory.dirtyArr[mmuMemory.start] = 0;
            }

            mmuMemory.validArr[mmuMemory.start] = 0;
//            mmuMemory.size -= 1;
            printf("EVICTOR CLEANING MEM\n");
            fflush(stdout);
            // cyclic FIFO eviction
            mmuMemory.start = (mmuMemory.start + 1) % N;

            // give back the control to the MMU after the first eviction
            if (first_eviction) {
                pthread_mutex_lock(&evictCondMut);
                state = MMU_STATE;
                pthread_cond_signal(&condMmu);
                pthread_mutex_unlock(&evictCondMut);
                first_eviction = FALSE;
            }
        }
        pthread_mutex_unlock(&memMutex);
    }

}

// The printer Thread prints checkpoints of the memory every TIME_BETWEEN_SNAPSHOTS[ns]
void printerThr(){
    printf("Hi I'm the Printer\n");
    while(TRUE){

        printfunc();
        usleep(TIME_BETWEEN_SNAPSHOTS/(double)1000);
    }
}
void printfunc(){
    memory memCopy;
    int i=0;

    char c;
    myMutexLock(&memMutex,PRINTER_IDX);
    memCopy = mmuMemory;
    pthread_mutex_unlock(&memMutex);
    printf("PRINTER copy memsize= %d\n",memCopy.size);
    myMutexLock(&memMutex,PRINTER_IDX);
    printf("PRINTER real memsize= %d\n",mmuMemory.size);
    pthread_mutex_unlock(&memMutex);
    for (i = 0; i < N; i++){
        if (memCopy.validArr[i]){
            if (memCopy.dirtyArr[i]){
                c = '1';
            } else {
                c = '0';
            }
        } else {
            c = '-';
        }
        printf("%d|%c\n",i,c);
    }
    printf("\n\n");
}
// Wrapper to the msgrcv function that is responsible for receiving messages and closing the simulation in the case of an error
int myMsgReceive(int mailBoxId, msgbuf* rxMsg, int msgType){
    if((msgrcv(mailBoxes[mailBoxId] , rxMsg, sizeof(msgbuf) - sizeof(long), msgType,0)) == -1){

        perror("MESSAGE RECEIVE ERROR");
        printf("RECEIVE ERROR TO ID = %d",mailBoxId);
        sendStopSim(mailBoxId, RECV_FAILED);
        return FALSE;
    }
    return TRUE;
}

int myMsgSend(int mailBoxId, const msgbuf* msgp){
    int res;

    res = msgsnd(mailBoxes[mailBoxId],msgp,sizeof(msgbuf) - sizeof(long) ,0);
    if (res == -1){
        sendStopSim(msgp->srcMbx,SEND_FAILED);
    }
    return res;
}

// this is a wrapper for the lock that makes sure the return value of the lock is okay and if not it closes the system
int myMutexLock(pthread_mutex_t* mutex,int self_id) {
    int res;
    res = pthread_mutex_lock(mutex);
    if (res == 0) {
        return 0;
    }
    if (res == EBUSY){
        return 1;
    }
    // if we reached here then the lock failed
    sendStopSim(self_id,MUTEX_LOCK_FAILED);
}


int sendStopSim(int id,int reason){
    msgbuf data;
    data.mtype = 1;
    data.srcMbx = id;
    data.mtext = reason;
    printf("STOP SIM, id = %d\n",id);
    // send message to main to stop simulation
    msgsnd(mailBoxes[MAIN_IDX], &data,0, 0);
    exit(reason);
}
void timer(){
    printf("hi Im Timer!\n");
    sleep(SIM_T);
    printf("times up!\n");
    sendStopSim(TIMER_IDX,0);
}
// a wrapper to the msgget function which other than getting the requested mailbox also flushes it from any existing
// messages
int init_mailbox(int key){
    int mailbox;
    int sizeOfMsg = sizeof(msgbuf)-sizeof(long);
    msgbuf tempMsg;
    if ((mailbox = msgget(key, 0600 | IPC_CREAT)) == -1){
        return -1; // failed to open mailbox
    }

    // flush mailbox
    while(msgrcv(mailbox, &tempMsg, sizeOfMsg , 0 , IPC_NOWAIT) != -1){
        printf("FLUSH CLEANED MESSAGE from mailbox id = %d\n",key-BASE_KEY);
    }

    // test the stop reason to make sure we stoped flushing because the mailbox is empty
    if (errno != ENOMSG){
        perror("MSGGET FAILED!");
        return -1;
    }
    return mailbox;
}
