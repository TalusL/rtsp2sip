
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <csignal>
#include <sys/socket.h>
#include <netinet/in.h>
#include <eXosip2/eXosip.h>
#include <string>
#include <sstream>
#include <regex>
#include <thread>
#include <arpa/inet.h>
#include <eXosip2/eX_subscribe.h>

#define ualog(a,b...) fprintf(stderr,b);fprintf(stderr,"\n>")
#define null_if_empty(s) (((s)!=NULL&&(s)[0]!='\0')?(s):NULL)
#define UA_VERSION "Decoder"
#define	BUFFER_LEN (1024)

#define	UA_CMD_REGISTER	('1')

typedef struct ua_core{
    /* config */
    int expiry;
    int localport;
    int calltimeout;
    char *proxy;
    char *outboundproxy;
    char *username;
    char *password;
    char *from;
    char *to;
    char *contact;
    char *localip;
    char *firewallip;
    char *transport;

    /* dynamic */
    struct eXosip_t *context;
    pthread_t notifythread;
    int running;
    int regid;
    int callid;
    int dialogid;
    int transactionid;
    int cmd;
} uacore;

uacore g_core;

static int ua_add_outboundproxy(osip_message_t *msg, const char *outboundproxy);
int ua_cmd_register(uacore *core);
int ua_cmd_callanswer(uacore *core);
int ua_notify_callack(uacore *core, eXosip_event_t *je);
int ua_notify_callkeep(uacore *core, eXosip_event_t *je);
void *ua_notify_thread(void *arg);


bool SendResponse(const eXosip_event_t*  osipEvent, int StatusCode)
{
    if(!osipEvent) return false;
    osip_message_t* answer = NULL;
    if(    MSG_IS_MESSAGE(osipEvent->request)
           || MSG_IS_REGISTER(osipEvent->request)
           || MSG_IS_NOTIFY(osipEvent->request)
           || MSG_IS_SUBSCRIBE(osipEvent->request)
           || MSG_IS_INFO(osipEvent->request)
            )
    {
        eXosip_lock(g_core.context);
        eXosip_message_build_answer(g_core.context, osipEvent->tid, StatusCode, &answer);
        eXosip_message_send_answer(g_core.context, osipEvent->tid, StatusCode, answer);
        eXosip_unlock(g_core.context);
    }
    return true;
}

int	SendMsg(const std::string& body, std::string msgType)
{
    osip_message_t*  rqt_msg = NULL;

    eXosip_lock(g_core.context);
    eXosip_message_build_request(g_core.context, &rqt_msg, msgType.c_str(),g_core.to, g_core.from, g_core.proxy);
    osip_message_set_body(rqt_msg, body.c_str(), body.length());
    osip_message_set_content_type(rqt_msg, "Application/MANSCDP+xml");
    int ret = eXosip_message_send_request(g_core.context, rqt_msg);
    eXosip_unlock(g_core.context);
    return ret;
}




int ua_quit(uacore *core){
    if (NULL != core->proxy) free(core->proxy);
    if (NULL != core->from) free(core->from);
    if (NULL != core->to) free(core->to);
    if (NULL != core->contact) free(core->contact);
    if (NULL != core->localip) free(core->localip);
    if (NULL != core->username) free(core->username);
    if (NULL != core->password) free(core->password);
    if (NULL != core->outboundproxy) free(core->outboundproxy);
    if (NULL != core->firewallip) free(core->firewallip);
    if (NULL != core->transport) free(core->transport);
    return 0;
}

/***************************** command *****************************/
static int
ua_add_outboundproxy(osip_message_t *msg, const char *outboundproxy)
{
    int ret = 0;
    char head[BUFFER_LEN] = { 0 };

    if (NULL == null_if_empty(outboundproxy)){
        return 0;
    }
    snprintf(head, sizeof(head)-1, "<%s;lr>", outboundproxy);

    osip_list_special_free(&msg->routes, (void(*)(void*))osip_route_free);
    ret = osip_message_set_route(msg, head);
    return ret;
}

int
ua_cmd_register(uacore *core)
{
    int ret = -1;
    osip_message_t *msg = NULL;

    if (core->regid > 0){ // refresh register
        ret = eXosip_register_build_register(core->context, core->regid, core->expiry, &msg);
        if (0 != ret){
            ualog(LOG_ERR, "register %d refresh build failed %d", core->regid, ret);
            return -1;
        }
    }
    else{ // new register
        core->regid = eXosip_register_build_initial_register(core->context,
                                                             core->from, core->proxy, core->contact, core->expiry, &msg);
        if (core->regid <= 0){
            ualog(LOG_ERR, "register build failed %d", core->regid);
            return -1;
        }
        ua_add_outboundproxy(msg, core->outboundproxy);
    }
    ret = eXosip_register_send_register(core->context, core->regid, msg);
    if (0 != ret){
        ualog(LOG_ERR, "register %d send failed", core->regid);
        return ret;
    }
    return ret;
}

int
ua_cmd_unregister(uacore *core)
{
    int ret = -1;
    osip_message_t *msg = NULL;
    int expiry = 0; //unregister

    ret = eXosip_register_build_register(core->context, core->regid, expiry, &msg);
    if (0 != ret){
        ualog(LOG_ERR, "unregister %d build failed %d", core->regid, ret);
        return -1;
    }

    ret = eXosip_register_send_register(core->context, core->regid, msg);
    if (0 != ret){
        ualog(LOG_ERR, "register %d send failed %d", core->regid, ret);
        return ret;
    }
    core->regid = 0;
    return ret;
}



int
ua_cmd_callanswer(uacore *core)
{
    int ret = 0;
    int code = 200;
    osip_message_t *msg = NULL;

    ret = eXosip_call_build_answer(core->context, core->transactionid, code, &msg);
    if (0 != ret){
        ualog(LOG_ERR, "call %d build answer failed", core->callid);
        return ret;
    }

    /* UAS call timeout */
    osip_message_set_supported(msg, "timer");


#define TCP_SETUP_PASSIVE 0
#define TCP 1
    std::stringstream sdpStream;
    sdpStream<<"v=0\r\n";
    sdpStream<<"o="<<g_core.username<<" 0 0 IN IP4 "<<g_core.localip<<"\r\n";
    sdpStream<<"s=Play \r\n";
    sdpStream<<"c=IN IP4 "<<g_core.localip<<"\r\n";
    sdpStream<<"t=0 0 \r\n";
#if TCP
    sdpStream<<"m=video 1009 TCP/RTP/AVP 96\r\n";
#else
    sdpStream<<"m=video 1009 RTP/AVP 96\r\n";
#endif
    sdpStream<<"a=rtpmap:98 H264/90000\r\n";
    sdpStream<<"a=recvonly\r\n";
#if TCP
#if TCP_SETUP_PASSIVE
    sdpStream<<"a=setup:passive\r\n";
#else
    sdpStream<<"a=setup:active\r\n";
#endif
    sdpStream<<"a=connection:new\r\n";
#endif

    std::string sdp = sdpStream.str();

    osip_message_set_body(msg, sdp.c_str(), sdp.length());
    osip_message_set_content_type(msg, "application/sdp");

    ret = eXosip_call_send_answer(core->context, core->transactionid, code, msg);
    if (0 != ret){
        ualog(LOG_ERR, "call %d send answer failed", core->callid);
        return ret;
    }
    return ret;
}



/***************************** notify *****************************/
int
ua_notify_callack(uacore *core, eXosip_event_t *je)
{
    int ret = 0;
    osip_message_t *msg = NULL;

    ret = eXosip_call_build_ack(core->context, je->did, &msg);
    if (0 != ret){
        ualog(LOG_ERR, "call %d build ack failed", je->cid);
        return ret;
    }
    ua_add_outboundproxy(msg, core->outboundproxy);

    ret = eXosip_call_send_ack(core->context, je->did, msg);
    if (0 != ret){
        ualog(LOG_ERR, "call %d send ack failed", je->cid);
        return ret;
    }
    return ret;
}

int
ua_notify_callkeep(uacore *core, eXosip_event_t *je)
{
    int ret = 0;
    int code = 200;
    osip_message_t *msg = NULL;
    eXosip_call_build_answer(core->context, je->tid, code, &msg);
    if (NULL == msg){
        ualog(LOG_ERR, "call %d send keep answer failed", je->cid);
    }
    ret = eXosip_call_send_answer(core->context, je->tid, code, msg);
    if (0 != ret){
        ualog(LOG_ERR, "call %d send keep answer failed", je->cid);
        return ret;
    }
    return ret;
}

int
ua_notidy_callid(uacore *core, eXosip_event_t *je)
{
    core->callid = je->cid;
    core->dialogid = je->did;
    core->transactionid = je->tid;
    return 0;
}

/* event notify loop */
void *
ua_notify_thread(void *arg)
{
    uacore *core = (uacore *)arg;
    int ret = 0;
    int code = -1;


    while (core->running){
        osip_message_t *msg = NULL;
        eXosip_event_t *je = eXosip_event_wait(core->context, 0, 1);
        if (NULL == je){
            /* auto process,such as:register refresh,auth,call keep... */
            eXosip_automatic_action(core->context);
            osip_usleep(100000);
            continue;
        }
        std::string body;
        if(je->request){
            osip_body_t* msgBody = NULL;
            osip_message_get_body(je->request, 0, &msgBody);
            if(msgBody){
                body = msgBody->body;
            }
        }
//        eXosip_automatic_action(core->context);
        switch (je->type){

            case EXOSIP_REGISTRATION_SUCCESS:
                if (UA_CMD_REGISTER == core->cmd){
                    ualog(LOG_INFO, "register %d sucess", je->rid);
                }
                else {
                    ualog(LOG_INFO, "unregister %d sucess", je->rid);
                }
                break;
            case EXOSIP_REGISTRATION_FAILURE:
                if (UA_CMD_REGISTER == core->cmd){
                    ualog(LOG_INFO, "register %d failure", je->rid);
                }
                else{
                    ualog(LOG_INFO, "unregister %d failure", je->rid);
                }
                break;
            case EXOSIP_CALL_INVITE:
                ua_notidy_callid(core, je);
//                ua_cmd_callring(core);
                ualog(LOG_INFO, "call %d incoming,please answer...", je->cid);
                ua_cmd_callanswer(core);

                break;
            case EXOSIP_CALL_REINVITE:
                ua_notidy_callid(core, je);
                ualog(LOG_INFO, "call %d keep", je->cid);
                ua_notify_callkeep(core, je);
                break;
            case EXOSIP_CALL_RINGING:
                ua_notidy_callid(core, je);
                ualog(LOG_INFO, "call %d ring", je->cid);
                break;
            case EXOSIP_CALL_ANSWERED:
                ua_notidy_callid(core, je);
                if (je->response)
                    code = osip_message_get_status_code(je->response);
                ualog(LOG_INFO, "call %d answer %d", je->cid, code);
                ua_notify_callack(core, je);
                break;
            case EXOSIP_CALL_NOANSWER:
                ua_notidy_callid(core, je);
                ualog(LOG_INFO, "call %d noanswer", je->cid);
                break;
            case EXOSIP_CALL_REQUESTFAILURE:
            case EXOSIP_CALL_GLOBALFAILURE:
            case EXOSIP_CALL_SERVERFAILURE:
                ua_notidy_callid(core, je);
                if (je->response)
                    code = osip_message_get_status_code(je->response);
                ualog(LOG_INFO, "call %d failture %d", je->cid, code);
                break;
            case EXOSIP_CALL_ACK:
            {
                ua_notidy_callid(core, je);
                ualog(LOG_INFO, "call %d ack", je->cid);
                osip_body_t* msgBody = NULL;
                osip_message_get_body(je->ack, 0, &msgBody);
                if(msgBody){
                    body = msgBody->body;
                }

                if(body.find("TCP") == std::string::npos|body.find("passive") == std::string::npos){
                    break;
                }

                ualog(LOG_INFO, body.c_str());

                struct sockaddr_in servaddr, cliaddr;
                int sockfd = -1;

                if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
                    perror("socket creation failed");
                    exit(EXIT_FAILURE);
                }

                std::string serverIp;
                int serverPort;
                std::smatch smIp;
                if(std::regex_search(body, smIp, std::regex(R"(c=.*?(\d+\.\d+\.\d+\.\d+))")))
                {
                    serverIp = smIp.str(1);
                }
                std::smatch smPort;
                if(std::regex_search(body, smPort, std::regex(R"(m=.*?(\d+))")))
                {
                    serverPort = stoi(smPort.str(1));
                }

                memset(&servaddr, 0, sizeof(servaddr));
                memset(&cliaddr, 0, sizeof(cliaddr));

                // Filling server information
                servaddr.sin_family    = AF_INET; // IPv4
                servaddr.sin_addr.s_addr = INADDR_ANY;
                servaddr.sin_port = htons(1009);

                // Bind the socket with the server address
                if ( bind(sockfd, (const struct sockaddr *)&servaddr,
                          sizeof(servaddr)) < 0 )
                {
                    perror("bind failed");
                    exit(EXIT_FAILURE);
                }
                int recvSocket = sockfd;

                sockaddr_in addrSer{};
                int addrLen = 0;


                addrSer.sin_family = AF_INET;
                addrSer.sin_addr.s_addr = inet_addr(serverIp.c_str());
                addrSer.sin_port = htons(serverPort);

                connect(sockfd, reinterpret_cast<const sockaddr *>(&addrSer), sizeof(sockaddr_in));


                int len, n;

                len = sizeof(cliaddr);  //len is value/result

                std::shared_ptr<char> buff = std::shared_ptr<char>(new char[1600]);

                while (g_core.running){
                    n = recv(sockfd, buff.get(), 1600,0);
                    if(n>0){
                        ualog(LOG_INFO,("recv:"+std::to_string(n)+"\r\n").c_str());
                    }
                }
            }
                break;
            case EXOSIP_CALL_CLOSED:
            ualog(LOG_INFO, "call %d stop", je->cid);
                break;
            case EXOSIP_CALL_CANCELLED:
            ualog(LOG_INFO, "call %d cancel", je->cid);
                break;
            case EXOSIP_CALL_RELEASED:
            ualog(LOG_INFO, "call %d release", je->cid);
                break;
            case EXOSIP_IN_SUBSCRIPTION_NEW:{
                osip_message_t *mmsg;
                eXosip_insubscription_build_answer(g_core.context,je->tid,505,&mmsg);
                eXosip_insubscription_send_answer(g_core.context,je->tid,505,mmsg);
            }
                break;
            case EXOSIP_MESSAGE_NEW:{
                SendResponse(je,200);

                std::string sn;
                std::regex r(R"(<SN>(.*)</SN>)");
                std::smatch sm;
                if(std::regex_search(body, sm, r))
                {
                    sn = sm.str(1);
                }

                std::stringstream catalog;
                catalog<<"<?xml version=\"1.0\" encoding=\"GB2312\"?>\r\n"
                         "<Response>\r\n"
                         "<CmdType>Catalog</CmdType>\r\n"
                         "<SN>"<<sn<<"</SN>\r\n"
                                     "<DeviceID>"<<g_core.username<<"</DeviceID>\r\n"
                                                                    "<SumNum>1</SumNum>\r\n"
                                                                    "<DeviceList Num=\"1\">\r\n"
                                                                    "<Item>\r\n"
                                                                    "<DeviceID>"<<g_core.username<<"</DeviceID>\r\n"
                                                                                                   "<Name>HDMI1</Name>\r\n"
                                                                                                   "<Manufacturer>Manufacturer</Manufacturer>\r\n"
                                                                                                   "<Model>VIDEOOUT</Model>\r\n"
                                                                                                   "<Owner>Owner</Owner>\r\n"
                                                                                                   "<CivilCode>CivilCode</CivilCode>\r\n"
                                                                                                   "<Address>Address</Address>\r\n"
                                                                                                   "<Parental>0</Parental>\r\n"
                                                                                                   "<ParentID>"<<g_core.username<<"</ParentID>\r\n"
                                                                                                                                  "<SafetyWay>0</SafetyWay>\r\n"
                                                                                                                                  "<RegisterWay>1</RegisterWay>\r\n"
                                                                                                                                  "<Secrecy>0</Secrecy>\r\n"
                                                                                                                                  "<Status>ON</Status>\r\n"
                                                                                                                                  "</Item>\r\n"
                                                                                                                                  "</DeviceList>\r\n"
                                                                                                                                  "</Response>\r\n";

                SendMsg(catalog.str(),"MESSAGE");
            }
                break;
            default:
                SendResponse(je,400);
                break;
        }
        eXosip_event_free(je);
    }
    eXosip_quit(core->context);
    osip_free(core->context);

    pthread_detach(pthread_self());
    return 0;
}

/***************************** main *****************************/
int
main(int argc, char *argv[])
{
    int ret = 0;

    memset(&g_core, 0, sizeof(uacore));
    g_core.running = 1;
    g_core.expiry = 3600;
    g_core.localport = 25060;
    g_core.calltimeout = 1800;
    g_core.transport = strdup("UDP");
    g_core.localip = strdup("10.8.9.244");
    g_core.username = strdup("11000000511140000001");
    g_core.password = strdup("12345678");
    std::string serverId = "34020000002000000001";
    std::string serverIp = "10.8.9.244";
    std::string serverPort = "5060";
    g_core.from = strdup(std::string(std::string("sip:")+g_core.username+"@"+serverId.substr(0,10)).c_str());
    g_core.contact = strdup(std::string(std::string()+"sip:"+g_core.username+"@"+g_core.localip+":"+ std::to_string(g_core.localport)).c_str());
    g_core.to = strdup(std::string("sip:"+serverId+"@"+serverId.substr(0,10)).c_str());
    g_core.proxy = strdup(std::string("sip:"+serverId+"@"+serverIp+":"+serverPort).c_str());

    g_core.context = eXosip_malloc();
    if (eXosip_init(g_core.context)){
        ualog(LOG_ERR, "init failed");
        return -1;
    }
    if (osip_strcasecmp(g_core.transport, "UDP") == 0){
        ret = eXosip_listen_addr(g_core.context, IPPROTO_UDP, NULL, g_core.localport, AF_INET, 0);
    }
    else if (osip_strcasecmp(g_core.transport, "TCP") == 0){
        ret = eXosip_listen_addr(g_core.context, IPPROTO_TCP, NULL, g_core.localport, AF_INET, 0);
    }
    else if (osip_strcasecmp(g_core.transport, "TLS") == 0){
        ret = eXosip_listen_addr(g_core.context, IPPROTO_TCP, NULL, g_core.localport, AF_INET, 1);
    }
    else if (osip_strcasecmp(g_core.transport, "DTLS") == 0){
        ret = eXosip_listen_addr(g_core.context, IPPROTO_UDP, NULL, g_core.localport, AF_INET, 1);
    }
    else{
        ret = -1;
    }
    if (ret){
        ualog(LOG_ERR, "listen failed");
        return -1;
    }
    if (g_core.localip){
        ualog(LOG_INFO, "local address: %s", g_core.localip);
        eXosip_masquerade_contact(g_core.context, g_core.localip, g_core.localport);
    }
    if (g_core.firewallip){
        ualog(LOG_INFO, "firewall address: %s:%i", g_core.firewallip, g_core.localport);
        eXosip_masquerade_contact(g_core.context, g_core.firewallip, g_core.localport);
    }
    eXosip_set_user_agent(g_core.context, UA_VERSION);
    if (g_core.username && g_core.password){
        ualog(LOG_INFO, "username: %s", g_core.username);
        ualog(LOG_INFO, "password: ******");
        if (eXosip_add_authentication_info(g_core.context, g_core.username,
                                           g_core.username, g_core.password, NULL, NULL)){
            ualog(LOG_ERR, "add_authentication_info failed");
            return -1;
        }
    }

    /* start */
    pthread_create(&g_core.notifythread, NULL, ua_notify_thread, &g_core);
    ualog(LOG_INFO, UA_VERSION " start");


    while (g_core.running){
        eXosip_lock(g_core.context);
        ua_cmd_register(&g_core);
        eXosip_unlock(g_core.context);
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }

    /* stop */
    ua_quit(&g_core);
    printf("%s stop\n", UA_VERSION);
    return 0;
}

//int
//ua_cmd_callkeep(uacore *core)
//{
//    int ret = -1;
//    char session_exp[BUFFER_LEN] = { 0 };
//    osip_message_t *msg = NULL;
//
//    ret = eXosip_call_build_request(core->context, core->dialogid, "INVITE", &msg);
//    if (NULL == msg){
//        ualog(LOG_ERR, "call %d build keep failed", core->callid);
//        return ret;
//    }
//
//    ret = eXosip_call_send_request(core->context, core->dialogid, msg);
//    if (0 != ret){
//        ualog(LOG_ERR, "call %d send keep failed", core->callid);
//        return ret;
//    }
//    return ret;
//}
//
//int
//ua_cmd_callstop(uacore *core)
//{
//    int ret = 0;
//    ret = eXosip_call_terminate(core->context, core->callid, core->dialogid);
//    if (0 != ret){
//        ualog(LOG_ERR, "call %d send stop failed", core->callid);
//        return ret;
//    }
//    return ret;
//}

//int
//ua_cmd_callstart(uacore *core)
//{
//    int ret = -1;
//    char session_exp[BUFFER_LEN] = { 0 };
//    osip_message_t *msg = NULL;
//
//    ret = eXosip_call_build_initial_invite(core->context, &msg, core->to, core->from, NULL, NULL);
//    if (0 != ret){
//        ualog(LOG_ERR, "call build failed", core->from, core->to);
//        return -1;
//    }
//    ua_add_outboundproxy(msg, core->outboundproxy);
//    osip_message_set_body(msg, g_test_sdp, strlen(g_test_sdp));
//    osip_message_set_content_type(msg, "application/sdp");
//
//    /* UAC call timeout */
//    snprintf(session_exp, sizeof(session_exp)-1, "%i;refresher=uac", core->calltimeout);
//    osip_message_set_header(msg, "Session-Expires", session_exp);
//    osip_message_set_supported(msg, "timer");
//
//    core->callid = eXosip_call_send_initial_invite(core->context, msg);
//    ret = (core->callid > 0) ? 0 : -1;
//    return ret;
//}

//int
//ua_cmd_callring(uacore *core)
//{
//    int ret = 0;
//    int code = 180;
//    osip_message_t *msg = NULL;
//
//    ret = eXosip_call_build_answer(core->context, core->transactionid, code, &msg);
//    if (0 != ret){
//        ualog(LOG_ERR, "call %d build ring failed", core->callid);
//        return ret;
//    }
//
//    ret = eXosip_call_send_answer(core->context, core->transactionid, code, msg);
//    if (0 != ret){
//        ualog(LOG_ERR, "call %d send ring failed", core->callid);
//        return ret;
//    }
//    return ret;
//}

//void
//ua_stop(int signum){
//    g_core.running = 0;
//}