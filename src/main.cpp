#include "SipClient.h"
#include <Rtsp/RtspSession.h>
#include <Network/TcpServer.h>
int main(){
    SipClient sipClient;
    sipClient.StartStack();


    this_thread::sleep_for(chrono::seconds(1000));
}