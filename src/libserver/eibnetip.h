/*
    EIBD eib bus access and management daemon
    Copyright (C) 2005-2011 Martin Koegler <mkoegler@auto.tuwien.ac.at>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef EIBNETIP_H
#define EIBNETIP_H

#include <netinet/in.h>
#include <ev++.h>
#include "common.h"
#include "iobuf.h" // for nonblocking
#include "lpdu.h"
#include "ipsupport.h"

#define SEARCH_REQUEST 0x0201
#define SEARCH_RESPONSE 0x0202
#define DESCRIPTION_REQUEST 0x0203
#define DESCRIPTION_RESPONSE 0x0204

#define CONNECTION_REQUEST 0x0205
#define CONNECTION_RESPONSE 0x0206
#define CONNECTIONSTATE_REQUEST 0x0207
#define CONNECTIONSTATE_RESPONSE 0x0208
#define DISCONNECT_REQUEST 0x0209
#define DISCONNECT_RESPONSE 0x020A

#define TUNNEL_REQUEST 0x0420
#define TUNNEL_RESPONSE 0x0421

#define DEVICE_CONFIGURATION_REQUEST 0x0310
#define DEVICE_CONFIGURATION_ACK 0x0311

#define ROUTING_INDICATION 0x0530

typedef enum {
  S_RDWR, S_RD, S_WR,
} SockMode;

/** represents a EIBnet/IP packet */
class EIBNetIPPacket
{

public:
  /** service code*/
  int service;
  /** payload */
  CArray data;
  /** source address */
  struct sockaddr_in src;

  EIBNetIPPacket ();
  /** create from character array */
  static EIBNetIPPacket *fromPacket (const CArray & c,
				     const struct sockaddr_in src);
  /** convert to character array */
  CArray ToPacket () const;
  virtual ~EIBNetIPPacket ()
  {
  }
};

class EIBnet_ConnectRequest
{
public:
  EIBnet_ConnectRequest ();
  struct sockaddr_in caddr;
  struct sockaddr_in daddr;
  CArray CRI;
  bool nat;
  EIBNetIPPacket ToPacket () const;
};

int parseEIBnet_ConnectRequest (const EIBNetIPPacket & p,
				EIBnet_ConnectRequest & r);

class EIBnet_ConnectResponse
{
public:
  EIBnet_ConnectResponse ();
  uchar channel;
  uchar status;
  struct sockaddr_in daddr;
  bool nat;
  CArray CRD;
  EIBNetIPPacket ToPacket () const;
};

int parseEIBnet_ConnectResponse (const EIBNetIPPacket & p,
				 EIBnet_ConnectResponse & r);

class EIBnet_ConnectionStateRequest
{
public:
  EIBnet_ConnectionStateRequest ();
  uchar channel;
  uchar status;
  struct sockaddr_in caddr;
  bool nat;
  EIBNetIPPacket ToPacket () const;
};

int parseEIBnet_ConnectionStateRequest (const EIBNetIPPacket & p,
					EIBnet_ConnectionStateRequest & r);

class EIBnet_ConnectionStateResponse
{
public:
  EIBnet_ConnectionStateResponse ();
  uchar channel;
  uchar status;
  EIBNetIPPacket ToPacket () const;
};

int parseEIBnet_ConnectionStateResponse (const EIBNetIPPacket & p,
					 EIBnet_ConnectionStateResponse & r);

class EIBnet_DisconnectRequest
{
public:
  EIBnet_DisconnectRequest ();
  struct sockaddr_in caddr;
  uchar channel;
  bool nat;
  EIBNetIPPacket ToPacket () const;
};

int parseEIBnet_DisconnectRequest (const EIBNetIPPacket & p,
				   EIBnet_DisconnectRequest & r);

class EIBnet_DisconnectResponse
{
public:
  EIBnet_DisconnectResponse ();
  uchar channel;
  uchar status;
  EIBNetIPPacket ToPacket () const;
};

int parseEIBnet_DisconnectResponse (const EIBNetIPPacket & p,
				    EIBnet_DisconnectResponse & r);

class EIBnet_TunnelRequest
{
public:
  EIBnet_TunnelRequest ();
  uchar channel;
  uchar seqno;
  CArray CEMI;
  EIBNetIPPacket ToPacket () const;
};

int parseEIBnet_TunnelRequest (const EIBNetIPPacket & p,
			       EIBnet_TunnelRequest & r);

class EIBnet_TunnelACK
{
public:
  EIBnet_TunnelACK ();
  uchar channel;
  uchar seqno;
  uchar status;
  EIBNetIPPacket ToPacket () const;
};

int parseEIBnet_TunnelACK (const EIBNetIPPacket & p, EIBnet_TunnelACK & r);

class EIBnet_ConfigRequest
{
public:
  EIBnet_ConfigRequest ();
  uchar channel;
  uchar seqno;
  CArray CEMI;
  EIBNetIPPacket ToPacket () const;
};

int parseEIBnet_ConfigRequest (const EIBNetIPPacket & p,
			       EIBnet_ConfigRequest & r);

class EIBnet_ConfigACK
{
public:
  EIBnet_ConfigACK ();
  uchar channel;
  uchar seqno;
  uchar status;
  EIBNetIPPacket ToPacket () const;
};

int parseEIBnet_ConfigACK (const EIBNetIPPacket & p, EIBnet_ConfigACK & r);

typedef struct
{
  uchar family;
  uchar version;
} DIB_service_Entry;

class EIBnet_DescriptionRequest
{
public:
  EIBnet_DescriptionRequest ();
  struct sockaddr_in caddr;
  bool nat;
  EIBNetIPPacket ToPacket () const;
};

int parseEIBnet_DescriptionRequest (const EIBNetIPPacket & p,
				    EIBnet_DescriptionRequest & r);

class EIBnet_DescriptionResponse
{
public:
  EIBnet_DescriptionResponse ();
  uchar KNXmedium;
  uchar devicestatus;
  eibaddr_t individual_addr;
  uint16_t installid;
  serialnumber_t serial;
  Array < DIB_service_Entry > services;
  struct in_addr multicastaddr;
  uchar MAC[6];
  uchar name[30];
  CArray optional;
  EIBNetIPPacket ToPacket () const;
};

int parseEIBnet_DescriptionResponse (const EIBNetIPPacket & p,
				     EIBnet_DescriptionResponse & r);

class EIBnet_SearchRequest
{
public:
  EIBnet_SearchRequest ();
  struct sockaddr_in caddr;
  bool nat;
  EIBNetIPPacket ToPacket () const;
};

int parseEIBnet_SearchRequest (const EIBNetIPPacket & p,
			       EIBnet_SearchRequest & r);

class EIBnet_SearchResponse
{
public:
  EIBnet_SearchResponse ();
  uchar KNXmedium;
  uchar devicestatus;
  eibaddr_t individual_addr;
  uint16_t installid;
  serialnumber_t serial;
  Array < DIB_service_Entry > services;
  struct in_addr multicastaddr;
  uchar MAC[6];
  uchar name[30];
  struct sockaddr_in caddr;
  bool nat = false;
  EIBNetIPPacket ToPacket () const;
};

int parseEIBnet_SearchResponse (const EIBNetIPPacket & p,
				EIBnet_SearchResponse & r);



typedef void (*recv_cb_t)(void *data, EIBNetIPPacket *p);

class p_recv_cb {
    recv_cb_t cb_code = 0;
    void *cb_data = 0;

    void set_ (const void *data, recv_cb_t cb)
    {
      this->cb_data = (void *)data;
      this->cb_code = cb;
    }

public:
    // function callback
    template<void (*function)(EIBNetIPPacket *p)>
    void set (void *data = 0) throw ()
    {
      set_ (data, function_thunk<function>);
    }

    template<void (*function)(EIBNetIPPacket *p)>
    static void function_thunk (void *arg, EIBNetIPPacket *p)
    {
      function(p);
    }

    // method callback
    template<class K, void (K::*method)(EIBNetIPPacket *p)>
    void set (K *object)
    {
      set_ (object, method_thunk<K, method>);
    }

    template<class K, void (K::*method)(EIBNetIPPacket *p)>
    static void method_thunk (void *arg, EIBNetIPPacket *p)
    {
      (static_cast<K *>(arg)->*method) (p);
    }

    void operator()(EIBNetIPPacket *p) {
        (*cb_code)(cb_data, p);
    }
};


/** represents a EIBnet/IP packet to send*/
struct _EIBNetIP_Send
{
  /** packat */
  EIBNetIPPacket data;
  /** destination address */
  struct sockaddr_in addr;
};

/** EIBnet/IP socket */
class EIBNetIPSocket
{
  /** debug output */
  TracePtr t;
  /** input */
  ev::io io_recv; void io_recv_cb (ev::io &w, int revents);
  /** output */
  ev::io io_send; void io_send_cb (ev::io &w, int revents);
  unsigned int send_error;

public:
  p_recv_cb on_recv;
private:
  void on_recv_cb(EIBNetIPPacket *p) { delete p; }

  /** input queue */
  Queue < struct _EIBNetIP_Send > send_q;
  void send_q_drop();
  /** multicast address */
  struct ip_mreq maddr;
  /** file descriptor */
  int fd;
  /** multicast in use? */
  bool multicast;

  const char *Name() { return "eibnetipsocket"; }
public:
  EIBNetIPSocket (struct sockaddr_in bindaddr, bool reuseaddr, TracePtr tr,
                  SockMode mode = S_RDWR);
  virtual ~EIBNetIPSocket ();
  bool init ();
  void stop();

  /** enables multicast */
  bool SetMulticast (struct ip_mreq multicastaddr);
  /** sends a packet */
  void Send (EIBNetIPPacket p, struct sockaddr_in addr);
  void Send (EIBNetIPPacket p) {
    Send (p, sendaddr);
  }

  /** get the port this socket is bound to (network byte order) */
  int port ();

  bool SetInterface(const char *iface);

  /** default send address */
  struct sockaddr_in sendaddr;

  /** address to accept packets from, if recvall is 0 */
  struct sockaddr_in recvaddr;
  /** address to also accept packets from, if 'recvall' is 3 */
  struct sockaddr_in recvaddr2;
  /** address to NOT accept packets from, if 'recvall' is 2 */
  struct sockaddr_in localaddr;

  void pause();
  void unpause();
  bool paused;

  /** flag whether to accept (almost) all packets */
  uchar recvall;
};

#endif
