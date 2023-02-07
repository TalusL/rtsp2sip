#include "SipClient.h"
#include <Rtsp/RtspSession.h>
#include <Network/TcpServer.h>
int main(){
    SipClient sipClient;
    sipClient.StartStack();

    toolkit::TcpServer::Ptr rtspSrv = make_shared<toolkit::TcpServer>();
    rtspSrv->start<mediakit::RtspSession>(554);

    this_thread::sleep_for(chrono::seconds(1000));
}