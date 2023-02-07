

#include "CallSession.h"
#include "osipparser2/osip_port.h"
#include "eXosip2/eXosip.h"
#include <Util/mini.h>

#define SIP_APP "SIP"

using namespace mediakit;
using namespace toolkit;

static unordered_map<string, PlayerProxy::Ptr> s_proxyMap;

int allocUdpPort(){
    auto sock = Socket::createSocket();
    sock->bindUdpSock(0);
    int port = sock->get_local_port();
    sock->closeSock();
    return port;
}

CallSession::CallSession(const string &remoteSdp,const string& localIp,const string& phoneNumber) {
    m_remoteSdpStr = remoteSdp;
    m_localIp = localIp;
    m_phoneNumber = phoneNumber;
}

bool CallSession::Init() {
    if(sdp_message_parse(&m_remoteSdp,m_remoteSdpStr.c_str())!=OSIP_SUCCESS){
        return false;
    }
    if(eXosip_get_audio_media(&m_remoteSdp)){
        m_remoteAudioPort = stoi(eXosip_get_audio_media(&m_remoteSdp)->m_port);
        m_localAudioPort = allocUdpPort();
    }
    if(eXosip_get_video_media(&m_remoteSdp)){
        m_remoteVideoPort = stoi(eXosip_get_video_media(&m_remoteSdp)->m_port);
        m_localVideoPort = allocUdpPort();
    }
    if(!m_localAudioPort&&!m_localVideoPort){
        return false;
    }
    m_mediaDestHost = m_remoteSdp.c_connection->c_addr;
    auto src = MediaSource::find(RTSP_SCHEMA,DEFAULT_VHOST,SIP_APP,m_phoneNumber);
    if(!src){
        ProtocolOption option;
        PlayerProxy::Ptr mediaPlayer = std::make_shared<PlayerProxy>(DEFAULT_VHOST, SIP_APP, m_phoneNumber, option, 2);
        auto url = getStreamUrl(m_phoneNumber);
        if(url.empty()){
            return false;
        }
        mediaPlayer->play(url);
        s_proxyMap[m_phoneNumber] = mediaPlayer;
    }
    return true;
}

string CallSession::GetLocalSdp() {
    MediaInfo info;
    info._streamid = m_phoneNumber;
    info._vhost = DEFAULT_VHOST;
    info._app = SIP_APP;
    info._schema = RTSP_SCHEMA;

    toolkit::semaphore sem;

    auto genSdp = [&](const std::shared_ptr<MediaSource> &src){
        stringstream localSdp;
        localSdp << "v=0\r\n";
        localSdp << "o=" << m_phoneNumber << " " << m_remoteSdp.o_sess_id << " "
                 << m_remoteSdp.o_sess_version << " IN IP4 " << m_localIp << "\r\n";
        localSdp << "s=TalusIPC\r\n";
        localSdp << "c=IN IP4 " << m_localIp << "\r\n";
        localSdp << "t=0 0\r\n";
        auto tracks = src->getTracks();
        for (const auto &item: tracks) {
            if(item->getTrackType() == TrackVideo){
                if (!m_localVideoPort) {
                    continue;
                }
                auto sdp = item->getSdp();
                auto strStr = sdp->getSdp();
                replace(strStr, to_string(sdp->getPayloadType()), "99");
                replace(strStr, "0 RTP/AVP", to_string(m_localVideoPort) + " RTP/AVP");
                localSdp << strStr;
                localSdp << "a=sendonly\r\n";
            }
            if (item->getTrackType() == TrackAudio) {
                auto sdp = item->getSdp();
                auto strStr = sdp->getSdp();
                localSdp << strStr;
                localSdp << "a=sendrecv\r\n";

            }
        }
        return localSdp.str();
    };
    MediaSource::findAsync(info, this->shared_from_this(), [&](const std::shared_ptr<MediaSource> &src) {
        if (src) {
            m_localSdpStr = genSdp(src);
            sem.post();
        }
    });
    sem.wait();
    return m_localSdpStr;
}

bool CallSession::Start() {
//    MediaInfo info;
//    info._streamid = "8003";
//    info._vhost = DEFAULT_VHOST;
//    info._app = "SIP";
//    info._schema = RTSP_SCHEMA;
//    auto src = MediaSource::find(info);
//    if(src){
//        MediaSourceEvent::SendRtpArgs args;
//        if(audioPair.first&&audioPair.second){
//            args.dst_port = audioPair.second;
//            args.src_port = audioPair.first;
//            args.dst_url = destHost;
//            args.only_audio = true;
//            args.is_udp = true;
//            args.use_ps = false;
//            args.pt = 0;
//            args.ssrc = to_string(audioPair.first);
//            src->startSendRtp(args, [](uint16_t, const toolkit::SockException & e){
//                InfoL<<"audio start! "<<e.what();
//            });
//        }
//        if(videoPair.first&&videoPair.second){
//            args.dst_port = videoPair.second;
//            args.src_port = videoPair.first;
//            args.dst_url = destHost;
//            args.only_audio = false;
//            args.is_udp = true;
//            args.use_ps = false;
//            args.pt = 99;
//            args.ssrc = to_string(videoPair.first);
//            src->startSendRtp(args, [](uint16_t, const toolkit::SockException &e){
//                InfoL<<"video start! "<<e.what();
//            });
//        }
//    }
    return false;
}

bool CallSession::Stop() {
    return false;
}

string CallSession::getStreamUrl(const string &phoneNumber) {
    return mINI::Instance()["sip_proxy."+phoneNumber];
}
