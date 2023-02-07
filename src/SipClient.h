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
    string m_localPort;
    string m_localIp;
    string m_serverIp;
    string m_serverPort;
    string m_proxy;
    string m_outboundProxy;
    string m_username;
    string m_password;
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
