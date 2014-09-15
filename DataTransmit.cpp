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

int NetCore::socket_new_connect(int port, const struct in_addr *addr)
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

    sock = socket_new(SOCK_STREAM);
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

int NetCore::udp_connect(/*int localport, */int remoteport, const struct in_addr *addr)
{
    int ret, sock;
    struct sockaddr_in myaddr;

    sock = socket_new(SOCK_DGRAM);
    if (sock < 0)
        return -1;
    myaddr.sin_family = AF_INET;
    /*myaddr.sin_port = htons(localport);
    myaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    ret = bind(sock, (struct sockaddr *)&myaddr, sizeof(myaddr));
    if (ret < 0){
        close(sock);
        return -2;
    }*/
    myaddr.sin_port = htons(remoteport);
    memcpy(&myaddr.sin_addr, addr, sizeof(myaddr.sin_addr));
    ret = connect(sock, (struct sockaddr *)&myaddr, sizeof(myaddr));
    if (ret < 0){
        close(sock);
        return -3;
    }
    return sock;
}

DataTransmit::~DataTransmit()
{
    StopConnection();
}

DataTransmit::DataTransmit(const char *svr_ip, int svr_port)
{
    initialParam();
    m_isserver = false;
    resolveHost(svr_ip);
    m_svrport = svr_port;
}

DataTransmit::DataTransmit(int local_port, const char *local_ip)
{
    initialParam();
    m_isserver = true;
    m_localport = local_port;
    if (local_ip){
        strcpy(m_localip, local_ip);
        m_islocalip = true;
    }
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
    m_isudp = false;
    m_islocalip = false;
    m_issimplify = false;
    m_callbackfunc = NULL;
    m_sign[0] = 0xf9;
    m_sign[1] = 0x9f;
    m_sign[2] = 0xec;
    m_sign[3] = 0xff;
    m_sign[4] = 0xff;
    m_sign[5] = 0x0a;
    m_sign[6] = 0x9f;
    m_sign[7] = 0xf9;
    init_crc_table();
    init_key();
}

void DataTransmit::InitialConnection()
{
    int err;
    if (m_isserver)
    {
        if (!m_isudp)
            err = pthread_create(&m_ptd_lsnclt, NULL, listen_clt, this);
        else
        {
            if (m_issimplify)
                err = pthread_create(&m_ptd_lsnclt, NULL, udp_clt_simplify, this);
            else
                err = pthread_create(&m_ptd_lsnclt, NULL, udp_clt, this);
        }
    }
    else
    {
        err = pthread_create(&m_ptd_connsvr, NULL, connect_svr, this);
    }

}

void DataTransmit::StopConnection()
{
    m_isconnect = false;
    m_isterminate = true;
    shutdown(m_conn_sock, 2);
    close(m_conn_sock);
}

void DataTransmit::SetCallbackfunction(callback_t func)
{
    m_callbackfunc = func;
}

void DataTransmit::SetUseUdp(bool set)
{
    m_isudp = set;
    m_isheartbeat = !m_isudp;
    if (set){
        m_udpaddr.sin_family = PF_INET;
        m_udpaddr.sin_addr = m_addr;
        m_udpaddr.sin_port = htons(m_svrport);
    }
}

void DataTransmit::SetSimplify(bool set)
{
    m_issimplify = set;
    m_isheartbeat = !set;
}

int DataTransmit::SendData(char *buf, int len)
{
    if (!m_isconnect)
        return -1;

    if (m_issimplify)
        return senddatasimplify(buf, len);
    else
        return senddatanormaly(buf, len);
}

int DataTransmit::senddatanormaly(char *buf, int len)
{
    int sendbytes;
    int leftbytes;
    int totalbytes;
    socklen_t addrlen;
    char *outbuf;
    //Send block head
    BH bh;
    memcpy(bh.sign, m_sign, 8);
    bh.blen = len;
    bh.chksum = crc32(0xffffffff, (unsigned char*)buf, len);
    bh.flag = 0;
    addrlen = sizeof(struct sockaddr_in);

    if (m_isudp)
        sendbytes = sendto(m_conn_sock, &bh, sizeof(bh), 0, (struct sockaddr*)&m_udpaddr, addrlen);
    else
        sendbytes = send(m_conn_sock, &bh, sizeof(bh), 0);

    if ((unsigned int)sendbytes < sizeof(bh)){
        m_isconnect = false;
        errMsg("send block head failed, %d bytes", sizeof(bh));
        return -1;
    }

    leftbytes = len;
    totalbytes = 0;
    sendbytes = 0;
    //crypt
    outbuf = (char *)malloc(MAX_DATA_LEN);
    P_RC4(m_key, (unsigned char*)buf, (unsigned char*)outbuf, len);
    if (m_isconnect){
        while (sendbytes < leftbytes){
            leftbytes = len - totalbytes;
            if (m_isudp)
                sendbytes = sendto(m_conn_sock, outbuf+totalbytes, leftbytes, 0, (struct sockaddr*)&m_udpaddr, addrlen);
            else
                sendbytes = send(m_conn_sock, outbuf+totalbytes, leftbytes, 0);

            if (sendbytes < 0){
                m_isconnect = false;
                free(outbuf);
                perror("send");
                errMsg("send data failed, %d bytes", leftbytes);
                return -1;
            }
            totalbytes += sendbytes;
        }
    }
    free(outbuf);
    return len;
}

int DataTransmit::senddatasimplify(char *buf, int len)
{
    int sendbytes;
    int leftbytes;
    int totalbytes;
    socklen_t addrlen;

    addrlen = sizeof(struct sockaddr_in);

    leftbytes = len;
    totalbytes = 0;
    sendbytes = 0;

    if (m_isconnect){
        while (sendbytes < leftbytes){
            leftbytes = len - totalbytes;
            if (m_isudp)
                sendbytes = sendto(m_conn_sock, buf+totalbytes, leftbytes, 0, (struct sockaddr*)&m_udpaddr, addrlen);
            else
                sendbytes = send(m_conn_sock, buf+totalbytes, leftbytes, 0);

            if (sendbytes < 0){
                m_isconnect = false;
                perror("send");
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

int DataTransmit::GetConnectionPort()
{
    if (m_isserver)
        return m_localport;
    else
        return m_svrport;
}

HOST_INFO DataTransmit::GetRemoteHostInfo()
{
    return m_remote;
}

void *DataTransmit::listen_clt(void *param)
{
    DataTransmit *dt = (DataTransmit *)param;
    int sockfd;
    struct in_addr addr;
    if (dt->m_islocalip)
        addr.s_addr = inet_addr(dt->m_localip);

    sockfd = dt->m_nc.socket_new_listen(SOCK_STREAM, dt->m_localport, dt->m_islocalip?&addr:NULL);

    int acc_time = ACCEPT_TIME;
    void *tret;
    if (sockfd < 0){
        dt->errMsg("listen on %d failed", dt->m_localport);
        return NULL;
    }

    while (!dt->m_isterminate){
        dt->errMsg("listening on %d...", dt->m_localport);
        dt->m_conn_sock = dt->m_nc.socket_accept(sockfd, ACCEPT_TIMEOUT, &dt->m_remote);
        if (dt->m_conn_sock > 0){
            dt->errMsg("get a connection from %s(%d)", dt->m_remote.szip, dt->m_remote.port);
            dt->m_isconnect = true;
            if (dt->m_issimplify)
                pthread_create(&dt->m_ptd_recv, NULL, dt->recv_data_simplify, dt);
            else
                pthread_create(&dt->m_ptd_recv, NULL, dt->recv_data, dt);
            if (dt->m_isheartbeat)
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

void *DataTransmit::udp_clt(void *param)
{
    DataTransmit *dt = (DataTransmit *)param;
    int sockfd, ret;
    socklen_t len;
    char *buf;
    char *outbuf;
    BH bh;
    struct sockaddr_in addr;

    sockfd = dt->m_nc.socket_new(SOCK_DGRAM);
    if (sockfd < 0)
        return NULL;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(dt->m_localport);
    if (dt->m_islocalip)
        addr.sin_addr.s_addr = inet_addr(dt->m_localip);
    else
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

    ret = bind(sockfd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0){
        close(sockfd);
        return NULL;
    }

    buf = (char *)malloc(MAX_DATA_LEN);
    outbuf = (char *)malloc(MAX_DATA_LEN);
    memset(&bh, 0, sizeof(BH));
    memset(buf, 0, MAX_DATA_LEN);
    memset(outbuf, 0, MAX_DATA_LEN);

    dt->m_conn_sock = sockfd;
    dt->errMsg("listening on %d(udp)...", dt->m_localport);

    while(!dt->m_isterminate){
        len = sizeof(addr);
        ret = recvfrom(sockfd, buf, MAX_DATA_LEN, MSG_DONTWAIT, (struct sockaddr*)&addr, &len);
        if (ret < 0)
            continue;
        memcpy(&dt->m_udpaddr, &addr, sizeof(addr));
        dt->errMsg("recv %d bytes from %s", ret, inet_ntoa(addr.sin_addr));
        dt->m_isconnect = true;
        if (ret == 0){
            dt->m_isconnect = false;
            close(sockfd);
            break;
        }
        if (ret == sizeof(BH) && memcmp(buf, dt->m_sign, 8) == 0){
            if (bh.blen)
                continue;

            memcpy(&bh, buf, sizeof(bh));
            ret = recv(dt->m_conn_sock, buf, bh.blen, 0);
            dt->P_RC4(dt->m_key, (unsigned char*)buf, (unsigned char*)outbuf, bh.blen);
            if (bh.chksum != (unsigned int)dt->crc32(0xffffffff, (unsigned char*)outbuf, ret)){
                dt->errMsg("checksum error");
            }else{
                //callback function
                if (dt->m_callbackfunc != NULL)
                    dt->m_callbackfunc(outbuf, bh.blen);
            }
        }else{
            ret = recv(dt->m_conn_sock, buf, MAX_DATA_LEN, 0);
        }
    }
    free(buf);
    free(outbuf);
    dt->errMsg("udp_clt thread terminate");
    return NULL;
}

void *DataTransmit::udp_clt_simplify(void *param)
{
    DataTransmit *dt = (DataTransmit *)param;
    int sockfd, ret;
    socklen_t len;
    char *buf;
    struct sockaddr_in addr;

    sockfd = dt->m_nc.socket_new(SOCK_DGRAM);
    if (sockfd < 0)
        return NULL;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(dt->m_localport);
    if (dt->m_islocalip)
        addr.sin_addr.s_addr = inet_addr(dt->m_localip);
    else
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

    ret = bind(sockfd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret < 0){
        close(sockfd);
        return NULL;
    }

    buf = (char *)malloc(MAX_DATA_LEN);
    memset(buf, 0, MAX_DATA_LEN);

    dt->m_conn_sock = sockfd;
    dt->errMsg("listening on %d(udp)...", dt->m_localport);

    while(!dt->m_isterminate){
        len = sizeof(addr);
        ret = recvfrom(sockfd, buf, MAX_DATA_LEN, MSG_DONTWAIT, (struct sockaddr*)&addr, &len);
        if (ret < 0)
            continue;
        memcpy(&dt->m_udpaddr, &addr, sizeof(addr));
        dt->errMsg("recv %d bytes from %s", ret, inet_ntoa(addr.sin_addr));
        dt->m_isconnect = true;
        if (ret <= 0){
            dt->m_isconnect = false;
            close(sockfd);
            break;
        }

        //callback function
        if (dt->m_callbackfunc != NULL)
            dt->m_callbackfunc(buf, ret);

    }
    dt->errMsg("udp_clt_simplify thread terminate");
    free(buf);
    return NULL;
}

void *DataTransmit::heart_beat(void *param)
{
    DataTransmit *dt = (DataTransmit *)param;
    char buf[16];
    int ret;
    strcpy(buf, "85j#$^dfgl@s23\0");
    while(!dt->m_isterminate && dt->m_isconnect){
        ret = send(dt->m_conn_sock, buf, 16, 0);
        if (ret < 0){
            dt->m_isconnect = false;
            break;
        }
        sleep(HEARTBEAT_INTERVAL);
    }
    dt->errMsg("heart_beat thread terminate");
    return NULL;
}

void *DataTransmit::connect_svr(void *param)
{
    DataTransmit *dt = (DataTransmit *)param;
    void *tret;
    while (!dt->m_isterminate){
        dt->errMsg("connecting %s(%d)...", inet_ntoa(dt->m_addr), dt->m_svrport);
        if (!dt->m_isudp)
            dt->m_conn_sock = dt->m_nc.socket_new_connect(dt->m_svrport, &dt->m_addr);
        else
            dt->m_conn_sock = dt->m_nc.udp_connect(dt->m_svrport, &dt->m_addr);
        if (dt->m_conn_sock > 0){
            dt->m_isconnect = true;
            dt->errMsg("connect success");
            if (dt->m_issimplify)
                pthread_create(&dt->m_ptd_recv, NULL, dt->recv_data_simplify, dt);
            else
                pthread_create(&dt->m_ptd_recv, NULL, dt->recv_data, dt);
            if (dt->m_isheartbeat)
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
    char *outbuf;
    struct timeval timest;

    FD_ZERO(&in);
    FD_SET(dt->m_conn_sock, &in);
    timest.tv_sec = RECV_TIMEOUT;
    timest.tv_usec = 0;

    buf = (char *)malloc(MAX_RECV_LEN);
    outbuf = (char *)malloc(MAX_RECV_LEN);
    memset(&bh, 0, sizeof(BH));
    memset(buf, 0, MAX_RECV_LEN);
    memset(outbuf, 0, MAX_RECV_LEN);

    while (!dt->m_isterminate && dt->m_isconnect){
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

            if (ret == sizeof(BH) && memcmp(&bh.sign, dt->m_sign, 8) == 0){
                if (bh.blen > MAX_RECV_LEN)
                    continue;

                ret = recv(dt->m_conn_sock, buf, bh.blen, 0);
                //decrypt
                dt->P_RC4(dt->m_key, (unsigned char*)buf, (unsigned char*)outbuf, bh.blen);
                if (bh.chksum != (unsigned int)dt->crc32(0xffffffff, (unsigned char*)outbuf, ret)){
                    dt->errMsg("checksum error");
                }
                else{
                    //callback function
                    if (dt->m_callbackfunc != NULL)
                        dt->m_callbackfunc(outbuf, bh.blen);
                }
            }
            else if(ret == 16){
                //heart beat packet
            }
            else{
                ret = recv(dt->m_conn_sock, buf, MAX_DATA_LEN, 0);
            }
        }
    }
    dt->errMsg("recv_data thread terminate");
    free(outbuf);
    free(buf);
    return NULL;
}

void *DataTransmit::recv_data_simplify(void *param)
{
    DataTransmit *dt = (DataTransmit *)param;
    fd_set in;
    int ret;
    char *buf;
    struct timeval timest;

    FD_ZERO(&in);
    FD_SET(dt->m_conn_sock, &in);
    timest.tv_sec = RECV_TIMEOUT;
    timest.tv_usec = 0;

    buf = (char *)malloc(MAX_DATA_LEN);
    memset(buf, 0, MAX_DATA_LEN);

    while (!dt->m_isterminate && dt->m_isconnect){
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
            ret = recv(dt->m_conn_sock, buf, MAX_DATA_LEN, 0);
            if (ret <= 0){
                dt->m_isconnect = false;
                dt->errMsg("disconnected");
                break;
            }
            if (dt->m_callbackfunc != NULL)
                dt->m_callbackfunc(buf, ret);
        }
    }
    dt->errMsg("recv_data_simplify thread terminate");
    free(buf);
    return NULL;
}

void DataTransmit::init_crc_table()
{
    unsigned int c;
    unsigned int i,j;

    for(i=0; i<256; i++)
    {
        c = i;
        for(j=0; j<8; j++)
        {
            if(c & 1)
                c = 0xedb88320L ^ (c>>1);
            else
                c = c >> 1;
        }
        crc_table[i] = c;
    }
}

int DataTransmit::crc32(unsigned int crc, unsigned char *buffer, unsigned int size)
{
    unsigned int i;
    for(i=0; i<size; i++)
        crc = crc_table[(crc^buffer[i])&0xff]^(crc>>8);
    return crc;
}

void DataTransmit::init_key()
{
    m_key[0] = 0;
    m_key[1] = 3;
    m_key[2] = 0;
    m_key[3] = 2;
    m_key[4] = 7;
    m_key[5] = 0;
    m_key[6] = 5;
    m_key[7] = 6;
    m_key[8] = 0x0A;
    m_key[9] = 5;
    m_key[10] = 6;
    m_key[11] = 0x0B;
    m_key[12] = 5;
    m_key[13] = 6;
    m_key[14] = 6;
    m_key[15] = 0x0B;
}

void DataTransmit::P_RC4(unsigned char *pkey, unsigned char *pin, unsigned char *pout, unsigned int len)
{
    unsigned char S[256],K[256],temp;
    unsigned int  i,j,t,x;

    j = 1;
    for(i=0;i<256;i++)
    {
        S[i] = (unsigned char)i;
        if(j > 16) j = 1;
        K[i] = pkey[j-1];
        j++;
    }
    j = 0;
    for(i=0;i<256;i++)
    {
        j = (j + S[i] + K[i]) % 256;
        temp = S[i];
        S[i] = S[j];
        S[j] = temp;
    }
    i = j = 0;
    for(x=0;x<len;x++)
    {
        i = (i+1) % 256;
        j = (j + S[i]) % 256;
        temp = S[i];
        S[i] = S[j];
        S[j] = temp;
        t = (S[i] + (S[j] % 256)) % 256;
        pout[x] = pin[x] ^ S[t];
    }
}
