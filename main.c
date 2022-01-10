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
#define WR_RATE 					0.02
#define WR_RATE_IN_PERCENTS			WR_RATE*100

/// Times
#define SIM_T 					20			//in seconds
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

# define BASE_KEY 2000


#define PROC_NUMBER 5


# define MMU_IDX 0
# define PROC1_IDX 1
# define PROC2_IDX 2
# define HD_IDX 3
# define TIMER_IDX 4
# define MAIN_IDX 5




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


//////////////////////////////// prototypes ////////////////////////////////
int myMsgGet(int mailBoxId, msgbuf* rxMsg);
int user_proc(int id);
int myMsgSend(int msqid, const msgbuf*msgp);
int sendStopSim(int id,int reason);
int MMU();
void timer();
int HD();
void closeSystem();
void initSystem();
void killAllProc();
//////////////////////////////// prototypes ////////////////////////////////

key_t mailBoxes[PROC_NUMBER];
pid_t processLst[PROC_NUMBER];


void sig_handler(int signum){
    printf("caught signal %d", signum);
    closeSystem();
}

void closeSystem() {
    // Kill all processes and queues
    killAllProc();
    // DESTROY MUTEX
    // DESTROY THREADS
}

void killAllProc(){
    int i;
    for(i=0;i<5;i++){
        msgctl(processLst[i],IPC_RMID,NULL);
        if (i<4)
            // do not kill the main process.
            kill(processLst[i], SIGKILL);
    }
}

void initSystem() {
    int i, j;

    for (i=0;i<PROC_NUMBER+1;i++){
        if ((mailBoxes[i] = msgget(BASE_KEY+i, 0600 | IPC_CREAT) == -1))
        {
            // Roll back
            for(j=i;j>=0;j--)
                msgctl(mailBoxes[j],IPC_RMID,NULL);
        }
    }
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
                    //HD();
                    break;
                case MMU_IDX:
                    //MMU();
                    break;
                case TIMER_IDX:
                    //timer();
                case PROC1_IDX:
                    //user_proc(i);
                    break;
                case PROC2_IDX:
                    //user_proc(i);
                    break;
            }
        }
    }
}

int main() {
    msgbuf return_msg;

    signal(SIGKILL, sig_handler); // Register signal handler
    signal(SIGTERM, sig_handler); // Register signal handler

    initSystem();
    myMsgGet(MAIN_IDX, &return_msg);
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
        printf("id = %d\n",id);
        myMsgSend(mailBoxes[MMU_IDX],&tx);
        myMsgGet(mailBoxes[MMU_IDX],&rx);
    }
    return 1;
}
int HD(){
    printf("hi Im HD!\n");
    exit(1);
   //return 1;
}

int MMU(){
    printf("hi Im MMU!\n");
    msgbuf rxMsg, txMsg;
    while (myMsgGet(MMU_IDX, &rxMsg))
    {


        switch (rxMsg.srcMbx) {

            case PROC1_IDX:
                printf("Message from PROC1 %c\n", '0'+rxMsg.mtext);
            case PROC2_IDX:
                printf("Message from PROC2 %c\n", '0'+rxMsg.mtext);
                break;
            case HD_IDX:
                printf("Message from HD %c\n", '0'+rxMsg.mtext);
                break;
            default:
                break;
        }
    }
    exit(1);

}

int myMsgGet(int mailBoxId, msgbuf* rxMsg){

    int res;
    if((res = msgrcv(mailBoxes[mailBoxId] , rxMsg, sizeof(msgbuf) - sizeof(long), 1,0)) == -1){
        perror("MESSAGE RECEIVE ERROR");
        sendStopSim(mailBoxId, RECV_FAILED);
        return FALSE;
    }
    return TRUE;

}

int myMsgSend(int msqid, const msgbuf* msgp){
    int res;

    res = msgsnd(msqid,msgp,sizeof(msgbuf) - sizeof(long) ,0);
    if (res == -1){
        sendStopSim(msgp->srcMbx,SEND_FAILED);
    }
    return res;
}

// this is a wrapper for the try lock that makes sure the return value of the lock is okay and if not it closes the car
int myTryMutexLock(pthread_mutex_t* mutex,int self_id) {
    int res;
    res = pthread_mutex_trylock(mutex);
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
    sendStopSim(TIMER_IDX,0);
}

