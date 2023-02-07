

#ifndef RTSP2SIP_CALLSESSION_H
#define RTSP2SIP_CALLSESSION_H
#include <Player/PlayerProxy.h>
#include <Network/Session.h>
#include <osipparser2/sdp_message.h>

using namespace std;
using namespace toolkit;

class FakeSession : public Session{
public:
    explicit FakeSession() :  Session(Socket::createSocket()){}
    void onRecv(const Buffer::Ptr &buf) override {}
    void onError(const SockException &err) override {}
    void onManager() override {}
};

class CallSession:public FakeSession {
public:
    ~CallSession() override;
    using Ptr = shared_ptr<CallSession>;
    explicit CallSession(const string& remoteSdp,const string& phoneNumber);
    bool Init();
    string GetLocalSdp();
    bool Start();
    bool Stop();
private:
    sdp_message_t m_remoteSdp{};
    string m_mediaDestHost;
    string m_remoteSdpStr;
    string m_localSdpStr;
    string m_localIp;
    string m_phoneNumber;
    int m_localAudioPort{};
    int m_remoteAudioPort{};
    int m_localVideoPort{};
    int m_remoteVideoPort{};

    static string getStreamUrl(const string& phoneNumber);
};


#endif //RTSP2SIP_CALLSESSION_H
