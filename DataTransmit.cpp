#include "DataTransmit.h"

int NetCore::socket_new(int type)
{
    int sock, ret, sockopt;
    struct linger fix_ling;

    sock = socket(PF_INET, type, 0);
    if (sock < 0)
        return -1;

    fix_ling.l_onoff = 1;
    fix_ling.l_linger = 0;
    ret = setsockopt(sock, SOL_SOCKET, SO_LINGER, &fix_ling, sizeof(fix_ling));
    if (ret < 0){
        close(sock);
        return -2;
    }
    sockopt = 1;
    ret = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(sockopt));
    if (ret < 0){
        close(sock);
        return -2;
    }

    return sock;
}

int NetCore::socket_new_connect(int type, int port, const struct in_addr *addr)
{
    int sock, ret, error, flags;
    socklen_t len;
    fd_set wset;
    struct timeval tval;
    struct sockaddr_in rem_addr;

    assert(addr);
    memset(&rem_addr, 0, sizeof(rem_addr));
    rem_addr.sin_family = AF_INET;
    rem_addr.sin_port = htons(port);
    memcpy(&rem_addr.sin_addr, addr, sizeof(rem_addr.sin_addr));

    sock = socket_new(type);
    if (sock < 0)
        return sock;
    /* add the non-blocking flag to this socket */
    if ((flags = fcntl(sock, F_GETFL, 0)) >= 0)
        ret = fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    if (ret < 0) {
        close(sock);
        return -4;
    }
    ret = connect(sock, (struct sockaddr *)&rem_addr, sizeof(rem_addr));
    if (ret == 0)
        goto done;
    else if (ret < 0 && errno != EINPROGRESS){
        perror("connect");
        close(sock);
        return -5;
    }

    FD_ZERO(&wset);
    FD_SET(sock, &wset);
    tval.tv_sec = CONN_TIMEOUT;
    tval.tv_usec = 0;

    ret = select(sock+1, NULL, &wset, NULL, &tval);
    if (ret == 0){
        close(sock);
        return -6;
    }

    if (FD_ISSET(sock, &wset)){
        len = sizeof(error);
        ret = getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len);
        if (ret < 0 || error){
            close(sock);
            if (error)
                errno = error;
            return -7;
        }
    }
    else{
        return -8;
    }

done:
    fcntl(sock, F_SETFL, flags);
    return sock;
}

int NetCore::socket_new_listen(int type, int port, const struct in_addr *addr)
{
    int sock, ret;
    struct sockaddr_in my_addr;

    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = PF_INET;
    my_addr.sin_port = htons(port);
    my_addr.sin_addr.s_addr = INADDR_ANY;

    if (addr)
        memcpy(&my_addr.sin_addr, addr, sizeof(my_addr.sin_addr));
    sock = socket_new(type);
    if (sock < 0)
        return sock;

    ret = bind(sock, (struct sockaddr *)&my_addr, sizeof(my_addr));
    if (ret < 0){
        close(sock);
        return -3;
    }

    ret = listen(sock, 4);
    if (ret < 0){
        close(sock);
        return -4;
    }
    return sock;
}

int NetCore::socket_accept(int sockfd, int timeout, struct HOST_INFO *hostinfo)
{
    fd_set in;
    int ret;
    int flag;
    socklen_t len;
    struct timeval timest;
    struct sockaddr_in client;

    flag = 1;

    while (flag){
        FD_ZERO(&in);
        FD_SET(sockfd, &in);

        timest.tv_sec = timeout;
        timest.tv_usec = 0;

        ret = select(sockfd+1, &in, NULL, NULL, &timest);
        if (ret < 0){
            if (errno == EINTR)
                continue;
            perror("select");
            return -1;
        }
        if (ret == 0)
            continue;

        if (FD_ISSET(sockfd, &in)){
            int new_sock;
            len = sizeof(client);
            new_sock = accept(sockfd, (struct sockaddr*)&client, &len);
            strcpy(hostinfo->szip, inet_ntoa(client.sin_addr));
            hostinfo->port = ntohs(client.sin_port);
            return new_sock;
        }
    }
    return -1;
}

DataTransmit::~DataTransmit()
{
    StopConnection();
}

DataTransmit::DataTransmit(const char *svr_ip, int svr_port)
{
    m_isserver = false;
    resolveHost(svr_ip);
    m_svrport = svr_port;
}

DataTransmit::DataTransmit(int local_port)
{
    m_isserver = true;
    m_localport = local_port;
}

void DataTransmit::errMsg(const char *fmt, ...)
{
#ifdef DEBUG
    char buf[512], newline = '\n';
    va_list args;
    FILE *fstream = stderr;

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    fprintf(fstream, "%s%c", buf, newline);
#endif
}

void DataTransmit::resolveHost(const char *szname)
{
    int ret;
    struct hostent *hostent;

    ret = inet_aton(szname, &m_addr);
    if (ret == 0){
        hostent = gethostbyname(szname);
        if (hostent == NULL){
            errMsg("resolve host name %s failed", szname);
            return;
        }
        errMsg("resolve to ip %s", inet_ntoa(*(struct in_addr *)hostent->h_addr));
        m_addr.s_addr = inet_addr(inet_ntoa(*(struct in_addr *)hostent->h_addr));
    }
}

void DataTransmit::initialParam()
{
    m_isterminate = false;
    m_isconnect = false;
    m_isheartbeat = true;
    m_callbackfunc = NULL;
    m_sign[0] = 0xf9;
    m_sign[1] = 0x9f;
    m_sign[2] = 0xec;
    m_sign[3] = 0xff;
    m_sign[4] = 0xff;
    m_sign[5] = 0x0a;
    m_sign[6] = 0x9f;
    m_sign[7] = 0xf9;
}

void DataTransmit::InitialConnection()
{
    int err;
    if (m_isserver)
    {
        err = pthread_create(&m_ptd_clt, NULL, listen_clt, this);
    }
    else
    {
        err = pthread_create(&m_ptd_connsvr, NULL, connect_svr, this);
    }

}

void DataTransmit::StopConnection()
{
    m_isconnect = false;
    shutdown(m_conn_sock, 2);
    close(m_conn_sock);
}

void DataTransmit::SetCallbackfunction(callback_t func)
{
    m_callbackfunc = func;
}

int DataTransmit::SendData(char *buf, int len)
{
    int sendbytes;
    int leftbytes;
    int totalbytes;

    //Send block head
    BH bh;
    memcpy(bh.sign, m_sign, 8);
    bh.blen = len;
    bh.chksum = 0;
    bh.flag = 0;

    sendbytes = send(m_conn_sock, &bh, sizeof(bh), 0);
    if ((unsigned int)sendbytes < sizeof(bh)){
        m_isconnect = false;
        errMsg("send block head failed, %d bytes", sizeof(bh));
        return -1;
    }

    leftbytes = len;
    totalbytes = 0;
    sendbytes = 0;
    if (m_isconnect){
        while (sendbytes < leftbytes){
            leftbytes = len - totalbytes;
            sendbytes = send(m_conn_sock, buf+totalbytes, leftbytes, 0);
            if (sendbytes < 0){
                m_isconnect = false;
                errMsg("send data failed, %d bytes", leftbytes);
                return -1;
            }
            totalbytes += sendbytes;
        }
    }
    return len;
}

int DataTransmit::RecvData(char *buf, int len)
{
    int recvbytes;
    recvbytes = 0;
    if (m_isconnect){
        recvbytes = recv(m_conn_sock, buf, len, 0);
        if (recvbytes < 0){
            errMsg("recv data failed");
        }
    }
    return recvbytes;
}

int DataTransmit::GetConnectionStatus()
{
    return m_isconnect;
}

HOST_INFO DataTransmit::GetRemoteHostInfo()
{
    return m_remote;
}

void *DataTransmit::listen_clt(void *param)
{
    DataTransmit *dt = (DataTransmit *)param;
    int sockfd = dt->m_nc.socket_new_listen(SOCK_STREAM, dt->m_localport, NULL);
    //dt->errMsg("%d", sockfd);
    int acc_time = ACCEPT_TIME;
    void *tret;
    if (sockfd < 0){
        dt->errMsg("listen on %d failed", dt->m_localport);
        return NULL;
    }
    dt->errMsg("listening on %d...", dt->m_localport);
    while (!dt->m_isterminate){
        dt->m_conn_sock = dt->m_nc.socket_accept(sockfd, ACCEPT_TIMEOUT, &dt->m_remote);
        if (dt->m_conn_sock > 0){
            dt->errMsg("get a connection from %s(%d)", dt->m_remote.szip, dt->m_remote.port);
            dt->m_isconnect = true;
            pthread_create(&dt->m_ptd_recv, NULL, dt->recv_data, dt);
            pthread_create(&dt->m_ptd_heartbeat, NULL, dt->heart_beat, dt);
            pthread_join(dt->m_ptd_recv, &tret);
        }
        if (acc_time != -1){
            acc_time--;
            if (acc_time <= 0)
                break;
        }
    }

    return NULL;
}

void *DataTransmit::heart_beat(void *param)
{
    DataTransmit *dt = (DataTransmit *)param;
    char buf[16];
    int ret;
    strcpy(buf, "aaaaaaaaaaa\0");
    while(!dt->m_isterminate && dt->m_isconnect){
        ret = send(dt->m_conn_sock, buf, 16, 0);
        if (ret < 0){
            dt->m_isconnect = false;
            break;
        }
        sleep(HEARTBEAT_INTERVAL);
    }
    return NULL;
}

void *DataTransmit::connect_svr(void *param)
{
    DataTransmit *dt = (DataTransmit *)param;
    void *tret;
    while (!dt->m_isterminate){
        dt->errMsg("connecting %s(%d)...", inet_ntoa(dt->m_addr), dt->m_svrport);
        dt->m_conn_sock = dt->m_nc.socket_new_connect(SOCK_STREAM, dt->m_svrport, &dt->m_addr);
        if (dt->m_conn_sock > 0){
            dt->m_isconnect = true;
            dt->errMsg("connect success");
            pthread_create(&dt->m_ptd_recv, NULL, dt->recv_data, dt);
            pthread_create(&dt->m_ptd_heartbeat, NULL, dt->heart_beat, dt);
            pthread_join(dt->m_ptd_recv, &tret);
        }
        sleep(CONN_INTERVAL);
    }
    return NULL;
}

void *DataTransmit::recv_data(void *param)
{
    DataTransmit *dt = (DataTransmit *)param;
    fd_set in;
    int ret;
    BH bh;
    char *buf;
    struct timeval timest;

    FD_ZERO(&in);
    FD_SET(dt->m_conn_sock, &in);
    timest.tv_sec = RECV_TIMEOUT;
    timest.tv_usec = 0;
    buf = (char *)malloc(MAX_DATA_LEN);
    memset(&bh, 0, sizeof(BH));
    memset(buf, 0, MAX_DATA_LEN);

    while (!dt->m_isterminate){
        ret = select(dt->m_conn_sock+1, &in, NULL, NULL, &timest);
        if (ret < 0){
            if (errno == EINTR)
                continue;
            perror("select");
            break;
        }
        if (ret == 0)
            continue;
        if (FD_ISSET(dt->m_conn_sock, &in)){
            ret = recv(dt->m_conn_sock, &bh, sizeof(BH), 0);
            if (ret == 0){
                dt->m_isconnect = false;
                dt->errMsg("disconnected");
                break;
            }
            dt->errMsg("recv %d bytes %s", ret, bh.sign);
            if (ret == sizeof(BH) && memcmp(&bh.sign, dt->m_sign, 8) == 0){
                ret = recv(dt->m_conn_sock, buf, bh.blen, 0);
                //callback function
                if (dt->m_callbackfunc != NULL)
                    dt->m_callbackfunc(buf, bh.blen);
            }
            else if(ret == 16){
                //heart beat packet
            }
            else{
                ret = recv(dt->m_conn_sock, buf, MAX_DATA_LEN, 0);
            }
        }
    }
    free(buf);
    return NULL;
}
