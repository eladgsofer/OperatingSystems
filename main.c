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
#define AGENTS 4
#define CARS

// scheduler is aware of mutex...
// double matrix of [][] mutexes..

int run_failed;

typedef struct Cell{
    int x;
    int y;
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
void printBoard();
void generateCar(void* carGen);
void initCarAgent(carGenerator* carGen, int agentId);
void carEntity(void* args);
Cell get_next_position(Cell curr_pos);
void closeSystem();
int safe_mutex_lock(CarNode* me, pthread_mutex_t* mutex);
void delete_self(CarNode* me);

pthread_t agents[4];
pthread_t cars[4 * N];


Board board;
carGenerator carAgents[4];

void initCarAgent(carGenerator* carGen, int agentId){
    carGen->agentId = agentId;

    switch (agentId) {
        case 1:
            carGen->curCell.x =  N-1;
            carGen->curCell.y = 0;
            carGen->prevCell.x = N-1;
            carGen->prevCell.y = 1;
            break;
        case 2:
            carGen->curCell.x =  0;
            carGen->curCell.y = 0;
            carGen->prevCell.x = 1;
            carGen->prevCell.y = 1;
            break;
        case 3:
            carGen->curCell.x =  0;
            carGen->curCell.y = N-1;
            carGen->prevCell.x = N-2;
            carGen->prevCell.y = 0;
            break;
        case 4:
            carGen->curCell.x =  N-1;
            carGen->curCell.y = N-1;
            carGen->prevCell.x = N-2;
            carGen->prevCell.y = N-1;
            break;
        default:
            break;
    }

    if(pthread_create(carGen->genThread, NULL, generateCar, (void*)carGen)){
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

    // boardPrinter & carGenerators Threads

    pthread_t boardPrinter;

    // Create Station threads
    for(i=0;i<=3;i++){
        // create the Car Generators
        initCarAgent(&carAgents[i],i);
    }

    // Create printer thread
    if (pthread_create(boardPrinter, NULL, printBoard, NULL)) {
        perror("Error in creating printer thread!\n");
        exit(EXIT_FAILURE);
    }

    sleep(SIM_TIME);

    //freeAll(EXIT_SUCCESS);

    return 0;
}

void initBoard() {
    int i, j;
    // Initialize board chars
    for (i = 0;i < N;i++)
        for (j = 0;j < N;j++)
        {
            if (i == 0 || j == 0 || i == N - 1 || j == N - 1)
                board.charsBoard[i][j] = ' ';
            else
                board.charsBoard[i][j] = '@';
        }
    // Initialize Mutex rows & columns
    for (i = 0;i < N;i += N - 1)
        for (j = 0;j < N;j++)
            if (pthread_mutex_init(&board.mutexBoard[i][j], NULL) || pthread_mutex_init(&board.mutexBoard[j][i], NULL)) {
                perror("Error in initializing panel mutex!\n");
                exit(EXIT_FAILURE);
            }

    // Initialize LinkedList deletion mutex

    if (pthread_mutex_init(&board.carListMutex, NULL)){
        perror("Error in initializing panel mutex!\n");
        exit(EXIT_FAILURE);
    }
}

void printBoard() {
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

void generateCar(void* carGen)
{
    int rand_interval;

    carGenerator* carGenObj = (carGenerator*)carGen;

    while (1){
        rand_interval = (rand() % (MAX_INTER_ARRIVAL_IN_NS - MIN_INTER_ARRIVAL_IN_NS + 1)) + MIN_INTER_ARRIVAL_IN_NS;
        sleep(rand_interval);
        // generate a car in the board

        CarNode* newCar = (struct CarNode*)malloc(sizeof(CarNode));
        newCar -> justBorn = 1;
        newCar ->location = carGenObj->curCell;
        newCar ->prevCar = NULL;

        CarNode* temp = board.carList;

        board.carList = newCar;
        board.carList->nextCar = temp;

        pthread_mutex_lock(&board.mutexBoard[carGenObj->curCell.x][carGenObj->curCell.y]);
        pthread_mutex_lock(&board.mutexBoard[carGenObj->prevCell.x][carGenObj->prevCell.y]);

        if(pthread_create(& temp ->carThread, NULL, carEntity, newCar)){
            perror("Error in creating!\n");
            freeAll(EXIT_FAILURE);
        }

        pthread_mutex_unlock(&board.mutexBoard[carGenObj->curCell.x][carGenObj->curCell.y]);
        pthread_mutex_unlock(&board.mutexBoard[carGenObj->prevCell.x][carGenObj->prevCell.y]);



    }
}


//gcc t91.c -l pthread
/*
*
*
pthread_self();
pthread_create();
pthread_mutex_init();
if (pthread_mutex_lock(&lock,NULL)!=0){
    printf("mutex failed");
    return 1
}
pthread_mutex_trylock(); // try.. wil lreturn error if not working
pthread_mutex_unlock();*/

// array of mutexes for each square...


//    0  1  2  ... N-2 N-1
//0   X                X
//1      @  @  ...  @
//2      @  @  ...  @ 
//.      .  .  
//N-2    @  @       @
//N-1 X                X
//
void closeSystem(int returnCode) {
    Node* tempNode;

    // close the generating agents
    for (int i = 0;i < 4;i++) {
        pthread_cancel(carAgents[i].genThread);
    }
    // close all the cars(threads and memory)
    while (board.doublyLL != NULL) {
        tempNode = board.doublyLL;
        board.doublyLL = tempNode->nextCar;

        pthread_cancel(tempNode->carThread);
        free(tempNode);
    }
    pthread_cancel(board.printThread); // TODO: change this to printer thread

    // close the LL mutex
    pthread_mutex_destroy(&board.carListMutex);

    // close all the board mutex
    for (int i = 0;i < N;i++) {
        for (int j = 0;j < N; j++) {
            pthread_mutex_destroy(&board.mutexBoard[i][j]);
        }
    }
    exit(returnCode);
}
void carEntity(void* args) {
    int sleepTime;
    Node* carPtr = args;
    Cell next_pos;
    while (1)
    {
        usleep(INTER_MOVES_IN_NS / (double)1000);
        if (on_corner(carPtr->location) && carPtr->location.just_born == 0 && (rand() % 100 < FIN_PROB * 100)) {
            // remove car
            safe_mutex_lock(carPtr, board.carListMutex);
            // delete yourself from the doubly_linked_list
            delete_self(carPtr);
            pthread_exit(NULL);
            // 
            pthread_mutex_unlock(board.carListMutex);
        }
        else {
            // move car
            next_pos = get_next_position(carPtr->location);

            // this code is a deadlock potential code since we first lock and then unlock but it is neccessary to insure that a car doesn't lose it's spot
            // the deadlock will not happen only because the generator insures there will never be too many cars in the circle(by adding a new car only if there are two open slots)
            safe_mutex_lock(carPtr, board.mutexBoard[next_pos.x][next_pos.y]); // lock the next position
            pthread_mutex_unlock(board.mutexBoard[carPtr->location.x][carPtr->location.y]);         // unlock the current position

            carPtr->location = next_pos;
            if (on_corner(carPtr->location)) {
                carPtr->location.just_born = 0;
            }
        }
    }

}


int safe_mutex_lock(Node* me, pthread_mutex_t* mutex) {
    int returnValue;
    returnValue = pthread_mutex_lock(mutex);
    if (returnValue == 0) {
        return 0;
    }

    // if we reached here then the lock failed
    run_failed = 1;
    delete_self(me);
    pthread_exit(NULL);
    return -1;
}

void delete_self(Node* me) {
    if (me->prevCar == NULL) { // we are the first car
        pthread_mutex_lock(&board.carListMutex);   // if this failes we have have nothing we can do.
        board.doublyLL = me->nextCar;
        me->nextCar->prevCar = NULL;
        pthread_mutex_unlock(&board.carListMutex);
    }
    else { // we are in the middle
        pthread_mutex_lock(&board.carListMutex);   // if this failes we have have nothing we can do.
        me->prevCar->nextCar = me->nextCar;
        me->nextCar->prevCar = me->prevCar;
        pthread_mutex_unlock(&board.carListMutex);
    }
    free(me);
}

Cell get_next_position(Cell curr_pos) {
    Cell new_loc;
    if (curr_pos.x == 0 && curr_pos.y > 0) {
        // we are in the top row and moving left
        new_loc.x = curr_pos.x;
        new_loc.y = curr_pos.y - 1;
    }
    else if (curr_pos.x == N - 1 && curr_pos.y < N - 1) {
        // we are in the bottom row and moving right
        new_loc.x = curr_pos.x;
        new_loc.y = curr_pos.y + 1;
    }
    else if (curr_pos.y == 0 || curr_pos.x < N - 1) {
        // we are in the left column and moving down
        new_loc.x = curr_pos.x + 1;
        new_loc.y = curr_pos.y;
    }
    else {
        // we are in the right column and moving up
        new_loc.x = curr_pos.x - 1;
        new_loc.y = curr_pos.y;
    }
}

// check if you are on a sink/generator square(on the corners)
int on_corner(Cell curr_pos) {
    return ((curr_pos.x == 0 || curr_pos.x == N - 1) && (curr_pos.y == 0 || curr_pos.y == N - 1));
}
void producer() {
    printf("Hello producer id:%d", pthread_self());
}
