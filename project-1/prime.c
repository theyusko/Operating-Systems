#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include "linkedlist.c"

#define READ_END 0
#define WRITE_END 1
#define PIPE_SIZE 1024
#define SLEEP_TIME 0.001


int read_nb(int fd) {
    int result;
    int input = -1;
    while (1) {
        //Try to read
        result = read(fd, (char * ) & input, sizeof(int));
        if (result == sizeof(int)) {
            return input;
        } else if (errno == EAGAIN) //pipe full
            sleep(SLEEP_TIME); //try later
        else {
            printf("Error: %d. Couldn't read data %d properly from CM.\n", errno, input);
            exit(0);
        }
    }
}

void write_nb(int fd, int input) {
    int result;
    while (1) {
        //Try to send
        result = write(fd, (char * ) & input, sizeof(int));
        if (result == sizeof(int)) {
            return;
        } else if (errno == EAGAIN) //pipe full
            sleep(SLEEP_TIME); //try later
        else {
            printf("Error: %d. Couldn't write data %d properly from CM.\n", errno, input);
            exit(0);
        }
    }
}

void init_pipe(int * * pipefd, int index) {
    if (pipe(pipefd[index]) == -1) {
        fprintf(stderr, "Error: Couldn't create pipe %d receives.\n",index);
        return;
    }
    fcntl(pipefd[index][READ_END], F_SETFL, O_NONBLOCK);
    fcntl(pipefd[index][WRITE_END], F_SETFL, O_NONBLOCK);
    fcntl(pipefd[index][READ_END], F_SETPIPE_SZ, PIPE_SIZE * sizeof(int));
    fcntl(pipefd[index][WRITE_END], F_SETPIPE_SZ, PIPE_SIZE * sizeof(int));
}

int main(int argc, char * argv[]) {
    int M, N, i, input, result, num, start;
    int * * pipefd; // (M+2)*2 array for read and write address of M+2 pipes
    pid_t pid = 1;
    int done;
    int send;
    struct Queue * sendq, *recq;
    sendq = createQueue();
    recq = createQueue();

    //Check arguments
    if (argc != 3) {
        fprintf(stderr, "Usage: prime N M\n");
        exit(EXIT_FAILURE);
    }

    if (!(N = atoi(argv[1])) || !(M = atoi(argv[2]))) {
        fprintf(stderr, "Error: Arguments must be positive integers.");
        return 1;
    }

    if (M > 50 || M < 1) {
        fprintf(stderr, "Error: Argument M must be a positive integer between 1 and 50 inclusive\n");
        return 1;
    }

    if (N > 1000000 || N < 1000) {
        fprintf(stderr, "Error: Argument N must be a positive integer between 1000 and 1000000 inclusive\n");
        return 1;
    }

    //Initialize pipefd
    pipefd = calloc((M + 2), sizeof(int * ));
    for (i = 0; i < M + 2; i++)
        pipefd[i] = calloc(2, sizeof(int));

    // Create pipe and processes
    //Master process (MP) inits pipes it will send to C1, PR(printing process), and it will receive from CM
    init_pipe(pipefd, 0);
    init_pipe(pipefd, M);
    init_pipe(pipefd, M + 1);

    //MP forks PR
    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Error: Couldn't fork process PR.\n");
        return 1;
    } else if (pid == 0) { //PR
        //printf("PR is created \n");
        close(pipefd[M + 1][WRITE_END]); //PR only receives

        done = 0;
        while (!done) {
            //printf("PR receiving the sequence\n");
            start = 1;
            //Receive from Cis
            while (1) {
                input = read_nb(pipefd[M + 1][READ_END]);
                if (input == -1)
                    done = 1;
                else
                    printf("%d\n", input);
                //printf("PR received value %d.\n", input);
            }
        }

        close(pipefd[M + 1][READ_END]);

        sleep(1);
        exit(0);

    } else { //not PR

        //Create C1...CM
        i = 0;
        pid = -1;
        while (i < M) {
            pid = fork(); //fork Ci

            if (pid < 0) {
                fprintf(stderr, "Error: Couldn't fork process C%d.\n", i);
                return 1;
            } else if (pid == 0) { //Ci
                //printf("C%d is created\n",i+1);
                close(pipefd[i][WRITE_END]); //Process Ci won't write to process Ci-1

                if (i != M - 1) { //CM already has its link with MP
                    init_pipe(pipefd, i + 1);
                }
            }
            if (pid) //Already has a child
                break;

            i++;
        }

        if (!i) { //MP
            //MP creates sequence of integers
            for (int j = 2; j <= N; j++) {
                enQueue(sendq, j);
            }
            enQueue(sendq, -1); //indicating end
            close(pipefd[M][WRITE_END]); //MP will only read from CM
            close(pipefd[0][READ_END]); // Close unused read end

            done = 0;
            send = 1;
            while (!done) {
                num = 0;
                //Send to C1
               //printf("MP sending the sequence\n");
               while (!(sendq -> front == NULL) && send) { //there is an integer in the sequence MP sends
                    num = (sendq->front)->key; //don't dequeue, maybe sending won't be possible
                    while (1) {
                        //Try to send
                        result = write(pipefd[0][WRITE_END], (char *)&num, sizeof(int));
                        if (result == sizeof(int)) { //success
                            //printf("MP sent value %d\n", num);
                            deQueue(sendq);
                            break;
                        } 
                        else if (errno == EAGAIN) {
                            //start receiving since there is nothing left in the sequence
                            send = 0;
                            sleep(SLEEP_TIME); //try later
                            break;
                        }
                        else {
                            printf("Error: %d. Couldn't write data %d properly from process.\n", errno, num);
                            exit(0);
                        }
                    }   
                    
                } 
                
                send = 0;

                //printf("MP receiving the sequence\n");
                int input = -1;
                //Receive from CM
                while (!send) {
                    input = -2;
                    //Try to read
                    while (1) {
                        result = read(pipefd[M][READ_END], (char *)&input, sizeof(int));
                        if (result == sizeof(int)) {
                            if (input == -1 && recq -> front == NULL){
                                //printf("MP will exit\n");
                                done = 1;
                            }
                            //printf("MP receive value %d\n", input);
                            enQueue(recq, input);
                            if (input == -1){
                                //printf("MP swapping\n");
                                sendq = recq;
                                recq = createQueue();
                                send = 1;
                            }
                            start = 0; 
                            break; //success
                        } else if (errno == EAGAIN) {  
                            send = 1;
                            sleep(SLEEP_TIME); //try later
                            break;
                        }
                        else {
                            printf("Error: %d. Couldn't read data %d properly from CM.\n", errno, input);
                            exit(0);
                        }
                        //size of intten küçükse yapamadın
                        //eagain döndü ise queue dolu imiş.
                        //eagain değilse sorun var. exit
                    }
                }
            }
            //Sequence is done, close PR by sending -1
            int num = deQueue(sendq) -> key;
            //printf("We have %d left.\n", num);
            write_nb(pipefd[0][WRITE_END], num);
            write_nb(pipefd[M + 1][WRITE_END], num);

            close(pipefd[M][WRITE_END]); 
            close(pipefd[M][READ_END]);
            close(pipefd[0][WRITE_END]); 
            close(pipefd[0][READ_END]);       
            //sleep(1);
            exit(0);
    
        } else { //Ci

             int prime;

            done = 0;
            start = 1;
            input = -2;
            while (!done) {
                //printf("C%d prev is %d\n", i, prev);
                input = read_nb(pipefd[i - 1][READ_END]);
                //printf("C%d received value %d\n",i, input);
                if (start) {
                    if (input != -1) {
                        prime = input;
                        start = 0;
                        write_nb(pipefd[M + 1][WRITE_END], input);
                        //printf("C%d sent value %d to PR.\n", i, input);
                    } else {
                      write_nb(pipefd[i][WRITE_END], input);
                      //printf("C%d will exit!\n", i);
                      done = 1;
                    }
                } else {
                    if (input == -1) {
                            //printf("C%d sent value %d to next process.\n", i, input);
                            write_nb(pipefd[i][WRITE_END], input);
                            start = 1;
                    } else if (input % prime != 0) { //send to next child
                        write_nb(pipefd[i][WRITE_END], input);
                        //printf("C%d sent value %d to next process.\n", i, input);
    
                    }
                }
            }
            close(pipefd[i-1][READ_END]);
            close(pipefd[i][WRITE_END]);
            close(pipefd[M+1][WRITE_END]);
            exit(0);
        }
    }
    
    //Free pipefd space
    for(int k = 0; k < M+2; k++)
        free(pipefd[i]);
    free(pipefd);
    
    
}
