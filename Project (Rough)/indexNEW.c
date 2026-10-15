#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <stdio.h>
#include <time.h>
#include <arpa/inet.h>

// 404 Error
#define MSG1 "Cannot find content"
// Max UDP Message Size
#define BUFLEN 100
// Max File Name Size
#define NAMESIZ 20
// Max Content
#define MAX_NUM_CON 200
// Max Content

typedef struct entry {
  char usr[NAMESIZ];
  char ip[16];
  char port[7];
  short token;
  struct entry * next;
}
ENTRY;

typedef struct {
  char name[NAMESIZ]; // Content Name
  ENTRY * head;
}
LIST;
LIST list[MAX_NUM_CON];
int max_index = 0;

// Pdu for udp communication
typedef struct {
  char type;
  char data[BUFLEN];
}
PDU;
PDU tpdu;

void search(int, char * , struct sockaddr_in * );
void registration(int, char * , struct sockaddr_in * );
void deregistration(int, char * , struct sockaddr_in * );

struct sockaddr_in fsin;

// UDP Content Indexing Service
int main(int argc, char * argv[]) {
  struct sockaddr_in sin, * p_addr; // the from address of a client	
  ENTRY * p_entry;
  char * service = "10000"; // service name or port number	
  char name[NAMESIZ], usr[NAMESIZ];
  int alen = sizeof(struct sockaddr_in); // from-address length		
  int s, n, i, len, p_sock; // socket descriptor and socket type    
  int pdulen = sizeof(PDU);
  struct hostent * hp;
  PDU rpdu;
  int j = 0;

  PDU spdu;

  for (n = 0; n < MAX_NUM_CON; n++)
    list[n].head = NULL;
  switch (argc) {
  case 1:
    break;
  case 2:
    service = argv[1];
    break;
  default:
    fprintf(stderr, "Incorrect Arguments \n Use the format: server [host] [port]\n");
  }

  memset( & sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = INADDR_ANY;

  // Map service name to port number 
  sin.sin_port = htons((u_short) atoi(service));

  // Allocate socket 
  s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0) {
    fprintf(stderr, "can't creat socket\n");
    exit(1);
  }

  // Bind socket 
  if (bind(s, (struct sockaddr * ) & sin, sizeof(sin)) < 0)
    fprintf(stderr, "can't bind to %s port\n", service);

  while (1) {
    if ((n = recvfrom(s, & rpdu, pdulen, 0, (struct sockaddr * ) & fsin, & alen)) < 0) {
      printf("recvfrom error: n=%d\n", n);
    }
    //Content Registration Request 'R'			
    if (rpdu.type == 'R') {
      printf("Registering\n");
      registration(s, rpdu.data, & fsin);//call registration function with socket, data, client address
      printf("%d\n", s);
    }

    // Search Content 'S'		
    if (rpdu.type == 'S') {
      printf("Searching\n");
      search(s, rpdu.data, & fsin);//call search function with socket, data, client address
    }

    //List Content 'O' 
    if (rpdu.type == 'O') {
      printf("List content\n");
      // Read from the content list and send the list to the client 		
      for (j = 0; j < max_index; j++) {
        if (list[j].head != NULL) {
          PDU spdu;
          spdu.type = 'O';
          strcpy(spdu.data, list[j].name);
          printf("%s\n", list[j].name);
          (void) sendto(s, & spdu, sizeof(spdu), 0, (struct sockaddr * ) & fsin, sizeof(fsin));
        }
      }
      // Acknowledge End of Response
      PDU opdu;
      opdu.type = 'A';
      (void) sendto(s, & opdu, sizeof(opdu), 0, (struct sockaddr * ) & fsin, sizeof(fsin));
    }

    //Deregister 'T'		
    if (rpdu.type == 'T') {
      printf("de-register\n");
      deregistration(s, rpdu.data, & fsin);//call de-registration function with socket, data, client address
    }
  }
  return;
}

// s is socket address
// *data is the data name
// *addr is the address of the client sending/requesting
void search(int s, char * data, struct sockaddr_in * addr) {
  // Search content list and return the answer:
  int j;//loop counter
  int found = 0;//variable to indicate content is found
  int used = 999;//variable keep track of least used token
  ENTRY * use;//pointer to store least used token
  ENTRY * head;//pointer to store the entry with least used token
  int pdulen = sizeof(PDU);//size PDU structure
  PDU spdu;//structure to hold response pdu
  char usr[20];//variable to store extracted username
  char ouput[100];//buffer formatted output data
  char fileName[20];//variable to store file name
  char rep[2] = ",";

  //split recieved data into username and filename
  strcpy(usr, strtok(data, rep));
  strcpy(fileName, strtok(NULL, rep));

  // loop through list until you find name == data.
  for (j = 0; j < max_index; j++) {
    // check if the value at the list index 
    // is equal to the requested file
    printf("%s\n", list[j].name);//print current name content name in list
    if (strcmp(list[j].name, fileName) == 0 && (list[j].head != NULL)) {
      found = 1;//set found flag to true
      head = list[j].head;//point to the head of linked list
      // loop through the linked list until you find name
      while (head != NULL) {
        if (head -> token < used) {
          used = head -> token;//update least used token
          use = head;//store pointer to the entry with least used token
        }
        head = head -> next;//move to next entry 
      }
      break;//exit if content is found
    }
  }
  //if content is found prepare the response
  if (found == 1) {
    spdu.type = 'S'; //set to search response
    //Format out data with username, filename, ip address and port
    strcpy(ouput, use -> usr);
    strcat(ouput, ",");
    strcat(ouput, fileName);
    strcat(ouput, ",");
    strcat(ouput, use -> ip);
    strcat(ouput, ",");
    strcat(ouput, use -> port);
    printf("%s\n", ouput);//print formatted data
    strcpy(spdu.data, ouput);//copy outputted data into PDU's data feild 
    use -> token++;//increament usage token entry
//send to client
    (void) sendto(s, & spdu, sizeof(spdu), 0, (struct sockaddr * ) & fsin, sizeof(fsin));
  } else {
    spdu.type = 'E';//set 'E' for error
    strcpy(spdu.data, "File not found");//print message
//send error response PDU to client
    (void) sendto(s, & spdu, sizeof(spdu), 0, (struct sockaddr * ) & fsin, sizeof(fsin));
  }
  printf("Ending\n");
}

// Deregister content from the server
void deregistration(int s, char * data, struct sockaddr_in * addr) {
  int j;//loop counter
  int use = -1;//variable to store the index of found entry 
  ENTRY * prev;// pointer to keep traack of previous entry in linked list
  ENTRY * head;//pointer to traverse the linked list
  int listIndex = 0;//variable to keep track of current index
  PDU spdu;//structure to hold the reponse PDU
  char rep[2] = ",";//delimiters used for splitting data
  char usr[20];//variables to store username from data
  char file[20];//variable to store the filename from data
  printf("Deregistering %s\n", data);//print data being processed

  //Split the data into username and filename
  strcpy(usr, strtok(data, rep));
  strcpy(file, strtok(NULL, rep));
  //Loop through the list of registered contents
  for (j = 0; j < max_index; j++) {
  //check if the current list entry matches the filename
    if (strcmp(list[j].name, file) == 0) {
      head = list[j].head; //point to the head of linked list
      prev = list[j].head; //initialize previous to the head of linked list
      listIndex = 0; //reset index

      //Traverse the linked list to find user
      while (head != NULL) {
        printf("Usr list = %s\n", head -> usr);//print usernmae in the list
        printf("Usr given = %s\n", usr);//print given username

        //Check if the current entry matches the given username
        if (strcmp(head -> usr, usr) == 0) {
          printf("Compare name success\n");
          printf("List index = %d\n", listIndex);
          use = listIndex;//store the index of ofund entry

	  //if entry to be removed is first
          if (listIndex == 0) {
            list[j].head = head -> next; //update head to the next entry 
          } else {
            prev -> next = head -> next; //bypass the currnt entry in the list
          }
          break;//exit loop if entry is found
        }
        listIndex++;//increment list index
        prev = head;// update prev to current entry
        head = head -> next;//move to next entry in the list
      }
      break;//exit if file is found
    }
  }
  //check if entry was removed 
  if (use != -1) {
    //send acknowledgement 'A'
    spdu.type = 'A';
    strcpy(spdu.data, "Done");
    (void) sendto(s, & spdu, sizeof(spdu), 0, (struct sockaddr * ) & fsin, sizeof(fsin));
  } else {
    spdu.type = 'E';
    strcpy(spdu.data, "Failed");
    (void) sendto(s, & spdu, sizeof(spdu), 0, (struct sockaddr * ) & fsin, sizeof(fsin));
  }
}

// s is socket address
// *data is the data name
// *addr is the address of the client sending/requesting
// Register Content to the Server
void registration(int s, char * data, struct sockaddr_in * addr) {
  // Register the content and the server of the content
  ENTRY * new = NULL;
  new = (ENTRY * ) malloc(sizeof(ENTRY));//allocate memory for a new entry 
  int j;
  ENTRY * head;
  int used = 999;

  int duplicateUser = 0;
  char rep[2] = ",";
  PDU spdu;
  char fileName[20];
  int found = 0;
  char * ip = inet_ntoa(fsin.sin_addr); //convert IP to string

  printf("Sending ip address %s\n", ip);
  printf("Socket %d\n", s);
  printf("Data %s\n", data);
  // get user, file name, and port from the recieved 
  strcpy(new -> usr, strtok(data, rep));
  strcpy(fileName, strtok(NULL, rep));
  strcpy(new -> port, strtok(NULL, rep));
  strcpy(new -> ip, ip);

  printf("Stored Ip %s\n", new -> ip);
  new -> token = 0;// initialize token count
  new -> next = NULL;//initialize the next pointer to null
  // iterate list until you find name == data
  for (j = 0; j < max_index; j++) {
    if (strcmp(list[j].name, fileName) == 0) {
      head = list[j].head;
      found = 1;
      // loop through the linked list until you get to the end to add content to tail
      while (head != NULL) {
        // stops seg faults when registering content after full deregister
        if (head -> next == NULL) {
          break;
        }
        // check if username is already used
        if (strcmp(head -> usr, data) == 0) {
          duplicateUser = 1;
          break;
        }
        head = head -> next;
      }
      if (head == NULL) { // First Content on Linked List Initialize
        list[j].head = new;
      } else { // Other Cases (add new entry to the end of the list)
        head -> next = new;
      }
      break;
    }
  }
  //File not Found
  if (found == 0) {
    strcpy(list[max_index].name, fileName);
    list[max_index].head = new;
    max_index++;
  }
  //handle duplicate user registration
  if (duplicateUser == 1) {
    printf("Duplicate\n");
    spdu.type = 'E';
    strcpy(spdu.data, "Duplicate user name"); // Duplicate File Name?
    (void) sendto(s, & spdu, sizeof(spdu), 0, (struct sockaddr * ) & fsin, sizeof(fsin));

  } else {
    printf("Unique\n");
    // Otherwise send acknowledgement 'A'
    spdu.type = 'A';
    strcpy(spdu.data, "Done");
    (void) sendto(s, & spdu, sizeof(spdu), 0, (struct sockaddr * ) & fsin, sizeof(fsin));
  }
}
