#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including these long lines in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";

void doit(int fd);
void read_requesthdrs(rio_t *rp, char *host_header, char *remaining_headers);
int parse_uri(char *uri, char *hostname, char *path, char *port);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);
void compile_request(char * request, char *host_header, char* path, 
	char *remaining_headers);
int main(int argc, char **argv) 
{
    int listenfd, connfd, port, clientlen;
    struct sockaddr_in clientaddr;

    /* Check command line args */
    if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
    }
    port = atoi(argv[1]);

    listenfd = Open_listenfd(port);
    while (1) {
		clientlen = sizeof(clientaddr);
		connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *)&clientlen);
		doit(connfd);
		Close(connfd);
    }
}


/*
 * doit - handle one HTTP request/response transaction
 */

void doit(int fd) 
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE], port[MAXLINE];
    char host_header[MAXLINE], remaining_headers[MAXLINE];
    char buf_server[MAXLINE], request[MAXLINE];
    rio_t rio;
    rio_t server_rio;
  
    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    Rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")) { 
       clienterror(fd, method, "501", "Not Implemented",
                "Tiny does not implement this method");
        return;
    }
    read_requesthdrs(&rio, host_header, remaining_headers);

    /* Parse URI from GET request */
   	if (parse_uri(uri, hostname, path, port) < 0) {
   		//ERROR
   	}
   	if (strncmp(host_header, "Host: ", strlen("Host: ")) != 0){
   		sprintf(host_header, "Host: %s\r\n", hostname);
   	}

   	compile_request(request, host_header, path, remaining_headers);
    int port_num = atoi(port);

    int server_fd = Open_clientfd(hostname, port_num);
    Rio_writen(server_fd, request, strlen(request));
    Rio_readinitb(&server_rio, server_fd);
    Rio_readlineb(&server_rio, buf_server, MAXLINE);
    
    Close(server_fd);
    


    
}

void compile_request(char * request, char *host_header, char* path, 
	char *remaining_headers){
	sprintf(request, "GET %s HTTP/1.0\r\n", path);
	sprintf(request, "%s%s", request, host_header);
	sprintf(request, "%s%s", request, user_agent_hdr);
	sprintf(request, "%s%s", request, accept_hdr);
	sprintf(request, "%s%s", request, accept_encoding_hdr);
	sprintf(request, "%sConnection: close\r\n", request);
	sprintf(request, "%sProxy-Connection: close\r\n", request);
	sprintf(request, "%s%s", request, remaining_headers);
	sprintf(request, "%s\r\n", request);
	return;

}


/*
 * read_requesthdrs - read and parse HTTP request headers
 */
/* $begin read_requesthdrs */
void read_requesthdrs(rio_t *rp, char *host_header, char *remaining_headers) 
{
    char buf[MAXLINE];
    Rio_readlineb(rp, buf, MAXLINE);
    while(strcmp(buf, "\r\n")) {
		if (strncmp(buf, "Host: ", strlen("Host: ")) == 0){
			strcpy(host_header, buf);
		}
		if ((strncmp(buf, "User-Agent: ", strlen("User-Agent: ")) != 0) &&
			(strncmp(buf, "Accept: ", strlen("Accept: ")) != 0) &&
			(strncmp(buf, "Accept-Encoding: ", 
				strlen("Accept-Encoding: ")) != 0) &&
			(strncmp(buf, "Connection: ", strlen("Connection: ")) != 0) &&
			(strncmp(buf, "Proxy-Connection: ", 
				strlen("Proxy-Connection: ")) != 0)){
			sprintf(remaining_headers, "%s%s", remaining_headers, buf);
		}
		Rio_readlineb(rp, buf, MAXLINE);
    }
    return;
}
/* $end read_requesthdrs */

/*
 * parse_uri - 
 *             
 */

int parse_uri(char *uri, char *hostname, char *path, char *port) 
{
    int i = 0;
    char *is_uri = strstr(uri, "http://");
    if (is_uri != NULL){
    	while (uri[i] != '/'){
    		i += 1;
    	}
    	i += 2;
    	int j = 0;
    	while (uri[i] != '\0' && uri[i] != '/' && uri[i] != ':'){
    		hostname[j] = uri[i];
    		j += 1;
    		i += 1;
    	}
    	hostname[j] = '\0';
    	int k = 0;
    	if (uri[i] == ':'){
    		i += 1;
    		while (uri[i] != '/'){
    			port[k] = uri[i];
    			i += 1;
    			k += 1;
    		}
    		port[k] = '\0';
    	}
    	else {
    		port = "80\0";
    	}
    	int n = 0;
    	while (uri[i] != '\0'){
    		path[n] = uri[i];
    		i += 1;
    		n += 1;
    	}
    	path[n] = '\0';
    	return 0;
    }
    else {
    	return -1;
    }
    
}


/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}


