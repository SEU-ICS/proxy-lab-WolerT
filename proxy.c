#include <stdio.h>
#include <pthread.h>
#include "csapp.h"
#include <string.h>
#include <stdlib.h>
#include "uthash.h"


/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

int parse_uri(char *uri, char *filename, char *host, char *port) {
    char *uri_copy = strdup(uri);
    char *dash = strstr(uri_copy, "://");
    char *host_start = NULL;
    char *host_end = NULL;
    char *port_start = NULL;

    if (dash) {
        host_start = dash + 3;
    } else {
        host_start = uri_copy;
        return -1;
    }

    host_end = strchr(host_start, '/');
    if (host_end) {
        *host_end = '\0';
        strcpy(host, host_start);
    } else {
        strcpy(host, host_start);
    }

    port_start = strchr(host_start, ':');
    if (port_start && (port_start[1] != '\0')) {
        *port_start = '\0';
        port_start++;
        sprintf(port, "%d", atoi(port_start));
    } else {
        strcpy(port, "80");
    }

    if (host_end) {
        strcpy(filename, host_end);
    } else {
        strcpy(filename, "/");
    }

    free(uri_copy);
    return 1;
}

typedef struct cache_block {
    char uri[MAXLINE];
    char data[MAX_OBJECT_SIZE];
    int len;
    UT_hash_handle hh; // makes this structure hashable
} cache_block;

cache_block *cache = NULL;
pthread_mutex_t cache_lock;

void init_cache() {
    pthread_mutex_init(&cache_lock, NULL);
}

cache_block *find_cache(char *uri) {
    pthread_mutex_lock(&cache_lock);
    cache_block *tmp, *ret = NULL;
    HASH_FIND_STR(cache, uri, tmp);
    if (tmp) {
        ret = tmp;
    }
    pthread_mutex_unlock(&cache_lock);
    return ret;
}

void insert_cache(char *uri, char *data, int len) {
    if (len > MAX_OBJECT_SIZE) {
        return;
    }
    pthread_mutex_lock(&cache_lock);

    cache_block *block = find_cache(uri);
    if (block) {
        memcpy(block->data, data, len);
        block->len = len;
    } else {
        block = malloc(sizeof(cache_block));
        strcpy(block->uri, uri);
        memcpy(block->data, data, len);
        block->len = len;
        HASH_ADD_KEYPTR(hh, cache, block->uri, strlen(block->uri), block);
    }

    pthread_mutex_unlock(&cache_lock);
}

void doit(int fd) {
    int parse_success;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char host[MAXLINE], port[MAXLINE];
    char filename[MAXLINE];
    char cache[MAXLINE];
    rio_t rio, rio2;

    /* Read request line and headers */
    Rio_readinitb(&rio, fd);

    if (!Rio_readlineb(&rio, buf, MAXLINE))
        return;

    printf("%s", buf);

    sscanf(buf, "%s %s %s", method, uri, version);

    char *pos = strstr(buf, "HTTP/1.1");
    if (pos != NULL)
        strcpy(pos, "HTTP/1.0");

    if (strcasecmp(method, "GET")) {
        printf("Proxy does not implement this method\r\n");
        return;
    }

    parse_success = parse_uri(uri, filename, host, port);
    if (parse_success < 0) {
        printf("Proxy couldn't find this file");
        return;
    }

    cache_block *cached = find_cache(uri);
    if (cached) {
        Rio_writen(fd, cached->data, cached->len);
        return;
    }

    int clientfd = open_clientfd(host, port);
    if (clientfd < 0) {
        printf("Cannot connect to server.\n");
        return;
    }

    Rio_readinitb(&rio2, clientfd);
    char buf2[MAXLINE * 5];
    size_t size = sprintf(buf2, "%s %s %s\r\nHost: %s\r\nConnection: close\r\nUser-Agent: Mozilla/5.0\r\n\r\n", method, filename, version, host);
    Rio_writen(clientfd, buf2, size);

    size_t n;
    size_t len = 0;
    char content[MAX_OBJECT_SIZE];
    while ((n = Rio_readlineb(&rio2, buf, MAXLINE)) != 0) {
        printf("proxy received %d bytes,then send\n", (int)n);
        Rio_writen(fd, buf, n);
        if (len + n < MAX_OBJECT_SIZE) {
            memcpy(content + len, buf, n);
            len += n;
        }
    }
    Close(clientfd);
    insert_cache(uri, content, len);
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

    init_cache();

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        pthread_t tid;
        pthread_create(&tid, NULL, (void * (*)(void *))doit, (void *)&connfd);
        pthread_detach(tid);
        Close(connfd);
    }

    printf("%s", user_agent_hdr);
    return 0;
}
