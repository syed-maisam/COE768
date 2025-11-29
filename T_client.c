/* UDP File Download Client */
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

#define SERVER_UDP_PORT 3000	/* well-known port */
#define BUFLEN		100	/* buffer length for data field */

/* PDU Structure */
struct pdu {
	char type;
	char data[BUFLEN];
};

int receiveFile(int sd, char fileName[])
{
	struct pdu rpdu;
	int n;
	FILE *file;
	
	file = fopen(fileName, "w");
	if (file == NULL) {
		fprintf(stderr, "Can't create file\n");
		return 1;
	}
	
	printf("\nReceiving file...\n");
	
	while (1) {
		bzero(&rpdu, sizeof(rpdu));
		n = read(sd, &rpdu, sizeof(rpdu));
		
		if (n <= 0) {
			fprintf(stderr, "Error receiving data\n");
			fclose(file);
			remove(fileName);
			return 1;
		}
		
		if (rpdu.type == 'E') {
			printf("Server Error: %s\n", rpdu.data);
			fclose(file);
			remove(fileName);
			return 1;
		}
		else if (rpdu.type == 'D') {
			fwrite(rpdu.data, 1, n - 1, file);
		}
		else if (rpdu.type == 'F') {
			fwrite(rpdu.data, 1, n - 1, file);
			break;
		}
	}
	
	fclose(file);
	return 0;
}

int main(int argc, char **argv)
{
	int 	sd, port, successfulReceive;
	struct	hostent *hp;
	struct	sockaddr_in server;
	char	*host;
	struct pdu spdu;
	char choice;
	
	switch(argc){
	case 2:
		host = argv[1];
		port = SERVER_UDP_PORT;
		break;
	case 3:
		host = argv[1];
		port = atoi(argv[2]);
		break;
	default:
		fprintf(stderr, "Usage: %s host [port]\n", argv[0]);
		exit(1);
	}
	
	/* Create a datagram socket */
	if ((sd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		fprintf(stderr, "Can't create a socket\n");
		exit(1);
	}
	
	/* Prepare server address */
	bzero((char *)&server, sizeof(struct sockaddr_in));
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	
	if (hp = gethostbyname(host)) {
		bcopy(hp->h_addr, (char *)&server.sin_addr, hp->h_length);
	}
	else if (inet_aton(host, (struct in_addr *) &server.sin_addr)) {
		/* Address is valid */
	}
	else {
		fprintf(stderr, "Can't get server's address\n");
		exit(1);
	}
	
	/* Connect socket to server address */
	if (connect(sd, (struct sockaddr *)&server, sizeof(server)) == -1) {
		fprintf(stderr, "Can't connect\n");
		exit(1);
	}
	
	printf("Connected to UDP File Download Server\n");
	
	/* Allow multiple file downloads */
	while(1) {
		printf("\n======================================\n");
		printf("Menu:\n");
		printf("  D - Download a file\n");
		printf("  Q - Quit\n");
		printf("Enter choice: ");
		
		scanf(" %c", &choice);
		getchar();  // consume newline
		
		if (choice == 'Q' || choice == 'q') {
			printf("Exiting...\n");
			break;
		}
		else if (choice == 'D' || choice == 'd') {
			printf("\nEnter file name: ");
			
			/* Prepare FILENAME PDU */
			spdu.type = 'C';
			bzero(spdu.data, BUFLEN);
			scanf("%s", spdu.data);
			
			/* Send FILENAME PDU to server */
			write(sd, &spdu, strlen(spdu.data) + 2);
			
			/* Receive file from server */
			successfulReceive = receiveFile(sd, spdu.data);
			
			if (successfulReceive == 0) {
				printf("File '%s' received successfully\n", spdu.data);
			}
		}
		else {
			printf("Invalid choice. Please try again.\n");
		}
	}
	
	close(sd);
	return(0);
}
