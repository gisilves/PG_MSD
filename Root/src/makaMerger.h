/*!
  @file makaMerger.h
  @brief Header for merger to collect data from remote detectors
  @author Mattia Barbanera (mattia.barbanera@infn.it)
*/

#ifndef MAKA_H
#define MAKA_H


#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctime>
#include <bitset>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <stdio.h>
#include <string>
#include <thread>
#include <vector>

//#include <netinet/in.h>
//#include <arpa/inet.h>
//#include <sys/poll.h>
//#include <sys/ioctl.h>

#include "utility.h"
#include "tcpServer.h"
#include "tcpclient.h"
#include "udpSocket.h"

using namespace std;

class makaMerger : public tcpServer {

  private:
    //Configuration parameters
    const uint32_t kOkVal  = 0xb01af1ca; //!< OK  answer to client
    const uint32_t kBadVal = 0x000cacca; //!< NOK Answer to client
    vector<const char*> kDetAddrs; //!<Remote detectors addresses
    vector<int> kDetPorts; //!<Remote detectors ports
    string kDataPath = "./data/"; //!<Folder path where to store data
    int kCmdLen = 24; //!<Server commands length, handshaken with the client
    thread kMerger3d; //!<Thread that hosts the merger
    bool kRunning = false; //!<Flag for run state
    uint32_t kNEvts = 0; //!< Events in a run
    //uint32_t kRunTime //!<Run time
    char kRunType[9];     //!<Run information: type
    uint32_t kRunNum;   //!<Run information: number
    uint32_t kRunTime;  //!<Run information: start time, in unix time

    //UDP server to on-line monitor
    std::string kUdpAddr = "localhost"; //!< UDP Server address (x.x.x.x format)
    int kUdpPort = 8890;  //!< UDP server port
    udpClient* omClient;

    //#pragma pack(push,1)
    //#pragma pack(1)
    struct setupPacket {
       int pktLen;
       int pathLen;
       int detNum;
       vector<int> ports;
       vector<const char*> addr;
       string path;
    };
    //#pragma pack(pop)
    //}__attribute__((packed));
    setupPacket* sp;

    
    //N clients for remote detectors
    vector<tcpclient*> kDet; //!<Remote detectors clients

    //1 UDP socket for on-line monitor

    /*
      Write file header
    */
    int fileHeader(FILE* _dataFile);

    /*
      Create file
      While kRunning, collect data from clients
      When finished, close file
    */
    int merger();

    /*
      Create a header
      Collect data from clients
      Write data to disk
      Parasitically send data via UDP
    */
    int collector(FILE* _dataFile);

    /*
      Read an event from one detector
      First word must be the event length in uint32_t units
    */
    int getEvent(std::vector<uint32_t>& _evt, uint32_t& _evtLen, int _det);

    /*
      Handshake command length
    */
    void cmdLenHandshake();

    /*
      Process commands received 
    */
    void processCmds(char* msg);

    /*

    */
   void cmdReply(const char* cmd);


  public:
    makaMerger(int port, int verb=0, bool _net=true);
    ~makaMerger();

    /*
      Clear detector address and port lists
    */
    void clearDetLists();

    /*
      Add detector address and port to lists
    */
    void addDet(char* _addr, int _port);

    /*
      Close clients for the remote detectors
    */
    void clearDetectors();

    /*
      Create actual clients for the remote detectors
      FIXME: tcpclient connects as soon as it is created -> separate functions
    */
    void setUpDetectors();

    /*
      Start clients
      kRunning = true
      Start thread
    */
    void runStart();

    /*
      kRunning = false
      Stop thread
      Close clients
    */
    void runStop(int _sleep=0);

    /*
      Accept connections
      Loop to listen commands
    */
    void* listenCmd();

};

#endif
