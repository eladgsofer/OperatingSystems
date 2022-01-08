#include <stdio.h>
#include <unistd.h>     /* standard unix functions, like getpid() */
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/msg.h>
#include <stdlib.h>
#define CENTRAL_MAILBOX 1200    //Central Mailbox number
typedef struct data_str{
    long mtype;
    int data;
}data_str;
int main() {
    int msqidC;
    int result;
    printf("Hello, World!\n");
    pid_t child_pid;

    if ((child_pid = fork()) == 0) {
        data_str data;

        // child proc
        printf("my id is %d\n",child_pid);
        msqidC = msgget(CENTRAL_MAILBOX, 0600 | IPC_CREAT);
        printf("msqidC %d\n",msqidC);
        printf("sending 5\n");
        data.mtype = 1;
        data.data = 7;
//        result = msgsnd( msqidC, &data, sizeof(data_str), 0);
//        result = msgsnd( msqidC, &data, sizeof(data_str), 0);
//        result = msgsnd( msqidC, &data, sizeof(data_str), 0);
//        result = msgsnd( msqidC, &data, sizeof(data_str), 0);

        //printf("sent 5\n");

        return child_pid;
    }
    // Parent
    data_str data;

    printf("my id is %d\n",child_pid);
    msqidC = msgget(CENTRAL_MAILBOX, 0600 | IPC_CREAT);
    printf("parent msqidC %d\n",msqidC);
    printf("waiting for receive\n");
    result = msgrcv( msqidC, &data, sizeof(data_str), 1, 0);
    printf("received %d\n",data.data);

    return 0;
}
