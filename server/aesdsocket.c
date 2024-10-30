#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <syslog.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <pthread.h>
#include <time.h>
#include "queue.h"

#define PORT_NUM "9000"
#define BACKLOG_QUEUE 10     
#define BUFFER_SIZE 1024
#define TIME_BUFFER_SIZE 100
#define SLEEP_TIME_S 10

#define USE_AESD_CHAR_DEVICE 0

#if USE_AESD_CHAR_DEVICE
    #define LOG_FILE_PATH "/dev/aesdchar"
#else
    #define LOG_FILE_PATH "/var/tmp/aesdsocketdata"
#endif

//global defs
bool sig_caught = false;
int sockfd;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

//linked list head decloration
SLIST_HEAD( ThreadHead, ThreadParams ) threadHead;

//thread parameter struct
struct ThreadParams
{
    pthread_t threadId;
    bool threadDone;

    //hold unique client fd
    int connFd;
    
    SLIST_ENTRY( ThreadParams ) entries;
};



//signal handler
static void signal_handler(int signal_number){
	if (signal_number == SIGINT || signal_number == SIGTERM){
		sig_caught = true;
		syslog(LOG_DEBUG, "Singal caught");
	}
}

//initialize signals to catch
static void init_signals(){
	struct sigaction sa = {0};
	sig_caught = false;
    sa.sa_handler=signal_handler;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
}

//Source: https://beej.us/guide/bgnet/html/
//Sets up socket server
static int init_socket(){

	struct addrinfo hints, *res;
	int sockfd;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;      
	hints.ai_socktype = SOCK_STREAM; 
	hints.ai_flags = AI_PASSIVE;   
	
	//obtain server address for given port number
	if (getaddrinfo(NULL, PORT_NUM, &hints, &res) != 0) {
		syslog(LOG_ERR, "Error getaddrinfo: %s\n", strerror(errno));
	    return -1;
	}
	
	//create socket fd
	if((sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol)) == -1){
		syslog(LOG_ERR, "Error socket: %s\n", strerror(errno));
	    return -1;
	}


	//set socket option to allow for port reuse
	if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) != 0){
		syslog(LOG_ERR, "Error setting socket option: %s\n", strerror(errno));
	    return -1;
	}

	//bind created socket to server
	if(bind(sockfd, res->ai_addr, res->ai_addrlen) != 0){
		syslog(LOG_ERR, "Error binding socket: %s\n", strerror(errno));
	    return -1;
	}

	//free address list object
    freeaddrinfo(res);

    return sockfd;
}

//socket server task
void *socket_handler ( void *arg )
{

    FILE *logFd;
    struct ThreadParams *threadP = (struct ThreadParams *) arg;
    int connFd = threadP->connFd;
    
    bool packetsCompleted = false;
	bool newlinefound = false;
    ssize_t bytesRecv;

    char recvBuff[BUFFER_SIZE];

    //read while there are still packets to be recieved
    while (!packetsCompleted)
    {
        pthread_mutex_lock(&mutex);

        //recevied packets from client until no more bytes to be recieved
        if ((bytesRecv += recv(connFd, recvBuff, sizeof(recvBuff), 0)) <= 0){
            packetsCompleted = true;
			break;
        }

        int bytesRecvApp = bytesRecv;

        //open log file to append
        logFd = fopen(LOG_FILE_PATH, "a");
        if (logFd == NULL){
            syslog(LOG_ERR, "Error could not write to file");
            pthread_mutex_unlock(&mutex);
            threadP->threadDone = true;
            close(connFd);
            pthread_exit(NULL);
        }

        //write to log file
        fwrite(recvBuff, sizeof(recvBuff[0]), bytesRecvApp, logFd);
        fclose(logFd);

        //newline char recieved
        if (strchr(recvBuff, '\n') != NULL){
            packetsCompleted = true;
            newlinefound = true;
        }

        pthread_mutex_unlock(&mutex);
    }


    //if new line is found read back data to client
    if(newlinefound){

        pthread_mutex_lock(&mutex);
        //open file to read only
        logFd = fopen(LOG_FILE_PATH, "r");
        if (logFd == NULL)
        {
            syslog(LOG_ERR, "Error could not write to file");
            pthread_mutex_unlock(&mutex);
            threadP->threadDone = true;
            close(connFd);
            pthread_exit(NULL);
        }

        int bytesSent = 0;

        //send file contents to client
        while ((bytesRecv = fread(recvBuff, sizeof(recvBuff[0]), sizeof( recvBuff ), logFd)) > 0)
        {
            bytesSent += send(connFd, recvBuff, bytesRecv, 0);
        }

        //make sure bytes sent and recived are the same
        if(bytesRecv != bytesSent){
            syslog(LOG_ERR, "Error sending data back to client");
            pthread_mutex_unlock(&mutex);
            threadP->threadDone = true;
            close(connFd);
            pthread_exit(NULL);
        }

        fclose(logFd);

        pthread_mutex_unlock(&mutex);
    }
    
    threadP->threadDone = true;

    //close client connection and exit
    close(connFd);
    pthread_exit(NULL);
}

void *appendTimestamp (void *arg){
    FILE *logFd;

    char timestamp[TIME_BUFFER_SIZE];
    struct timespec sleepDuration = {SLEEP_TIME_S, 0}; 

    struct timespec currTime;
    struct tm timeStuct;

    while (!sig_caught)
    {
        //get current time to closest ns
        if (clock_gettime(CLOCK_REALTIME, &currTime) == -1)
        {
            syslog(LOG_ERR, "Could not recieve current time");
            pthread_exit(NULL);
        }

        //convert from timespec to time struct
        localtime_r(&currTime.tv_sec, &timeStuct);

        //format to valid timestamp string
        strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %T %z", &timeStuct);

        pthread_mutex_lock(&mutex);
        
        //open log file to append
        logFd = fopen(LOG_FILE_PATH, "a");
        if ( logFd == NULL )
        {
            syslog(LOG_ERR, "Error could not write to file");
            pthread_mutex_unlock( &mutex );
            pthread_exit( NULL );
        }

        //add new line as last char
        size_t length = strlen(timestamp) + 1;
		timestamp[length -1] = '\n';

        // Write the timestamp to the file
        if (fwrite(timestamp, 1, length, logFd) != length)
        {
            syslog(LOG_ERR, "Error could not write to file");
            fclose(logFd);
            pthread_mutex_unlock(&mutex);
            pthread_exit(NULL);
        }

        fclose(logFd);
        pthread_mutex_unlock(&mutex);

        //sleep for 10 sec
        nanosleep(&sleepDuration, NULL);
    }

    return 0;
}

// Main function
int main ( int argc, char *argv[] )
{
	//open syslog
	openlog (NULL, 0, LOG_USER);
    
	//create and setup socket
	if((sockfd = init_socket()) == -1){
		syslog(LOG_ERR, "Could not setup server socket");
        closelog();
        exit(-1);
	}

 	//enter daemon mode if specified
	//https://stackoverflow.com/questions/5384168/how-to-make-a-daemon-process
	if(argc > 1){
		if(strcmp(argv[1], "-d") == 0){
			if(daemon(0,0) == -1) {
				syslog(LOG_ERR, "Could not spawn daemon: %s\n", strerror(errno));
			}
		}
	}

    //map signals needed to terminate gracefully
	init_signals();

	//listen for client connections
	if(listen(sockfd, BACKLOG_QUEUE) != 0){
		syslog(LOG_ERR, "Could listen for client: %s\n", strerror(errno));
        closelog();
        exit(-1);
	}

    //initializte linked list
    SLIST_INIT(&threadHead);

#if (USE_AESD_CHAR_DEVICE == 0)
    //create and spawn thread for timestamping
    pthread_t timestampThread;
    if (pthread_create( &timestampThread, NULL, appendTimestamp, NULL ) != 0)
    {
        syslog(LOG_ERR, "Could not spawn timestamping thread");
        closelog();
        exit(-1);
    }
#endif
    while (!sig_caught)
    {
        //wait for connection
		struct sockaddr_storage connAddr;
	  	socklen_t addrLen = sizeof(connAddr);
	    int connFd = accept(sockfd, (struct sockaddr *)&connAddr, &addrLen);

        if (connFd < 0){
			syslog(LOG_ERR, "Error accepting connection on socket with error: %s", strerror(errno));
			close(connFd);
            continue;
		}

		//print out connection IP address
		char addrStr[INET_ADDRSTRLEN];
		inet_ntop(connAddr.ss_family, &(((struct sockaddr_in *)&connAddr)->sin_addr), addrStr, sizeof(connAddr));
		syslog(LOG_DEBUG, "Accepted connection from %s", addrStr);

        //create new client conn thread
        struct ThreadParams *threadP = (struct ThreadParams *)malloc(sizeof(struct ThreadParams));
        threadP->connFd = connFd;
        threadP->threadDone = false;
        //create client connection thread
        if ( pthread_create( &threadP->threadId, NULL, socket_handler, threadP ) != 0 ){
            close(connFd);
            free(threadP);
            continue;
        }

        //insert thread at head to list
        SLIST_INSERT_HEAD(&threadHead, threadP, entries);

        //loop through thread entries and join thread
        struct ThreadParams *currThread, *nxtThread;
        SLIST_FOREACH_SAFE(currThread, &threadHead, entries, nxtThread){
            if (currThread->threadDone){
                pthread_join(currThread->threadId, NULL);
                SLIST_REMOVE(&threadHead, currThread, ThreadParams, entries);
                free(currThread);
            }
        }
    }
    
#if (USE_AESD_CHAR_DEVICE == 0)
    pthread_cancel(timestampThread);
#endif

    struct ThreadParams *currThread, *nxtThread;
    SLIST_FOREACH_SAFE(currThread, &threadHead, entries, nxtThread){
        pthread_join(currThread->threadId, NULL);
        SLIST_REMOVE(&threadHead, currThread, ThreadParams, entries);
        free(currThread);
    }

    pthread_mutex_destroy(&mutex);

    closelog();
    remove(LOG_FILE_PATH);
    shutdown(sockfd, SHUT_RDWR);

    return 0;
}
