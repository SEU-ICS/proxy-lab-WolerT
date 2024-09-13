//Name: Tang Xiangjie
//Number:09J22120

#include <stdio.h>
#include "csapp.h"
#include <string.h>
#include <pthread.h>

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


void doit(int fd) 
{
    int parse_success;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char host[MAXLINE], port[MAXLINE];
    char filename[MAXLINE]/*cgiargs[MAXLINE]*/;
    char cache[MAXLINE];
    rio_t rio,rio2;

    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    
    //if fd empty
    if (!Rio_readlineb(&rio, buf, MAXLINE))  
        return;
    
    printf("%s", buf);

    //read method uri version
    sscanf(buf, "%s %s %s", method, uri, version);       //line:netp:doit:parserequest
    
    //transform http version
    char *pos = strstr(buf, "HTTP/1.1");
    if (pos != NULL) 
        strcpy(pos, "HTTP/1.0");

    //if get?
    if (strcasecmp(method, "GET")) 
    {                     //line:netp:doit:beginrequesterr
        printf("Proxy does not implement this method\r\n");
        return;
    }                                                    //line:netp:doit:endrequesterr
    
                                //line:netp:doit:readrequesthdrs

    /* Parse URI from GET request */
    parse_success = parse_uri(&uri[0], &filename[0], /*cgiargs,*/ &host[0], &port[0]);       //line:netp:doit:staticcheck
    if (parse_success < 0) 
    {                     //line:netp:doit:beginnotfound
	    printf("Proxy couldn't find this file");
	    return;
    }                                                    //line:netp:doit:endnotfound

    
    
    //open client
    int clientfd=open_clientfd(host, port); 
    if (clientfd < 0)
    {
        printf("Cannot connect to server.\n");
        return;
    }

    Rio_readinitb(&rio2, clientfd);
    char buf2[MAXLINE*5];
    size_t size = sprintf(buf2,"%s %s %s\r\nHost: %s\r\nConnection: close\r\nUser-Agent: Mozilla/5.0\r\n\r\n", method, filename, version,host);
    Rio_writen(clientfd, buf2, size);

    size_t n;
    size_t len=0;
    char content[MAX_OBJECT_SIZE];
    while ((n = Rio_readlineb(&rio2, buf, MAXLINE)) != 0)
    {
        printf("proxy received %d bytes,then send\n", (int)n);
        Rio_writen(fd, buf, n);
        if(len + n < MAX_OBJECT_SIZE)
        {
            memcpy(content + len, buf, n);
            len += n;
        }
    }
    Close(clientfd);
}

int main(int argc, char **argv)
{
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
    
    while (1) {
	clientlen = sizeof(clientaddr);
	connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
    Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
	
    doit(connfd);                                             //line:netp:tiny:doit
	
    Close(connfd);                                            //line:netp:tiny:close
    }

    
    printf("%s", user_agent_hdr);
    return 0;
}
