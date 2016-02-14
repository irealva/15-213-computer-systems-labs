/* proxy.c
 *
 * Irene Alvarado - ialvarad@andrew.cmu.edu
 *
 * Implementation of a simple proxy that acts as an intermediary between 
 * clients and servers. It heavily uses the Rio I/O package found in the CS:APP
 * book. It only parses HTTP GET requests.
 * The proxy handles multiple concurrent requests by using the POSIX threads 
 * library.
 * Finally, the proxy caches web resources up to a MAX_CACHE_SIZE. 
 * I've created a data structure found in cache.c to store previously 
 * requested web resources
 *
 * I've modified the following functions in csapp.c so proxy does not terminate
 * on errno set to ECONNREST or EPIPE:
 * - Rio_writen()
 * - Rio_readlineb() 
 * - Rio_readnb()
 */

#include <stdio.h>
#include "csapp.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

void parse_client_request(int fd) ;
void client_error(int fd, char *cause, char *errnum, 
	char *shortmsg, char *longmsg) ;
void parse_uri(char *uri, char *hostname, char *filename, char *port) ;
int open_server_connection(rio_t *rio, char *hostname, char *port) ;
void read_client_request(int serverfd, rio_t *clientrio, 
	char *filename, char *uri) ;
void read_server_response(int fd, int serverfd, rio_t *serverrio, char *uri) ;
void check_headers(char *buf) ;
void *thread(void *vargp) ;
void quit_handler(int sig) ;
void send_from_cache(int fd, char *uri) ;

/* You won't lose style points for including these long lines in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
//static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
//static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";

/* main() - main routine to launch proxy and block signals */
int main(int argc, char *argv[])
{
	// If too few arguments, launch an error message
    if (argc != 2) {
    	fprintf(stderr, "Not enough arguments. Usage: ./proxy <port>\n");
    	exit(0);
	}

	int port = atoi(argv[1]) ;

	if((port < 1025) || (port > 65535)) {
		fprintf(stderr, "You must choose a non-privileged port\n") ;
		exit(0) ;
	}

    //Ignore SIGPIPE signal
	Signal(SIGPIPE, SIG_IGN);
	//Call quit_handler to free cache space when program terminated
	Signal(SIGQUIT, quit_handler) ;
	Signal(SIGINT, quit_handler) ; 

    //Client variables
	int listenfd ;
	int *connfd ;
	struct sockaddr_in clientaddr ;
	socklen_t clientlen = sizeof(struct sockaddr_in) ;
	pthread_t tid;

    listenfd = Open_listenfd(argv[1]);
    initialize_cache(MAX_CACHE_SIZE, MAX_OBJECT_SIZE) ; // Initialize cache

    // Accept incoming client connections indefinitely
	while(1) {
		connfd = Malloc(sizeof(int)) ;
		*connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen) ;
		Pthread_create(&tid, NULL, thread, connfd) ; // Create a new thread
	}

    return 0;
}

/* thread() - Create a new POSIX thread on which to parse a client request */ 
void *thread(void *vargp) {
	int connfd = *((int *) vargp) ;
	Pthread_detach(pthread_self()) ;
	Free(vargp) ;

	parse_client_request(connfd) ; // Handle the client request

	Close(connfd) ;

	return NULL ;
}

/* parse_client_request() - Main proxy function to handle all of the client 
 * request. Reads the request from the client and then either a) serves
 * the resource from the cache or b) fetches the resource from a server
 */
void parse_client_request(int fd) {
	char buf[MAXLINE] ;
	char method[MAXLINE] ;
	char uri[MAXLINE] ;
	char version[MAXLINE] ;
	rio_t rio ;

	char hostname[MAXLINE] ;
	char filename[MAXLINE] ;
	char port[MAXLINE] ;

	int serverfd ;
	rio_t serverrio ;

	/* Read request line and headers */
	//sets up an empty read buffer and associates an open file descriptor 
	// with that buffer. function is called once per open descriptor.
	Rio_readinitb(&rio, fd); 
	Rio_readlineb(&rio, buf, MAXLINE); 
	sscanf(buf, "%s %s %s", method, uri, version);
	if (strcasecmp(method, "GET")) {
	   client_error(fd, method, "501", "Not Implemented",
	            "Proxy does not implement this method");
		return; 
	}

	printf("***** INCOMING REQUEST *****\n") ;

	parse_uri(uri, hostname, filename, port) ; // Parse the request uri
	printf("Client request: %s\n\n", uri) ;

	// If resource is found in the cache
	if(find_cache(uri) != NULL) {
		send_from_cache(fd, uri) ;
	}
	// If resource is NOT found in the cache, fetch from a server
	else {
		serverfd = open_server_connection(&serverrio, hostname, port) ;
		read_client_request(serverfd, &rio, filename, uri) ;
		read_server_response(fd, serverfd, &serverrio, uri) ;
	}

	printf("***** END INCOMING REQUEST *****\n\n\n") ;
}

/* client_error() - function to format a client error. Mainly from CSAPP book */ 
void client_error(int fd, char *cause, char *errnum, 
	char *shortmsg, char *longmsg) {

	char buf[MAXLINE] ;
	char body [MAXLINE] ;

	/* Build the HTTP response body */
	sprintf(body, "<html><title>Proxy Error</title>") ;
	sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
	sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
	sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);

	/* Print the HTTP response */
	sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-type: text/html\r\n");
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
	Rio_writen(fd, buf, strlen(buf));

	Rio_writen(fd, body, strlen(body));
}

/* parse_uri() - parse the client uri and break into hostname, filename,
 * port number
 */
void parse_uri(char *uri, char *hostname, char *filename, char *port) {
	char *ptr ;
	char *ptr2 ;
	char *tmpfilename ;
	char tmpuri[MAXLINE] ;
	char *tmphostname ;
	char *tmpport ;

	strcpy(tmpuri, uri) ;

	//Get the hostname
	tmphostname = strtok_r(tmpuri, "//", &ptr) ;
	tmphostname = strtok_r(NULL, "/", &ptr) ;
	strcpy(hostname, tmphostname) ;
	
	//Get the filename ilename
	tmpfilename = strtok_r(NULL, "", &ptr) ;

	if(tmpfilename != NULL) {
		strcpy(filename, "/") ;
		strcat(filename, tmpfilename) ;
	}
	else {
		strcpy(filename, "/") ;
	}

	//Get port from hostname
	strtok_r(hostname, ":", &ptr2) ;
	tmpport = strtok_r(NULL, "", &ptr2) ;

	if(tmpport == NULL) {
		strcpy(port, "80") ;
	}
	else {
		strcpy(port, tmpport) ;
	}
}

/* open_server_connection() - open a server connection  and return a 
 * file descriptor for the server 
 */
int open_server_connection(rio_t *rio, char *hostname, char *port) {
	int serverfd = Open_clientfd(hostname, port) ;
	Rio_readinitb(rio, serverfd) ;

	return serverfd ;
} 

/* read_client_request() - reads the request from the client and sends 
 * buffered request to the server
 */ 
void read_client_request(int serverfd, rio_t *clientrio, 
	char *filename, char *uri) {

	char buf[MAXLINE] ;

	//Send request to server
	char request[MAXLINE] ;
	strcpy(request, "GET ") ;
	strcat(request, filename) ;
	strcat(request, " HTTP/1.0\r\n") ;
	Rio_writen(serverfd, request, strlen(request)) ;

	int n ;
	int finish = 0 ;

	//While still reading request lines from the client, 
	// send buffer to the server
	while(!finish && (n = Rio_readlineb(clientrio, buf, MAXLINE)) != 0) {
		check_headers(buf) ;
		Rio_writen(serverfd, buf, strlen(buf)) ; //Send buf to the server
 		finish = (buf[0] == '\r'); // Add proper EOF
	}

}

/* check_headers() - change the HTTP headers before sending to the server */
void check_headers(char *buf) {
	if (strstr(buf, "User-Agent") != NULL) {
    	strcpy(buf, user_agent_hdr) ;
	}
	else if (strstr(buf, "Proxy-Connection:") != NULL) {
    	strcpy(buf, "Proxy-Connection: Close\r\n") ;
	}

	else if (strstr(buf, "Connection:") != NULL) {
    	strcpy(buf, "Connection: Close\r\n") ;
	}
}

/* read_server_response() - Reads response from a server and sends response
 * back to client that requested it
 */ 
void read_server_response(int fd, int serverfd, rio_t *serverrio, char *uri) {
	char buf[MAXLINE] ;
	int n ; 
	int objsize = 0;
	char *objbuf = NULL ;

	//While server still sends buffered response
	while((n = Rio_readnb(serverrio, buf, MAXLINE)) != 0) {
		Rio_writen(fd, buf, n) ; // Write to the client

		objsize += n ;
		objbuf = Realloc(objbuf, objsize) ;
		memcpy(objbuf + (objsize - n), buf, n) ;
	}

	//If the resource fits inside the cache then add it
	if (objsize <= MAX_OBJECT_SIZE) {
		printf("Inserting into cache\n") ;
		insert_cache(objsize, uri, objbuf) ;
	}

	Free(objbuf) ;

	Close(serverfd) ;	
}

/* send_from_cache() - send a particular resource as identified by the uri 
 * to a client from the cache
 */
void send_from_cache(int fd, char *uri) {
	char *buf = find_cache(uri) ; // find the data in the cache
	Rio_writen(fd, buf, strlen(buf)) ; // send to the client
}

/* quit_handler() - handler to call upon quitting proxy program. Mainly 
 * frees the cache
 */
void quit_handler(int sig) {
	free_cache() ;
	exit(0) ;
}

