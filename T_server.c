/* UDP File Download Server */
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>

#define SERVER_UDP_PORT 3000	/* well-known port */
#define BUFLEN		100	/* buffer length for data field */

/* PDU Structure */
struct pdu {
	char type;
	char data[BUFLEN];
};

int main(int argc, char **argv)
{
	int 	sd, n, port;
	struct	sockaddr_in server, client;
	socklen_t client_len;
	struct pdu rpdu, spdu;
	FILE *file;
	struct stat st;
	long file_size, bytes_sent;
	
	switch(argc){
	case 1:
		port = SERVER_UDP_PORT;
		break;
	case 2:
		port = atoi(argv[1]);
		break;
	default:
		fprintf(stderr, "Usage: %s [port]\n", argv[0]);
		exit(1);
	}
	
	/* Create a datagram socket	*/
	if ((sd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
		fprintf(stderr, "Can't create a socket\n");
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
	
	printf("UDP File Download Server running on port %d\n", port);
	
	while(1) {
		client_len = sizeof(client);
		
		/* Wait for FILENAME PDU from client */
		n = recvfrom(sd, &rpdu, sizeof(rpdu), 0, 
		             (struct sockaddr *)&client, &client_len);
		
		if (n < 0) {
			fprintf(stderr, "recvfrom error\n");
			continue;
		}
		
		if (rpdu.type == 'C') {  // FILENAME PDU
			printf("\nClient requested file: '%s'\n", rpdu.data);
			
			/* Add current directory to filename */
			char filePath[BUFLEN + 3];
			snprintf(filePath, sizeof(filePath), "./%s", rpdu.data);
			
			/* Try to open the file */
			file = fopen(filePath, "r");
			
			if (file == NULL) {
				/* Send ERROR PDU */
				printf("File not found\n");
				spdu.type = 'E';
				strcpy(spdu.data, "FILE NOT FOUND");
				sendto(sd, &spdu, strlen(spdu.data) + 2, 0,
				       (struct sockaddr *)&client, client_len);
			}
			else {
				/* Get file size using stat */
				stat(filePath, &st);
				file_size = st.st_size;
				bytes_sent = 0;
				
				printf("File found. Size: %ld bytes\n", file_size);
				printf("Sending file...\n");
				
				/* Read and send file in chunks */
				while (!feof(file)) {
					bzero(spdu.data, BUFLEN);
					n = fread(spdu.data, 1, BUFLEN, file);
					
					if (n > 0) {
						bytes_sent += n;
						
						/* Check if this is the last batch */
						if (bytes_sent >= file_size || feof(file)) {
							spdu.type = 'F';  // FINAL PDU
						}
						else {
							spdu.type = 'D';  // DATA PDU
						}
						
						/* Send PDU to client */
						sendto(sd, &spdu, n + 1, 0,
						       (struct sockaddr *)&client, client_len);
					}
				}
				
				fclose(file);
				printf("Successfully sent file (%ld bytes)\n\n", bytes_sent);
			}
		}
	}
	
	close(sd);
	return(0);
}
