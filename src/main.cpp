#include "SipClient.h"
#include <Rtsp/RtspSession.h>
#include <Network/TcpServer.h>
#include <Util/logger.h>
#include <Util/mini.h>
#include <Network/Socket.h>
int main(){
    toolkit::Logger::Instance().add(std::make_shared<toolkit::ConsoleChannel>("ConsoleChannel", toolkit::LTrace));


    toolkit::mINI::Instance()["protocol.enable_hls"] = 0;

    toolkit::mINI::Instance()["sip_proxy.8003"] = "rtsp://admin:123456@192.168.1.186/h264/ch1/main/av_stream";

    toolkit::mINI::Instance()["sip.expiry"] = 3600;
    toolkit::mINI::Instance()["sip.localPort"] = "5070";
    toolkit::mINI::Instance()["sip.localIp"] = toolkit::SockUtil::get_local_ip();
    toolkit::mINI::Instance()["sip.serverIp"] = "192.168.1.212";
    toolkit::mINI::Instance()["sip.serverPort"] = "5060";
    toolkit::mINI::Instance()["sip.username"] = "8003";
    toolkit::mINI::Instance()["sip.password"] = "8003";

    SipClient sipClient;
    sipClient.StartStack();



    this_thread::sleep_for(chrono::seconds(1000));
}