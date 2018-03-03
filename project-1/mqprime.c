#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <mqueue.h>
#include "linkedlist.c"

#define READ_END 0
#define WRITE_END 1
#define SLEEP_TIME 0.001

int read_nb(mqd_t mq) {
    int result = -1;
    int input = -1;
    while (1) {
        //Try to read
        result = mq_receive(mq, (char *)&input, sizeof(int), NULL);
        if (result == sizeof(int)) {
            //printf("read input %d\n", input);
            return input;
        } else if (errno == EAGAIN) 
            sleep(SLEEP_TIME); //try later
        else {
            printf("Error: %d. Couldn't read data %d properly from CM.\n", errno, input);
            exit(0);
        }
    }
}

void write_nb(mqd_t mq, int input) {
    int result;
    while (1) {
        //Try to send
        result = mq_send(mq, (char *)&input, sizeof(int), 0);
        if (result == 0) {
            return;
        } else if (errno == EAGAIN)
            sleep(SLEEP_TIME); //try later
        else {
            printf("Error: %d. Couldn't write data %d properly from process.\n", errno, input);
            exit(0);
        }
    }
}

void init_mq(mqd_t *mq, int index) {
    struct mq_attr attr;
    int i = index+1;
    char* name = calloc(20, sizeof(char)); //MP(C0), PR(CM+1), C1, .. CM (M<=5)
    
    sprintf(name, "/C%d", index);
	
    attr.mq_flags = 0;
    attr.mq_curmsgs = 0;
    attr.mq_msgsize = sizeof(int);
    attr.mq_maxmsg = 10;
    
	mq[index] = mq_open(name, O_RDWR | O_NONBLOCK | O_CREAT, 0666, &attr);
    if (((int)mq[index]) == -1) {
        fprintf(stderr, "Error %d: Couldn't create message queue %d.\n",errno, i);
        return;
    }
}

void erase_mq(mqd_t *mq, int index) {
    int i = index + 1;
    char* name = calloc(20, sizeof(char)); //MP(C0), PR(CM+1), C1, .. CM (M<=5)
    
    sprintf(name, "/C%d", index);
    
    if( (mqd_t)-1 == mq_close(mq[index])) {
        fprintf(stderr, "Error: Couldn't close message queue %d receives.\n",i);
        return;
    }
    if((mqd_t)-1 == mq_unlink(name)) {
        fprintf(stderr, "Error: Couldn't unlink message queue %d receives.\n",i);
        return;
    }
}


int main(int argc, char * argv[]) {
    int M, N, i, input, result, num, start;
    mqd_t *mq;
    pid_t pid = 1;
    int done;
    int send;
    struct Queue * sendq, * recq;
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

    //Initialize mq
    mq = calloc((M + 2), sizeof(mqd_t));
    
    // Create message queues and processes
    //Master process (MP) inits queue with C1, PR(printing process), and CM
    init_mq(mq, 0);
    init_mq(mq, M);
    init_mq(mq, M+1);

    //MP forks PR
    pid = fork();
    if (pid < 0) { 
        fprintf(stderr, "Error: Couldn't fork process PR.\n");
        return 1;
    } else if (pid == 0) { //PR
        //printf("PR is created \n");
        done = 0;
        while (!done) {
            //printf("PR receiving the sequence\n");
            //Receive from Cis
            input = read_nb(mq[M + 1]);
            if (input == -1)
                done = 1;
            else
                printf("%d\n", input);
            //printf("PR received value %d.\n", input);
        }
        
        erase_mq(mq, M+1);        
        
        //sleep(1);
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
                
                if (i != M - 1) { //CM already has its link with MP
                    init_mq(mq, i + 1);
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
                        result = mq_send(mq[0], (char *)&num, sizeof(int), 0);
                        if (result == 0) { //success
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
                        result = mq_receive(mq[M], (char *)&input, sizeof(int), 0);
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
            write_nb(mq[0], num);
            write_nb(mq[M + 1], num);
            erase_mq(mq, 0);
            erase_mq(mq, M);
            //sleep(1);
            exit(0);
        } else { //Ci

            int prime;

            done = 0;
            start = 1;
            input = -2;
            while (!done) {
                //printf("C%d prev is %d\n", i, prev);
                input = read_nb(mq[i - 1]);
                //printf("C%d received value %d\n",i, input);
                if (start) {
                    if (input != -1) {
                        prime = input;
                        start = 0;
                        write_nb(mq[M + 1], input);
                        //printf("C%d sent value %d to PR.\n", i, input);
                    } else {
                      write_nb(mq[i], input);
                      //printf("C%d will exit!\n", i);
                      done = 1;
                    }
                } else {
                    if (input == -1) {
                            //printf("C%d sent value %d to next process.\n", i, input);
                            write_nb(mq[i], input);
                            start = 1;
                    } else if (input % prime != 0) { //send to next child
                        write_nb(mq[i], input);
                        //printf("C%d sent value %d to next process.\n", i, input);
    
                    }
                }
            }
            if (i != M)
                erase_mq(mq, i);
            exit(0);
        }
    }
}
