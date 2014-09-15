#include "DataTransmit.h"

void recvfunc(char *buf, int len)
{
    printf("recvfunc (%d):%s\n", len, buf);
}

int main()
{
    char buf[1000];

    DataTransmit *dt;
    dt = new DataTransmit(9999);
    dt->SetCallbackfunction(recvfunc);
    //dt->SetUseUdp(true);
    dt->SetSimplify(true);
    dt->InitialConnection();
    strcpy(buf, "hello world!\0");
    while (true){
        if (dt->GetConnectionStatus()){
            dt->SendData(buf, 1000);
            printf("sending %s\n", buf);
            break;
        }
        sleep(3);
    }
    sleep(100);

    return 0;
}
