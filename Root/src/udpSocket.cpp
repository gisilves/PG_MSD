/*!
  \file udpSocket.cpp
  \brief UDP socket (client, server) management, tx, rx
  \author Mattia Barbanera (mattia.barbanera@infn.it)
*/
#include "udpSocket.h"
#include "utility.h"

udpSocket::udpSocket(const std::string &_addr, int _port, bool _blocking) {
  kVerbosity = 0;
  kPort = _port;
  kBlocking = _blocking;
  kAddr = _addr;

  //Convert int port into C string port
  char portString[16];
  snprintf(portString, sizeof(portString), "%d", _port);
  portString[sizeof(portString) / sizeof(portString[0]) - 1] = '\0';
  
  //Specify socket criteria
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC; //Allows IPv4 or IPv6. AF_INET for IPv4 only
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = IPPROTO_UDP;
  //hints.ai_flags = AI_PASSIVE;
  //hints.ai_cannonname = NULL;
  //hints.ai_addr = NULL;
  //hints.ai_next = NULL;

  // Get an address that can be specified in a call to bind
  int ret = -1;
  ret = (getaddrinfo(_addr.c_str(), portString, &hints, &kAddrInfo));
  if(ret != 0 || kAddrInfo == NULL) {
    printf("Port %s or address %s not valid.\n", portString, _addr.c_str());
    exit(EXIT_FAILURE);
  }

  kSockDesc = -1;
  //Setup the socket
  udpSocket::setup();

}

udpSocket::~udpSocket() {
  printf("%s) Deleting UDP socket...\n", __METHOD_NAME__);
  if (kSockDesc > 0) {
    freeaddrinfo(kAddrInfo);
    close(kSockDesc);
  }
  kSockDesc = -1;
}

void udpSocket::setup() {
  int optval = 1;
  unsigned int buff_size = 16700000;
  socklen_t optLen = sizeof(optval);

  //Exit, if already exists
  if (kSockDesc>0) {
    freeaddrinfo(kAddrInfo);
    printf("Socket already exists.\n");
    exit(EXIT_FAILURE);
  }

  //Create a new socket
  printf("UDP socket: Opening... ");
  if (kBlocking) {
    kSockDesc = socket(kAddrInfo->ai_family, SOCK_DGRAM | SOCK_CLOEXEC | \
                                              SO_REUSEADDR, IPPROTO_UDP);
  } else {
    kSockDesc = socket(kAddrInfo->ai_family, SOCK_DGRAM | SOCK_CLOEXEC | \
                                     SO_REUSEADDR | SOCK_NONBLOCK, IPPROTO_UDP);
  }
  if(kSockDesc < 0) {
    freeaddrinfo(kAddrInfo);
    printf("Socket creation error: Port %d, address %s.\n", kPort, kAddr.c_str());
    exit(EXIT_FAILURE);
  }

  //Set socket options
  //Set SO_RCVBUF to maximum allowed
  buff_size = 16700000;
  if (setsockopt(kSockDesc, SOL_SOCKET, SO_RCVBUF, &buff_size,
                  optLen) == -1) {
    printf("setsockopt failed...\n");
    exit(EXIT_FAILURE);
  }
  //Set SO_SNDBUF to maximum allowed
  buff_size = 16700000;
  if (setsockopt(kSockDesc, SOL_SOCKET, SO_SNDBUF, &buff_size,
                  optLen) == -1) {
    printf("setsockopt failed...\n");
    exit(EXIT_FAILURE);
  }
  printf("ok\n");
}

int udpSocket::Tx(const void* _msg, size_t _size) {
  int n;
  //FIXME: Can UDP fragment packets?
  n = sendto(kSockDesc, _msg, _size, 0,
             kAddrInfo->ai_addr, kAddrInfo->ai_addrlen); //FIXME: who is the destination address?
  if(n < 0){
    fprintf(stderr, "Error in writing to the socket\n");
    std::error_code ec (errno, std::generic_category());
    std::cout << "Error: " << ec.value() << ", Message: " << ec.message() << '\n';
    //exit(1);
    return n;
  }
  if (kVerbosity > 3) printf("%s) Sent %d bytes\n", __METHOD_NAME__, n);
  return n;
}

int udpSocket::Rx(void* _msg, size_t _maxSize){
  int n;
  n = recvfrom(kSockDesc, _msg, _maxSize, 0, \
               kAddrInfo->ai_addr, &kAddrInfo->ai_addrlen);
  //n = recv(kSockDesc, _msg, _maxSize, 0);
  if (n < 0) {
    fprintf(stderr, "Error in reading the socket\n");
    std::error_code ec (errno, std::generic_category());
    std::cout << "Error: " << ec.value() << ", Message: " << ec.message() << '\n';
  }
  return n;
}

int udpSocket::RxTimeout(void* _msg, size_t _maxSize, int _maxWaitMs) {
  if (_maxWaitMs <= 0) {
    return Rx(_msg, _maxSize);
  }

  if (waitForReadEvent(_maxWaitMs) == true) {
    return Rx(_msg, _maxSize);
  } else {
    return -2;
  }
}

bool udpSocket::waitForReadEvent(int _timeout) {
  //Initialize readSet and add the listening socket to the set readSet
  fd_set readSet;
  FD_ZERO(&readSet); //FIXME if kSockDesc already in the list, FD_SET returns no error
  FD_SET(kSockDesc, &readSet);

  struct timeval waitTime;
  waitTime.tv_sec  = static_cast<int>(_timeout/1000);
  waitTime.tv_usec = static_cast<int>((_timeout%1000)*1000);
	
  //Wait the socket to be readable
  //pselect for signal capture; poll/ppoll are an upgrade
	int retSel = select(kSockDesc+1, &readSet, NULL, NULL, &waitTime);
  //int retSel = select(kSockDesc+1, &readSet, &readSet, &readSet, &waitTime);
	if(retSel > 0) {
    //Socket has data
	  return true;
	} else if(retSel < 0) {
    //Error in select
	  printf("%s) Select returned negative value: %d\n", __METHOD_NAME__, retSel);
	} else {
    //Timeout
    errno = EAGAIN;
    printf("%s) Timeout\n", __METHOD_NAME__);
	}
  
  return false;
}

//--- UDP Server ---------------------------------------------------------------
udpServer::udpServer(const std::string& _addr, int _port, bool _blocking) :\
            udpSocket::udpSocket(_addr, _port, _blocking) {
  char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
  
  int ret = bind(kSockDesc, kAddrInfo->ai_addr, kAddrInfo->ai_addrlen);
  if(ret != 0) {
    freeaddrinfo(kAddrInfo);
    close(kSockDesc);
    printf("Socket creation error: Port %d, address %s.\n", kPort, kAddr.c_str());
    exit(EXIT_FAILURE);
  }

  if (getnameinfo(kAddrInfo->ai_addr, kAddrInfo->ai_addrlen, hbuf,\
        sizeof(hbuf), sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV)\
      == 0)
    printf("%s) UDP Server created and binded: host=%s, serv=%s\n",\
              __METHOD_NAME__, hbuf, sbuf);

  std::cout << kAddrInfo->ai_addr <<std::endl;
}

udpServer::~udpServer(){
  printf("Deleting UDP Server...\n");
}

//--- UDP Client ---------------------------------------------------------------
udpClient::udpClient(const std::string& _addr, int _port, bool _blocking) :\
            udpSocket::udpSocket(_addr, _port, _blocking) {
  char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

  //if (connect(kSockDesc, kAddrInfo->ai_addr, kAddrInfo->ai_addrlen) < 0) {
  //  printf("%s) Cannot connect client to socket\n", __METHOD_NAME__);
  //  exit(1);
  //}

  if (getnameinfo(kAddrInfo->ai_addr, kAddrInfo->ai_addrlen, hbuf,\
        sizeof(hbuf), sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV)\
      == 0)
    printf("%s) UDP Client created and binded: host=%s, serv=%s\n",\
              __METHOD_NAME__, hbuf, sbuf);

}

udpClient::~udpClient(){
  printf("Deleting UDP Client...\n");
}
