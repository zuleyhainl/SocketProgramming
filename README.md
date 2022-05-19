# socketProgramming
## This repo includes socket programming with shared memory implementation. This is a client-server project. There is also a shared queue and each client can read and write on it. When a client connect to the server, server forks a new child process and this child serves to the client.
## Client can handle these commands:
### SEND MES: sending message over socket
### FETCHIF: fetch next message if exist
### FETCH: fetch next message if exist, otherwise wait (get blocked)
### AUTO: fetch messages automatically with the help of thread
### NOAUTO: eixt AUTO mode. Demand base
### QUIT: exiting

#### synchronization tools such as semaphores were used for this project
