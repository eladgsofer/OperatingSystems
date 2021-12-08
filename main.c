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
#define AGENTS 4
#define CARS

// scheduler is aware of mutex...
// double matrix of [][] mutexes..

pthread_mutex_t board_locks[N][N];
int board[N][N] = {0};
pthread_t agents[4];
pthread_t cars[4*N];

int main() {
    int i;
    // Initialize mutex board
    for(i=0;i<N;i+=N-1)
        for (int j=0;j<N;j++)
            pthread_mutex_init(&board_locks[i][j],PTHREAD_MUTEX_ERRORCHECK);

    /*for (i=0;i<AGENTS;i++)
    {
        pthread_create(&agents[i],NULL, startAgent, &i);
    }*/

    return 0;
}//gcc t91.c -l pthread
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

void startAgent(int agentId)
{

    // liter -> m km
    //

    int rand_interval;
    while (1){
        rand_interval = (rand() % (MAX_INTER_ARRIVAL_IN_NS - MIN_INTER_ARRIVAL_IN_NS + 1)) + MIN_INTER_ARRIVAL_IN_NS;
        sleep(rand_interval);
        // generate a car in the board
        switch (agentId) {

            case 1:
                pthread_mutex_lock(&board_locks[0][N-1]);
                pthread_mutex_lock(&board_locks[1][N-1]);
                //if (!board[0][N-1] && !board[1][N-1])
                    //pthread_create(carEntity)

                pthread_mutex_unlock(&board_locks[0][N-1]);
                pthread_mutex_unlock(&board_locks[1][N-1]);
                break;

        }
    }
}

void carEntity(startX, startY){
    while (1)
    {

    }

}

void producer(){
    printf("Hello producer id:%d", pthread_self());
}