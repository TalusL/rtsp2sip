#include "SipClient.h"
#include <Util/logger.h>
#include <Util/mini.h>
#include <Network/Socket.h>
#include <csignal>
#include <Common/config.h>
#include <sys/wait.h>

void startDaemon(bool kill_parent_if_failed) {
    kill_parent_if_failed = true;
#ifndef _WIN32
    static pid_t pid;
    do {
        pid = fork();
        if (pid == -1) {
            WarnL << "fork失败:";
            //休眠1秒再试
            sleep(1);
            continue;
        }

        if (pid == 0) {
            //子进程
            return;
        }

        //父进程,监视子进程是否退出
        DebugL << "启动子进程:" << pid;
        signal(SIGINT, [](int) {
            WarnL << "收到主动退出信号,关闭父进程与子进程";
            kill(pid, SIGINT);
            exit(0);
        });

        do {
            int status = 0;
            if (waitpid(pid, &status, 0) >= 0) {
                WarnL << "子进程退出";
                //休眠3秒再启动子进程
                sleep(3);
                //重启子进程，如果子进程重启失败，那么不应该杀掉守护进程，这样守护进程可以一直尝试重启子进程
                kill_parent_if_failed = false;
                break;
            }
            DebugL << "waitpid被中断:";
        } while (true);
    } while (true);
#endif // _WIN32
}

int main(){
    toolkit::Logger::Instance().add(std::make_shared<toolkit::ConsoleChannel>("ConsoleChannel", toolkit::LTrace));


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

    startDaemon(true);
    InfoL<<"start!";


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