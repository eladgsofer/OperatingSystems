//cse-os_Ex5_203642988_317302362

///////////////////////////////////////////Includes////////////////////////////////////////////////////
#include <stdio.h>					// Printing simulation
#include <string.h>					// For strcpy
#include <stdlib.h>					// Randomizing probabilities
#include <wait.h>					// Giving threads a little rest
#include <unistd.h>					// Giving threads a little rest
#include <pthread.h>				// Managing threads
#include <sys/ipc.h>				// For message use
#include <sys/types.h>				// For message use
#include <sys/msg.h>				// For message use
#include <sys/errno.h>				// For errno

///////////////////////////////////////////Defines/////////////////////////////////////////////////////
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
	char mtext[MSG_BUFFER_SIZE];
} msgBuff;

///////////////////////////////////////////Functions///////////////////////////////////////////////////
void MMU_Main();
void MMU_Evicter();
void MMU_Printer();
void HardDisk();
void process_i(int i);
int openQ(key_t keyval);
void myMsgSnd(int qid, msgBuff* msg);
void myMsgGet(int qid, msgBuff* msg, long type);
void initializer();
void exiter();
void pagesUp();
void pagesDown();
void killAllProc();

///////////////////////////////////////////Global Variables/////////////////////////////////////////////
int qid[3];							// Message queue ID array. 0 is for HD. 1,2 are for the processes
pthread_t tid[3];					// Thread IDs
pid_t pid[3];						// Process IDs
int pages[N];						// Pages array
int pagesInUse;						// Page counter
pthread_mutex_t pageMutex;			// Mutex for pages array
pthread_mutex_t pagesInUseMutex;	// Mutex for page counter
pthread_mutex_t indexFIFOMutex;		// Mutex for FIFO index
int arrIndexFIFO;					// Index of the first in line in page array

pthread_cond_t mmuCondM = PTHREAD_COND_INITIALIZER;	// Condition variable for MMU_Main
pthread_cond_t mmuCondE = PTHREAD_COND_INITIALIZER;	// Condition variable for MMU_Evicter
pthread_mutex_t condMutex;							// Mutex for condition variable
int mmuSleep;										// A flag that indicates if MMU_Main sleeps or not

int main(void){
	initializer();
	sleep(SIM_T);
	exiter();
	return 0;
}

void MMU_Main(){
	/* This function simulates the MMU main unit.
	 * 		NOTE: Works as described in Exercise 5 description
	 * Input: None
	 * Output: None
	 * */

	int state, nextInQueue = 0;
	int j, tmp;
	int procID = 0;
	char buffer[MSG_BUFFER_SIZE];	// Save old message in case of a MISS

	msgBuff rcvMsg, sndMsg;

	while(0xf00d){

		// Test which process sent a request
		while(!procID){
			// Process 1
			if((msgrcv(qid[1], &rcvMsg, sizeof(msgBuff)-sizeof(long) , PROC_REQ , IPC_NOWAIT)) != -1){
				procID = 1;
			}
			else{
				if (errno!=ENOMSG){
					puts("Error in receiving message");
					exiter();
					exit(1);
				}
				// Process 2
				if((msgrcv(qid[2], &rcvMsg, sizeof(msgBuff)-sizeof(long) , PROC_REQ , IPC_NOWAIT)) != -1){
					procID = 2;
				}
				else{
					if (errno!=ENOMSG){
						puts("Error in receiving message");
						exiter();
						exit(1);
					}
				}
			}
		}

		if (!pagesInUse)
			state = MISS;
		else
			state = rand() % 100 < HIT_RATE_IN_PERCENTS? HIT : MISS;

		switch (state){
			case HIT:
				if (rcvMsg.mtext[0]-'0' == PROC_WRITE_REQ){
					usleep(MEM_WR_T/(double)1000);

					//// Give dirty status to random page
					// Generate random index
					while(pthread_mutex_trylock(&pagesInUseMutex));
					j = (int)rand()%pagesInUse;
					pthread_mutex_unlock(&pagesInUseMutex);

					// Make it dirty
					while(pthread_mutex_trylock(&indexFIFOMutex));
					tmp = (arrIndexFIFO+j)%N; // Index of the dirtied page
					pthread_mutex_unlock(&indexFIFOMutex);

					while(pthread_mutex_trylock(&pageMutex));
					pages[tmp] = DIRTY;
					pthread_mutex_unlock(&pageMutex);

				}
				break;

			case MISS:
				if (pagesInUse == N){

					// Wake evictor up
					pthread_mutex_lock(&condMutex);
					mmuSleep = TRUE;
					pthread_cond_signal(&mmuCondE);
					pthread_mutex_unlock(&condMutex);

					// Wait for evictor
					pthread_mutex_lock(&condMutex);
					while(mmuSleep)
						pthread_cond_wait(&mmuCondM, &condMutex);
					pthread_mutex_unlock(&condMutex);

				}
				// Send a request to HD
				sndMsg.mtype = HD_REQ;
				strcpy(sndMsg.mtext, "HD Request from MMU main");
				myMsgSnd(qid[0], &sndMsg);

				strcpy(buffer,rcvMsg.mtext);	// Backup

				//// Got new page
				myMsgGet(qid[0], &rcvMsg, HD_ACK);

				pagesUp();
				if (buffer[0]-'0' == PROC_WRITE_REQ){
					usleep(MEM_WR_T/(double)1000);

					// If write: mark as dirty
					while(pthread_mutex_trylock(&pageMutex));
					pages[nextInQueue] = DIRTY;
					pthread_mutex_unlock(&pageMutex);
				}
				else{
					// If read: mark as valid
					while(pthread_mutex_trylock(&pageMutex));
					pages[nextInQueue] = VALID;
					pthread_mutex_unlock(&pageMutex);
				}
				nextInQueue = (nextInQueue+1) % N;

				break;
		}

		// Send ACK
		sndMsg.mtype = MMU_ACK;
		strcpy(sndMsg.mtext, "MMU ACK");
		myMsgSnd(qid[procID], &sndMsg);
		procID = 0;
	}
}

void MMU_Evicter(){
	/* This function simulates the MMU evicter unit.
	 * 		NOTE: Works as described in Exercise 5 description
	 * Input: None
	 * Output: None
	 * */

	msgBuff rcvMsg, sndMsg;
	int first = TRUE, tmp;

	// Create the message now as it will be reused heavily
	sndMsg.mtype = HD_REQ;
	strcpy(sndMsg.mtext, "HD Request from evicter");

	// Wait for MMU main
	pthread_mutex_lock(&condMutex);
	while(!mmuSleep)
		pthread_cond_wait(&mmuCondE, &condMutex);
	pthread_mutex_unlock(&condMutex);

	while(0xfeedbee){
		if (pagesInUse > N-USED_SLOTS_TH){

			// Write dirty pages to HD
			while(pthread_mutex_trylock(&pageMutex));
			tmp = pages[arrIndexFIFO];
			pthread_mutex_unlock(&pageMutex);

			if (tmp == DIRTY){
				myMsgSnd(qid[0], &sndMsg);
				myMsgGet(qid[0], &rcvMsg, HD_ACK);
			}

			// Evict a page
			while(pthread_mutex_trylock(&pageMutex));
			pages[arrIndexFIFO] = INVALID;
			pthread_mutex_unlock(&pageMutex);

			// Increase FIFO index
			while(pthread_mutex_trylock(&indexFIFOMutex));
			arrIndexFIFO = (arrIndexFIFO+1) % N; // Cyclic advance
			pthread_mutex_unlock(&indexFIFOMutex);

			pagesDown();

			// Wake MMU main up
			if (first){
				pthread_mutex_lock(&condMutex);
				mmuSleep = FALSE;
				pthread_cond_signal(&mmuCondM);
				pthread_mutex_unlock(&condMutex);

				first = FALSE;
			}
		}
		else{
			// End of eviction
			first = TRUE;

			// Go back to sleep
			pthread_mutex_lock(&condMutex);
			while(!mmuSleep)
				pthread_cond_wait(&mmuCondE, &condMutex);
			pthread_mutex_unlock(&condMutex);
		}
	}


}

void MMU_Printer(){
	/* This function prints MMU's table in simulation.
	 * 		NOTE: Works as described in Exercise 5 description
	 * Input: None
	 * Output: None
	 * */

	int i, tmp;

	while(0xabba){
		for(i=0;i<N;i++){

			printf("%d|",i);
			while(pthread_mutex_trylock(&pageMutex));
			tmp = pages[i];
			pthread_mutex_unlock(&pageMutex);
			switch (tmp){
				case INVALID:
					printf("-\n");
					break;
				case VALID:
					printf("0\n");
					break;
				case DIRTY:
					printf("1\n");
					break;
			}
		}
		printf("\n\n");
		usleep(TIME_BETWEEN_SNAPSHOTS/(double)1000);
	}
}

void HardDisk(){
	/* This function simulates the Hard Disk.
	 * 		NOTE: Works as described in Exercise 5 description
	 * Input: None
	 * Output: None
	 * */

	msgBuff rcvMsg, sndMsg;

	// Create the message now as it will be reused heavily
	sndMsg.mtype = HD_ACK;
	strcpy(sndMsg.mtext,"HD ACK");

	while(0xdeadbeef){
		myMsgGet(qid[0], &rcvMsg, HD_REQ);
		usleep(HD_ACCS_T/(double)1000);
		myMsgSnd(qid[0], &sndMsg);
	}
}

void process_i(int i){
	/* This function simulates the processes that access memory.
	 * 		NOTE: Works as described in Exercise 5 description
	 * Input: Process serial number, for queue and debugging purposes
	 * Output: None
	 **/

	int type;
	msgBuff rcvMsg, sndMsg;

	sndMsg.mtype = PROC_REQ;

	while(0xf331900d){
		usleep(INTER_MEM_ACCS_T/(double)1000);
		type = rand() % 100 <= WR_RATE_IN_PERCENTS? PROC_WRITE_REQ : PROC_READ_REQ;
		sprintf(sndMsg.mtext, "%d - Process %d", type, i);
		myMsgSnd(qid[i], &sndMsg);
		myMsgGet(qid[i], &rcvMsg,MMU_ACK);
	}
}

int openQ(key_t keyval){
	/* Generates queue to forward messages through it (Or access an existing queue)
	 * Input: [keyval] - Queue identifier
	 * Output: Queue ID
	 * */

	int qid_t;

	if ((qid_t = msgget(keyval, IPC_CREAT | 0600 )) == -1){
		puts("Error in opening Q!");
		return -1;
	}

	return qid_t;
}

void myMsgSnd(int qid_t, msgBuff* msg){
	/* Sends a message to the given Q (via its ID). Exits program if there is an error
	 * 		NOTE: Blocks process if queue is full
	 * Input: [qid_t] - Queue ID
	 * 		  [msg] - Pointer to a message
	 * Output: None
	 * */

	int res;

	if((res = msgsnd(qid_t, msg, sizeof(msgBuff)-sizeof(long) , 0)) == -1){
		puts("Error in sending message");
		exiter();
	}
}

void myMsgGet(int qid_t, msgBuff* msg, long type){
	/* Reads a message from a the given Q (via its ID). Exits program if there is an error
	 * 		NOTE: Blocks process if there is no such message
	 * Input: [qid_t] - Queue ID
	 * 		  [msg] - Pointer to a message
	 * 		  [type] - type of message we are waiting for
	 * Output: None
	 * */

	int res;

	if((res = msgrcv(qid_t, msg, sizeof(msgBuff)-sizeof(long) , type , 0)) == -1){
		puts("Error in receiving message");
		exiter();
	}
}

void exiter(){
	/* A function that terminates the program after everything
	 * 		was initialized via [initializer] function.
	 * 		NOTE: Must be used after [initializer] function and
	 * 			  NOT inside of it
	 * 		destroys mutexes, cancels threads and kills processes
	 * 		and releases queues
	 * Input: None
	 * Output: None
	 * */

	int i;

	// Cancel threads
	for(i=0;i<3;i++){
		pthread_cancel(tid[i]);
	}

	// Kill all processes and queues
	killAllProc();

	// Destroy mutexes
	pthread_mutex_destroy(&pageMutex);
	pthread_mutex_destroy(&pagesInUseMutex);
	pthread_mutex_destroy(&indexFIFOMutex);
	pthread_mutex_destroy(&condMutex);

}

void initializer(){
	/* A function to initialize all elements in simulation.
	 * 		Creates threads, initializes mutexes, creates
	 * 		message queues, and the different processes (forks)
	 * 		NOTE: Must be the first function used in [main]
	 * Input: None
	 * Output: None, but updates [qid] array
	 * */

	key_t msgkey;
	time_t t;
	int i,j;

	// Init Qs
	srand((unsigned) time(&t));
	msgkey = ftok(".",'m');
	for(i=0;i<3;i++)
		if((qid[i] = openQ(msgkey))==-1){
			// Error occurred
			for(j=i;j>=0;j--)
				msgctl(qid[j],IPC_RMID,NULL);
			exit(1);
		}

	// Create processes
	for(i=0;i<3;i++){
		pid[i] = fork();
		if (pid[i]<0){
			puts("Error in fork!");
			for(j=i;j>=0;j--){
				kill(pid[j], SIGKILL);
			}
			exit(1);
		}
		else if(!pid[i]){
			switch (i){
				case 2:
					HardDisk();
					break;
				default:
					process_i(i+1);
					break;
			}
		}
	}

	// Init mutex
	if (pthread_mutex_init(&pageMutex, NULL)){
		puts("Error in initializing page mutex!\n");
		killAllProc();
		exit(1);
	}
	if (pthread_mutex_init(&pagesInUseMutex, NULL)){
		puts("Error in initializing counter's mutex!\n");
		pthread_mutex_destroy(&pageMutex);
		killAllProc();
		exit(1);
	}
	if (pthread_mutex_init(&indexFIFOMutex, NULL)){
		puts("Error in initializing index' mutex!\n");
		pthread_mutex_destroy(&pageMutex);
		pthread_mutex_destroy(&pagesInUseMutex);
		killAllProc();
		exit(1);
	}
	if (pthread_mutex_init(&condMutex, NULL)){
		puts("Error in initializing index' mutex!\n");
		pthread_mutex_destroy(&pageMutex);
		pthread_mutex_destroy(&pagesInUseMutex);
		pthread_mutex_destroy(&indexFIFOMutex);
		killAllProc();
		exit(1);
	}

	// Create threads
	if((pthread_create(&tid[0], NULL, (void*)MMU_Main, NULL))){
		puts("Error in creating MMU's main thread!\n");
		pthread_mutex_destroy(&pageMutex);
		pthread_mutex_destroy(&pagesInUseMutex);
		pthread_mutex_destroy(&indexFIFOMutex);
		killAllProc();
		exit(1);
	}
	if((pthread_create(&tid[1], NULL, (void*)MMU_Evicter, NULL))){
		puts("Error in creating Evicter's thread!\n");
		pthread_cancel(tid[0]);
		pthread_mutex_destroy(&pageMutex);
		pthread_mutex_destroy(&pagesInUseMutex);
		pthread_mutex_destroy(&indexFIFOMutex);
		killAllProc();
		exit(1);
	}
	if((pthread_create(&tid[2], NULL, (void*)MMU_Printer, NULL))){
		puts("Error in creating Printer's thread!\n");
		pthread_cancel(tid[0]);
		pthread_cancel(tid[1]);
		pthread_mutex_destroy(&pageMutex);
		pthread_mutex_destroy(&pagesInUseMutex);
		pthread_mutex_destroy(&indexFIFOMutex);
		killAllProc();
		exit(1);
	}
}

void pagesUp(){
	/* Mutex for pagesInUse, to protect the 'pagesInUse++' command
	 * Input: None
	 * Output: None, but updates [pagesInUse]
	 * */

	while(pthread_mutex_trylock(&pagesInUseMutex));
	pagesInUse++;
	pthread_mutex_unlock(&pagesInUseMutex);
}

void pagesDown(){
	/* Mutex for pagesInUse, to protect the 'pagesInUse--' command
	 * Input: None
	 * Output: None, but updates [pagesInUse]
	 * */

	while(pthread_mutex_trylock(&pagesInUseMutex));
	pagesInUse--;
	pthread_mutex_unlock(&pagesInUseMutex);
}

void killAllProc(){
	/* Kill all processes in [pid] and queues in [qid] arrays. For code re-use
	 * Input: None
	 * Output: None
	 * */

	int i;
	for(i=0;i<3;i++){
		kill(pid[i], SIGKILL);
		msgctl(qid[i],IPC_RMID,NULL);
	}
}
