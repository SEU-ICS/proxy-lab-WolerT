//Name:唐祥杰
//Number：09J22120

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

void parse_uri(char* uri, char* host, char* port, char* fileName)
{
    if (strncmp(uri, WEB_PREFIX, strlen(WEB_PREFIX)) != 0)
    {
        fprintf(stderr, "Error: Invalid URI format, must start with 'http://'\n");
        return;
    }

    char* hostp = uri + strlen(WEB_PREFIX);

    char* dash = strstr(hostp, "/");
    if (!dash)
    {
        fprintf(stderr, "Error: Invalid URI format, missing path\n");
        return;
    }

    char* colon = strstr(hostp, ":");
    
    if (colon)
    {
        int host_len = colon - hostp;
        strncpy(host, hostp, host_len);
        host[host_len] = '\0';

        int port_len = dash - colon - 1;
        strncpy(port, colon + 1, port_len);
        port[port_len] = '\0';
    }
    else
    {
        int host_len = dash - hostp;
        strncpy(host, hostp, host_len);
        host[host_len] = '\0';
        strcpy(port, "80");
    }

    strcpy(fileName, dash);

    if (strlen(host) == 0 || strlen(port) == 0 || strlen(fileName) == 0)
    {
        fprintf(stderr, "Error: Failed to parse URI components. Host: %s, Port: %s, Filename: %s\n", host, port, fileName);
    }
}

typedef struct cache_entry
{
    char uri[MAXLINE+5];
    char data[MAX_OBJECT_SIZE];
    int len;
    int valid;
} cache_entry;

cache_entry cache[MAX_CACHE_ENTRIES];

void init()
{
    for (int i = 0; i < MAX_CACHE_ENTRIES; i++)
    {
        cache[i].valid = 0;
    }
    sem_init(&mutex, 0, MAX_THREADS);
}

int find(char *uri)
{
    for (int i = 0; i < MAX_CACHE_ENTRIES; i++)
    {
        if (cache[i].valid && strcmp(cache[i].uri, uri) == 0)
        {
            return i;
        }
    }
    return -1;
}

void insert(char *uri, char *data, int len)
{
    if (len > MAX_OBJECT_SIZE)
    {
        return;
    }
    sem_wait(&mutex);

    int index = -1;
    for (int i = 0; i < MAX_CACHE_ENTRIES; i++)
    {
        if (!cache[i].valid)
        {
            index = i;
            break;
        }
    }

    if (index == -1)
    {
        index = 0;
    }

    strcpy(cache[index].uri, uri);
    memcpy(cache[index].data, data, len);
    cache[index].len = len;
    cache[index].valid = 1;

    sem_post(&mutex);
}

void *thread(void *varg)
{
    int connfd = * ((int *) varg);
    Pthread_detach(pthread_self());

    sem_wait(&mutex);
    doit(connfd);
    sem_post(&mutex);

    Close(connfd);
    return NULL;
}

int main(int argc, char **argv)
{
    pthread_t tid;
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    init();
    
    while (1)
    {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        pthread_create(&tid, NULL, thread, (void *)&connfd);
    }
    return 0;
}
