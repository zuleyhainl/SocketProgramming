#include <stdio.h>
#include <string.h>	//strlen
#include <stdlib.h>	//strlen
#include <sys/socket.h>
#include <arpa/inet.h>	//inet_addr
#include <unistd.h>
//#include <errno.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <pthread.h>

#define NAME "/shm-multicastingQueue"
#define SIZE (sizeof(struct multicast_queue))
#define PORT 8086
#define messageNumber 1024//bucket size
typedef char message[512];//max message size 512byte
typedef message m_bucket[messageNumber];

struct multicast_queue
{
	sem_t m_sem;//mutex semaphore
	sem_t s_sem;//senk semaphore
	sem_t c_sem;//counter semaphore

	int newM;
	int head;//head of active messages on bucket
	int tail;//end of active messages on bucket
			//the new message is written to the tail index on the bucket
	int isFull;
	int number_of_clients;//Number of concurrent connections is bounded by 100
	m_bucket bucket;//circular multicasting queue

	int alive_unread[messageNumber];//This array holds the number of clients that will read the messages 
								//which on the bucket.
};
struct multicast_queue *multicast_queue;

struct passingValues
{
    struct multicast_queue *multicast_queue;
    int local_loc;
    int trigger;
    int isAllReaded;
};
struct passingValues *passing;

//function declarations
void* auto_handler(void* passingValues);
int isEmptyBucket(struct multicast_queue *multicast_queue);
int noNewMessage(struct passingValues passing, struct multicast_queue *multicast_queue);
void fetchNewMessage(struct passingValues *passing, struct multicast_queue *multicast_queue);

int main() {

	int sock_fd, threadExistFlag = 0;
	char message[512], command[550];//command holds both command and message for send
	struct sockaddr_in server_addr;
	pthread_t _thread;
	char * token;
	int shm_fd;

    /*POSIX SHM*/
    shm_fd = shm_open(NAME, O_CREAT | O_RDWR, 0666);// shm_open() function shall establish a connection between a shared memory object and a file descriptor
													//0666: the directory permissions of the shared-memory object.
	if (shm_fd == -1)//If successful, returns a file descriptor for the shared memory object that is the lowest numbered file descriptor
	{
        perror("shm_open()");//return -1 for unsuccessful calls
        return EXIT_FAILURE;
    }

    if(ftruncate(shm_fd, SIZE) == -1)
	{
		/*Error*/
		perror("ftruncate()");
        return EXIT_FAILURE;
	}

	/* Map shared memory object */
    multicast_queue = mmap(0, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);//The mmap() function shall establish a mapping between a process' address space and a shared memory object
					//successful completion, the mmap() function shall return the address at which the mapping was placed
    passing= malloc(sizeof(struct passingValues)); 

	//passin values initialization
	passing->local_loc = multicast_queue->head;
    passing->trigger = 0;
    passing->isAllReaded = 0;

    /*Create socket*/
	sock_fd = socket(AF_INET , SOCK_STREAM , 0);//Upon successful completion, return a non-negative integer, the socket file descriptor
	if (sock_fd == -1)
	{
		perror("Socket failed");
        exit(EXIT_FAILURE);
	}
	puts("Socket created");

	bzero(&server_addr, sizeof(server_addr));
    // assign IP, PORT
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(PORT);

	// Convert IPv4 and IPv6 addresses from text to binary
    // form
    /*if (inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr) <= 0) {
        printf("\nInvalid address/ Address not supported \n");
        return -1;
    }*/
 

	//Connect to remote server
	if (connect(sock_fd , (struct sockaddr *)&server_addr , sizeof(server_addr)) < 0)
	{
		perror("Connect failed");
		exit(EXIT_FAILURE);
	}
	puts("Connected\n");

	//now, sock_fd can be used for send-recv data
	while(1)
	{
		if(threadExistFlag == 0)
		{
			printf("Command \"SEND-MES:(SEND MES) FETCHIF FETCH AUTO NOAUTO QUIT\": ");
		}
		fgets(command, 550, stdin);

		/* Remove trailing newline, if there. */
		if ((strlen(command) > 0) && (command[strlen (command) - 1] == '\n'))
			command[strlen (command) - 1] = '\0';

		token = strtok(command, " ");//get the first token
		strcpy(command, token);
		
		if(strcmp(command, "SEND") == 0 && threadExistFlag == 0)//send message over socket
		{
			token = strtok(NULL, " ");
			if(token != NULL)
			{
				strcpy(message, token);//for the second time, token includes the message
				puts(message);
				send(sock_fd, message, 512, 0);	
			}
		}
		else if(strcmp(command, "FETCHIF") == 0 && threadExistFlag == 0)//if message exist, then write. If not, then puts "no new mess"
		{
			if(isEmptyBucket(multicast_queue) == -1)//empty bucket
			{
				puts("Empty bucket\n");
			}
            else if(noNewMessage(*passing, multicast_queue) == -1)//Client read all messages
			{
				puts("No new messages\n");
			}
			else
			{				
                fetchNewMessage(passing, multicast_queue);//fetching new message for this client process
			}			
		}
		else if(strcmp(command, "FETCH") == 0 && threadExistFlag == 0)//if message exist, then write. If not, then be blocked and wait
		{
            if(noNewMessage(*passing, multicast_queue) == -1 || isEmptyBucket(multicast_queue) == -1)//empty bucket or there is no new message for the client
			{
				//puts("No new messages. Blocking...\n");

				sem_post(&multicast_queue->c_sem);//inc 1
				sem_wait(&multicast_queue->s_sem); //if 0, then wait
				sem_wait(&multicast_queue->c_sem);//dec 1

				int value; 
				sem_getvalue(&multicast_queue->c_sem, &value);//if there are other processes waiting wake them up too
				if(value != 0 )//if not, s_sem must be remains as 0
				{
					sem_post(&multicast_queue->s_sem);//wake the other process
				}
				fetchNewMessage(passing, multicast_queue);//now this client can fetch the new message
			}
			else
			{
				fetchNewMessage(passing, multicast_queue);
			}

		}
		else if(strcmp(command, "AUTO") == 0 && threadExistFlag == 0)//a thread serves the client process for auto mode
		{//In the AUTO mode, client automatically fetches messages as soon as they arrive 
		//with help of the shared memory synchronization between the processes.
			
            passing->multicast_queue = multicast_queue;
            if(pthread_create( &_thread , NULL ,  auto_handler , passing) < 0)//create thread
            {
                perror("Could not create thread");
                exit(EXIT_FAILURE);
            }
			threadExistFlag = 1;
		}
		else if(strcmp(command, "NOAUTO") == 0)//demand base mode
		{
			if(threadExistFlag == 1)//there is a thread and it must be killed
			{
				sem_wait(&multicast_queue->c_sem);
				printf("Thread canceling..\n");
				pthread_cancel(_thread);
				threadExistFlag = 0;
			}	
			printf("NOAUTO mod. Demand base..\n");
		}

		else if(strcmp(command, "QUIT") == 0 && threadExistFlag == 0)
		{
			//send quit message over socket
			send(sock_fd, command, 512, 0);
			
			//with every quit command, between local loc(client's index on queue)-tail on queue is decremented on alive_unread because now there is one less client to read thoose messages
			if(!(multicast_queue->head == multicast_queue->tail && multicast_queue->isFull == 0))//not empty
			{
				if(passing->isAllReaded != 1)//if true, client read all messages so no need to dec alive_unread
				{
					multicast_queue->alive_unread[passing->local_loc]--;
					passing->local_loc = (passing->local_loc+1)%messageNumber;
					while(passing->local_loc != multicast_queue->tail)
					{
						multicast_queue->alive_unread[passing->local_loc]--;
						passing->local_loc = (passing->local_loc+1)%messageNumber;
					}
				}

			}
			multicast_queue->number_of_clients--;
			printf("[-]Disconnected from server.\n");
			close(sock_fd);
			close(shm_fd);
			munmap(multicast_queue, SIZE);
			exit(1);
			
		}
		
        else
        {
            puts("Wrong command\n");
        }

	}
}


void* auto_handler(void* passingValues)//auto thread func
{
    struct passingValues* passing = (struct passingValues*) passingValues;
    int press;
	printf("\nAUTO MODE...Type NOAUTO to exit\n");
    while(1)
	{

		if(noNewMessage(*passing, passing->multicast_queue) == -1 || isEmptyBucket(passing->multicast_queue) == -1)//taildan 1 arkadaysa tüm mesajları okumuş
		{
			//puts("No new messages. Blocking...\n");//bloke ol//yeni mesaj geldiğinde uyan

			sem_post(&passing->multicast_queue->c_sem);////inc 1 (number of pending processes)
			sem_wait(&passing->multicast_queue->s_sem); //if 0, then wait
			sem_wait(&passing->multicast_queue->c_sem);//dec 1 (number of pending processes)

			int value; 
			sem_getvalue(&passing->multicast_queue->c_sem, &value);////if there are other processes waiting wake them up too
			if(value != 0 )//if not, s_sem must be remains as 0
			{
				sem_post(&passing->multicast_queue->s_sem);//wake the other process
			}
			//fetchNewMessage(passing, passing->multicast_queue);//now this client can fetch the new message
		}
		else//Here, all messages are written to the console in order until they are read.
		{
            fetchNewMessage(passing, passing->multicast_queue);
		}
	}
}
int isEmptyBucket(struct multicast_queue *multicast_queue)//bucket is empty or not
{
    if(multicast_queue->head == multicast_queue->tail && multicast_queue->isFull == 0)//head taila eşit ve bucket ful değil, o zaman boş
	{
		return -1;
	}
    return 0;
}
int noNewMessage(struct passingValues passing, struct multicast_queue *multicast_queue)//there exist new message for the client or not
{
    if((passing.local_loc + 1)%messageNumber == multicast_queue->tail && passing.isAllReaded == 1 && passing.trigger == multicast_queue->newM)
	//If local_loc is missing 1 from the tail, it means that client has read all the messages and no new messages have been received.
	{
		return -1;		
	}
    return 0;
}
void fetchNewMessage(struct passingValues *passing, struct multicast_queue *multicast_queue)//fetching new message for the client
{
    if(passing->isAllReaded == 1)
	{
		passing->local_loc = (passing->local_loc+1)%messageNumber;
	}

	passing->isAllReaded = 0;
	printf("%s\n", multicast_queue->bucket[passing->local_loc]);//display new message on console
	multicast_queue->alive_unread[passing->local_loc]--;
			
	if((passing->local_loc + 1)%messageNumber == multicast_queue->tail)
	{
		passing->isAllReaded = 1;//all messages read
		passing->trigger = multicast_queue->newM;
	}
	else
	{
		passing->local_loc = (passing->local_loc+1)%messageNumber;
	}
}
