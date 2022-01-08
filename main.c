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


#define TRUE 1
#define FALSE 0
#define READ 0
#define WRITE 1
#define SEND_FAILED -2
#define RECV_FAILED -3

/// Probabilities
#define HIT_RATE 					0.9
#define HIT_RATE_IN_PERCENTS 		HIT_RATE*100
#define WR_RATE 					0.99
#define WR_RATE_IN_PERCENTS			WR_RATE*100

/// Times
#define SIM_T 					5			//in seconds
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
# define MAIN_IDX 0
# define MMU_IDX 1
# define PROC0_IDX 2
# define PROC1_IDX 3
# define HD_IDX 4




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

key_t mailBoxes[5];

int main() {
    pid_t child_pid;

    for (int i=0;i<5;i++){


        mailBoxes[i] = msgget(BASE_KEY+i, 0600 | IPC_CREAT);
        // clear the buffer of old messages
        msgctl(mailBoxes[i],IPC_RMID,NULL);
    }
    ;
//    if ((child_pid = fork()) == 0){
//        user_proc(PROC0_IDX);
//    }
//    child_pid = fork();
//    if (child_pid == 0){
//        user_proc(PROC1_IDX);
//    }
    MMU();
//    child_pid = fork();
//    if (child_pid == 0){
//        MMU();
//    }
    sleep(15);
    return 0;
}

int user_proc(int id){
    float writeProb = 0;
    msgbuf data;
    data.mtype = 1;
    data.srcMbx = id;
    while(TRUE){
        usleep(INTER_MEM_ACCS_T/(double)1000);
        writeProb = (rand()%1000)/(double)1000;
        if (writeProb<WR_RATE){ // write
            data.mtext = WRITE;
        } else { // read
            data.mtext = READ;
        }
        myMsgSend(mailBoxes[MMU_IDX],&data);
    }
    return 1;
}

int MMU(){
    msgbuf rxMsg, txMsg;
    printf("hello\n");
    while (myMsgGet(MMU_IDX, &rxMsg))
    {
        printf("balo\n");

        switch (rxMsg.srcMbx) {

            case PROC0_IDX:
                printf("Message from PROC0 %c", rxMsg.mtext);
            case PROC1_IDX:
                printf("Message from PROC1 %c", rxMsg.mtext);
                break;
            case HD_IDX:
                printf("Message from HD %c", rxMsg.mtext);
                break;
            default:
                break;
        }
    }
    exit(1);

}

int myMsgGet(int mailBoxId, msgbuf* rxMsg){

    int res;
    printf("wait...");
    if((res = msgrcv(mailBoxes[mailBoxId] , &rxMsg, sizeof(msgbuf) - sizeof(long), 1,0)) == -1){
        puts("MESSAGE RECEIVE ERROR");
        sendStopSim(mailBoxId,RECV_FAILED);
        return FALSE;
    }
    printf(" msg rx %c", rxMsg->mtext);
    return TRUE;

}


int myMsgSend(int msqid, const msgbuf* msgp){
    int res;
    res = msgsnd(msqid,msgp,sizeof(msgbuf) - sizeof(long),0);
    if (res == -1){
        sendStopSim(msgp->srcMbx,SEND_FAILED);
    }
    return res;
}


int sendStopSim(int id,int reason){
    msgbuf data;
    data.mtype = 0;
    data.srcMbx = id;
    data.mtext = reason;
    // send message to main to stop simulation
    msgsnd(mailBoxes[MAIN_IDX], &data,0,0);
    exit(reason);
}