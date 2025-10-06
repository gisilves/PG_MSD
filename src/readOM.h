#ifndef READOM_HH
#define READOM_HH

#include "TGraph.h"
#include "udpSocket.h"
#include "event.h"

class readOM
{
private:
    // UDP server to on-line monitor
    std::string kUdpAddr = "localhost"; //!< UDP Server address (x.x.x.x format)
    int kUdpPort = 8890;                //!< UDP server port
    udpServer *omServer;

    std::vector<uint32_t> evt;
    std::vector<uint32_t> evt_buffer;
    std::vector<uint32_t> detJ5;
    std::vector<uint32_t> detJ7;

public:
    readOM();
    virtual ~readOM();
    void DoGetUDP();
};

#endif
