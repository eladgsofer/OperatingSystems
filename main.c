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
void initBoard();
void printBoard();

typedef struct Car{
    int x, y;
    int justBorn;
} Car;

// LinkedList
typedef struct Node{
    struct Node* nextDelCar;
    struct Node* prevCar;
} Node;

typedef struct Cell{
    int i;
    int j;
} Cell;
typedef struct Board{
    char charsBoard[N][N];
    pthread_mutex_t mutexBoard[N][N];
    pthread_mutex_t delListMutex;
} Board;


pthread_t agents[4];
pthread_t cars[4*N];
Board board;

int main() {
    time_t t;
    srand((unsigned) time(&t));

    int i, id[4] = {0,1,2,3}; //Station ids
    
    // boardPrinter & carGenerators Threads
    pthread_t boardPrinter, carGenerators[4];
	initBoard();

    // Create Station threads
    for(i=0;i<=3;i++){
        switch (i) {
            
        }
        if(pthread_create(carGenerators[i], NULL, generateCar, id+i)){
            perror("Error in creating station threads!\n");
            exit(EXIT_FAILURE);
        }
    }

    // Create printer thread
    if(pthread_create(boardPrinter, NULL, printBoard, NULL)){
        perror("Error in creating printer thread!\n");
        exit(EXIT_FAILURE);
    }

    sleep(SIM_TIME);

    //freeAll(EXIT_SUCCESS);

    return 0;
}

void initBoard(){
    int i, j;
    // Initialize board chars
    for (i=0;i<N;i++)
        for (j=0;j<N;j++)
        {
            if (i==0 || j==0 || i==N-1||j==N-1)
                board.charsBoard[i][j] = ' ';
            else
                board.charsBoard[i][j] = '@';
        }

    // Initialize Mutex rows & columns
    for(i=0;i<N;i+=N-1)
        for (j=0;j<N;j++)
            if (pthread_mutex_init(&board.mutexBoard[i][j], NULL) || pthread_mutex_init(&board.mutexBoard[j][i], NULL)){
                perror("Error in initializing panel mutex!\n");
                exit(EXIT_FAILURE);
            }

    // Initialize LinkedList deletion mutex
    if (pthread_mutex_init(&board.delListMutex, NULL)){
        perror("Error in initializing panel mutex!\n");
        exit(EXIT_FAILURE);
    }
}

void printBoard(){
    while (1) {

        usleep((SIM_TIME/(double)(1+BOARD_PRINTS)) * 1000000); // Convert [sec] to [usec]
        for (int i = 0; i < N; i++) {
            for (int j = 0; j < N; j++) {
                printf("%c", board.charsBoard[i][j]);
            }
            printf("\n");
        }
        printf("\n");
    }
}

void generateCar(int agentId, Cell prevCell, Cell currCell)
{

    int rand_interval;
    while (1){
        rand_interval = (rand() % (MAX_INTER_ARRIVAL_IN_NS - MIN_INTER_ARRIVAL_IN_NS + 1)) + MIN_INTER_ARRIVAL_IN_NS;
        sleep(rand_interval);
        // generate a car in the board

        pthread_mutex_lock(&mutexBoard[0][N-1]);
        pthread_mutex_lock(&mutexBoard[1][N-1]);
        //if (!board[0][N-1] && !board[1][N-1])
            //pthread_create(carEntity)

        pthread_mutex_unlock(&mutexBoard[0][N-1]);
        pthread_mutex_unlock(&mutexBoard[1][N-1]);


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
void carEntity(car_pos loc){
    while (1)
    {
	
    }

}
car_pos get_next_position(car_pos curr_pos){
//	car_pos new_loc;
//	if (loc.x == 0 && loc.y>0){
//	 	// we are in the top row and moving left
//	 	new_loc.
//	}
//	else if (loc.x == N-1){
//		// we are in the bottom row
//	}
//	if (loc.y == 0 || loc.y == N-1){
	
	//}
}
void producer(){
    printf("Hello producer id:%d", pthread_self());
}
