/* A simple echo client using TCP */
#include <stdio.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>



#define SERVER_TCP_PORT 3000	/* well-known port */
#define BUFLEN		255	/* buffer length */
#define FILESIZE	1024	/* File size */

int receiveFile(int sd, char fileName[])
{
	char	fileBuf[FILESIZE];
	int	n;
	FILE	*file;
	
	file = fopen(fileName, "w");	// Create file
	if (file == NULL) {
		fprintf(stderr, "Can't create file \n");
		return 1;
	}
	
	while (1) {
		n = recv(sd, fileBuf, FILESIZE, 0);
		if (n  <= 0){					// When no more data, break loop
			break;
		}
		if (strstr(fileBuf, "[E]") != NULL){	// If error received, print error
			printf("%s", fileBuf);
			remove(fileName);			// Delete file created
			return 1;
		}
		fprintf(file, "%s", fileBuf);			// Write to file
		bzero(fileBuf, FILESIZE);			// Erases fileBuf data
	
	}
	return 0;
}

int main(int argc, char **argv)
{
	int 	n, successfulReceive;
	int 	sd, port;
	struct	hostent		*hp;
	struct	sockaddr_in server;
	char	*host, *bp, fileName[BUFLEN];

	switch(argc){
	case 2:
		host = argv[1];
		port = SERVER_TCP_PORT;
		break;
	case 3:
		host = argv[1];
		port = atoi(argv[2]);
		break;
	default:
		fprintf(stderr, "Usage: %s host [port]\n", argv[0]);
		exit(1);
	}

	/* Create a stream socket	*/	
	if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		fprintf(stderr, "Can't creat a socket\n");
		exit(1);
	}

	bzero((char *)&server, sizeof(struct sockaddr_in));
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	if (hp = gethostbyname(host)) 
	  bcopy(hp->h_addr, (char *)&server.sin_addr, hp->h_length);
	else if ( inet_aton(host, (struct in_addr *) &server.sin_addr) ){
	  fprintf(stderr, "Can't get server's address\n");
	  exit(1);
	}

	/* Connecting to the server */
	if (connect(sd, (struct sockaddr *)&server, sizeof(server)) == -1){
	  fprintf(stderr, "Can't connect \n");
	  exit(1);
	}
	
	printf("\nEnter file name: \n");
	while(n=read(0, fileName, BUFLEN)){	// Get user input file name
	  break;
	}

	fileName[n-1]='\0';			// Replace \n with null terminating char
	write(sd,fileName,n);			// Write file name to server
	
	printf("Server: \n");
	successfulReceive = receiveFile(sd, fileName);	// Get file
	if (successfulReceive == 0)
		printf("File Received\n");

	close(sd);
	return(0);
}
