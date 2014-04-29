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
void *doit_thread(void *vargp);

/* Locking */
pthread_rwlock_t lock;

/* Cache data structures */

struct cache_node{
	char *url;
	char *data;
	unsigned int data_size;
	struct cache_node *prev;
	struct cache_node *next;
};

typedef struct cache_node cache_node;

struct cache{
	unsigned int cache_size;
	struct cache_node *start;
	struct cache_node *end;
};

typedef struct cache cache;

cache *proxy_cache;

/* Cache functions */

void fix_linking(cache_node *p, cache *c_cache);
cache_node *check_for_hit(cache *c_cache, char *query);
void add_to_cache(cache *c_cache, char *query, char *q_data, 
	unsigned int q_size);
void delete_from_cache(cache *c_cache);
cache *initialize_cache();


cache *initialize_cache(){
	cache *proxy_cache = Calloc(1, sizeof(cache));
	proxy_cache->start = NULL;
	proxy_cache->end = NULL;
	proxy_cache->cache_size = 0;
	return proxy_cache;
}

void fix_linking(cache_node *p, cache *c_cache){
	if (p->prev != NULL){
		if (p->next == NULL){
			p->prev->next = NULL;
			c_cache->end = p->prev;
		}
		else {
			p->prev->next = p->next;
			p->next->prev = p->prev;
		}
		p->next = c_cache->start;
		c_cache->start->prev = p;
		p->prev = NULL;
		c_cache->start = p;
	}
}

cache_node *check_for_hit(cache *c_cache, char *query){
	pthread_rwlock_wrlock(&lock);
	printf("%s:%s\n", "Checking for hit in cache", query);
	cache_node *p = c_cache->start;
	while (p != NULL){
		if (strcmp(p->url, query) == 0){
			printf("Hit! : %s\n", p->url);
			fix_linking(p, c_cache);
			break;
		}
		p = p->next;
	}
	// pthread_rwlock_unlock(&lock);
	return p;
}

void add_to_cache(cache *c_cache, char *query, char *q_data, unsigned int q_size){
	if (!(q_size > MAX_OBJECT_SIZE)){
		pthread_rwlock_wrlock(&lock);
		printf("%s:%s\n", "Adding to cache", query);
		cache_node *to_add = Calloc(1, sizeof(cache_node));
		to_add->url = Calloc(strlen(query) + 1, sizeof(char));
		strcpy(to_add->url, query);
		to_add->data = Malloc(q_size);
		memcpy(to_add->data, q_data, q_size);
		to_add->data_size = q_size;
		if (c_cache->start == NULL){
			to_add->prev = NULL;
			to_add->next = NULL;
			c_cache->start = to_add;
			c_cache->end = to_add;
		}
		else {
			to_add->prev = NULL;
			to_add->next = c_cache->start;
			c_cache->start->prev = to_add;
			c_cache->start = to_add;
		}
		c_cache->cache_size += q_size;
		while (c_cache->cache_size > MAX_CACHE_SIZE){
			delete_from_cache(c_cache);
		}
		pthread_rwlock_unlock(&lock);
	}
}

void delete_from_cache(cache *c_cache){
	if (c_cache->end != NULL){
		printf("%s\n", "Deleting from cache");
		// pthread_rwlock_wrlock(&lock);
		c_cache->cache_size -= c_cache->end->data_size;
		if (c_cache->end->prev != NULL){
			c_cache->end->prev->next = NULL;
		}
		cache_node *temp = c_cache->end;
		Free(temp->url);
		Free(temp->data);
		c_cache->end = c_cache->end->prev;
		Free(temp);
		// pthread_rwlock_unlock(&lock);
	}
	
	
	
}


/* Proxy implementation */

int main(int argc, char **argv) 
{
    int listenfd, port, clientlen;
    struct sockaddr_in clientaddr;

    /* Check command line args */
    if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
    }
    Signal(SIGPIPE, SIG_IGN);
    pthread_rwlock_init(&lock, 0);
    port = atoi(argv[1]);
    pthread_t tid;
    proxy_cache = initialize_cache();
    listenfd = Open_listenfd(port);
    while (1) {
		clientlen = sizeof(clientaddr);
		int *connfdp = Malloc(sizeof(int));
		*connfdp = Accept(listenfd, (SA *)&clientaddr, (socklen_t *)&clientlen);
		Pthread_create(&tid, NULL, doit_thread, connfdp);
    }
}

void *doit_thread(void *vargp){
	int connfd = *((int *)vargp);
	Pthread_detach(pthread_self());
	Free(vargp);
	doit(connfd);
	Close(connfd);
	return NULL;
}


/*
 * doit - handle one HTTP request/response transaction
 */

void doit(int fd) 
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE], port[MAXLINE];
    char host_header[MAXLINE], remaining_headers[MAXLINE];
    char request[MAXLINE], server_buf[MAXLINE];
    rio_t rio;
    rio_t server_rio;
    memset(buf, 0, MAXLINE);
    memset(method, 0, MAXLINE);
    memset(uri, 0, MAXLINE);
    memset(version, 0, MAXLINE);
    memset(hostname, 0, MAXLINE);
    memset(path, 0, MAXLINE);
    memset(port, 0, MAXLINE);
    memset(host_header, 0, MAXLINE);
    memset(remaining_headers, 0, MAXLINE);
    memset(request, 0, MAXLINE);
    memset(server_buf, 0, MAXLINE);
    
  	
    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    Rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")) { 
       clienterror(fd, method, "501", "Not Implemented",
                "Tiny does not implement this method");
        return;
    }

    cache_node *cache_hit = check_for_hit(proxy_cache, uri);
    if (cache_hit != NULL){
    	if(rio_writen(fd, cache_hit->data, cache_hit->data_size) < 0){
   		    pthread_rwlock_unlock(&lock);
    		return;
    	}
    }
	pthread_rwlock_unlock(&lock);

    if (cache_hit == NULL) {
    	char *response_data = Malloc(sizeof(char));
    	unsigned int data_size = 0;
    	read_requesthdrs(&rio, host_header, remaining_headers);
    	/* Parse URI from GET request */
   		if (parse_uri(uri, hostname, path, port) < 0) {
   			return;
   		}
   		if (strncmp(host_header, "Host: ", strlen("Host: ")) != 0){
   			sprintf(host_header, "Host: %s\r\n", hostname);
   		}
   		// printf("%s %s %s\n", hostname, path, port);
   		compile_request(request, host_header, path, remaining_headers);
    	int port_num = atoi(port);

    	int server_fd = Open_clientfd_r(hostname, port_num);
    	if (rio_writen(server_fd, request, strlen(request)) < 0){
    		return;
    	}
    
    	Rio_readinitb(&server_rio, server_fd);
    	int len;
    	while ((len = rio_readnb(&server_rio, server_buf, MAXLINE)) > 0){
    		if (len < 0 && errno == ECONNRESET){
    			Close(server_fd);
    			return;
    		}
    		if (rio_writen(fd, server_buf, len) < 0){
    			return;
    		}
    		response_data = Realloc(response_data, data_size + len);
    		memcpy(response_data + data_size, server_buf, len);
    		data_size += len;
    	}

    	add_to_cache(proxy_cache, uri, response_data, data_size);
    
    	Close(server_fd);
    	Free(response_data);
    	return; 
    } 
}

void compile_request(char *request, char *host_header, char* path, 
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

void read_requesthdrs(rio_t *rp, char *host_header, char *remaining_headers) 
{
    char buf[MAXLINE];
    Rio_readlineb(rp, buf, MAXLINE);
    while(strcmp(buf, "\r\n")) {
    	Rio_readlineb(rp, buf, MAXLINE);
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
		
    }
    return;
}


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
    		strcpy(port, "80\0");
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




