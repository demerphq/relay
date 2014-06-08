/*
 From https://raw.githubusercontent.com/vdevos/graphite-c-client/master/graphite-client.h
 */

#include <stdio.h> 
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "graphite.h"

#define MAX_MSG_PATH 100
#define MAX_MSG_LEN_PLAIN 130

static int n = 0;
static int sockfd = 0;
static struct sockaddr_in serv_addr; 

int graphite_init(const char *host, int port)
{
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket"); return 1;
    } 

    memset(&serv_addr, '0', sizeof(serv_addr)); 
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port); 

    if(inet_pton(AF_INET, host, &serv_addr.sin_addr)<=0)
    {
        perror("inet_pton"); return 1;
    } 

    if( connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
       perror("connect"); return 1;
    } 
    
    return 0;
}

void graphite_finalize()
{
    if (sockfd != -1) 
    {
        close(sockfd);
        sockfd = -1;
    }
}

void graphite_send(const char *message)
{
    n = write(sockfd, message, strlen(message));
    if (n < 0) {
         perror("write");
         exit(1);
    }
}

void graphite_send_plain( const char* path, float value, unsigned long timestamp )
{
    char spath[MAX_MSG_PATH];
    char message[MAX_MSG_LEN_PLAIN]; /* size = path + (value + timestamp) */
    
    /* make sure that path has a restricted length so it does not push the value + timestamp out of the message */
    snprintf( spath, MAX_MSG_PATH, "%s", path);     
    
    /* format message as: <metric path> <metric value> <metric timestamp> */
    snprintf( message, MAX_MSG_LEN_PLAIN, "%s %.2f %lu\n", spath, value, timestamp );
    
    /* send to message to graphite */
    graphite_send(message);
}



