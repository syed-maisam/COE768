#include <stdio.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <errno.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <strings.h>
#include <sys/wait.h>
#include <sys/signal.h>


#define QUIT "quit"
#define SERVER_PORT 10000
#define CONNECTION_REQUEST_LIMIT 5
#define BUFLEN 100
#define NAMESIZ 20
#define MAXCON 200

typedef struct {
  char type;
  char data[BUFLEN];
}
PDU;

typedef struct {
  char type;
  char data[BUFLEN];
  int size;
}
SizePDU;

PDU rpdu;

struct {
  int val;
  char name[NAMESIZ];
}
table[MAXCON]; // Registered File Tracking

char usr[NAMESIZ];

int s_sock, peer_port;
int fd, nfds;
fd_set rfds, afds;

void registration(int, char * );
int search_content(int, char * , PDU * );
int client_download(char * , PDU * );
int server_download(int);
void deregistration(int, char * );
void online_list(int);
void local_list();
void quit(int);
void handler();
void reaper(int);
int indexs = 0;
char peerName[10];
int pid;

int main(int argc, char ** argv) {
  int s_port = SERVER_PORT;
  int n;
  int alen = sizeof(struct sockaddr_in);

  struct hostent * hp;
  struct sockaddr_in server;
  char c, * host, name[NAMESIZ];
  struct sigaction sa;
  char dataToSend[100];
  switch (argc) {
  case 2:
    host = argv[1];
    break;
  case 3:
    host = argv[1];
    s_port = atoi(argv[2]);
    break;
  default:
    printf("Usage: %s host [port]\n", argv[0]);
    exit(1);
  }
  // TCP
  int sd, new_sd, client_len, port, m;
  struct sockaddr_in client, client2;
  /* Create a stream socket	*/
  if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    fprintf(stderr, "Can't creat a socket\n");
    exit(1);
  }

  char myIP[16];
  bzero((char * ) & client, sizeof(struct sockaddr_in));
  socklen_t len = sizeof(client);
  if (bind(sd, (struct sockaddr * ) & client, sizeof(client)) == -1) {
    fprintf(stderr, "Can't bind name to socket\n");
    exit(1);
  }

  // Listen up to the limit of connection requests
  listen(sd, CONNECTION_REQUEST_LIMIT);

  (void) signal(SIGCHLD, reaper);

  //UDP 
  memset( & server, 0, alen);
  server.sin_family = AF_INET;
  server.sin_port = htons(s_port);

  if (hp = gethostbyname(host))
    memcpy( & server.sin_addr, hp -> h_addr, hp -> h_length);
  else if ((server.sin_addr.s_addr = inet_addr(host)) == INADDR_NONE) {
    printf("Can't get host entry \n");
    exit(1);
  }
  s_sock = socket(AF_INET, SOCK_DGRAM, 0); // Allocate a socket for the index server

  if (s_sock < 0) {
    printf("Can't create socket \n");
    exit(1);
  }
  if (connect(s_sock, (struct sockaddr * ) & server, sizeof(server)) < 0) {
    printf("Can't connect \n");
    exit(1);
  }
  //Get the Port that the Client has opened
  getsockname(sd, (struct sockaddr * ) & client, & len);
  //memcpy(&client.sin_addr, connectClient->h_addr, connectClient->h_length);
  inet_ntop(AF_INET, & client.sin_addr, myIP, sizeof(myIP));
  int myPort;
  myPort = ntohs(client.sin_port);
  char * ip = inet_ntoa(((struct sockaddr_in * ) & client) -> sin_addr);
  char address[45];
  char cport[25];
  sprintf(address, "%s", myIP);
  sprintf(cport, "%u", myPort);

  /* 	Enter User Name		*/
  printf("Choose a user name\n");
  n = read(0, peerName, 10);

  /* Initialization of SELECT`structure and table structure	*/
  FD_ZERO( & afds);
  FD_SET(s_sock, & afds); /* Listening on the index server socket  */
  FD_SET(0, & afds); /* Listening on the read descriptor   */
  FD_SET(sd, & afds); /* Listening on the client server socket  */

  nfds = 1;
  for (n = 0; n < MAXCON; n++)
    table[n].val = -1;

  // Signal Handler Setup
  sa.sa_handler = handler;
  sigemptyset( & sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, & sa, NULL);
  switch ((pid = fork())) { //Fork for user and for TCP server
  case 0: // child
    while (1) {
      client_len = sizeof(client2);
      new_sd = accept(sd, (struct sockaddr * ) & client2, & client_len);
      if (new_sd < 0) {
        fprintf(stderr, "Can't accept client \n");
        exit(1);
      }
      switch (fork()) {
      case 0: // child 
        (void) close(sd);
        exit(server_download(new_sd)); //Let the Client download the files from this server
      default: // parent 
        (void) close(new_sd);
        break;
      case -1:
        fprintf(stderr, "Error Creating New Process\n");
      }
    }
    default:
      // User Command Loop
      while (1) { 
        printf("Command:\n");

        memcpy( & rfds, & afds, sizeof(rfds));
        if (select(nfds, & rfds, NULL, NULL, NULL) == -1) {
          printf("select error: %s\n", strerror(errno));
          exit(1);
        }

        if (FD_ISSET(0, & rfds)) {
          /* Command from the user  */
          char command[40];
          n = read(0, command, 40);

          //COMMAND HELP ?
          if (command[0] == '?') {
            printf("R-Content Registration; T-Content Deregistration; L-List Local Content\n");
            printf("D-Download Content; O-List all the On-line Content; Q-Quit\n\n");
            continue;
          }

          //REGISTER CONTENT 'R'
          if (command[0] == 'R') {
            rpdu.type = 'R';	//set the PDU type to 'R' indicating a registration request
            printf("Content Name:\n");
            char contentName[20];
            n = read(0, contentName, 20); //Read the content name from user input
            peerName[strcspn(peerName, "\n")] = 0; //Remove any trailing newline characters from peer name
            contentName[strcspn(contentName, "\n")] = 0;
            if (fopen(contentName, "r")==NULL){
               printf("File Does Not Exist!"); //Check if file exists
               continue;
            }
            //Preapare date to be sent: Username, Request Content, IP and Port
            strcpy(dataToSend, peerName);
            strcat(dataToSend, ",");
            strcat(dataToSend, contentName);
            strcat(dataToSend, ",");
            strcat(dataToSend, cport);
            strcpy(rpdu.data, dataToSend);//copy the prepared data into the PDU's data field
            write(s_sock, & rpdu, sizeof(rpdu));//send PDU to server
            PDU r2pdu;
            while (1) {
              n = read(s_sock, & r2pdu, sizeof(r2pdu));//Read resposne PDU from server
              printf("%d\n", n);
              if (r2pdu.type == 'E') {//if the reponse type is 'E', print error message and break loop
                printf("%s\n", r2pdu.data);
                break;
              }
              if (r2pdu.type == 'A') {//if the reponse type is 'A', print acknowledge message and register content locally
                printf("%s\n", r2pdu.data);
                // Register file for local user table	
                table[indexs].val = 1; //mark content as registered in local table
                strcpy(table[indexs].name, contentName);//store content name in local table
                // Index also maps to num files
                indexs++;//increment index for next content
                break;
              }
            }
          }
          // List Content 'L'
          if (command[0] == 'L') {
            local_list();
          }

          // List Online Content 'O'
          if (command[0] == 'O') {
            /* Call online_list()	*/
            rpdu.type = 'O'; //req server online content
            write(s_sock, & rpdu, sizeof(rpdu));
            PDU r2pdu;
            while (1) {
              n = read(s_sock, & r2pdu, sizeof(r2pdu));
              if (r2pdu.type == 'E') {
                printf("%s\n", r2pdu.data);
                break;
              }
              if (r2pdu.type == 'O') {
                printf("%s\n", r2pdu.data);
              }
              if (r2pdu.type == 'A') {
                break;
              }

            }
          }

          // Download Content 'D'
          if (command[0] == 'D') {
            rpdu.type = 'S';//set PDU type to 'S' indicating search request
            printf("Content Name:\n");
            char contentName[20];
            n = read(0, contentName, 20);//Read the content name from user input
            peerName[strcspn(peerName, "\n")] = 0;//Remove any trailing newline characters from peer name

            strcpy(dataToSend, peerName);
            strcat(dataToSend, ",");
            contentName[strcspn(contentName, "\n")] = 0;//Remove any trailing newline characters from content name
            strcat(dataToSend, contentName);
            strcpy(rpdu.data, dataToSend);//copy the prepared data into the PDU's data field

            write(s_sock, & rpdu, sizeof(rpdu));//send PDU to server
            PDU r2pdu;
            while (1) {
              n = read(s_sock, & r2pdu, sizeof(r2pdu));//Read resposne PDU from server
              if (r2pdu.type == 'E') {//if the reponse type is 'E', print error message and break loop
                printf("%s\n", r2pdu.data);
                break;
              }
	      //if the reponse type is 'S', initiate client download process
              if (r2pdu.type == 'S') { //If the server has the content branch to the slient_download to request the data
                printf("%s\n", r2pdu.data);
                client_download(contentName, & r2pdu);
                
		// Register download content 'R'
                rpdu.type = 'R';
                peerName[strcspn(peerName, "\n")] = 0;
                contentName[strcspn(contentName, "\n")] = 0;
                strcpy(dataToSend, peerName);
                strcat(dataToSend, ",");
                strcat(dataToSend, contentName);
                strcat(dataToSend, ",");
                strcat(dataToSend, cport);

                table[indexs].val = 1;//mark content as registered in local table
                strcpy(table[indexs].name, contentName);//store content name in local table
                indexs++;//increment index for next content

                strcpy(rpdu.data, dataToSend);
                write(s_sock, & rpdu, sizeof(rpdu));
                PDU r2pdu;
                while (1) {
                  n = read(s_sock, & r2pdu, sizeof(r2pdu));//Read response PDU from server
                  //printf("%d\n",n);
                  if (r2pdu.type == 'E') {//if the reponse type is 'E', print error message and break loop
                    printf("%s\n", r2pdu.data);
                    break;
                  }
                  if (r2pdu.type == 'A') {//if the reponse type is 'A', print acknowledge message and break loop
                    printf("%s\n", r2pdu.data);
                    break;
                  }
                }
                break;
              }
            }
          }

          // Deregister Content 'T'
          if (command[0] == 'T') {
            rpdu.type = 'T';//set PDU type to 'S' indicating deregistration request

            char contentName[20];
            local_list(); //list the local content and user chooses a number from the list		
            printf("Enter Number In List:\n");
            int theValueInList;
            while (scanf("%d", & theValueInList) != 1) {//Read user input to deregister
              printf("Please enter a number\n");
            }
            printf("Index chosen = %d\n", theValueInList);
            if (theValueInList >= indexs) {//check if index is valid
              continue;
            }
            strcpy(contentName, table[theValueInList].name); //send the name of the file to remove

            peerName[strcspn(peerName, "\n")] = 0;//Remove any trailing newline characters from peer name

            strcpy(dataToSend, peerName);
            strcat(dataToSend, ",");
            contentName[strcspn(contentName, "\n")] = 0;//Remove any trailing newline characters from content name
            strcat(dataToSend, contentName);
            strcpy(rpdu.data, dataToSend);//copy the prepared data into the PDU's data field
            write(s_sock, & rpdu, sizeof(rpdu));//send PDU to server
            PDU r2pdu;
            while (1) {
              n = read(s_sock, & r2pdu, sizeof(r2pdu));//Read response PDU from server
              printf("%d\n", n);
              if (r2pdu.type == 'E') {//if the reponse type is 'E', print error message and break loop
                printf("%s\n", r2pdu.data);
                break;
              }
              // Acknowledge 'A' on completion
              if (r2pdu.type == 'A') {//if the reponse type is 'A', print acknowledge message and deregister the content locally
                printf("%s\n", r2pdu.data);
                table[theValueInList].val = 0; // Unregister content in local table
                break;
              }
            }
          }
          // Quit 'Q'
          if (command[0] == 'Q') {
            quit(s_sock); //quit the peer
            exit(0);
          }
        }
      }
  }
}

void quit(int s_sock) {
  /* De-register all the registrations in the index server	*/
  rpdu.type = 'T';
  int a = 0;
  char dataToSend[100];
  int n;
  for (a = 0; a < indexs; a++) {
    if (table[a].val == 1) { //send all active registries and deregister them
      char contentName[20];
      strcpy(contentName, table[a].name);
      peerName[strcspn(peerName, "\n")] = 0;

      strcpy(dataToSend, peerName);
      strcat(dataToSend, ",");
      contentName[strcspn(contentName, "\n")] = 0;
      strcat(dataToSend, contentName);
      strcpy(rpdu.data, dataToSend);
      write(s_sock, & rpdu, sizeof(rpdu));
      PDU r2pdu;

      while (1) {
        n = read(s_sock, & r2pdu, sizeof(r2pdu));
        //printf("%d\n",n);
        if (r2pdu.type == 'E') {
          printf("%s\n", r2pdu.data);
          break;			
        }
        if (r2pdu.type == 'A') {
          printf("%s\n", r2pdu.data);
          break;
        }
      }
    }
  }
  kill(pid, SIGKILL); //End ALL
}

void local_list() {
  int i = 0;
  /* List local content	*/
  for (i = 0; i < indexs; i++) {
    //printf("All files in store = %s\n",table[i].name);	
    //printf("Index of above file = %d\n",table[i].val);
    if (table[i].val == 1) {
      printf("[%d]: %s\n", i, table[i].name); //display value in list and name of file			
    }
  }
}
void online_list(int s_sock) {
  /* Contact index server to acquire the list of content */
}

int server_download(int sd) {
  /* Respond to the download request from a peer	*/
  char * bp, buf[BUFLEN], rbuf[BUFLEN], sbuf[BUFLEN];
  int n, bytes_to_read, m;
  FILE * pFile;
  SizePDU spdu;

  n = read(sd, buf, BUFLEN);

  pFile = fopen(buf, "r");
  if (pFile == NULL) {
    printf("Error file not found\n");
    spdu.type = 'E';
    strcpy(spdu.data, "Error file not found\n");
    write(sd, & spdu, sizeof(spdu));
  } else {
    while ((m = fread(spdu.data, sizeof(char), 100, pFile)) > 0) { //send each file in a pdu
      spdu.type = 'C'; //data pdu for file creation
      spdu.size = m;
      write(sd, & spdu, sizeof(spdu));
    }
  }
  close(pFile);

  close(sd);
  return (0);
}

int search_content(int s_sock, char * name, PDU * rpdu) {
  /* Contact index server to search for the content
     If the content is available, the index server will return
     the IP address and port number of the content server.	*/
}

int client_download(char * name, PDU * pdu) {
  // Initiate Download with Content Server
  const char s[2] = ",";
  char fileName[20];
  char ouput[100];
  char user[20];
  int sd, port, i, n;
  struct sockaddr_in server;
  struct hostent * hp;
  char host[20], portString[20], * bp, rbuf[BUFLEN], sbuf[BUFLEN];
  SizePDU rpdu;
  strcpy(user, strtok(pdu -> data, s)); //split the pdu from where to read the data
  strcpy(fileName, strtok(NULL, s)); //split it with each comma character
  strcpy(host, strtok(NULL, s)); //we extract the host
  strcpy(portString, strtok(NULL, s)); //and the port in string format

  if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
    fprintf(stderr, "Socket Creation Failed\n");
    exit(1);
  }

  bzero((char * ) & server, sizeof(struct sockaddr_in));
  server.sin_family = AF_INET;
  server.sin_port = htons(atoi(portString)); //pass the port string as an int
  if (hp = gethostbyname(host)) //open a connection to host as the given host address
    bcopy(hp -> h_addr, (char * ) & server.sin_addr, hp -> h_length);
  else if (inet_aton(host, (struct in_addr * ) & server.sin_addr)) {
    fprintf(stderr, "Acquiring Server Address Failed\n");
    exit(1);
  }
  // Connecting to server
  if (connect(sd, (struct sockaddr * ) & server, sizeof(server)) == -1) {
    fprintf(stderr, "Can't connect to a server \n");
    exit(1);
  }

  printf("Transmitting\n");

  strcpy(sbuf, fileName); //copy the file name into the send buffer
  // send filename to server
  write(sd, sbuf, 100);

  bp = rbuf;
  // Create Local File and Write into it from Server Transmitted Data
  FILE * fp = fopen(fileName, "w");
  i = read(sd, & rpdu, sizeof(rpdu));
  while (i > 0) {
    // Data received 'C'
    if (rpdu.type == 'C') {
      fwrite(rpdu.data, sizeof(char), rpdu.size, fp);
      fflush(fp);
      i = read(sd, & rpdu, sizeof(rpdu)); //keep reading the incoming stream
      // File not found error		
    } else if (rpdu.type == 'E') {
      printf("%s\n", rpdu.data);
      //fwrite(rpdu.data,sizeof(char),i,fp);
      //fflush(fp);
      break;
    }
  }
  // close the file connection
  fclose(fp);
  // close the socket connection
  close(sd);
  return 0;
}

void deregistration(int s_sock, char * name) {
  /* Contact the index server to deregister a content registration;	   Update nfds. */
}

void registration(int s_sock, char * name) {
  /* Create a TCP socket for content download 
  		 one socket per content;
     Register the content to the index server;
     Update nfds;	*/
}

void handler() {
  quit(s_sock);
}
void reaper(int sig) {
  int status;
  while (wait3( & status, WNOHANG, (struct rusage * ) 0) >= 0);
}
