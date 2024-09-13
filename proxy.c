#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_CACHE_ENTRIES 500
#define MAX_THREADS 100
#define WEB_PREFIX "http://"

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

sem_t mutex;

typedef struct cache_entry {
    char uri[MAXLINE+5];
    char data[MAX_OBJECT_SIZE];
    int len;
    int valid;
} cache_entry;

cache_entry cache[MAX_CACHE_ENTRIES];

void init() {
    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        cache[i].valid = 0;
    }
    sem_init(&mutex, 0, MAX_THREADS);
}

int find(char *uri) {
    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (cache[i].valid && strcmp(cache[i].uri, uri) == 0) {
            return i;
        }
    }
    return -1;
}

void cache_insert(char *uri, char *data, int len) {
    if (len > MAX_OBJECT_SIZE) {
        return;
    }
    sem_wait(&mutex);

    int index = -1;
    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (!cache[i].valid) {
            index = i;
            break;
        }
    }

    if (index == -1) {
        index = 0;
    }

    strcpy(cache[index].uri, uri);
    memcpy(cache[index].data, data, len);
    cache[index].len = len;
    cache[index].valid = 1;

    sem_post(&mutex);
}

void *thread(void *varg) {
    int connfd = *( (int *) varg);
    Pthread_detach(pthread_self());

    sem_wait(&mutex);
    doit(connfd);
    sem_post(&mutex);

    Close(connfd);
    return NULL;
}

void parse_uri(char* uri, char* host, char* port, char* fileName)
{
    sscanf(buf, "%s %s %s", method, uri, version);
    char* hostp = strstr(uri, WEB_PREFIX) + strlen(WEB_PREFIX);
    char* dash = strstr(hostp, "/");
    char* colon = strstr(hostp, ":");
    if(!colon)
    {
        strncpy(host, hostp, dash - hostp);
        host[dash - hostp] = '\0'
        strcpy(port,'80');
    }
    else
    {
        strncpy(host, hostp, colon - hostp);
        host[colon - host] = '\0';
        strncpy(port, colon + 1, dash - colon -1);
        port[dash - colon -1] = '\0'
    }

    strcpy(fileName, dash);
}

void doit(int fd) {
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], port[MAXLINE], filename[MAXLINE];
    char server[MAXLINE*3];
    rio_t rio, rio2;

    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE)) {
        return;
    }
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")) {
        printf("Proxy does not implement this method\r\n");
        return;
    }

    cache_entry *block = &cache[find(uri)];
    
    if (block != NULL && block->valid) {
        Rio_writen(fd, block->data, block->len);
        return;
    }

    parse_uri(uri, hostname, port, filename);
    char buf2[MAXLINE*5];
    size_t size = sprintf(buf2, "%s %s %s\r\nHost: %s\r\nConnection: close\r\nUser-Agent: %s\r\n\r\n", method, filename, version, hostname, user_agent_hdr);

    int serverfd = open_clientfd(hostname, port);
    Rio_readinitb(&rio2, serverfd);
    Rio_writen(serverfd, buf2, strlen(buf2));

    size_t n;
    size_t len = 0;
    char content[MAX_OBJECT_SIZE];
    while ((n = Rio_readlineb(&rio2, buf, MAXLINE)) != 0) {
        printf("proxy received %d bytes, then send\n", (int)n);
        Rio_writen(fd, buf, n);
        if (len + n < MAX_OBJECT_SIZE) {
            memcpy(content + len, buf, n);
            len += n;
        }
    }
    Close(serverfd);
    cache_insert(uri, content, len);
}

int main(int argc, char **argv) {
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    pthread_t tid;

    init();

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        pthread_create(&tid, NULL, thread, (void *)&connfd);
    }
    return 0;
}
