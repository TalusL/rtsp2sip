

#include "CallSession.h"
#include "osipparser2/osip_port.h"
#include "eXosip2/eXosip.h"
#include <Util/mini.h>
#include <regex>

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

CallSession::CallSession(const string &remoteSdp,const string& localPhoneNumber,const string& remotePhoneNumber) {
    m_remoteSdpStr = remoteSdp;
    m_localIp  = toolkit::mINI::Instance()["sip.localIp"];
    m_localPhoneNumber = localPhoneNumber;
    m_remotePhoneNumber = remotePhoneNumber;
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
    auto src = MediaSource::find(RTSP_SCHEMA, DEFAULT_VHOST, SIP_APP, m_localPhoneNumber);
    if(!src){
        ProtocolOption option;
        PlayerProxy::Ptr mediaPlayer = std::make_shared<PlayerProxy>(DEFAULT_VHOST, SIP_APP, m_localPhoneNumber, option, 2);
        //RTSP OVER TCP
        (*mediaPlayer)[Client::kRtpType] = Rtsp::RTP_TCP;
        auto url = getStreamUrl(m_localPhoneNumber);
        if(url.empty()){
            return false;
        }
        mediaPlayer->play(url);
        s_proxyMap[m_localPhoneNumber] = mediaPlayer;
        {
            auto phUn = m_localPhoneNumber;
            mediaPlayer->getPoller()->doDelayTask(1000*10,[phUn](){
                if(s_proxyMap.find(phUn)!=s_proxyMap.end()){
                    auto src = MediaSource::find(RTSP_SCHEMA,DEFAULT_VHOST,SIP_APP,phUn);
                    if(src&&!src->totalReaderCount()){
                        s_proxyMap.erase(phUn);
                        return false;
                    }
                }
                return true;
            });
        }
    }
    return true;
}

string CallSession::GetLocalSdp(bool recvRemoteAudio,bool recvRemoteVideo) {
    MediaInfo info;
    info._streamid = m_localPhoneNumber;
    info._vhost = DEFAULT_VHOST;
    info._app = SIP_APP;
    info._schema = RTSP_SCHEMA;

    m_recvRemoteAudio = recvRemoteAudio;
    m_recvRemoteVideo = recvRemoteVideo;

    toolkit::semaphore sem;

    auto genSdp = [this](const std::shared_ptr<MediaSource> &src,bool recvRemoteAudio,bool recvRemoteVideo){
        stringstream localSdp;
        localSdp << "v=0\r\n";
        localSdp << "o=" << m_localPhoneNumber << " " << m_remoteSdp.o_sess_id << " "
                 << m_remoteSdp.o_sess_version << " IN IP4 " << m_localIp << "\r\n";
        localSdp << "s=TalusIPC\r\n";
        localSdp << "c=IN IP4 " << m_localIp << "\r\n";
        localSdp << "t=0 0\r\n";
        auto tracks = src->getTracks();

        auto getSdpLine = [](const string& sdp,
                const string& codecName,int& pt,string& rtpmapLine,string& fmtpLine,
                string& profile,string& audioTransportType,string& videoTransportType){
            auto sdpLines = toolkit::split(sdp,"\r\n");
            for (const auto &item: sdpLines){
                if(item.find(codecName)!=string::npos){
                    sscanf(item.c_str(),"a=rtpmap:%d",&pt);
                    rtpmapLine = item;
                }
                if(item.find("fmtp:"+ to_string(pt))!=string::npos){
                    fmtpLine = item;
                    smatch r;
                    if(regex_search(item,r,regex("profile-level-id=([0-9a-fA-F]+)"))){
                        profile = r.str(1);
                    }
                }
            }
            {
                smatch ar;
                if(regex_search(sdp,ar,regex("m=audio[\\S\\s]*?(sendonly|recvonly|sendrecv)"))){
                    audioTransportType = ar.str(1);
                }
                smatch vr;
                if(regex_search(sdp,vr,regex("m=video[\\S\\s]*?(sendonly|recvonly|sendrecv)"))){
                    videoTransportType = vr.str(1);
                }
                if(audioTransportType.empty()){
                    audioTransportType = "sendrecv";
                }
                if(videoTransportType.empty()){
                    videoTransportType = "sendrecv";
                }
            }
        };

        int pt;
        string rtpmapLine,fmtpLine,profile,audioTransportType,videoTransportType;
        static map<string,string> transportMap = {{"sendrecv","sendrecv"},{"sendonly","recvonly"},{"recvonly","sendonly"}};
        for (const auto &item: tracks) {
            if(item->getTrackType() == TrackVideo){
                if (!m_localVideoPort) {
                    continue;
                }
                getSdpLine(m_remoteSdpStr,item->getCodecName(),pt,rtpmapLine,fmtpLine,profile,audioTransportType,videoTransportType);
                if(rtpmapLine.empty()){
                    continue;
                }
                m_sendVideoPt = pt;
                localSdp <<"m=video "<<m_localVideoPort<<" RTP/AVP "<<pt<<"\r\n";
                localSdp << rtpmapLine << "\r\n";
                getSdpLine(item->getSdp()->getSdp(),item->getCodecName(),pt,rtpmapLine,fmtpLine,profile,audioTransportType,videoTransportType);
                replace(fmtpLine,"fmtp:" + to_string(pt),"fmtp:" + to_string(m_sendVideoPt));
                localSdp << fmtpLine << "\r\n";
                if(recvRemoteVideo){
                    localSdp << "a="<<transportMap[videoTransportType]<<"\r\n";
                }else{
                    localSdp << "a=sendonly\r\n";
                }
            }
            if (item->getTrackType() == TrackAudio) {
                getSdpLine(m_remoteSdpStr,item->getCodecName(),pt,rtpmapLine,fmtpLine,profile,audioTransportType,videoTransportType);
                if(rtpmapLine.empty()){
                    continue;
                }
                m_sendAudioPt = pt;
                localSdp <<"m=audio "<<m_localAudioPort<<" RTP/AVP "<<pt<<"\r\n";
                localSdp << rtpmapLine << "\r\n";
                if(recvRemoteAudio){
                    localSdp << "a="<<transportMap[audioTransportType]<<"\r\n";
                }else{
                    localSdp << "a=sendonly\r\n";
                }
            }
        }
        return localSdp.str();
    };

    auto waitEnd = getCurrentMillisecond() + 10000;
    EventPollerPool::Instance().getPoller()->doDelayTask(100,[&](){
        auto src = MediaSource::find(RTSP_SCHEMA,DEFAULT_VHOST,SIP_APP,m_localPhoneNumber);
        if(!src&&getCurrentMillisecond()<waitEnd){
            return true;
        }
        if(!src){
            sem.post();
            return false;
        }

        if(m_localVideoPort&&!src->getTrack(TrackType::TrackVideo)&&getCurrentMillisecond()<waitEnd){
            return true;
        }
        if(m_localAudioPort&&!src->getTrack(TrackType::TrackAudio)&&getCurrentMillisecond()<waitEnd ){
            return true;
        }
        auto aTrack = src->getTrack(TrackType::TrackAudio);
        auto vTrack = src->getTrack(TrackType::TrackVideo);
        if(aTrack){
            if (!aTrack->ready()&&getCurrentMillisecond()<waitEnd){
                return true;
            }
        }
        if(vTrack){
            if (!vTrack->ready()&&getCurrentMillisecond()<waitEnd){
                return true;
            }
        }
        m_localSdpStr = genSdp(src,recvRemoteAudio,recvRemoteVideo);
        sem.post();
        return false;
    });
    sem.wait();
    return m_localSdpStr;
}

bool CallSession::Start() {
    if(!m_localVideoPort&&!m_localAudioPort){
        return false;
    }
    MediaInfo info;
    info._streamid = m_localPhoneNumber;
    info._vhost = DEFAULT_VHOST;
    info._app = SIP_APP;
    info._schema = RTSP_SCHEMA;
    auto src = MediaSource::find(info);
    if(src){
        if(m_localAudioPort){
            MediaSourceEvent::SendRtpArgs args;
            args.dst_port = m_remoteAudioPort;
            args.src_port = m_localAudioPort;
            args.dst_url = m_mediaDestHost;
            args.only_audio = true;
            args.is_udp = true;
            args.use_ps = false;
            args.pt = m_sendAudioPt;
            args.ssrc = to_string(m_localAudioPort);
            if(m_recvRemoteAudio){
                args.recv_stream_id = to_string(m_localAudioPort)+"_recv";
            }
            src->startSendRtp(args, [](uint16_t, const toolkit::SockException & e){
                InfoL<<"audio start! "<<e.what();
            });
        }
        if(m_localVideoPort){
            MediaSourceEvent::SendRtpArgs args;
            args.dst_port = m_remoteVideoPort;
            args.src_port = m_localVideoPort;
            args.dst_url = m_mediaDestHost;
            args.only_audio = false;
            args.is_udp = true;
            args.use_ps = false;
            args.pt = m_sendVideoPt;
            args.ssrc = to_string(m_localVideoPort);
            if(m_recvRemoteVideo){
                args.recv_stream_id = to_string(m_localVideoPort)+"_recv";
            }
            updatePt();
            src->startSendRtp(args, [](uint16_t, const toolkit::SockException &e){
                InfoL<<"video start! "<<e.what();
            });
        }
    }
    return true;
}

bool CallSession::Stop() {
    auto src = MediaSource::find(RTSP_SCHEMA, DEFAULT_VHOST, SIP_APP, m_localPhoneNumber);
    if(!src){
        return true;
    }
    src->stopSendRtp(to_string(m_localVideoPort));
    src->stopSendRtp(to_string(m_localAudioPort));
    auto recv_vSrc = MediaSource::find(RTSP_SCHEMA, DEFAULT_VHOST, SIP_APP, to_string(m_localVideoPort)+"_recv");
    if(recv_vSrc){
        recv_vSrc->close(true);
    }
    auto recv_aSrc = MediaSource::find(RTSP_SCHEMA, DEFAULT_VHOST, SIP_APP, to_string(m_localAudioPort)+"_recv");
    if(recv_aSrc){
        recv_aSrc->close(true);
    }
    return true;
}

string CallSession::getStreamUrl(const string &phoneNumber) {
    return mINI::Instance()["sip_proxy."+phoneNumber];
}

CallSession::~CallSession() {
}

void CallSession::updatePt() {
    smatch r;
    auto lines = toolkit::split(m_remoteSdpStr,"\n");
    for (const auto &item: lines){
        if(regex_search(item,r,regex("a=rtpmap:(\\d+)\\s+(H264|H265|OPUS|PS)"))){
            auto ptStr = "rtp_proxy."+strToLower(r.str(2)+"_pt");
            auto pt = stoi(r.str(1));
            mINI::Instance()[ptStr] = pt;
        }
    }
    NoticeCenter::Instance().emitEvent(Broadcast::kBroadcastReloadConfig);
}
