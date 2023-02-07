//
// Created by Wind on 2023/2/4.
//

#include <netinet/in.h>
#include "SipClient.h"
#include <cstring>
#include <iostream>
#include <Player/PlayerProxy.h>
#include <Network/Session.h>

using namespace mediakit;
using namespace toolkit;


int allocUdpPort(){
    auto sock = Socket::createSocket();
    sock->bindUdpSock(0);
    int port = sock->get_local_port();
    sock->closeSock();
    return port;
}

std::pair<int,int> videoPair {0,0};
std::pair<int,int> audioPair {0,0};
string destHost;


MediaPlayer::Ptr mediaPlayer;
class FakeSession : public Session{
public:
    explicit FakeSession(const Socket::Ptr& ptr) :  Session(ptr){}
    void onRecv(const Buffer::Ptr &buf) override {}
    void onError(const SockException &err) override {}
    void onManager() override {}
};

static FakeSession::Ptr session;


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

    session = make_shared<FakeSession>(Socket::createSocket());
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
        eXosip_event_t * msg = nullptr;
        /* auto process,such as:register refresh,auth,call keep... */
        if (!(msg = eXosip_event_wait (m_context, 0, 1)))
        {
            eXosip_automatic_action (m_context);
            continue;
        }
        shared_ptr<eXosip_event_t> je = shared_ptr<eXosip_event_t>(msg,[](eXosip_event_t* je){
            eXosip_event_free(je);
        });
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
            case EXOSIP_CALL_INVITE: {
                ProtocolOption option;
                mediaPlayer = std::make_shared<PlayerProxy>(DEFAULT_VHOST, "SIP", "8003", option, 2);
//                mediaPlayer->play("rtsp://admin:xxxx@192.168.1.186/h264/ch1/main/av_stream");
                mediaPlayer->play("rtsp://admin:xxxx@192.168.1.151/stream=0");
                MediaInfo info;
                info._streamid = "8003";
                info._vhost = DEFAULT_VHOST;
                info._app = "SIP";
                info._schema = RTSP_SCHEMA;
                this_thread::sleep_for(chrono::seconds(5));
                MediaSource::findAsync(info, session, [this,body,je](const std::shared_ptr<MediaSource> &src) {
                    if(src){
                        sdp_message_t remoteSdp{};
                        if(sdp_message_parse(&remoteSdp,body.c_str())!=OSIP_SUCCESS){
                            return;
                        }
                        stringstream localSdp;
                        localSdp<<"v=0\r\n";
                        localSdp<<"o="<<remoteSdp.o_username<<" "<<remoteSdp.o_sess_id<<" "<<remoteSdp.o_sess_version<<" IN IP4 "<<m_localIp<<"\r\n";
                        localSdp<<"s=TalusIPC\r\n";
                        localSdp<<"c=IN IP4 "<<m_localIp<<"\r\n";
                        localSdp<<"t=0 0\r\n";
                        auto tracks = src->getTracks(false);
                        for (const auto &item: tracks){
                            int port = allocUdpPort();
                            auto sdp = item->getSdp()->getSdp();
                            if(item->getCodecId() == mediakit::CodecH264){
                                replace(sdp,"96","99");
                            }
                            replace(sdp,"0 RTP/AVP", to_string(port)+" RTP/AVP");
                            localSdp<< sdp;
                            localSdp<<"a=sendonly\r\n";
                            if(item->getTrackType()==TrackVideo){
                                audioPair.first = port;
                                audioPair.second = atoi(eXosip_get_audio_media(&remoteSdp)->m_port);
                                destHost = remoteSdp.c_connection->c_addr;
                            }
                            if(item->getTrackType()==TrackAudio){
                                videoPair.first = port;
                                videoPair.second = atoi(eXosip_get_video_media(&remoteSdp)->m_port);
                                destHost = remoteSdp.c_connection->c_addr;
                            }
                        }
                        InfoL<<"\n"<<localSdp.str();
                        osip_message_t *msg = nullptr;
                        int ret = eXosip_call_build_answer(m_context, je->tid, 200, &msg);
                        if (OSIP_SUCCESS != ret){
                            return;
                        }
                        osip_message_set_body(msg, localSdp.str().c_str(), localSdp.str().length());
                        osip_message_set_content_type(msg, "application/sdp");

                        ret = eXosip_call_send_answer(m_context, je->tid, 200, msg);
                        if (OSIP_SUCCESS != ret){
                            return;
                        }
                        return;
                    }
                });
            }
                break;
            case EXOSIP_CALL_ACK:{
                MediaInfo info;
                info._streamid = "8003";
                info._vhost = DEFAULT_VHOST;
                info._app = "SIP";
                info._schema = RTSP_SCHEMA;
                auto src = MediaSource::find(info);
                if(src){
                    MediaSourceEvent::SendRtpArgs args;
                    if(audioPair.first&&audioPair.second){
                        args.dst_port = audioPair.second;
                        args.src_port = audioPair.first;
                        args.dst_url = destHost;
                        args.only_audio = true;
                        args.is_udp = true;
                        args.use_ps = false;
                        args.pt = 0;
                        src->startSendRtp(args, [](uint16_t, const toolkit::SockException & e){

                        });
                    }
                    if(videoPair.first&&videoPair.second){
                        args.dst_port = videoPair.second;
                        args.src_port = videoPair.first;
                        args.dst_url = destHost;
                        args.only_audio = false;
                        args.is_udp = true;
                        args.use_ps = false;
                        args.pt = 99;
                        src->startSendRtp(args, [](uint16_t, const toolkit::SockException &e){

                        });
                    }
                }
            }
            break;
            case EXOSIP_CALL_MESSAGE_NEW:{
                osip_message_t *resp;
                eXosip_call_build_answer(m_context,je->tid,200,&resp);
                eXosip_call_send_answer(m_context,je->tid,200,resp);
            };
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
                    sendResponse(je.get(),200);
                }
                break;
            default:{
            }
                break;
        }
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
