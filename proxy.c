/*
 * proxy.c
 * dsgoel - Druhin Sagar Goel
 * abist - Aditya Bist
 *
 * Proxy Lab - In this lab, we made a web proxy in 3 total steps.
               We started out with making a sequential proxy server,
               which simply makes a contact to the web via our server,
               and then forwards the client's request to the web server,
               reads the server's response and then forwards the response
               to the client. This web server only looks at static requests.

               The next part of this lab converts out sequential proxy to
               a concurrent one. This is done by using the pthread library,
               and usage of threads to divide the work parallely instead
               of sequential methods.

               The last part of the proxy lab, enhances the proxy using
               caches to save visited pages to reload them. This was similar
               to the cache lab, where caches were hit according to the url
               visited.
 */

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


/*
 * The cache structure is that of a doubly-linked list which is made up
 * of cache nodes (cache_node). Each cache_node stores the following
 * information:
 * url -> this acts as the tag into the cache, we search for cached data based
 * on the url with which the data is associated
 * data -> this stores the data associated with the url
 * data_size -> this stores the size of the data (in bytes)
 * next -> this is a pointer to the next cache_node in the cache linked list
 * prev -> this is a pointer to the previous cache_node in the cache linked
 * list
 * The cache structure holds the following information:
 * cache_size -> keeps track of the size of the total amount of data stored
 * in the cache
 * start -> this is a pointer to the start of the cache linked list
 * end -> this is a pointer to the end of the cache linked list
 *
 * The LRU - policy is implemented in the following way:
 * Any new data added to the cache (that is any new cache_node) is added
 * to the start of the linked list. Whenever there is a hit, that particular
 * node is moved to the start of the list. This ensures that the most recently
 * used node is at the start of the linked list while the least recently used
 * node is at the end of the linked list. Thus, any node that is deleted from
 * the cache is deleted from the end of the linked list.
 *
 * The reason behind the choice for implementing the cache as a doubly linked
 * list was that it allows for consant time addition to the list as well as
 * constant time deletion from the list.
 */

/* Cache data structures */

struct cache_node{
  char *url; //stores the url
  char *data; //stores the data
  unsigned int data_size; //size of data
  struct cache_node *prev; // previous cache node
  struct cache_node *next; // next cache node
};

typedef struct cache_node cache_node;

/* Cache main structure */
struct cache{
  unsigned int cache_size;
  struct cache_node *start;
  struct cache_node *end;
};

typedef struct cache cache;

/* Global variables */

cache *proxy_cache; //cache to be used by the proxy
pthread_rwlock_t lock; //used for thread-locking


/* Cache functions */

void fix_linking(cache_node *p, cache *c_cache);
cache_node *check_for_hit(cache *c_cache, char *query);
void add_to_cache(cache *c_cache, char *query, char *q_data,
	unsigned int q_size);
void delete_from_cache(cache *c_cache);
cache *initialize_cache();

/*
 * initialize_cache - this function allocates space for the
 * cache. It does what its name suggests, initializes a cache
 * which can be used by the proxy.
 */
cache *initialize_cache(){
  cache *proxy_cache = Calloc(1, sizeof(cache));
  proxy_cache->start = NULL; //initially there is no start
  proxy_cache->end = NULL; //initially there is no end
  proxy_cache->cache_size = 0; //initially there is no data in the cache
  return proxy_cache;
}

/*
 * fix_linking - this function basically the cache_node p
 * from wherever it is in the cache linked list, to the front
 * of the list to indicate that it has been most recently used
 */
void fix_linking(cache_node *p, cache *c_cache){
  if (p->prev != NULL){
    if (p->next == NULL){ //p is at the end of the cache
      //so we update end of the cache
      p->prev->next = NULL;
      c_cache->end = p->prev;
    }
    else { //p is somewhere in the middle of the cache
      p->prev->next = p->next;
      p->next->prev = p->prev;
    }
    //moving p to the front of the cache
    p->next = c_cache->start;
    c_cache->start->prev = p;
    p->prev = NULL;
    c_cache->start = p;
  }
}

/*
 * check_for_hit - this function checks whether the data associated
 * with the url query has been cached in our cache. If so,
 * then it returns the cache node which has the data in question
 * and moves that node to the front of the cache
 * Otherwise it returns NULL
 */
cache_node *check_for_hit(cache *c_cache, char *query){
  //we don't want other threads accessing the cache while we might be
  //changing the order of nodes in the cache
  pthread_rwlock_wrlock(&lock);
  cache_node *p = c_cache->start;
  while (p != NULL){
    if (strcmp(p->url, query) == 0){
      fix_linking(p, c_cache);
      break;
    }
    p = p->next;
  }
  return p;
}


/*
 * add_to_cache - this function creates a new cache node with the given
 * information: query as the url, q_data as the data associated with that
 * url and q_size as the size of q_data in bytes
 * the newly created node is added to the front of the cache
 */
void add_to_cache(cache *c_cache, char *query, char *q_data,
    unsigned int q_size)
{
  if (!(q_size > MAX_OBJECT_SIZE)){
    //we only add a web obect to the cache if its size is less than
    //the max object size allowed
    //again, we don't want other threads accessing the cache while we
    //are writing to it
    pthread_rwlock_wrlock(&lock);
    cache_node *to_add = Calloc(1, sizeof(cache_node));
    to_add->url = Calloc(strlen(query) + 1, sizeof(char));
    strcpy(to_add->url, query);
    to_add->data = Malloc(q_size);
    memcpy(to_add->data, q_data, q_size);
    to_add->data_size = q_size;
    if (c_cache->start == NULL){
      //the cache is empty, so we set both start and end
      //to the newly created cache_node
      to_add->prev = NULL;
      to_add->next = NULL;
      c_cache->start = to_add;
      c_cache->end = to_add;
    }
    else { //the cache has at least one node
      //so we just add the newly created node to the front
      //of the cache
      to_add->prev = NULL;
      to_add->next = c_cache->start;
      c_cache->start->prev = to_add;
      c_cache->start = to_add;
    }
    //update the cache size to include the size of the newly cached data
    c_cache->cache_size += q_size;
    //if the addition of the new data caused us to exceed the maximum cache
    //size allowed, we keep deleting nodes from the end of the cache till
    //it is within the required size bounds
    while (c_cache->cache_size > MAX_CACHE_SIZE){
      delete_from_cache(c_cache);
    }
    //we're done writing to the cache, so we can now allow other threads
    //to access it
    pthread_rwlock_unlock(&lock);
  }
}

/*
 * delete_from_cache - this function deletes the last node from the cache
 * (that is the end node), since this will always be the least recently
 * used node.
 */
void delete_from_cache(cache *c_cache){
  if (c_cache->end != NULL){ //can't delete from an empty cache!
    c_cache->cache_size -= c_cache->end->data_size;
    //since we're removing the end node, we update the size of
    //the cache to exclude the size of the data stored in the
    //end node
    if (c_cache->end->prev != NULL){
      c_cache->end->prev->next = NULL;
    }
    cache_node *temp = c_cache->end;
    //sine we allocate memory for url, data and the node itself,
    //we have to free them
    Free(temp->url);
    Free(temp->data);
    //update the end of the cache
    c_cache->end = c_cache->end->prev;
    Free(temp);
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
  //handling the SIGPIPE signal
  Signal(SIGPIPE, SIG_IGN); //ignore the SIGPIPE
  pthread_rwlock_init(&lock, 0);
  port = atoi(argv[1]);
  pthread_t tid;
  proxy_cache = initialize_cache(); //intitialize cache
  listenfd = Open_listenfd(port);
  while (1) {
    clientlen = sizeof(clientaddr);
    int *connfdp = Malloc(sizeof(int));
    *connfdp = Accept(listenfd, (SA *)&clientaddr, (socklen_t *)&clientlen);
    Pthread_create(&tid, NULL, doit_thread, connfdp);
  }
}

/*
 * doit_thread - this is the concurrent version of doit, written in the same
 * manner as explained in the lecture notes
 */

void *doit_thread(void *vargp){
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);
  doit(connfd);
  Close(connfd);
  return NULL;
}


/*
 * doit - the doit function firstly initializes all buffers to 0
 * and then proceeds to read and write. From where, there are two
 * cases that arise. If we've visited a page before, it is cached,
 * and hence we don't open a connection to the server, and simply
 * write the data to the client. If, however, it's not cached, then
 * we open a new connection to the server, write the request to
 * the server, get the response and then write the response to the
 * client.
 */

void doit(int fd)
{
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], path[MAXLINE], port[MAXLINE];
  char host_header[MAXLINE], remaining_headers[MAXLINE];
  char request[MAXLINE], server_buf[MAXLINE];
  rio_t rio;
  rio_t server_rio;

  //initialize all the buffers to 0
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


  //read the first line of the request to ensure that the
  //request is a GET request
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  sscanf(buf, "%s %s %s", method, uri, version);
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not Implemented",
      "Proxy does not implement this method");
      return;
  }
  //we first check if the given request has been cached
  cache_node *cache_hit = check_for_hit(proxy_cache, uri);
  if (cache_hit != NULL){
    //we found it in the cache, so we simply write the associated
    //data to the client
    if(rio_writen(fd, cache_hit->data, cache_hit->data_size) < 0){
      //since we've returned from check_for_hit, it is safe for
      //other threads to access the cache
      pthread_rwlock_unlock(&lock);
      //however, since the write failed, we return and effectively
      //close the connection with the client
      return;
    }
  }
  //since we've returned from check_for_hit, it is safe for
  //other threads to access the cache
  pthread_rwlock_unlock(&lock);

 /* the data we're looking for hasn't been cached, so we now need to
  * compile a request, connect with the server, write the request to the
  * server, read a response from the server, write that response to the
  * client and store the response in the cache
  */
  if (cache_hit == NULL) {
    //this is for storing the response
    char *response_data = Malloc(sizeof(char));
    //keeps track of the size of the response
    unsigned int data_size = 0;
    //get the request headers from the request
    read_requesthdrs(&rio, host_header, remaining_headers);
    //parse the uri to get the hostname, path and port number
    if (parse_uri(uri, hostname, path, port) < 0) {
      return;
    }
    //if the request didn't have a host header, we make our own
    if (strncmp(host_header, "Host: ", strlen("Host: ")) != 0){
      sprintf(host_header, "Host: %s\r\n", hostname);
    }
    //formats the request to be sent to the browser as one string
    compile_request(request, host_header, path, remaining_headers);
    int port_num = atoi(port);

    //open a connection with the server
    int server_fd = open_clientfd_r(hostname, port_num);
    if (server_fd < 0){
      //on failing to connect with the server, we effectively close
      //the connection with the client as well by returning
      return;
    }
    //write the request to the server
    if (rio_writen(server_fd, request, strlen(request)) < 0){
      //if writing request to the server fails, we close the
      //connection with the server and then return which effectively
      //closes the connection with the client
      Close(server_fd);
      return;
    }

    Rio_readinitb(&server_rio, server_fd);
    int len;
    while ((len = rio_readnb(&server_rio, server_buf, MAXLINE)) > 0){
      //read the response from the server and then write to the client
      if (rio_writen(fd, server_buf, len) < 0){
        //if write to client fails, close connection with server and
        //return, thereby closing connection with client
        Close(server_fd);
    	return;
      }
      //while we're reading response from the server, we need to keep
      //storing it in a string so that we can cache it
      response_data = Realloc(response_data, data_size + len);
      memcpy(response_data + data_size, server_buf, len);
      //keep a track of the size of the data needed to be cached
      data_size += len;
    }
    //now we have the data associated with request, and so we add it
    //to the cache
    add_to_cache(proxy_cache, uri, response_data, data_size);

    Close(server_fd);
    Free(response_data); //free response data since we allocated it
    return;
  }
}

/*
 * compile_request - this compiles all the requests and gets its constituents
 */

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
 * parse_uri - When an end user enters a URL, the HTTP request
 * is rather long, and not the request we seek. Thus,
 * this function parses the uri to only contain the
 * hostname and the path or query and everyhting following it.
 *
 */

int parse_uri(char *uri, char *hostname, char *path, char *port)
{
  int i = 0;
  char *is_uri = strstr(uri, "http://");
  //This part strips off the http:// part
  if (is_uri != NULL){
    while (uri[i] != '/'){
      i += 1;
    }
    i += 2;
    int j = 0;
    //if the url is over, or a new directory or port
    while (uri[i] != '\0' && uri[i] != '/' && uri[i] != ':'){
      hostname[j] = uri[i];
      j += 1;
      i += 1;
    }
    //end it
    hostname[j] = '\0';
    int k = 0;
    //to get the port
    if (uri[i] == ':'){
      i += 1;
      while (uri[i] != '/'){
        port[k] = uri[i];
    	i += 1;
    	k += 1;
      }
      port[k] = '\0';
    }
    //port 80 since it's a web request
    else {
      strcpy(port, "80\0");
    }
    int n = 0;
    //remaining path of the url
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




