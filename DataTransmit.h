#ifndef DATATRANSMIT_H
#define DATATRANSMIT_H

#include "CmnHdr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <signal.h>

class NetCore
{
public:
    static int socket_new(int type);
    static int socket_new_connect(int port, const struct in_addr *addr);
    static int socket_new_listen(int type, int port, const struct in_addr *addr);
    static int socket_accept(int sockfd, int timeout, struct HOST_INFO *hostinfo);
    static int udp_connect(/*int localport, */int remoteport, const struct in_addr *addr);
};

class DataTransmit
{
public:
    DataTransmit(const char *svr_ip, int svr_port);
    DataTransmit(int local_port, const char *local_ip=NULL);
    ~DataTransmit();
    void SetCallbackfunction(callback_t func);
    void SetUseUdp(bool set);
    int SendData(char *buf, int len);
    int RecvData(char *buf, int len);
    void InitialConnection();
    void StopConnection();
    int GetConnectionStatus();
    int GetConnectionPort();
    HOST_INFO GetRemoteHostInfo();

private:
    int m_svrport;
    int m_localport;
    struct in_addr m_addr;
    struct HOST_INFO m_local;
    struct HOST_INFO m_remote;
    bool m_isserver;
    bool m_isterminate;
    bool m_isconnect;
    bool m_isheartbeat;
    bool m_isudp;
    bool m_islocalip;
    char m_localip[16];
    unsigned char m_sign[8];
    unsigned char m_key[16];
    unsigned int  crc_table[256];
    int m_conn_sock;
    callback_t m_callbackfunc;
    NetCore m_nc;

    pthread_t m_ptd_connsvr;
    pthread_t m_ptd_lsnclt;
    pthread_t m_ptd_recv;
    pthread_t m_ptd_heartbeat;

    void initialParam();
    void resolveHost(const char *szname);
    void errMsg(const char *fmt, ...);
    void P_RC4(unsigned char* pkey, unsigned char* pin, unsigned char* pout, unsigned char len);
    void init_key();
    void init_crc_table();
    int  crc32(unsigned int crc, unsigned char *buffer, unsigned int size);

    static void *connect_svr(void *param);
    static void *listen_clt(void *param);
    static void *recv_data(void *param);
    static void *heart_beat(void *param);
    static void *udp_clt(void *param);
};

#endif // DATATRANSMIT_H
