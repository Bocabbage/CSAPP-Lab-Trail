/*
    proxy-lab: To build a Web proxy that acts
               as a middleman between a Web browser 
               and an end server.
    
    更新时间：2019/3/4(进度：搭建程序框架--基本照搬Tiny的设计，暂未考虑多线程，处理方式的不同将在doit()体现)
             2019/3/6(进度：doit接受terminal请求部分完成，向server发送请求部分完成)[均未验证]
*/

#include <stdio.h>
#include"csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

/* Functions */
void doit(int connfd);
void reqhd_rd(rio_t *rio,char* host);

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

void doit(int connfd)
{
    /* Accept and read the HTTP requests from terminal(s) */
    char ubuf[MAXLINE]; // For requests reading
    char method[MAXLINE],host[MAXLINE],uri[MAXLINE],version[MAXLINE];
    rio_t lrio;

    Rio_readinitb(&lrio,connfd);
    Rio_readlineb(&lrio,ubuf,MAXLINE);
    printf("Requests Header:\n");
    printf("%s",ubuf);

    sscanf(ubuf,"%s %s %s",method,uri,version);
    if(strcasecmp(method,"GET"))
    {
        clineterror(fd,method,"501","No implemented",
            "The TINY server doesn't implement this method");
        return;
    }
    // The lab requires implementing a HTTP/1.0 GET
    // but also asks 'Host' info from the Request headers
    // to 'coax sensible responses out of certain Web servers'
    // So we do need a ReadHead parsing
    reqhd_rd(&lrio,host);

    /* Send the HTTP requests to the Web Server */
    int clientfd;
    char *port = "8080";
    char reqline[MAXLINE];
    char reqhead[MAXLINE];
    rio_t rio;

    clientfd = Open_clientfd(host,port);
    Rio_readinitb(&rio,clientfd);
    // Build the HTTP request line
    strcmp(reqline,"GET ");
    strcat(reqline,host);
    strcat(reqline,uri);
    strcat(reqline," ");
    strcat(reqline,version);
    strcat(reqline,"\r\n");
    // request head is the User-Agent
    // request end is an "\r\n"

    Rio_writen(clientfd,reqline,strlen(reqline));
    Rio_writen(clientfd,user_agent_hdr,strlen(user_agent_hdr));
    Rio_writen(clientfd,"\r\n",strlen("\r\n"));

    

}

void reqhd_rd(rio_t *rio,char* host)
{
    char ubuf[MAXLINE];
    Rio_readlineb(&rio,ubuf,MAXLINE);
    // sscanf() will just replace the "\r\n" to '\0'
    sscanf(ubuf,"%*s%s",host);
    printf("Host: %s",host);
    while(strcmp(ubuf,"\r\n"))
    {
        Rio_readlineb(&rio,ubuf,MAXLINE);
        printf("%s",ubuf);
    }
    return;
}