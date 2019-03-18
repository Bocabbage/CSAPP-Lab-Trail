/*
    proxy-lab: To build a Web proxy that acts
               as a middleman between a Web browser 
               and an end server.
    
    更新时间：2019/3/4(进度：搭建程序框架--基本照搬Tiny的设计，暂未考虑多线程，处理方式的不同将在doit()体现)
             2019/3/6(进度：doit接受terminal请求部分完成，向server发送请求部分完成)[均未验证]
             2019/3/9(进度：完成PartI:单事务顺序处理proxy)
             2019/3/15(进度：partII--并发完成[基于线程并发])
             2019/3/18(进度：partIII--cache动工，未通过./driver.sh的测试)
*/

#include <stdio.h>
#include"csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

/* Struct deefinition */
typedef struct
{
    char *name;
    char *object;    
}CacheLine;

typedef struct 
{
    int used_cnt;
    CacheLine* objects;
}Cache;

/* some variables defined by myself */
Cache cache;
int readcnt_nums; // for counting the number of 'readers' 
sem_t mutex,w;    // mutex for locking 'readcnt_nums', w for locking 'write'

/* Functions */
void doit(int connfd);
void reqhd_rd(rio_t *rio);
void uri_parsing(char *uri, char* host,char* trac,char *port);
void *thread(void *Pconnfd);

void init_cache();
int reader(int fd, char* uri);
void writer(char* uri,char* buf);
//void clienterror(int fd,char *cause,char *errnum,
//    char *shortmsg,char *longmsg);
int main(int argc, char **argv)
{
    int listenfd,*connfd;
    char hostname[MAXLINE], port[MAXLINE];  // MAXLINE is defined in csapp.h
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    pthread_t tid;

    if(argc != 2)
    {
        fprintf(stderr,"usage: %s <port>\n",argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);

    init_cache();

    while(1)
    {
        clientlen = sizeof(clientaddr); // Keeps the code protocol-independent
        connfd = Malloc(sizeof(int));
        *connfd = Accept(listenfd,(SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr,clientlen,hostname,MAXLINE,port,MAXLINE,0);
        Pthread_create(&tid,NULL,thread,connfd);
        //doit(connfd);
        
    }
    return 0;
}

void *thread(void *Pconnfd)
{
    int connfd = *((int*)Pconnfd);
    Pthread_detach(pthread_self());
    Free(Pconnfd);
    doit(connfd);
    return NULL;
}

void doit(int connfd)
{
    /* Accept and read the HTTP requests from terminal(s) */
    char ubuf[MAXLINE]; // For requests reading
    char method[MAXLINE],host[MAXLINE],uri[MAXLINE],version[MAXLINE];
    char port[MAXLINE],trac[MAXLINE];
    rio_t lrio;

    Rio_readinitb(&lrio,connfd);
    Rio_readlineb(&lrio,ubuf,MAXLINE);
    printf("Requests Header:\n");
    printf("%s",ubuf);

    sscanf(ubuf,"%s %s %s",method,uri,version);
    /*
    if(strcmp(method,"GET"))
    {
        clienterror(connfd,method,"501","No implemented",
            "The TINY server doesn't implement this method");
        return;
    }
    */
    // URI parsing and
    // we need reqhd_rd() to ignore the req header
    uri_parsing(uri,host,trac,port);
    reqhd_rd(&lrio);

    if(reader(connfd,uri))
    {
        // In the cache
        fprintf(stdout, "%s from cache\n",uri);
        fflush(stdout);
        return;
    }

    /* Send the HTTP requests to the Web Server */
    int clientfd;
    int n;
    //char reqline[MAXLINE];
    char buf[MAXLINE];
    char *buf_head = buf;
    rio_t rio;

    clientfd = Open_clientfd(host,port);
    Rio_readinitb(&rio,clientfd);
    /*
    // Build the HTTP request line
    strcpy(reqline,method);
    strcat(reqline," ");
    //strcat(reqline,host);
    strcat(reqline,trac);
    strcat(reqline," ");
    //strcat(reqline,version);
    strcat(reqline,"HTTP/1.0");
    //strcat(reqline,"\r\n");
    // request head is the User-Agent
    // request end is an "\r\n"
    */
    
    // For the cache-writing
    char object_buf[MAX_OBJECT_SIZE];
    int total_size = 0;

    sprintf(buf_head,"GET %s HTTP/1.0\r\n",trac);
    buf_head = buf + strlen(buf);
    sprintf(buf_head,"Host: %s\r\n\r\n",host);
    Rio_writen(clientfd,buf,MAXLINE);

    while((n=Rio_readlineb(&rio,buf,MAXLINE)))
    {    
        Rio_writen(connfd,buf,n);
        strcpy(object_buf + total_size, buf);
        total_size += n;
    }

    // write the cache
    if(total_size < MAX_OBJECT_SIZE)
        writer(uri,object_buf);

    Close(connfd);

}

void uri_parsing(char *uri, char *host, char *trac, char *port)
{
    char turi[MAXLINE];
    strcpy(turi,uri);
    char *p = strstr(turi,"http://");
    p += strlen("http://");
    char *q = strchr(p,'/');
    *q = '\0';
    strcpy(host,p);
    *q = '/';
    strcpy(trac,q);
    if((p=strchr(host,':'))!=NULL)
        strcpy(port,p+1);
    // The host name might not include 
    // the ':port'
    *p = '\0';

}

void reqhd_rd(rio_t *rio)
{
    char ubuf[MAXLINE];
    Rio_readlineb(rio,ubuf,MAXLINE);
    // sscanf() will just replace the "\r\n" to '\0'
    while(strcmp(ubuf,"\r\n"))
    {
        Rio_readlineb(rio,ubuf,MAXLINE);
        printf("%s",ubuf);
    }
    return;
}

void init_cache()
{
    Sem_init(&mutex,0,1);
    Sem_init(&w,0,1);
    readcnt_nums = 0;
    // 10 web objects
    cache.objects = (CacheLine*)Malloc(sizeof(CacheLine)*10);
    cache.used_cnt = 0;
    for(int i=0;i<10;++i)
    {
        cache.objects[i].name = Malloc(sizeof(char) * MAXLINE);
        cache.objects[i].object = Malloc(sizeof(char) * MAX_OBJECT_SIZE);
    }
}

int reader(int fd, char* uri)
{
    int in_cache = 0;
    P(&mutex);
    readcnt_nums++;
    if(readcnt_nums == 1)
        P(&w);
    V(&mutex);

    for(int i=0;i<10;++i)
        if(!strcmp(cache.objects[i].name,uri))
        {
            Rio_writen(fd,cache.objects[i].object, MAX_OBJECT_SIZE);
            in_cache = 1;
            break;
        }

    P(&mutex);
    readcnt_nums--;
    if(readcnt_nums == 0)
        V(&w);
    V(&mutex);
    return in_cache;
}

void writer(char* uri, char* buf)
{
    P(&w);
    strcpy(cache.objects[cache.used_cnt].name, uri);
    strcpy(cache.objects[cache.used_cnt].object, buf);
    ++cache.used_cnt;
    V(&w);
}

/*
void clienterror(int fd, char *cause, char *errnum, 
         char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    // Build the HTTP response body 
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    // Print the HTTP response 
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
*//*
    

*/

#include <stdio.h>
#include"csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

/* Struct deefinition */
typedef struct
{
    char *name;
    char *object;    
}CacheLine;

typedef struct 
{
    int used_cnt;
    CacheLine* objects;
}Cache;

/* some variables defined by myself */
Cache cache;
int readcnt_nums; // for counting the number of 'readers' 
sem_t mutex,w;    // mutex for locking 'readcnt_nums', w for locking 'write'

/* Functions */
void doit(int connfd);
void reqhd_rd(rio_t *rio);
void uri_parsing(char *uri, char* host,char* trac,char *port);
void *thread(void *Pconnfd);

void init_cache();
int reader(int fd, char* uri);
void writer(char* uri,char* buf);
//void clienterror(int fd,char *cause,char *errnum,
//    char *shortmsg,char *longmsg);
int main(int argc, char **argv)
{
    int listenfd,*connfd;
    char hostname[MAXLINE], port[MAXLINE];  // MAXLINE is defined in csapp.h
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    pthread_t tid;

    if(argc != 2)
    {
        fprintf(stderr,"usage: %s <port>\n",argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);

    init_cache();

    while(1)
    {
        clientlen = sizeof(clientaddr); // Keeps the code protocol-independent
        connfd = Malloc(sizeof(int));
        *connfd = Accept(listenfd,(SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr,clientlen,hostname,MAXLINE,port,MAXLINE,0);
        Pthread_create(&tid,NULL,thread,connfd);
        //doit(connfd);
        
    }
    return 0;
}

void *thread(void *Pconnfd)
{
    int connfd = *((int*)Pconnfd);
    Pthread_detach(pthread_self());
    Free(Pconnfd);
    doit(connfd);
    return NULL;
}

void doit(int connfd)
{
    /* Accept and read the HTTP requests from terminal(s) */
    char ubuf[MAXLINE]; // For requests reading
    char method[MAXLINE],host[MAXLINE],uri[MAXLINE],version[MAXLINE];
    char port[MAXLINE],trac[MAXLINE];
    rio_t lrio;

    Rio_readinitb(&lrio,connfd);
    Rio_readlineb(&lrio,ubuf,MAXLINE);
    printf("Requests Header:\n");
    printf("%s",ubuf);

    sscanf(ubuf,"%s %s %s",method,uri,version);
    /*
    if(strcmp(method,"GET"))
    {
        clienterror(connfd,method,"501","No implemented",
            "The TINY server doesn't implement this method");
        return;
    }
    */
    // URI parsing and
    // we need reqhd_rd() to ignore the req header
    uri_parsing(uri,host,trac,port);
    reqhd_rd(&lrio);

    if(reader(connfd,uri))
    {
        // In the cache
        fprintf(stdout, "%s from cache\n",uri);
        fflush(stdout);
        return;
    }

    /* Send the HTTP requests to the Web Server */
    int clientfd;
    int n;
    //char reqline[MAXLINE];
    char buf[MAXLINE];
    char *buf_head = buf;
    rio_t rio;

    clientfd = Open_clientfd(host,port);
    Rio_readinitb(&rio,clientfd);
    /*
    // Build the HTTP request line
    strcpy(reqline,method);
    strcat(reqline," ");
    //strcat(reqline,host);
    strcat(reqline,trac);
    strcat(reqline," ");
    //strcat(reqline,version);
    strcat(reqline,"HTTP/1.0");
    //strcat(reqline,"\r\n");
    // request head is the User-Agent
    // request end is an "\r\n"
    */
    
    // For the cache-writing
    char object_buf[MAX_OBJECT_SIZE];
    int total_size = 0;

    sprintf(buf_head,"GET %s HTTP/1.0\r\n",trac);
    buf_head = buf + strlen(buf);
    sprintf(buf_head,"Host: %s\r\n\r\n",host);
    Rio_writen(clientfd,buf,MAXLINE);

    while((n=Rio_readlineb(&rio,buf,MAXLINE)))
    {    
        Rio_writen(connfd,buf,n);
        strcpy(object_buf + total_size, buf);
        total_size += n;
    }

    // write the cache
    if(total_size < MAX_OBJECT_SIZE)
        writer(uri,object_buf);

    Close(connfd);

}

void uri_parsing(char *uri, char *host, char *trac, char *port)
{
    char turi[MAXLINE];
    strcpy(turi,uri);
    char *p = strstr(turi,"http://");
    p += strlen("http://");
    char *q = strchr(p,'/');
    *q = '\0';
    strcpy(host,p);
    *q = '/';
    strcpy(trac,q);
    if((p=strchr(host,':'))!=NULL)
        strcpy(port,p+1);
    // The host name might not include 
    // the ':port'
    *p = '\0';

}

void reqhd_rd(rio_t *rio)
{
    char ubuf[MAXLINE];
    Rio_readlineb(rio,ubuf,MAXLINE);
    // sscanf() will just replace the "\r\n" to '\0'
    while(strcmp(ubuf,"\r\n"))
    {
        Rio_readlineb(rio,ubuf,MAXLINE);
        printf("%s",ubuf);
    }
    return;
}

void init_cache()
{
    Sem_init(&mutex,0,1);
    Sem_init(&w,0,1);
    readcnt_nums = 0;
    // 10 web objects
    cache.objects = (CacheLine*)Malloc(sizeof(CacheLine)*10);
    cache.used_cnt = 0;
    for(int i=0;i<10;++i)
    {
        cache.objects[i].name = Malloc(sizeof(char) * MAXLINE);
        cache.objects[i].object = Malloc(sizeof(char) * MAX_OBJECT_SIZE);
    }
}

int reader(int fd, char* uri)
{
    int in_cache = 0;
    P(&mutex);
    readcnt_nums++;
    if(readcnt_nums == 1)
        P(&w);
    V(&mutex);

    for(int i=0;i<10;++i)
        if(!strcmp(cache.objects[i].name,uri))
        {
            Rio_writen(fd,cache.objects[i].object, MAX_OBJECT_SIZE);
            in_cache = 1;
            break;
        }

    P(&mutex);
    readcnt_nums--;
    if(readcnt_nums == 0)
        V(&w);
    V(&mutex);
    return in_cache;
}

void writer(char* uri, char* buf)
{
    P(&w);
    strcpy(cache.objects[cache.used_cnt].name, uri);
    strcpy(cache.objects[cache.used_cnt].object, buf);
    ++cache.used_cnt;
    V(&w);
}

/*
void clienterror(int fd, char *cause, char *errnum, 
         char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    // Build the HTTP response body 
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    // Print the HTTP response 
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
*/