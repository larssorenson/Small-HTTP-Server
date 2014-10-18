
const char * usage =
"\nSimple server program that parses HTTP requests \n"
"Usage: \n"
"\t./myhttpd -[i|f|t|p] [<port>]\n\n"
"\t -i runs as an iterative server\n"
"\t -f runs a new process with every request\n"
"\t -t runs a new thread with every request\n"
"\t -p creates a pool of threads to handle requests iteratively\n\n";

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <string.h>
#include <stdio.h>
#include <sys/wait.h>
#include <time.h>       
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h> 
#include <inttypes.h>
#include <fcntl.h>
#include <semaphore.h>

sem_t sema;

pthread_mutex_t mutex;

int QueueLength = 5;

// Processes time request
void processRequest( int socket );
void processResponse(int fd, char* protocol, char* filePath, int code, char* fileType, int dir);
void * threadRequest(void * args);
void * threadPool(void * socket);
void directoryListing(char* filePath, int fd);

struct sockaddr_in clientIPAddress;
int alen;

int main( int argc, char ** argv )
{
 
	if ( sem_init(&sema,0,1) ) 
	{
		perror("init");
	}
	
	// Print usage if not enough arguments
	if ( argc < 3 ) 
	{
		fprintf( stderr, "%s", usage );
		exit( -1 );
	}

	// Get the port from the arguments
	int port = 0;
	port = atoi( argv[2] );
	
	if (port == 0)
	{
		fprintf( stderr, "%s", usage );
		exit( -1 );
		
	}
	if (port < 1024 || port > 65536)
	{
	
		port = 1337;
		
	}
	
	char concurrency = argv[1][1];

	// Set the IP address and port for this server
	struct sockaddr_in serverIPAddress; 
	memset( &serverIPAddress, 0, sizeof(serverIPAddress) );
	serverIPAddress.sin_family = AF_INET;
	serverIPAddress.sin_addr.s_addr = INADDR_ANY;
	serverIPAddress.sin_port = htons((u_short) port);

	// Allocate a socket
	int masterSocket =  socket(PF_INET, SOCK_STREAM, 0);
	if ( masterSocket < 0) 
	{
		perror("socket");
		exit( -1 );
	}

	// Set socket options to reuse port. Otherwise we will
	// have to wait about 2 minutes before reusing the same port number
	int optval = 1; 
	int err = setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR, 
		       (char *) &optval, sizeof( int ) );

	// Bind the socket to the IP address and port
	int error = bind( masterSocket,
		    (struct sockaddr *)&serverIPAddress,
		    sizeof(serverIPAddress) );
		    
	if ( error ) 
	{
		perror("bind");
		exit( -1 );
	}

	// Put socket in listening mode and set the 
	// size of the queue of unprocessed connections
	error = listen( masterSocket, QueueLength);
	if ( error ) 
	{
		perror("listen");
		exit( -1 );
	}
	
	// Accept incoming connections
	alen = sizeof( clientIPAddress );
	
	switch(concurrency)
	{
	
		case 'f':
		while ( 1 ) 
		{
		
			struct sockaddr_in clientIPAddress;
			int ret = 0;
			int slaveSocket = accept( masterSocket, (struct sockaddr *)&clientIPAddress, (socklen_t*)&alen);
				
			if ( slaveSocket >= 0 ) 
			{
			
				ret = fork();
				if (ret == 0)
				{
				
					// Process request.
					processRequest( slaveSocket );
					exit(0);
					
				}
				
				// Close socket
				close( slaveSocket );
				
			}
			
			wait3(0,0,NULL);
			while(waitpid(-1,NULL,WNOHANG) > 0){}
		}
		
		break;
		
		case 't':
		
			while( 1 )
			{
			
				struct sockaddr_in clientIPAddress;
				int ret = 0;
				int slaveSocket = accept( masterSocket, (struct sockaddr *)&clientIPAddress, (socklen_t*)&alen);
				
				pthread_t thread;
				
				if (slaveSocket >= 0) 
				{   
				
					if (pthread_create(&thread, NULL, &threadRequest, (void*)slaveSocket))
					{
					
						perror("Thread creation");
						exit(0);
					}
					
				}
				else
				{
				
					perror("ARGH THE PORTS");
					exit(0);
					
				}
				
			}
		
		break;
		
		case 'p':
		
			pthread_t thread[5];
			
			for (int x = 0; x < 4; x+=1)
			{
				
				pthread_create(&thread[x], NULL, threadPool, (void *)masterSocket);
				
			}
			
			threadPool((void *)masterSocket);
		
		break;
		
		case 'i':
		
			while( 1 )
			{
			
				struct sockaddr_in clientIPAddress;
				int slaveSocket = accept( masterSocket, (struct sockaddr *)&clientIPAddress, (socklen_t*)&alen);
							
				if ( slaveSocket < 0 ) 
				{
					perror( "accept" );
					exit( -1 );
				}
	
				// Process request.
				processRequest( slaveSocket );
	
				// Close socket
				close( slaveSocket );
		
			}
			
		break;
		
	}
  
}

void * threadPool(void * socket)
{
	int masterSocket = (intptr_t) socket;
	
	
	while( 1 )
	{
	
		
		sem_wait(&sema);
		
		struct sockaddr_in clientIPAddressThread;
		int alenThread = 0;
	
		int slaveSocket = accept( masterSocket, (struct sockaddr *)&clientIPAddressThread, (socklen_t*)&alenThread);
		
		
		if ( slaveSocket < 0 ) 
		{
			perror( "accept" );
			exit( -1 );
		}
		
	
		// Process request.
		processRequest( slaveSocket );	
		
	
		// Close socket
		close( slaveSocket );

		sem_post(&sema);
		
	}
}

void * threadRequest(void * args)
{
	int slaveSocket = (intptr_t) args;
	processRequest(slaveSocket);
	
	// Close socket
	close( slaveSocket );
}

void processRequest( int fd )
{
	
	//length of the file and response message
	int maxLength = 1024;
	
	char* Response = NULL;
	char* filePath = NULL;
	char* requestType = NULL;
	char* fileType = NULL;
	char* protocol = NULL;
	char* root = NULL; 
	
	
	//Response message and filepath strings
	filePath = (char*)malloc(maxLength + 1);
	requestType = (char*)malloc(maxLength + 1);
	fileType = (char*)malloc(maxLength + 1);
	protocol = (char*)malloc(9);
	
	memset(filePath, '\0', maxLength + 1);
	memset(requestType, '\0', maxLength + 1);
	memset(fileType, '\0', maxLength + 1);
	memset(protocol, '\0', 9);
	
	
	memcpy(protocol, (char*)"HTTP/1.0", 8);

	// Currently character read
	unsigned char newChar = 0;

	// Last character read
	unsigned char lastChar = 0;

	//
	// The client should send GET <sp> <Document Requested> <sp> HTTP/1.0 <cr><lf> {<Other Header Information> <cr><lf>}* <cr><lf>
	// Read the header until a
	// <CR><LF> is found.
	//
	int n = 0;
	int z = 0;
	
	while (n = read( fd, &newChar, sizeof(newChar)) && newChar != ' ')
	{
		requestType[z] = newChar;
		z+=1;
	}
	if (n < 0)
	{
		perror("Request parse");
		exit(0);
	}
	if (strcmp(requestType, "GET") == 0)
	{
		
		int x = 0;
		int y = 0;
		int fileExt = 0;
		while (n = read( fd, &newChar, sizeof(newChar) ) > 0 && newChar != ' ')
		{
			if (fileExt)
			{
			
				fileType[y] = newChar;
				y+=1;
				
			}
			
			if (newChar == '.')
			{
			
				fileExt = 1;
				
			}
			
			filePath[x] = newChar;
			x+=1;
		}
		
		while (read(fd, &newChar, sizeof(newChar)))
		{
		
			if (newChar == 10 && lastChar == 13)
			{
			
				read(fd, &newChar, sizeof(newChar));
				
				if (newChar == 13)
				{
				
					read(fd, &newChar, sizeof(newChar));
					
					if (newChar == 10)
						break;
						
				}
				
			}
			
			lastChar = newChar;
			
		}
		
		if(strcmp(filePath, "/") == 0)
		{
		
			root = (char*)malloc(31);
			memset(root, '\0', 31);
			memcpy(root, "http-root-dir/htdocs/index.html", 31);
			strcpy(fileType, "html");
			
		}
		else if (filePath[strlen(filePath) - 1] == '/')
		{
		
			strcat(filePath, "index.html");
			strcpy(fileType, "html");
			
			root = (char*)malloc(31);
			memset(root, '\0', 31);
			
			memcpy(root, "http-root-dir/htdocs", 20);
			strcat(root, filePath);
			
		}
		else
		{
		
			root = (char*)malloc(strlen(filePath) + 21);
			memset(root, '\0', strlen(filePath)+21);
			memcpy(root, "http-root-dir/htdocs/", 20);
			
			strcat(root, filePath);
			
		}
		
		
		
	}
	else
	{
	
		return;
		
	}
	if (open(root, O_RDONLY) <= 0 && strstr(root, "index.html"))
	{
		
		int x = strlen(root)-1;
		while (root[x] != '/')
		{
		
			root[x] = '\0';
			x-=1;
			
		}
		
	}
	if (opendir(root))
	{
	
		processResponse(fd, protocol, root, 200, fileType, 1);
		
	}
	else
	{
	
		if (open(root, O_RDONLY) > 0)
		{
		
			processResponse(fd, protocol, root, 200, fileType, 0);
			
		}
		else
		{
		
			processResponse(fd, protocol, root, 404, fileType, 0);
			
		}
		
	}
	
	
	close(fd);
	
	if(root != NULL && root != 0)
		free(root);
	else
		root = NULL;
		
	if(fileType != NULL && fileType != 0)
		free(fileType);
	else
		fileType = NULL;
	
	if(filePath != NULL && filePath != 0)
		free(filePath);
	else
		filePath = NULL;
	
	if(requestType != NULL && requestType != 0)
		free(requestType);
	else
		requestType = NULL;
	
	if(protocol != NULL && protocol != 0)
		free(protocol);
	else
		protocol = NULL;
	
}

void processResponse(int fd, char* protocol, char* filePath, int code, char* fileType, int dir)
{
	
	write(fd, "HTTP/1.0 ", 9);
	
	if (code == 200)
	{
	
		write(fd, "200 Document follows", 20);
		
	}
	else if (code == 404)
	{
	
		write(fd, "404 File Not Found", 18);
		
	}
	
	write(fd, "\r\n", 2); 
	
	write(fd, "Server: CS 252 lab5\r\n", 21);
	
	write(fd, "Content-type: ", 14);
	
	switch (fileType[0])
	{
	
		case 'g': write(fd, "image/gif", 9);
			break;
			
		case 'j': write(fd, "image/jpeg", 10);
			break;
			
		case 't': write(fd, "text/plain", 10);
			break;
			
		case 'h': write(fd, "text/html", 9);
			break;
			
		case 'i': write(fd, "image/x-icon", 12);
			break;
		
		default: if(!dir) write(fd, "text/plain", 10);
			break;
			
	}
	
	write(fd, "\r\n", 2);
	
	write(fd, "\r\n", 2);
	
	if (code == 200)
	{
	
		if (dir == 0)
		{
		
			char* buffer = (char*)malloc(sizeof(char)*128);
			
			FILE* in = fopen(filePath, "rb");
			
			int n = 0;
			
			while (n = fread(buffer, sizeof(char), 128, in))
			{
				
				if (n < 1)
				
					break;
					
				write(fd, buffer, n);
				
				if (n < 128)
				
					break;
			
			}
			
			free(buffer);
			
			fclose(in);
			
		}
		else
		{
		
			directoryListing(filePath, fd);
			
		}
		
	}
	else if (code == 404)
	{
	
		write(fd, "File could not be found!", 24);
		
	}
		
}

void directoryListing(char* filePath, int fd)
{

	char* directoryListing = NULL;

	directoryListing = (char*)malloc(sizeof(char)*1024);
	
	memset(directoryListing, '\0', 1024);
	
	strcpy(directoryListing, "<html>\n\t<head>\n\t\t<title>Index of ");
	
	strcat(directoryListing, filePath);
	
	strcat(directoryListing, "</title>\n\t</head>\n\t<body>\n\t\t<h1>Index of ");
	
	strcat(directoryListing, filePath);
	
	strcat(directoryListing, "</h1>\n");
	
	if(strlen(directoryListing) >= 896)
	{
	
		directoryListing = (char*)realloc(directoryListing, 2048);
		
	}
	
	strcat(directoryListing, "<table>"
					"<tr>"
						"<center><th>"
							"Name"
						"</th></center>"
					"</tr>"
					"<tr>"
						"<th colspan=\"5\">"
							"<hr>"
						"</th>"
					"</tr>");
	
	if(strlen(directoryListing) >= 1920)
	{
	
		directoryListing = (char*)realloc(directoryListing, 4096);
		
	}
	
	DIR* dir = opendir(filePath);
	
	struct dirent *ent = NULL;
	
	ent = (struct dirent *)malloc(sizeof(struct dirent));
	
	while(ent = readdir(dir))
	{
	
		if(strlen(directoryListing) >= strlen(directoryListing)*(3/4))
		{
		
			directoryListing = (char*)realloc(directoryListing, strlen(directoryListing)*2);
			
		}
		
		strcat(directoryListing, "<tr><center><td><a href=\"");
		
			strcat(directoryListing, ent->d_name);
		
		strcat(directoryListing, "\">");
		
			strcat(directoryListing, ent->d_name);
		
		strcat(directoryListing, "</a></td></center></tr>");
		
	}
	
	strcat(directoryListing, "<tr><th colspan=\"5\"><hr></th></tr></table></body></html>");
	
	write(fd, directoryListing, strlen(directoryListing));
	if(directoryListing != NULL)
		free(directoryListing);
	directoryListing = NULL;
	
	if(ent != NULL)	
		free(ent);
	ent = NULL;
}

