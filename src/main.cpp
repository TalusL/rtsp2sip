#include "SipClient.h"
#include <Util/logger.h>
#include <Util/mini.h>
#include <Network/Socket.h>
#include <csignal>
#include <Common/config.h>

int main(){
    toolkit::Logger::Instance().add(std::make_shared<toolkit::ConsoleChannel>("ConsoleChannel", toolkit::LTrace));

    InfoL<<"start!";

    toolkit::mINI::Instance()["protocol.enable_hls"] = 0;
    toolkit::mINI::Instance()["protocol.enable_rtmp"] = 0;
    toolkit::mINI::Instance()["protocol.enable_ts"] = 0;


    toolkit::mINI::Instance()["sip.expiry"] = 3600;
    toolkit::mINI::Instance()["sip.localPort"] = "5070";
    toolkit::mINI::Instance()["sip.localIp"] = toolkit::SockUtil::get_local_ip();
    toolkit::mINI::Instance()["sip.serverIp"] = "192.168.1.212";
    toolkit::mINI::Instance()["sip.serverPort"] = "5060";
    toolkit::mINI::Instance()["sip.username"] = "8006";
    toolkit::mINI::Instance()["sip.register"] = true;
    toolkit::mINI::Instance()["sip.password"] = toolkit::mINI::Instance()["sip.username"];

    toolkit::mINI::Instance()["sip_proxy."+toolkit::mINI::Instance()["sip.username"]] = "rtsp://admin:123456@192.168.122.2/stream=0";

    mediakit::loadIniConfig((toolkit::exeDir()+"/rtsp2sip.ini").c_str());

    SipClient sipClient;
    sipClient.StartStack();



    //设置退出信号处理函数
    static toolkit::semaphore sem;
    signal(SIGINT, [](int) {
        InfoL << "SIGINT:exit";
        signal(SIGINT, SIG_IGN);// 设置退出信号
        sem.post();
    });// 设置退出信号

    sem.wait();
    return 0;
}