#ifndef CMNHDR_H
#define CMNHDR_H

#define DEBUG
#define USE_UDP
//Data
#define MAX_DATA_LEN 4*1024*1024

//Port
#define DATA_PORT 8301
#define FILE_PORT 8302

//Time(second)
#define CONN_INTERVAL 5
#define CONN_TIMEOUT 5
#define RECV_TIMEOUT 5
#define ACCEPT_TIMEOUT 3
#define ACCEPT_TIME -1      //-1 means infinitely
#define HEARTBEAT_INTERVAL 5

//Struct
typedef struct BLOCK_HEAD{
    char sign[8];
    unsigned int blen;
    unsigned int flag;
    unsigned int chksum;
}BH, *PBH;

typedef struct HOST_INFO{
    char szip[16];
    unsigned short port;
}*PHI;

typedef void (*callback_t)(char *buf, int len);

#endif // CMNHDR_H
