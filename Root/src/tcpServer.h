/*!
  @file tcpServer.h
  @brief TCP server class
  @author Mattia Barbanera (mattia.barbanera@infn.it)
  @author Matteo Duranti (matteo.duranti@infn.it)
*/

#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <errno.h>
#include <cerrno>
#include <system_error>
#include <iostream>

/*!
  TCP server class
*/
class tcpServer {

  protected:
    int kVerbosity; //!< Verbosity level
    int kSockDesc;  //!< Socket descriptor before opening connections
    int kTcpConn;   //!< Accepted and open TCP connection
    volatile bool kListeningOn; //!< Turn on/off the listenings of commands
    int kPort;      //!< Port
    struct sockaddr_in kAddr; //!< Address
    bool kBlocking; //!< Blocking or non-blocking flag

    /*!
      Create, bind, and configure socket
    */
    void Setup();

    /*!
      Open connections after socket binding
    */
    void AcceptConnection();

    /*!
      Setup and AcceptConnections
    */
    void SockStart();

    /*!
      Send len bytes into msg
      @param msg Pointer to the TX buffer
      @param len Bytes to send to the socket
      @return Bytes sent, -1 for errors, 0 for EOF
    */
    int Tx(const void* msg, uint32_t len);

    /*!
      Receive len bytes into msg
      @param msg Pointer to the RX buffer
      @param len Bytes to read from the socket
      @return Bytes read, -1 for errors, 0 for EOF
    */
    int Rx(void* msg, uint32_t len);

    /*!
      Receive len bytes into msg, but waits until socket is readable
      @param msg     Pointer to the RX buffer
      @param len     Bytes to read from the socket
      @param timeout Microseconds to wait until socket is readable
      @return Bytes read, -1 for errors, -2 for timeout, 0 for EOF
    */
    int RxTimeout(void* msg, uint32_t len, int timeout = -1);

    /*!
      Wait until socket is readable
      @param timeout Microseconds to wait until socket is readable
      @return False if timed-out or select returns negative values
    */
    bool waitForReadEvent(int timeout);

  public:
    tcpServer(int port, int verb=0);
    virtual ~tcpServer();

    void SetVerbosity(int verb){
      kVerbosity = verb;
    }

    int GetVerbosity(){
      return kVerbosity;
    }

    /*!
      Stop incoming connections
    */
    void StopListening();

    /*!
      Set true kListeningOn
    */
    void StartListening();

};

#endif
