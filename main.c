#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/errno.h>
#include <signal.h>


/// RUN CONSTANTS
#define HIT_RATE 					0.5
#define HIT_RATE_IN_PERCENTS 		HIT_RATE*100
#define WR_RATE 					0.7
// SIM_T in [s]
#define SIM_T 					20

// ALL THE TIMERS HERE ARE IN [nS]
#define TIME_BETWEEN_SNAPSHOTS 	200000000
#define MEM_WR_T 				10
#define HD_ACCS_T 				10000000
#define INTER_MEM_ACCS_T 		500000000
#define N 				5
#define USED_SLOTS_TH 	3

#define TRUE 1
#define FALSE 0
#define READ 0
#define WRITE 1
/// ERRORS
#define SEND_FAILED (-2)
#define RECV_FAILED (-3)
#define MUTEX_LOCK_FAILED (-4)


#define HIT				1
#define MISS			0

/// Base key for mailbox creation
# define BASE_KEY 1200

/// Number of components to create
#define PROC_NUMBER 4
#define MAIL_BOX_NUMBER 7
#define THR_NUM 3

/// Components Indexes
# define PROC1_IDX 0
# define PROC2_IDX 1
# define HD_IDX 2
# define TIMER_IDX 3
# define MAIN_IDX 4
# define MMU_IDX 5
# define EVICTOR_IDX 6
# define PRINTER_IDX 7

/// MessageBox object
typedef struct msgbuf{
    long mtype;
    int  srcMbx;
    char mtext;
} msgbuf;

/// Memory objects
// full == all pages are valid
// empty == all the pages are invalid
pthread_mutex_t memMutex = PTHREAD_MUTEX_INITIALIZER;
typedef struct memory{
    int validArr[N];
    int dirtyArr[N];
    int start;
    int size;
} memory;
memory mmuMemory;

/// Memory illustration
//                                   S + size
//                         | UNUSED  |   USED
//                    ---->|         |------------------------->
//                    ------------------------------------------

// condition variable and mutex for switching control between the mmu and the evictor.
pthread_mutex_t evictCondMut = PTHREAD_MUTEX_INITIALIZER;
enum { MMU_STATE, EVICT_STATE } state = MMU_STATE;
pthread_cond_t      condMmu = PTHREAD_COND_INITIALIZER;
pthread_cond_t      condEvict = PTHREAD_COND_INITIALIZER;

/// Prototypes
int myMsgReceive(int mailBoxId, msgbuf* rxMsg, int msgType);
void user_proc(int id);

int myMsgSend(int mailBoxId, const msgbuf* msgp);
int myMutexLock(pthread_mutex_t* mutex,int self_id);
int init_mailbox(int key);

int sendStopSim(int id,int reason);
int MMU();
void timer();
int HD();
void closeSystem();
void initSystem();
void killMB();
void killProc();
void killMut();
void killThr();

void printerThr();
void evictorThr();

void printfunc();
void constructAMessage(msgbuf* msg, int srcMbx, int mType, char mText);

key_t mailBoxes[MAIL_BOX_NUMBER];
pid_t processLst[PROC_NUMBER];
pthread_t threadLst[THR_NUM];


void sig_handler(int signum){
    // in case of a KILL/INTR signal - Close the System accordingly.
    closeSystem();
}

void closeSystem() {
    // Close the entire system - free all the resources
    killProc();
    killThr();
    killMB();
    killMut();
}

void killThr(){
    // Cancel all the threads
    int i;
    for (i=0;i<THR_NUM;i++)
    {
        pthread_cancel(threadLst[i]);
    }
}

void killMut(){
    // Destroy all the mutexes
    pthread_mutex_destroy(&evictCondMut);
    pthread_mutex_destroy(&memMutex);
}

void killProc(){
    // kill all the processes beside the  main process.
    int i;
    for(i=0;i<PROC_NUMBER;i++){
        kill(processLst[i], SIGKILL);
    }
}

void killMB(){
    // Destroy all the mailBoxes queues
    int i;
    for(i=0;i<MAIL_BOX_NUMBER;i++){
        msgctl(mailBoxes[i],IPC_RMID,NULL);
    }
}

void initSystem() {
    int i, j;

    // open a mailbox for every process including main
    for (i=0;i<MAIL_BOX_NUMBER;i++){
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
        if (processLst[i]<0){
            // Roll back - clean all the processes until now since an error occured.
            for(j=i;j>=0;j--){
                kill(processLst[j], SIGKILL);
            }
            printf("Fork failed rolling back system\n");
            killMB();
            // and quit since there is a failure
            exit(1);
        } else if (processLst[i]==0){
            switch (i) {
                case HD_IDX:
                    HD();
                    break;

                case TIMER_IDX:
                    timer();
                    break;

                case PROC1_IDX:
                    user_proc(i);
                    break;

                case PROC2_IDX:
                    user_proc(i);
                    break;
                default:
                    break;
            }
        }
    }

    // Create threads
    if((pthread_create(&threadLst[0], NULL, (void*)MMU, NULL))){
        printf("MMU main thread failed to open\n");
        killMB();
        killProc();
        killMut();
        exit(1);
    }
    if((pthread_create(&threadLst[1], NULL, (void*)evictorThr, NULL))){
        printf("Evictor Thread failed to open!\n");
        pthread_cancel(threadLst[0]);
        killMB();
        killProc();
        killMut();
        exit(1);
    }
    if((pthread_create(&threadLst[2], NULL, (void*)printerThr, NULL))){
        printf("Printer Thread Failed to open!\n");
        pthread_cancel(threadLst[0]);
        pthread_cancel(threadLst[1]);
        killMB();
        killProc();
        killMut();
        exit(1);
    }
}
// The starting point of the code and also the main proccess responsible for opening all other procs and threads
// and in the case one of them failes or the time runs out he also closes the system.
int main() {
    msgbuf return_msg;
    signal(SIGKILL, sig_handler); // Register signal handler
    signal(SIGTERM, sig_handler); // Register signal handler
    initSystem();
    // Wait until a termination message arrives from one of the components or the timer component
    msgrcv(mailBoxes[MAIN_IDX], &return_msg,sizeof(msgbuf) - sizeof(long), 1,0);
    closeSystem();
    return 0;
}



void user_proc(int id){
    /*
    * the function responsible for the consumer processes
    */
//    printf("Hi I'm User %d\n", id);
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
//        printf("user id = %d->MMU\n",id);
        // send request to the MMU
        myMsgSend(MMU_IDX,&tx);
        // wait for ack
        myMsgReceive(id, &rx, 1);
    }
}
void constructAMessage(msgbuf* msg, int srcMbx, int mType, char mText){
    /*
     * Construct a msgbuf object via the params of our API.
     * mtext = the data in the message
     * mtype = the message type
     * srcMbx = which component id the message arrived from.
     */
    msg->mtext=mText;
    msg->mtype = mType;
    msg->srcMbx=srcMbx;
}
// The Hard Disk processes responsible for simulating the mechanisms of a hard disk by wasting time
int HD() {
    /*
     * Hard Disk component - The HD Thread's target function, the function takes
     * requests from the MMU & Evictor, sleeps and return an Ack for the requester.
     */

    msgbuf tx, rx;
    constructAMessage(&tx, HD_IDX, HD_IDX, 'A');

    while (TRUE) {
        // RECEIVE REQUEST
        myMsgReceive(HD_IDX, &rx, HD_IDX);

        // PROCESS
        usleep(HD_ACCS_T/(double)1000);
        // SEND ACK
        myMsgSend(rx.srcMbx, &tx);
    }
}

int MMU(){
    /*
     * MMU component - The MMU target function, Responsible for the flow's management of the memory unit
     * Responsible for receiving Page requests from the Processes' components and handling them as described
     * in EX5.
     */
    msgbuf rxMsg, txMsg;
    int randPage, nextPage;
    int currMemSize;
    int currProcessId;
    int missHitStatus, ReadWriteType;


    while (TRUE)
    {
//        printf("waiting for messages\n");

        // type is 1 == from a process

        myMsgReceive(MMU_IDX, &rxMsg, 1);

        ReadWriteType = rxMsg.mtext;

        myMutexLock(&memMutex, MMU_IDX);

        currProcessId = rxMsg.srcMbx;

        if (mmuMemory.size==0)
            missHitStatus = MISS;
        else
            missHitStatus = rand() % 100 < HIT_RATE_IN_PERCENTS? HIT : MISS;
        pthread_mutex_unlock(&memMutex);
//        printf("MMU message from %d type %s was a %s\n",rxMsg.srcMbx,(rxMsg.mtext == WRITE) ? ("write"):("read"),(missHitStatus)?("HIT"):("MISS"));
        fflush(stdout);
        if (missHitStatus==HIT){ // Its a HIT
            constructAMessage(&txMsg, MMU_IDX, 1, 'A');

            if (ReadWriteType == WRITE)
            {
                usleep(MEM_WR_T/(double)1000);

                myMutexLock(&memMutex, MMU_IDX);
                // Randomize a page since it's a hit flow as described
                randPage = (mmuMemory.start + ((int)rand()%mmuMemory.size)) % N;
                // since it's write - it should be dirty
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
                // Raise the evictor via condition signaling and changing the state.
                myMutexLock(&evictCondMut, MMU_IDX);
                state = EVICT_STATE;
                pthread_cond_signal(&condEvict);
                pthread_mutex_unlock(&evictCondMut);

                // wait for the evictor to finish. (to raise the MMU up via condition signal)
                myMutexLock(&evictCondMut, MMU_IDX);
                while (state != MMU_STATE)
                    pthread_cond_wait(&condMmu, &evictCondMut);

                pthread_mutex_unlock(&evictCondMut);
            }
            constructAMessage(&txMsg, MMU_IDX, HD_IDX, 'A');
            // Send a request to HD
            myMsgSend(HD_IDX, &txMsg);
            // Wait for an ACK from the HD to retrieve the page
            myMsgReceive(MMU_IDX, &rxMsg, HD_IDX);

            // The memory is not full, and now it's a hit since the page is inside.
            usleep(MEM_WR_T/(double)1000);

            // Memory access - now its a hit since the page is brought to memory.
            myMutexLock(&memMutex, MMU_IDX);
            // Calculate the next page in the FIFO
            nextPage = (mmuMemory.start + mmuMemory.size) % N;
            // if a WRITE request, the page's dirty bit is ON
            if (ReadWriteType == WRITE){
                mmuMemory.dirtyArr[nextPage] = TRUE;
            }
            // the page is being used
            mmuMemory.validArr[nextPage] = TRUE;
            // raising the page count
            mmuMemory.size = mmuMemory.size+1;

            pthread_mutex_unlock(&memMutex);
            // send an ACK the the process.
            constructAMessage(&txMsg, MMU_IDX, 1, 'A');
            myMsgSend(currProcessId, &txMsg);
        }
    }
}


void evictorThr(){
    /*
    * The evictor Thread wakes up to clean and free the memory and then sends back the control to the MMU
    */
    msgbuf hdRxMsg,hdTxMsg;
    int first_eviction = TRUE;
    while (1){
        // wait for your turn
        myMutexLock(&evictCondMut, EVICTOR_IDX);
        while (state != EVICT_STATE)
            pthread_cond_wait(&condEvict, &evictCondMut);
        pthread_mutex_unlock(&evictCondMut);


        first_eviction = TRUE;

        // we have awoken! start the cleansing!
        myMutexLock(&memMutex,EVICTOR_IDX);
//        printf("EVICTOR IS ACTIVE\n");
        fflush(stdout);
        while(mmuMemory.size > USED_SLOTS_TH){
//            printf("EVICTOR CLEANING MEM %s\n",(mmuMemory.dirtyArr[mmuMemory.start])?("DIRTY"):(""));
            // we need to synchronize the HD with the dirty page
            if (mmuMemory.dirtyArr[mmuMemory.start] == 1){

                // free the mutex to allow the mmu to work on the memory while we wait for the HD
                pthread_mutex_unlock(&memMutex);
                // send a request for the hard disk and wait for ack
                constructAMessage(&hdTxMsg,EVICTOR_IDX,HD_IDX,'R');
                myMsgSend(HD_IDX, &hdTxMsg);
                myMsgReceive(EVICTOR_IDX, &hdRxMsg, HD_IDX);
                // hard disk finished take back the control
                myMutexLock(&memMutex,EVICTOR_IDX);
                mmuMemory.dirtyArr[mmuMemory.start] = 0;
            }

            mmuMemory.validArr[mmuMemory.start] = 0;
            mmuMemory.size -= 1;
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
        // give back the control to the MMU after the first eviction
        if (first_eviction) {
            pthread_mutex_lock(&evictCondMut);
            state = MMU_STATE;
            pthread_cond_signal(&condMmu);
            pthread_mutex_unlock(&evictCondMut);
            first_eviction = FALSE;
        }
        pthread_mutex_unlock(&memMutex);
    }

}

/*
 * The printer Thread prints checkpoints of the memory every TIME_BETWEEN_SNAPSHOTS[ns]
 */
void printerThr(){
//    printf("Hi I'm the Printer\n");
    while(TRUE){

        printfunc();
        usleep(TIME_BETWEEN_SNAPSHOTS/(double)1000);
    }
}
/*
 * The print function is the printers logic.
 * it is seperated so that we can use it to debug easily.
 */
void printfunc(){
    memory memCopy;
    int i;

    char c;
    myMutexLock(&memMutex,PRINTER_IDX);
    memCopy = mmuMemory;
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

int myMsgReceive(int mailBoxId, msgbuf* rxMsg, int msgType){
    /*
     * A Wrapper to the msgrcv function that is responsible for receiving messages and closing
     * the simulation in the case of an error
     * Input:
     *      MailBoxId: MailboxId of the component, meaning - which mailbox is being looked
     *      rxMsg: the message object to insert the details to after the message was received
     *      msgType: which type to listen to.
     */
    if((msgrcv(mailBoxes[mailBoxId] , rxMsg, sizeof(msgbuf) - sizeof(long), msgType,0)) == -1){

        perror("MESSAGE RECEIVE ERROR");
        printf("RECEIVE ERROR TO ID = %d", mailBoxId);
        sendStopSim(mailBoxId, RECV_FAILED);
        return FALSE;
    }
    return TRUE;
}

int myMsgSend(int mailBoxId, const msgbuf* msgp){
    /*
     * This is a wrapper to the msgsend function
     * in case of a failure a message is sent to the main process
     * in order to close the system.
     */
    int res;

    res = msgsnd(mailBoxes[mailBoxId],msgp,sizeof(msgbuf) - sizeof(long) ,0);
    if (res == -1){
        sendStopSim(msgp->srcMbx,SEND_FAILED);
    }
    return res;
}


int myMutexLock(pthread_mutex_t* mutex,int self_id) {
    /*
     * this is a wrapper for the lock that makes sure the return value of the lock is okay and if not it closes the system
     */
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
    return 1;
}


int sendStopSim(int id,int reason){
    /*
     * Send_stop_sim sends a message to the main process noting him that a critical failure has happend
     * which causes the whole system to close. this is also used in normal scenarios such as when the
     * timer finishes simulation.
     */
    msgbuf data;
    data.mtype = 1;
    data.srcMbx = id;
    data.mtext = reason;
//    printf("STOP SIM, id = %d, reason= %d\n",id, reason);
    // send message to main to stop simulation
    msgsnd(mailBoxes[MAIN_IDX], &data,0, 0);
    exit(reason);
}

void timer(){
    /*
     * The timer process. it's whole purpose is to go to sleep and wake up when the simulation is over
     * waking the main process and telling it to shut the system down.
     */
//    printf("hi Im Timer!\n");
    sleep(SIM_T);
//    printf("times up!\n");
    sendStopSim(TIMER_IDX,0);
}

int init_mailbox(int key){
    /* a wrapper to the msgget function which other than getting the requested mailbox
     * also flushes it from any existing messages.
     */
    int mailbox;
    int sizeOfMsg = sizeof(msgbuf)-sizeof(long);
    msgbuf tempMsg;
    if ((mailbox = msgget(key, 0600 | IPC_CREAT)) == -1){
        return -1; // failed to open mailbox
    }

    // flush mailbox
    while(msgrcv(mailbox, &tempMsg, sizeOfMsg , 0 , IPC_NOWAIT) != -1){
//        printf("FLUSH CLEANED MESSAGE from mailbox id = %d\n",key-BASE_KEY);
    }

    // test the stop reason to make sure we stoped flushing because the mailbox is empty
    if (errno != ENOMSG){
        perror("MSGGET FAILED!");
        return -1;
    }
    return mailbox;
}
