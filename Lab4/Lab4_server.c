/* A simple echo server using TCP */
#include <stdio.h>
#include <sys/types.h>
#include <sys/unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/signal.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>


#define SERVER_TCP_PORT 3000	/* well-known port */
#define BUFLEN		255	/* buffer length */
#define FILESIZE	1024	/* File size */

int fileTransfer(int);
void sendFile(int, FILE *);
void reaper(int);

int main(int argc, char **argv)
{
	int 	sd, new_sd, client_len, port;
	struct	sockaddr_in server, client;

	switch(argc){
	case 1:
		port = SERVER_TCP_PORT;
		break;
	case 2:
		port = atoi(argv[1]);
		break;
	default:
		fprintf(stderr, "Usage: %s [port]\n", argv[0]);
		exit(1);
	}

	/* Create a stream socket	*/	
	if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		fprintf(stderr, "Can't creat a socket\n");
		exit(1);
	}

	/* Bind an address to the socket	*/
	bzero((char *)&server, sizeof(struct sockaddr_in));
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	server.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(sd, (struct sockaddr *)&server, sizeof(server)) == -1){
		fprintf(stderr, "Can't bind name to socket\n");
		exit(1);
	}

	/* queue up to 5 connect requests  */
	listen(sd, 5);

	(void) signal(SIGCHLD, reaper);

	while(1) {
	  client_len = sizeof(client);
	  new_sd = accept(sd, (struct sockaddr *)&client, &client_len);
	  if(new_sd < 0){
	    fprintf(stderr, "Can't accept client \n");
	    exit(1);
	  }
	  switch (fork()){
	  case 0:		/* child */
		(void) close(sd);
		exit(fileTransfer(new_sd));
	  default:		/* parent */
		(void) close(new_sd);
		break;
	  case -1:
		fprintf(stderr, "fork: error\n");
	  }
	}
}

/*	File Transfer program	*/
int fileTransfer(int sd)
{
	char	fileName[BUFLEN];
	char	fileNotFound[] = "[E] FILE NOT FOUND\n";
	int 	n;
	FILE	*file;
	
	printf("Awaiting file name from client...\n");
	while (n = read(sd, fileName, BUFLEN)){
		break;
	}

	char filePath[n+2];	// Add current directory to file name
	snprintf(filePath, sizeof(filePath), "%s%s", "./", fileName);
	printf("File Path = '%s'\n", filePath);
	
	file = fopen(filePath, "r");	
	if (file == NULL) {			// File does not exist
		printf("%s\n", fileNotFound);
		write(sd, fileNotFound, sizeof(fileNotFound));
	}
	else {
		printf("File Found\n");
		char sendMsg[] = "Sending file...\n";
		printf("%s", sendMsg);
		sendFile(sd, file);
		printf("Successfuly sent file\n\n");
	}
	fclose(file);
	close(sd);

	return(0);
}

void sendFile(int sd, FILE *p)
{
	char	fileData[FILESIZE] = {0};

	while(fgets(fileData, FILESIZE, p) != NULL) {	// NULL if EOF reached or error occurs
		if(send(sd, fileData, sizeof(fileData), 0) == -1){
		  fprintf(stderr, "Error sending data\n");
		  exit(1);
		}
		bzero(fileData, FILESIZE);	//Erase data
	}
}

/*	reaper		*/
void	reaper(int sig)
{
	int	status;
	while(wait3(&status, WNOHANG, (struct rusage *)0) >= 0);
}
