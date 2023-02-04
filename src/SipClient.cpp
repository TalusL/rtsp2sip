//
// Created by Wind on 2023/2/4.
//

#include <netinet/in.h>
#include "SipClient.h"
#include <cstring>
#include <iostream>

static int
addOutBoundProxy(osip_message_t *msg, const string& outBoundProxy)
{
    int ret = 0;
    char head[1024] = { 0 };

    if (outBoundProxy.empty()){
        return 0;
    }
    snprintf(head, sizeof(head)-1, "<%s;lr>", outBoundProxy.c_str());

    osip_list_special_free(&msg->routes, (void(*)(void*))osip_route_free);
    ret = osip_message_set_route(msg, head);
    return ret;
}
bool SipClient::sendResponse(const eXosip_event_t*  osipEvent, int StatusCode)
{
    if(!osipEvent) return false;
    osip_message_t* answer = nullptr;
    if(    MSG_IS_MESSAGE(osipEvent->request)
           || MSG_IS_REGISTER(osipEvent->request)
           || MSG_IS_NOTIFY(osipEvent->request)
           || MSG_IS_SUBSCRIBE(osipEvent->request)
           || MSG_IS_INFO(osipEvent->request)
           || MSG_IS_OPTIONS(osipEvent->request)
            )
    {
        eXosip_lock(m_context);
        eXosip_message_build_answer(m_context, osipEvent->tid, StatusCode, &answer);
        eXosip_message_send_answer(m_context, osipEvent->tid, StatusCode, answer);
        eXosip_unlock(m_context);
    }
    return true;
}

SipClient::SipClient() {
    m_to = "sip:"+m_username+"@"+m_serverIp+":"+m_serverPort;
    m_from = "sip:"+m_username+"@"+m_localIp+":"+m_localPort;
    m_proxy = "sip:"+m_username+"@"+m_serverIp+":"+m_serverPort;
}


bool SipClient::StartStack() {
    TRACE_INITIALIZE (static_cast<osip_trace_level_t>(6), nullptr);
    m_context = eXosip_malloc();
    if (eXosip_init(m_context)){
        return false;
    }
    auto ret = eXosip_listen_addr(m_context, IPPROTO_UDP, nullptr,stoi(m_localPort), AF_INET, 0);
    eXosip_masquerade_contact(m_context, m_localIp.c_str(), stoi(m_localPort));
    eXosip_set_user_agent(m_context, "TalusIPC");
    if (eXosip_add_authentication_info(m_context, m_username.c_str(),
                                       m_username.c_str(), m_password.c_str(), nullptr, nullptr)){
        return false;
    }
    m_running = true;
    m_pollingThread = thread([this](){
        ProcessEvent();
    });
    m_pollingThread.detach();
    registerUa();
    return ret == OSIP_SUCCESS;
}

void SipClient::ProcessEvent() {
    while (m_running) {
        osip_message_t *msg = nullptr;
        eXosip_event_t * je = nullptr;
        /* auto process,such as:register refresh,auth,call keep... */
        if (!(je = eXosip_event_wait (m_context, 0, 1)))
        {
            eXosip_automatic_action (m_context);
            continue;
        }
        std::string body;
        if (je->request) {
            osip_body_t *msgBody = nullptr;
            osip_message_get_body(je->request, 0, &msgBody);
            if (msgBody) {
                body = msgBody->body;
            }
        }
        switch (je->type) {
            case EXOSIP_REGISTRATION_SUCCESS:
                break;
            case EXOSIP_REGISTRATION_FAILURE:
                break;
            case EXOSIP_CALL_INVITE:
                break;
            case EXOSIP_CALL_REINVITE:
                break;
            case EXOSIP_CALL_RINGING:
                break;
            case EXOSIP_CALL_ANSWERED:
                break;
            case EXOSIP_CALL_NOANSWER:
                break;
            case EXOSIP_CALL_REQUESTFAILURE:
            case EXOSIP_CALL_GLOBALFAILURE:
            case EXOSIP_CALL_SERVERFAILURE:
                break;
            case EXOSIP_CALL_CLOSED:
                break;
            case EXOSIP_CALL_CANCELLED:
                break;
            case EXOSIP_CALL_RELEASED:
                break;
            case EXOSIP_IN_SUBSCRIPTION_NEW:
                break;
            case EXOSIP_MESSAGE_NEW:
                if(je->request&&je->request->sip_method==string("OPTIONS")){
                    std::cout<<"OPTIONS"<<std::endl;
                    sendResponse(je,200);
                }
                break;
            default:
                break;
        }
        eXosip_event_free(je);
    }
}

int SipClient::registerUa() {
    int ret = -1;
    osip_message_t *msg = nullptr;

    if (m_regid > 0){ // refresh register
        ret = eXosip_register_build_register(m_context, m_regid, m_expiry, &msg);
        if (0 != ret){
            return -1;
        }
    }
    else{
        m_regid = eXosip_register_build_initial_register(m_context,
                                                             m_from.c_str(), m_proxy.c_str(), nullptr, m_expiry, &msg);
        if (m_regid <= 0){
            return -1;
        }
        addOutBoundProxy(msg, m_outboundProxy);
    }
    eXosip_lock(m_context);
    ret = eXosip_register_send_register(m_context, m_regid, msg);
    eXosip_unlock(m_context);
    if (0 != ret){
        return ret;
    }
    return ret;
}
