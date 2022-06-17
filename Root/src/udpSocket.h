/*!
  \file udpSocket.h
  \brief UDP class, server and client
  \author Mattia Barbanera (mattia.barbanera@infn.it)
*/

#ifndef UDPSOCKET_H
#define UDPSOCKET_H

#include <unistd.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <cerrno>
#include <system_error>
#include <iostream>
#include <stdlib.h>     //exit, EXIT_FAILURE

//! \brief Generic UDP socket (client/socket not specified)
class udpSocket {
  protected:
    int kVerbosity; //!< Verbosity level
    int kPort;      //!< Port
    bool kBlocking; //!< Blocking or non-blocking flag
    std::string kAddr;    //!< Address in x.x.x.x format
    struct addrinfo * kAddrInfo; //!< 
    int kSockDesc;  //!< Socket descriptor before opening connections

    //! \brief Create and configure socket
    void setup();

    /*! \brief Wait until socket is readable.
      
      \param[in] timeout Microseconds to wait until socket is readable
      
      \return False on time out or select returns negative values
    */
    bool waitForReadEvent(int _timeout);

  public:
    udpSocket(const std::string &_addr, int _port);
    ~udpSocket();

    inline int getSocket() {
      return kSockDesc;
    };

    /*! \brief Receive len bytes into \p msg.
      UDP does not have a connection status, so the sender is not influenced by
      the presence of a receiver or not.
      This function can return only with message or error for blocking sockets.
      For non-blocking sockets, it will return immediately in case empty.

      \param[in] msg Pointer to the RX buffer
      \param[in] len Bytes to read from the socket
  
      \return Number of bytes read, -1 on error
    */
    int Rx(void* _msg, size_t _maxSize);

    /*! \brief Receive len bytes into \p msg, waiting for a maximum \p timeout.
      Wait for data for \p timeout time. If no data are available, errno is set
      to EAGAIN.

      \param[in] msg Pointer to the RX buffer
      \param[in] len Bytes to read from the socket
      \param[in] timeout Milliseconds to wait until socket is readable

      \return Number of bytes read, -1 on error, -2 on timeout
    */
    int RxTimeout(void* _msg, size_t _maxSize, int _maxWaitMs);

    /*! \brief Send a message through UDP.
      Send \p msg through the UDP socket, both client or socket. 
      \todo Check what happens with messages that are too long w.r.t. the UDP
      packet size

      \param[in] msg  Message to send
      \param[in] size Size of the message
     
      \return Number of bytes sent, -1 on error
    */
    int Tx(const void* _msg, size_t _size);

};

//! \brief UDP Server
class udpServer : public udpSocket {
  public:
    udpServer(const std::string& _addr, int _port);
    ~udpServer();
};

//! \brief UDP Client
class udpClient : public udpSocket {
  public:
    udpClient(const std::string& _addr, int _port);
    ~udpClient();
};

#endif
// UDPSOCKET_H
