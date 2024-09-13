#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_THREADS 4       // Number of worker threads
#define MAX_QUEUE_SIZE 16   // Maximum queue size for buffering connections

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

/* Queue for buffering incoming connections */
typedef struct {
    int buf[MAX_QUEUE_SIZE];
    int front;
    int rear;
    int count;
    sem_t mutex;
    sem_t slots;
    sem_t items;
} sbuf_t;

sbuf_t sbuf;

void sbuf_init(sbuf_t *sp, int n) {
    sp->front = sp->rear = 0;
    sp->count = 0;
    sem_init(&sp->mutex, 0, 1);
    sem_init(&sp->slots, 0, n);
    sem_init(&sp->items, 0, 0);
}

void sbuf_deinit(sbuf_t *sp) {
    sem_destroy(&sp->mutex);
    sem_destroy(&sp->slots);
    sem_destroy(&sp->items);
}

void sbuf_insert(sbuf_t *sp, int item) {
    sem_wait(&sp->slots);
    sem_wait(&sp->mutex);
    sp->buf[(++sp->rear) % MAX_QUEUE_SIZE] = item;
    sp->count++;
    sem_post(&sp->mutex);
    sem_post(&sp->items);
}

int sbuf_remove(sbuf_t *sp) {
    sem_wait(&sp->items);
    sem_wait(&sp->mutex);
    int item = sp->buf[(++sp->front) % MAX_QUEUE_SIZE];
    sp->count--;
    sem_post(&sp->mutex);
    sem_post(&sp->slots);
    return item;
}

int parse_uri(char *uri, char *filename, char *host, char *port) {
    char *uri_copy = strdup(uri);
    char *dash = strstr(uri_copy, "://");
    char *host_start = NULL;
    char *host_end = NULL;
    char *port_start = NULL;

    // Read host
    if (dash) 
        host_start = dash + 3; 
    else 
    {
        host_start = uri_copy;
        free(uri_copy);
        return -1;
    } 

    host_end = strchr(host_start, '/');
    
    if (host_end) 
    {
        *host_end = '\0';
        strcpy(host, host_start);
    }

    // Read port
    port_start = strchr(host_start, ':');
    
    if (port_start && (port_start[1] != '\0')) 
    {
        *port_start = '\0';
        port_start++;
        strcpy(port, port_start);
    } 
    else {
        strcpy(port, "80");
    }

    // Read filename
    if (host_end) {
        strcpy(filename, host_end);
    }

    free(uri_copy);
    return 1;
}

void doit(int fd) {
    int parse_success;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char host[MAXLINE], port[MAXLINE];
    char filename[MAXLINE];
    rio_t rio, rio2;

    // Read request line and headers from the client
    Rio_readinitb(&rio, fd);
    
    // If fd empty
    if (!Rio_readlineb(&rio, buf, MAXLINE))  
        return;
    
    printf("%s", buf);

    // Parse the request line to extract the method, URI, and version
    sscanf(buf, "%s %s %s", method, uri, version);
    
    // Transform HTTP version from 1.1 to 1.0
    char *pos = strstr(buf, "HTTP/1.1");
    if (pos != NULL) 
        strcpy(pos, "HTTP/1.0");

    // Check if the method is GET; if not, reject the request
    if (strcasecmp(method, "GET")) {
        printf("Proxy does not implement this method\r\n");
        return;
    }

    // Parse the URI to extract the host, port, and filename/path
    parse_success = parse_uri(&uri[0], &filename[0], &host[0], &port[0]);
    if (parse_success < 0) {
	    printf("Proxy couldn't find this file");
	    return;
    }

    // Open a connection to the target server
    int clientfd = open_clientfd(host, port); 
    if (clientfd < 0) {
        printf("Cannot connect to server.\n");
        return;
    }

    // Initialize a new Rio buffer for reading from the target server
    Rio_readinitb(&rio2, clientfd);

    // Build the request to forward to the target server
    char server_request[MAXLINE];
    sprintf(server_request, "%s %s HTTP/1.0\r\n", method, filename);

    // Forward headers from the client request to the target server
    while (Rio_readlineb(&rio, buf, MAXLINE) > 0) {
        // Stop if we reach the end of the headers
        if (strcmp(buf, "\r\n") == 0) break;

        // Ensure the Host header is included correctly
        if (strstr(buf, "Host:") != NULL) {
            strcpy(host, buf);  // Copy the host header
        }

        // Avoid forwarding Connection and Proxy-specific headers
        if (strstr(buf, "Connection:") == NULL && strstr(buf, "Proxy-Connection:") == NULL) {
            strcat(server_request, buf); // Append the valid headers
        }
    }

    // Ensure required headers are included
    if (strlen(host) == 0) {
        sprintf(buf, "Host: %s\r\n", host);
        strcat(server_request, buf);
    }
    strcat(server_request, "Connection: close\r\n");
    strcat(server_request, "Proxy-Connection: close\r\n");
    strcat(server_request, user_agent_hdr);
    strcat(server_request, "\r\n");

    // Send the constructed request to the target server
    Rio_writen(clientfd, server_request, strlen(server_request));

    // Read the response from the server and send it back to the client
    size_t n;
    char content[MAX_OBJECT_SIZE];
    size_t len = 0;
    while ((n = Rio_readlineb(&rio2, buf, MAXLINE)) != 0) {
        printf("proxy received %d bytes, then send\n", (int)n);
        Rio_writen(fd, buf, n);
        // Buffer response content for caching
        if (len + n < MAX_OBJECT_SIZE) {
            memcpy(content + len, buf, n);
            len += n;
        }
    }

    // Close the connection to the server
    Close(clientfd);
}

void *thread(void *vargp) {
    pthread_detach(pthread_self());
    while (1) {
        int connfd = sbuf_remove(&sbuf);
        doit(connfd);
        Close(connfd);
    }
}

int main(int argc, char **argv) {
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    sbuf_init(&sbuf, MAX_QUEUE_SIZE);

    // Create worker threads
    pthread_t tid;
    for (int i = 0; i < MAX_THREADS; i++) {
        pthread_create(&tid, NULL, thread, NULL);
    }

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        sbuf_insert(&sbuf, connfd);
    }

    sbuf_deinit(&sbuf);
    return 0;
}
