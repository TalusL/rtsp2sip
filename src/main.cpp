#include "SipClient.h"
int main(){
    SipClient sipClient;
    sipClient.StartStack();

    this_thread::sleep_for(chrono::seconds(1000));
}