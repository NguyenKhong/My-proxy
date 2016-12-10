/*
 * proxy.c - CS:APP Web proxy
 *
 * TEAM MEMBERS:
 *     Andrew Carnegie, ac00@cs.cmu.edu 
 *     Harry Q. Bovik, bovik@cs.cmu.edu
 * 
 * IMPORTANT: Give a high level description of your code here. You
 * must also provide a header comment at the beginning of each
 * function that describes what that function does.
 */ 

#include "csapp.h"

static const char *MSG_405_ERR = "HTTP/1.1 405 Method Not Allowed\r\n";
static const char *MSG_500_ERR = "HTTP/1.1 500 Internal Server Error\r\n";
static const char DEBUG = 1;

typedef struct _InfoClient
{
    int skClient;
    struct sockaddr_in ClientAddr;
} InfoClient, *pInfoClient;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_iTotalThread = PTHREAD_MUTEX_INITIALIZER;
 int iTotalThread = 0;

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *hostname, char *pathname, int  *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);
void proxy_server(int iPort);
void do_it(int socket, struct sockaddr_in* ClientAddr);
void proxy_client(char* sHostName, unsigned short iPort, rio_t* rio,  struct sockaddr_in* ClientAddr, char* sUri);
int Rio_Get_Socket(rio_t* rio);
char* replace_str(char* sSource, char* sSearchString, char* sReplaceString);
void *func_thread(void* arg);
void print_hex(char* a, int iLen);

/* 
 * main - Main routine for the proxy program 
 */
int main(int argc, char **argv)
{

    printf("Proxy Server Start...\n");
    if (argc != 2) {
    fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
    exit(0);
    }
    signal(SIGPIPE, SIG_IGN);

    int iPort = atoi(argv[1]);

    proxy_server(iPort);

    exit(0);
}

// Start Server 
void proxy_server(int iPort)
{
    int skServer; //socket Server
    int skClient;
    socklen_t skLen;
    struct sockaddr_in ClientAddr;

    skServer = open_listenfd(iPort);
    pthread_t thread;

    InfoClient infoClient;

   

    while (1)
    {

        skLen = sizeof(ClientAddr);
        memset(&ClientAddr, 0, skLen);
        if( (skClient = Accept(skServer, (struct sockaddr* )&ClientAddr, &skLen)) < 0 )  continue;
        printf(" +> Connect from: %s:%d\n", inet_ntoa(*(struct in_addr* )&ClientAddr.sin_addr.s_addr), ntohs(ClientAddr.sin_port));

        infoClient.skClient = skClient;
        infoClient.ClientAddr = ClientAddr;

        //do_it(skClient, &ClientAddr);
        pthread_create(&thread, NULL, func_thread, (void* )&infoClient);
        printf(" Total thread = %d\n", iTotalThread);
        //Close(skClient);

    }


}

void *func_thread(void* arg)
{
    pthread_mutex_lock(&mutex_iTotalThread);
    ++iTotalThread;
    pthread_mutex_unlock(&mutex_iTotalThread);
    
    InfoClient infoClient = *((pInfoClient)arg);
    pthread_detach(pthread_self());
    do_it(infoClient.skClient, &(infoClient.ClientAddr));
    if( infoClient.skClient ) Close(infoClient.skClient);

    pthread_mutex_lock(&mutex_iTotalThread);
    --iTotalThread;
    pthread_mutex_unlock(&mutex_iTotalThread);
    return NULL;
}


/*
 * do_it function receive http request from web browser 
 * Only method GET and parse it ( hostname, port, Uri )
 * After pass it into porxy_client
 *
 */
void do_it(int skClient, struct sockaddr_in* ClientAddr)
{
    char buff[MAXLINE], sUri[MAXLINE], sMethod[MAXLINE], sVersion[MAXLINE];
    char sHostName[MAXLINE], sPathName[MAXLINE];
    int iPort;
    rio_t rio;

    memset(&rio, 0, sizeof(rio));
    memset(buff, 0, MAXLINE);
    Rio_readinitb(&rio, skClient);
    if( Rio_readlineb(&rio, buff, MAXLINE) <= 0 )  return;
    sscanf(buff, "%s %s %s", sMethod, sUri, sVersion);
    if ( strcmp(sMethod, "GET") != 0 )
    {
        Rio_writen(skClient, (void*) MSG_405_ERR, sizeof(MSG_405_ERR));
        printf(" ==> Method is not GET is: %s\n", sMethod);
        return;
    }

    if ( parse_uri(sUri, sHostName, sPathName, &iPort) != 0 )
    {
        printf(" ==> Parse uri error\n");
        return; 
    }

    proxy_client(sHostName, iPort, &rio, ClientAddr, sUri);
}


/*
 * proxy_client function receive http request from web browser 
 * Open is a connect to server (hostname, portt), send http request of web browser  
 * and receive data from server, after send again data for web browser
 * Write log.
 */
void proxy_client(char* sHostName, unsigned short iPort, rio_t* rio,  struct sockaddr_in* ClientAddr, char* sUri)
{
    char tmpBuff[MAXLINE];

    printf(" proxy client start...\n");

    int skClient = Rio_Get_Socket(rio);

    memset(tmpBuff, 0, MAXLINE);

    strncat(tmpBuff, rio->rio_buf, MAXLINE-1);
    rio->rio_cnt = 0;
    rio->rio_bufptr = rio->rio_buf;

    int skProxyClient;
    if ( (skProxyClient = Open_clientfd(sHostName, iPort)) < 0 )
    {
        fprintf(stderr, "proxy_client cannot open connect to %s\n", sHostName);
        Rio_writen(skClient, (void* )MSG_500_ERR, sizeof(MSG_500_ERR));
        return;
    }

    char* buff = NULL;
    int nRead = 0;

    buff = replace_str(tmpBuff, "keep-alive", "close");
    if ( (buff != tmpBuff) && (!buff) ) free(buff);
    Rio_writen(skProxyClient, buff, strlen(buff));
    if( DEBUG ) puts(buff);

    while( strstr(tmpBuff,  "\r\n\r\n") == NULL )
    {
        if ( (nRead = Rio_readn(skClient, tmpBuff,  MAXLINE-2)) > 0 )
        {
            tmpBuff[nRead+1] = '\0';
            buff = replace_str(tmpBuff, "keep-alive", "close");
            if ( (buff != tmpBuff) && (!buff) ) free(buff);
            Rio_writen(skProxyClient, buff, nRead);
            if( DEBUG ) puts(buff);
        }
    }
    
    unsigned iTotal = 0;
    struct timeval TimeOut;
    TimeOut.tv_sec = 60;
    TimeOut.tv_usec = 0;
    if ( setsockopt(skProxyClient, SOL_SOCKET, SO_RCVTIMEO, (char* )&TimeOut, sizeof(TimeOut)) < 0 )
        error(" ==> setsockopt failed\n");

    memset(tmpBuff, 0, MAXLINE);

    char *pPos = NULL;

    // Reponse Header
    
    while( (nRead = Rio_readn(skProxyClient, tmpBuff, 1024)) > 0 )
    {
        iTotal += nRead;
        
        Rio_writen(skClient, tmpBuff, nRead);
        if( (pPos = strstr(tmpBuff, "\r\n\r\n")) != NULL ) 
        {
            if( DEBUG )
            {
                // print http response header
                tmpBuff[pPos - tmpBuff] = '\0'; 
                printf("%s", tmpBuff);  
            }
            
            break;
        }
        if( DEBUG ) printf("%s", tmpBuff);
        memset(tmpBuff, 0, nRead);
    }

    //Reponse body 
    
    //Rio_writen(skClient, pPos + 4, nRead - (pPos - tmpBuff));    

    while( (nRead = Rio_readn(skProxyClient, tmpBuff, MAXLINE)) > 0 )
    {
        iTotal += nRead;
        //printf("%s", tmpBuff);
        Rio_writen(skClient, tmpBuff, nRead);
        memset(tmpBuff, 0, nRead);
    } 

    printf("\r\n");
    if( errno == EAGAIN ) 
        fprintf(stderr, "==> Connection timeout host: %s\n",  sHostName);

    Close(skProxyClient);


    char logstring[MAXLINE];

    format_log_entry(logstring, ClientAddr, sUri, iTotal);

    pthread_mutex_lock(&mutex);
    FILE* hFile = fopen("./proxy.log", "at");
    fputs(logstring, hFile);
    fputs("\r\n", hFile);
    fflush(hFile);
    fclose(hFile);
    pthread_mutex_unlock(&mutex);


}
/*
 * parse_uri - URI parser
 * 
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0) {
        hostname[0] = '\0';
    return -1;
    }
       
    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';
    
    /* Extract the port number */
    *port = 80; /* default */
    if (*hostend == ':')   
    *port = atoi(hostend + 1);
    
    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
    pathname[0] = '\0';
    }
    else {
    pathbegin++;    
    strcpy(pathname, pathbegin);
    }

    return 0;
}

/*
 * format_log_entry - Create a formatted log entry in logstring. 
 * 
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, 
              char *uri, int size)
{
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /* 
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 13, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;


    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %d.%d.%d.%d %s size: %d", time_str, a, b, c, d, uri, size);
}


int Rio_Get_Socket(rio_t* rio)
{
    return rio->rio_fd;
}

/*
 * replace_str function Replaces substrings in a string
 * If it exist is return new string else return sSource
 */
char* replace_str(char* sSource, char* sSearchString, char* sReplaceString)
{
    char* p;
    if ( !(p = strstr(sSource, sSearchString)) )
        return sSource;

    size_t iByteAlloc = strlen(sSource) + strlen(sReplaceString) - strlen(sSearchString) + 1;

    char* buff = (char* )malloc(iByteAlloc);
    memset(buff, 0, iByteAlloc);
    if ( !buff ) return sSource;

    strncpy(buff, sSource, p - sSource);
    buff[p - sSource] = '\0';

    sprintf(buff + ( p - sSource ), "%s%s", sReplaceString, p + strlen(sSearchString) );

    return buff;
}

void print_hex(char* a, int iLen)
{
    int i;
    for (i = 0; i < iLen; ++i)
    {
        printf(" %d ", a[i]);
    }
}