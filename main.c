#include <stdio.h>					// Printing simulation
#include <string.h>					// For strcpy
#include <stdlib.h>					// Randomizing probabilities
#include <sys/wait.h>
#include <unistd.h>					// Giving threads a little rest
#include <pthread.h>				// Managing threads
#include <sys/ipc.h>				// For message use
#include <sys/types.h>				// For message use
#include <sys/msg.h>				// For message use
#include <sys/errno.h>				// For errno
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
#define SIM_T 					3			//in seconds
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




///////////////////////////////////////////Structs/////////////////////////////////////////////////////
/* Message struct - as suggested in given material in exercise 5
 * 		There are two fields in the struct:
 * 			[mtype] - Type of message. Each message will have its own type
 * 					  meaning we can use the same message queue for all
 * 					  the messages in the simulation.
 * 			[mtext] - Text to send. Used for debugging purposes (and the
 * 					  message has to include some data aside of [mtype],
 * 					  see given material).
 * */
typedef struct msgbuf{
    long mtype;
    int  srcMbx;
    char mtext;
} msgbuf;


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
int myMsgGet(int mailBoxId, msgbuf* rxMsg, int msgType);
int user_proc(int id);
int myMsgSend(int mailBoxId, const msgbuf* msgp);
int myMutexLock(pthread_mutex_t* mutex,int self_id);

int sendStopSim(int id,int reason);
int MMU();
void timer();
int HD();
void closeSystem();
void initSystem();
void killAllProc();
void printerThr();
void evictorThr();
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
        if ((mailBoxes[i] = msgget(BASE_KEY+i, 0600 | IPC_CREAT)) == -1)
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
//                    user_proc(i);
                    break;
                case PROC2_IDX:
//                      while(1){;}
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
        puts("Error in creating MMU's main thread!\n");
        pthread_mutex_destroy(&evictCondMut);
        pthread_mutex_destroy(&memMutex);
        killAllProc();
        exit(1);
    }
    if((pthread_create(&threadLst[1], NULL, (void*)evictorThr, NULL))){
        puts("Error in creating Evicter's thread!\n");
        pthread_cancel(threadLst[0]);
        pthread_mutex_destroy(&evictCondMut);
        pthread_mutex_destroy(&memMutex);
        killAllProc();
        exit(1);
    }
    if((pthread_create(&threadLst[2], NULL, (void*)printerThr, NULL))){
        puts("Error in creating Printer's thread!\n");
        pthread_cancel(threadLst[0]);
        pthread_cancel(threadLst[1]);
        pthread_mutex_destroy(&evictCondMut);
        pthread_mutex_destroy(&memMutex);
        killAllProc();
        exit(1);
    }
}

int main() {
    msgbuf return_msg;
    signal(SIGKILL, sig_handler); // Register signal handler
    signal(SIGTERM, sig_handler); // Register signal handler
    initSystem();
    myMsgGet(MAIN_IDX, &return_msg, 1);
    closeSystem();
    printf("closing system\n");
    printf("closed by: %d for reason: %d", return_msg.srcMbx,return_msg.mtext);
    return 0;
}

int user_proc(int id){
    printf("Hi I'm User %d\n", id);
    float writeProb = 0;
    msgbuf tx,rx;
    tx.mtype = 1;
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
        myMsgGet(id,&rx, 1);
    }
    return 1;
}
int HD() {

    msgbuf tx, rx;
    tx.mtype = 1;
    tx.srcMbx = HD_IDX;
    tx.mtext = 'A';

    while (TRUE) {
        // RECEIVE REQUEST
        myMsgGet(HD_IDX, &rx, 1);
        // PROCESS
        sleep(HD_ACCS_T);
        // SEND ACK
        myMsgSend(MMU_IDX, &tx);
    }
}

int MMU(){
    printf("hi Im MMU!\n");
    msgbuf rxMsg, txMsg;
    txMsg.mtype = 1;
    txMsg.mtext = 'A';
    txMsg.srcMbx = MMU_IDX;
    while (myMsgGet(MMU_IDX, &rxMsg, 1))
    {

    }
    exit(1);
}
void evictorThr(){
    msgbuf hdRxMsg,hdTxMsg;
    hdTxMsg.mtype  = HD_IDX; // we have a special type for the HD
    hdTxMsg.srcMbx = EVICTOR_IDX;
    hdTxMsg.mtext = 'A';
    while (1){
        // wait for your turn
        myMutexLock(&evictCondMut,EVICTOR_IDX);
        while (state != EVICT_STATE)
            pthread_cond_wait(&condEvict, &evictCondMut);
        pthread_mutex_unlock(&evictCondMut);

        // we have awoken! start the cleansing!
        myMutexLock(&memMutex,EVICTOR_IDX);
        while(mmuMemory.size > USED_SLOTS_TH){
            // we need to synchronize the HD with the dirty page
            if (mmuMemory.dirtyArr[mmuMemory.start] == 1){
                mmuMemory.dirtyArr[mmuMemory.start] = 0;
                // send a request for the hard disk
                myMsgSend(HD_IDX,&hdTxMsg);
                // wait for ack from the hard disk
                myMsgGet(HD_IDX,&hdRxMsg,1);
            }
            mmuMemory.validArr[mmuMemory.start] = 0;
            mmuMemory.start = (mmuMemory.start+1) % N;
        }
        pthread_mutex_unlock(&memMutex);

    }

}

void printerThr(){
    memory memCopy;
    printf("Hi I'm the Printer\n");
    int i=0;
    char c;
    while(TRUE){

        myMutexLock(&memMutex,PRINTER_IDX);
        memCopy = mmuMemory;
        pthread_mutex_unlock(&memMutex);
        for (i = 0; i < N; i++){
            c = (memCopy.validArr[i]) ? ((memCopy.dirtyArr[i]) ? ('0') : ('1')) : ('-');
            printf("%d|%c\n",i,c);
        }
        printf("\n\n");
        usleep(TIME_BETWEEN_SNAPSHOTS/(double)1000);
    }
}


int myMsgGet(int mailBoxId, msgbuf* rxMsg, int msgType){
    int res;
    if((res = msgrcv(mailBoxes[mailBoxId] , rxMsg, sizeof(msgbuf) - sizeof(long), msgType,0)) == -1){
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
