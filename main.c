#include <stdio.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define N 5
#define FIN_PROB 0.1
#define MIN_INTER_ARRIVAL_IN_NS 8000000
#define MAX_INTER_ARRIVAL_IN_NS 9000000
#define INTER_MOVES_IN_NS		100000
#define SIM_TIME 2
#define BOARD_PRINTS 10
#define SPACE_MARK				' '		// The character that represents a blank (in print)
#define CAR_MARK			'*'		// The character that represents a car (in print)
#define CENTER_MARK			'@'		// The character that represents the circle (in print)
#define TRUE				1
#define FALSE				0

typedef struct Cell{
    int i;
    int j;
} Cell;

// LinkedList
typedef struct CarNode{

    int justBorn;
    pthread_t carThread;
    struct CarNode* nextCar;
    struct CarNode* prevCar;
    struct Cell location;
} CarNode;

typedef struct carGenerator{
    pthread_t genThread;
    int agentId;
    Cell curCell, prevCell;
} carGenerator;

typedef struct Board{
    char charsBoard[N][N];
    pthread_mutex_t mutexBoard[N][N];
    pthread_mutex_t carListMutex;
    CarNode* carList;
} Board;

void initBoard();

_Noreturn void printBoard();
_Noreturn void generateCar(void* carGen);
void initCarAgent(carGenerator* carGen, int agentId);
void carEntity(void* args);
Cell get_next_position(Cell curr_pos);
void closeSystem();
int safe_mutex_lock(CarNode* me, pthread_mutex_t* mutex);
void delete_self(CarNode* me);
int on_corner(Cell curr_pos);

Board board;
pthread_t boardPrinter;
carGenerator carAgents[4];

void initCarAgent(carGenerator* carGen, int agentId){
    carGen->agentId = agentId;

    switch (agentId) {
        case 1:
            carGen->curCell.i = 0;
            carGen->curCell.j = N-1;
            carGen->prevCell.i = 1;
            carGen->prevCell.j = N-1;
            break;
        case 2:
            carGen->curCell.i =  0;
            carGen->curCell.j = 0;
            carGen->prevCell.i = 0;
            carGen->prevCell.j = 1;
            break;
        case 3:
            carGen->curCell.i =  N - 1;
            carGen->curCell.j = 0;
            carGen->prevCell.i = N - 2;
            carGen->prevCell.j = 0;
            break;
        case 4:
            carGen->curCell.i = N - 1;
            carGen->curCell.j = N - 1;
            carGen->prevCell.i = N - 1;
            carGen->prevCell.j = N - 2;
            break;
        default:
            break;
    }

    if(pthread_create(&carGen->genThread, NULL, generateCar, (void*)carGen)){
        perror("Error in creating station threads!\n");
        exit(EXIT_FAILURE);
    }
}

int main() {
    time_t t;
    board.carList = NULL;

    int i, id[4] = {0,1,2,3}; //Station ids
    srand((unsigned) time(&t));
    initBoard();

    // Create Station threads
    for(i=1;i<=4;i++){
        // create the Car Generators

        initCarAgent(&carAgents[i],i);
    }

    // Create printer thread
    if (pthread_create(&boardPrinter, NULL, printBoard, NULL)) {
        perror("Error in creating printer thread!\n");
        exit(EXIT_FAILURE);
    }

    sleep(SIM_TIME);

    closeSystem(EXIT_SUCCESS);

    return 0;
}

void initBoard() {
    int i, j;
    // Initialize board chars
    for (i = 0;i < N;i++)
        for (j = 0;j < N;j++)
        {
            if (i == 0 || j == 0 || i == N - 1 || j == N - 1)
                board.charsBoard[i][j] = SPACE_MARK;
            else
                board.charsBoard[i][j] = CENTER_MARK;
        }
    // Initialize Mutex rows & columns
    for (i = 0;i < N;i += N - 1)
        for (j = 0;j < N;j++)
            if (pthread_mutex_init(&board.mutexBoard[i][j], NULL) || pthread_mutex_init(&board.mutexBoard[j][i], NULL)) {
                perror("Error in initializing panel mutex!\n");
                exit(EXIT_FAILURE);
            }

    pthread_mutex_init(&board.carListMutex, NULL);
    // Initialize LinkedList deletion mutex

    if (pthread_mutex_init(&board.carListMutex, NULL)){
        perror("Error in initializing panel mutex!\n");
        exit(EXIT_FAILURE);
    }
}

_Noreturn void printBoard() {
    while (1) {

        usleep((SIM_TIME / (double)(1 + BOARD_PRINTS)) * 1000000); // Convert [sec] to [usec]
        for (int i = 0; i < N; i++) {
            for (int j = 0; j < N; j++) {
                printf("%c", board.charsBoard[i][j]);
            }
            printf("\n");
        }
        printf("\n");
    }
}
void print_action(){
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            printf("%c", board.charsBoard[i][j]);
        }
        printf("\n");
    }
    printf("\n");
}

_Noreturn void generateCar(void* carGen)
{
    int rand_interval;

    carGenerator* carGenObj = (carGenerator*)carGen;

    while (1){

        rand_interval = rand()%(MAX_INTER_ARRIVAL_IN_NS - MIN_INTER_ARRIVAL_IN_NS) + MIN_INTER_ARRIVAL_IN_NS;
        usleep(rand_interval / (double)1000); // Convert [nsec] to [usec]
        // generate a car in the board

        CarNode* newCar = (struct CarNode*)malloc(sizeof(CarNode));
        newCar -> justBorn = TRUE;
        newCar ->location = carGenObj->curCell;
        newCar ->prevCar = NULL;

        CarNode* temp = board.carList;

        board.carList = newCar;
        board.carList->nextCar = temp;

        pthread_mutex_lock(&board.mutexBoard[carGenObj->prevCell.i][carGenObj->prevCell.j]);
        pthread_mutex_lock(&board.mutexBoard[carGenObj->curCell.i][carGenObj->curCell.j]);


        if(pthread_create(&(newCar ->carThread), NULL, carEntity, newCar)){
            perror("Error in creating!\n");
            closeSystem(EXIT_FAILURE);
        }

        // pthread_mutex_unlock(&board.mutexBoard[carGenObj->curCell.i][carGenObj->curCell.j]);
        pthread_mutex_unlock(&board.mutexBoard[carGenObj->prevCell.i][carGenObj->prevCell.j]);

    }
}

void closeSystem(int returnCode) {
    CarNode* tempNode;

    // close the generating agents
    for (int i = 0;i < 4;i++) {
        pthread_cancel(carAgents[i].genThread);
    }
    // close all the cars(threads and memory)
    while (board.carList != NULL) {
        tempNode = board.carList;
        board.carList = tempNode->nextCar;

        pthread_cancel(tempNode->carThread);

        free(tempNode);
    }
    // close the LL mutex
    pthread_mutex_destroy(&board.carListMutex);


    // close all the board mutex
    for (int i = 0;i < N;i++) {
        for (int j = 0;j < N; j++) {
            pthread_mutex_destroy(&board.mutexBoard[i][j]);
        }
    }
    pthread_cancel(boardPrinter);
    exit(returnCode);
}
void carEntity(void* args) {
    int sleepTime;
    CarNode* carPtr = args;
    Cell next_pos;
    board.charsBoard[carPtr->location.i][carPtr->location.j] = CAR_MARK;
    //print_action();
    while (1)
    {
        usleep(INTER_MOVES_IN_NS / (double)1000);
        if (on_corner(carPtr->location) && carPtr->justBorn == FALSE && (rand() % 100 < FIN_PROB * 100)) {
            // delete yourself from the doubly_linked_list
            delete_self(carPtr);
            pthread_exit(NULL);
            // 
            pthread_mutex_unlock(&board.carListMutex);
        }
        else {
            // move car
            next_pos = get_next_position(carPtr->location);


            // this code is a deadlock potential code since we first lock and then unlock but it is neccessary to insure that a car doesn't lose it's spot
            // the deadlock will not happen only because the generator insures there will never be too many cars in the circle(by adding a new car only if there are two open slots)


            safe_mutex_lock(carPtr, &board.mutexBoard[next_pos.i][next_pos.j]); // lock the next position
            board.charsBoard[carPtr->location.i][carPtr->location.j] = ' ';
            board.charsBoard[next_pos.i][next_pos.j] = CAR_MARK;

            pthread_mutex_unlock(&board.mutexBoard[carPtr->location.i][carPtr->location.j]);         // unlock the current position
            //print_action();
            carPtr->location = next_pos;
            if (on_corner(carPtr->location)) {
                carPtr->justBorn = FALSE;
            }
        }

    }

}


int safe_mutex_lock(CarNode* me, pthread_mutex_t* mutex) {
    int returnValue;
    returnValue = pthread_mutex_lock(mutex);
    if (returnValue == 0) {
        return 0;
    }

    // if we reached here then the lock failed
    delete_self(me);
    pthread_exit(NULL);

}

void delete_self(CarNode* me) {

    pthread_mutex_lock(&board.carListMutex);   // if this failes we have have nothing we can do.
    board.charsBoard[me->location.i][me->location.j] = ' ';

    pthread_mutex_unlock(&board.mutexBoard[me->location.i][me->location.j]);         // unlock the current position

    //printf("FREE %d, %d THREAD %d\n",  me->location.i, me->location.j, pthread_self());
    if (me->nextCar==NULL && me->prevCar == NULL){
        board.carList = NULL;
    }
    else if (me->prevCar == NULL) { // we are the first car
        board.carList = me->nextCar;
        me->nextCar->prevCar = NULL;
    }

    else if (me->nextCar ==NULL) {
        me->prevCar->nextCar = NULL;
    }
    else { // we are in the middle
            me->prevCar->nextCar = me->nextCar;
            me->nextCar->prevCar = me->prevCar;
        }
    pthread_mutex_unlock(&board.carListMutex);
    // free(me);
}

Cell get_next_position(Cell curr_pos) {
    Cell new_loc;
    if (curr_pos.i == 0 && curr_pos.j > 0) {
        // we are in the top row and moving left
        new_loc.i = curr_pos.i;
        new_loc.j = curr_pos.j - 1 ;
    }

    else if (curr_pos.i == N - 1 && curr_pos.j < N - 1) {
        // we are in the bottom row and moving right
        new_loc.i = curr_pos.i;
        new_loc.j = curr_pos.j + 1;
    }
    else if (curr_pos.j == 0 && curr_pos.i < N - 1) {
        // we are in the left column and moving down
        new_loc.i = curr_pos.i + 1;
        new_loc.j = curr_pos.j;
    }
    else {
        // we are in the right column and moving up
        new_loc.i = curr_pos.i - 1;
        new_loc.j = curr_pos.j;
    }

    return new_loc;
}

// check if you are on a sink/generator square(on the corners)
int on_corner(Cell curr_pos) {
    return ((curr_pos.i == 0 || curr_pos.i == N - 1) && (curr_pos.j == 0 || curr_pos.j == N - 1));
}

