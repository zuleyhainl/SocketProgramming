#include <stdio.h>
#include <string.h>	//strlen
#include <stdlib.h>	//strlen
#include <sys/socket.h>
#include <arpa/inet.h>	//inet_addr
#include <unistd.h>	
#include <sys/ipc.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>//shm, mmap
#include <netinet/in.h>//
#include <semaphore.h>

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



int agent(int client_sock, struct multicast_queue *multicast_queue);//child process main func.

int main() {
	
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

	/*POSIX SEMAPHORE*/
	//if the second parameter is equal to 1: semaphore can be used betweens processes
	//if the second parameter is equal to 0: semaphore can be used betweens threads
	if (sem_init(&multicast_queue->m_sem, 1, 1) == -1) //mutex semaphore (binary). Initial value 1
	{
		perror("Failed to initialize binary semaphore m_sem");
	}
	if (sem_init(&multicast_queue->s_sem, 1, 0) == -1) //senk semaphore (binary). Initial value 0
	{
		perror("Failed to initialize semaphore s_sem");
	}
	if (sem_init(&multicast_queue->c_sem, 1, 0) == -1) //counter semaphore. Initial value 0
	{
		perror("Failed to initialize semaphore c_sem");
	}

	multicast_queue->head = 0;
	multicast_queue->tail = 0;
	multicast_queue->isFull = 0;
	multicast_queue->number_of_clients = 0;
	multicast_queue->newM = 0;

	for (int i = 0; i < messageNumber; i++)//initialize all indexes as 0
	{
		multicast_queue->alive_unread[i] =  0;
	}
	
    
	int socket_fd, new_socket , len, opt = 1;
	struct sockaddr_in server_addr;
	

    /*Create socket*/
	if ((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Forcefully attaching socket to the port 8080
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) //Prevents error such as: “address already in use”.
	{
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }
	
	bzero(&server_addr, sizeof(server_addr));
	//Prepare the sockaddr_in structure
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(PORT);
	
	//binding socket to the address and port 8086
	if(bind(socket_fd,(struct sockaddr *)&server_addr , sizeof(server_addr)) == -1)
	{
		perror("Bind failed");
		exit(EXIT_FAILURE);
	}
	puts("Bind done");
	
	//Listen
	if(listen(socket_fd , 3) == -1)
	{
		perror("Listen failed");
		exit(EXIT_FAILURE);
	}

	puts("Waiting for incoming connections...");
	len = sizeof(server_addr);
	
    pid_t childpid;//fork için
    while(1)
	{
		new_socket = accept(socket_fd, (struct sockaddr *)&server_addr, (socklen_t*)&len);
		if(new_socket < 0)
		{
			perror("Accept failed");
			exit(EXIT_FAILURE);
		}
		
		//for each client, server forks a new child process which is agent.
		//this agent process serves the client. Parent process continues to accept connections		
		if ((childpid = fork()) == -1)//error
		{
			close(new_socket);
			perror("Fork Error");
			exit(EXIT_FAILURE);
		}
		else if(childpid == 0)//child process
		{
			//agent
			close(socket_fd);
			while(1)
			{
				//agent func
				int k;
				if((k = agent(new_socket, multicast_queue)) == -1)//client send quit, agent returns -1
				{
					exit(0);
				} 
			}				
		}
			
		else if(childpid > 0)//returns chilpid for the parent process
		{
			
			close(new_socket);//new-socket for send-recv data. Parent process not use it

			//with every accepted conn, between head-tail on queue is incremented on alive_unread because now there is one more client to read thoose messages
			multicast_queue->number_of_clients++;
			if(!(multicast_queue->head == multicast_queue->tail && multicast_queue->isFull == 0))//queue not empty
			{
				int i = multicast_queue -> head;
				multicast_queue->alive_unread[i]++;
				i = (i+1)%messageNumber;
				while(i != multicast_queue->tail)
				{
					multicast_queue->alive_unread[i]++;
					i = (i+1)%messageNumber;
				}
			}
		}				
	}

	//when servers exit
    munmap(multicast_queue, SIZE);
    close(shm_fd);
    close(new_socket);
    shm_unlink(NAME);
	return 0;
}

int agent(int new_socket, struct multicast_queue *multicast_queue)
{
	char client_message[512];
	recv(new_socket, client_message, 512, 0);
	
    if(strcmp(client_message, "QUIT") == 0 || strcmp(client_message, "") == 0)
    {
    	//printf("A client disconnected from server..\n");
        return -1;
    }

	
    else
    {
    //critical section
	sem_wait(&multicast_queue->m_sem);//if sem = 0 wait, if sem = 1 then pass and dec sem
										//lock m_sem so other clients want to write must wait
        
		if(multicast_queue->isFull == 1)//bucket ful. We need to check can we write the new message to head index on queue
		{
			
			if(multicast_queue->alive_unread[multicast_queue->head] == 0)//every client read that message index so we can write the new message here
			{																//after that we need to wake the processes which are waiting for new message
				multicast_queue->head = (multicast_queue->head +1) % messageNumber;
				multicast_queue->isFull = 0;//now the queue is not full
				multicast_queue->newM ++;//this is a bad solution for the algorithm.newM used like a trigger
			}
			else
			{
				puts("Bucket full");
			}
			
		}
		if(multicast_queue->isFull != 1)//is bucket full or not
		{
			printf("client message \"%s\"\n", client_message);
			memset(multicast_queue->bucket[multicast_queue->tail],0,512);
			strcpy(multicast_queue->bucket[multicast_queue->tail], client_message);
			multicast_queue->alive_unread[multicast_queue->tail]+=multicast_queue->number_of_clients;

			multicast_queue->tail = (multicast_queue->tail+1)%messageNumber;//now tail is on the index where the new incoming message can be written.
			
			if(multicast_queue->tail == multicast_queue->head)//if tail = head, sign as bucket full
			{
				multicast_queue->isFull = 1;//bucket full
			}

			int value; 
			sem_getvalue(&multicast_queue->c_sem, &value);//if there are  processes waiting for new message, wake them up
			if(value != 0 )
			{
				sem_post(&multicast_queue->s_sem);//wake the process
			}
		}
		
		bzero(client_message, 512);
		//end of critical section
		sem_post(&multicast_queue->m_sem);
		return 0;
                       
	}			
}

