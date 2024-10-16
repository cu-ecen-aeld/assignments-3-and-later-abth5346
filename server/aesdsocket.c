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


#define PORT_NUM "9000"
#define BACKLOG_QUEUE 10     
#define LOG_FILE_PATH "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

bool sig_caught = false;

//signal handler
static void signal_handler(int signal_number){
	sig_caught = true;
	syslog(LOG_DEBUG, "Singal caught");
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
int socket_handler(int connFd, int logFd, int bytesReturned){

	bool packetsCompleted = false;
	bool newlinefound = false;
	int bytesRecv = 0;
	int count = 1;
	char * recvBuff = (char *)malloc(BUFFER_SIZE * sizeof(char));


	//read while there are still packets to be recieved
	while(!packetsCompleted){

		//recevied packets from client
		bytesRecv = recv(connFd, recvBuff, sizeof(recvBuff), 0);
		
		//newline char recieved
		if (strchr(recvBuff, '\n') != NULL){
			packetsCompleted = true;
			newlinefound = true;
		}

		//no more bytes to be recieved
		if(bytesRecv <= 0){
			packetsCompleted = true;
			break;
		}
		
		//grow array if need be by multiple of counter
		if (bytesReturned + bytesRecv >= BUFFER_SIZE * count * sizeof(char)){
			count++;
			char * temp = (char *)realloc(recvBuff, BUFFER_SIZE * count * sizeof(char));
			recvBuff = NULL;
			recvBuff = temp;
		}

		//track bytes received
		bytesReturned += bytesRecv;
	
		//move file pointer to end to append and append buffer from client to file
		lseek(logFd, 0, SEEK_END);
		int bytesToWrite = write(logFd, &recvBuff, bytesRecv);

		if (bytesToWrite <= 0){
			syslog(LOG_ERR, "Error, could not write to file");
			return(-1);
		} 
		else if (bytesToWrite < bytesRecv){
			syslog(LOG_ERR, "Error, incomplete write to file");
			return(-1);
		}

	}
	
	char sendBuff[bytesReturned];

	//if new line is found read back data to client
	if (newlinefound == true){
		//reset file pointer to start to send entire file and copy file contents
		lseek(logFd, 0, SEEK_SET);
		int bytesToRead = read(logFd, &sendBuff, bytesReturned);

		//send file contents to client
		int bytesSent = send(connFd, &sendBuff, bytesToRead, 0);
		
		if (bytesToRead != bytesSent) {
			syslog(LOG_ERR, "Error sending data back to client");
			return(-1);
		}
	}
	
	free(recvBuff);

	//return total bytes read/sent
	return bytesReturned;
}

int main(int argc, char** argv){


	int sockfd;
	int bytesReturned = 0;

	//open syslog
	openlog (NULL, 0, LOG_USER);

	//create and setup socket
	if((sockfd = init_socket()) == -1){
		syslog(LOG_ERR, "Could not setup server socket");
		return -1;
	}

	//open log to write client to
	int logFd = open(LOG_FILE_PATH, O_RDWR | O_CREAT, S_IRWXU | S_IRWXG | S_IRWXO);
	if (logFd == -1) 
	{
		syslog(LOG_ERR, "Error: (%s) while opening %s", strerror(errno), LOG_FILE_PATH);
		return(-1);
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
		exit(-1);
	}

	bool read_error = false;

	//while signal or client read error is not found
	while(!sig_caught && !read_error){
		
		//wait for connection
		struct sockaddr_storage connAddr;
	  	socklen_t addrLen = sizeof(connAddr);
	    int connFd = accept(sockfd, (struct sockaddr *)&connAddr, &addrLen);
		
		if (connFd < 0){
			syslog(LOG_ERR, "Error accepting connection on socket with error: %s", strerror(errno));
			exit(-1);
		}

		//print out connection IP address
		char addrStr[INET_ADDRSTRLEN];
		inet_ntop(connAddr.ss_family, &(((struct sockaddr_in *)&connAddr)->sin_addr), addrStr, sizeof(connAddr));
		syslog(LOG_DEBUG, "Accepted connection from %s", addrStr);

		//socket read operation to read from client and send back based on total number of bytes tracked
		if((bytesReturned = socket_handler(connFd, logFd, bytesReturned)) == -1){
			syslog(LOG_ERR, "Error could execute socket handler");
			read_error = true;
		}

	}

	//cleanup process
	closelog();
	
	close(logFd);
	remove(LOG_FILE_PATH);
	
	shutdown(sockfd, SHUT_RDWR);
	close(sockfd);

	if(read_error) return -1;

	return 0;	

}
