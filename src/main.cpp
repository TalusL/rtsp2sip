#include "SipClient.h"
#include <Rtsp/RtspSession.h>
#include <Network/TcpServer.h>
#include <Util/logger.h>
#include <Util/mini.h>
int main(){
    toolkit::Logger::Instance().add(std::make_shared<toolkit::ConsoleChannel>("ConsoleChannel", toolkit::LTrace));


    toolkit::mINI::Instance()["protocol.enable_hls"] = 0;

    SipClient sipClient;
    sipClient.StartStack();


    this_thread::sleep_for(chrono::seconds(1000));
}