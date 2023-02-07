//
// Created by Wind on 2023/2/4.
//

#ifndef RTSP2SIP_SIPCLIENT_H
#define RTSP2SIP_SIPCLIENT_H

#include <string>
#include <eXosip2/eXosip.h>
#include <thread>

using namespace std;

class SipClient {
public:
    SipClient();

    bool StartStack();

    void ProcessEvent();
private:
    int m_expiry = 3600;
    string m_localPort = "5070";
    string m_localIp = "10.8.9.244";
    string m_serverIp = "192.168.1.212";
    string m_serverPort = "5060";
    string m_proxy;
    string m_outboundProxy;
    string m_username = "8003";
    string m_password = "8003";
    string m_from;
    string m_to;

    int m_regid = -1;


    eXosip_t * m_context{};
    thread m_pollingThread;

    volatile bool m_running = false;

private:
    int registerUa();

    bool sendResponse(const eXosip_event_t*  osipEvent, int StatusCode);

};


#endif //RTSP2SIP_SIPCLIENT_H
