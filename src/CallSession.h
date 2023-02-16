

#ifndef RTSP2SIP_CALLSESSION_H
#define RTSP2SIP_CALLSESSION_H
#include <Player/PlayerProxy.h>
#include <Network/Session.h>
#include <osipparser2/sdp_message.h>

using namespace std;
using namespace toolkit;


class CallSession {
public:
    ~CallSession();
    using Ptr = shared_ptr<CallSession>;
    explicit CallSession(const string& remoteSdp,const string& localPhoneNumber,const string& remotePhoneNumber);
    bool Init();
    string GetLocalSdp(bool recvRemoteAudio = true,bool recvRemoteVideo = false);
    bool Start();
    bool Stop();
private:
    sdp_message_t m_remoteSdp{};
    string m_mediaDestHost;
    string m_remoteSdpStr;
    string m_localSdpStr;
    string m_localIp;
    string m_localPhoneNumber;
    string m_remotePhoneNumber;
    int m_localAudioPort{};
    int m_remoteAudioPort{};
    int m_localVideoPort{};
    int m_remoteVideoPort{};
    int m_sendVideoPt = 99;
    int m_sendAudioPt = 0;
    bool m_recvRemoteAudio = true;
    bool m_recvRemoteVideo = false;

    static string getStreamUrl(const string& phoneNumber);

    void updatePt();
};


#endif //RTSP2SIP_CALLSESSION_H
