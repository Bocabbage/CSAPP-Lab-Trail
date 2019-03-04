/*
    proxy-lab: To build a Web proxy that acts
               as a middleman between a Web browser 
               and an end server.
    
    更新时间：2019/3/4(进度：搭建程序框架--基本照搬Tiny的设计，暂未考虑多线程，处理方式的不同将在doit()体现)

*/

#include <stdio.h>
#include"csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

/* Functions */
void doit(connfd);

int main(int argc, char **argv)
{
    int listenfd,connfd;
    char hostname[MAXLINE], port[MAXLINE];  // MAXLINE is defined in csapp.h
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    if(argc != 2)
    {
        fprintf(stderr,"usage: %s <port>\n",argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    printf("%s", user_agent_hdr);

    while(1)
    {
        clientlen = sizeof(clientaddr); // Keeps the code protocol-independent
        connfd = Accept(listenfd,(SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr,clientlen,hostname,MAXLINE,port,MAXLINE,0);
        doit(connfd);
        Close(connfd);
    }
    return 0;
}
