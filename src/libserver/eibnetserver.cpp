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

#include "eibnetserver.h"
#include "emi.h"
#include "config.h"
#include <stdlib.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string.h>
#include <memory>

EIBnetServer::EIBnetServer (TracePtr tr, String serverName)
	: Layer2mixin::Layer2mixin (tr)
  , name(serverName)
  , mcast(NULL)
  , sock(NULL)
  , tunnel(false)
  , route(false)
  , discover(false)
  , Port(-1)
  , sock_mac(-1)
{
  drop_trigger.set<EIBnetServer,&EIBnetServer::drop_trigger_cb>(this);
  drop_trigger.start();
}

EIBnetDiscover::EIBnetDiscover (EIBnetServer *parent, const char *multicastaddr, int port, const char *intf)
{
  struct sockaddr_in baddr;
  struct ip_mreq mcfg;
  sock = 0;

  this->parent = parent;
  TRACEPRINTF (parent->t, 8, "OpenD");

  if (GetHostIP (parent->t, &maddr, multicastaddr) == 0)
    {
      ERRORPRINTF (parent->t, E_ERROR | 11, "Addr '%s' not resolvable", multicastaddr);
      goto err_out;
    }

  if (port)
    {
      maddr.sin_port = htons (port);
      memset (&baddr, 0, sizeof (baddr));
#ifdef HAVE_SOCKADDR_IN_LEN
      baddr.sin_len = sizeof (baddr);
#endif
      baddr.sin_family = AF_INET;
      baddr.sin_addr.s_addr = htonl (INADDR_ANY);
      baddr.sin_port = htons (port);

      sock = new EIBNetIPSocket (baddr, 1, parent->t);
      if (intf && !sock->SetInterface(intf))
        goto err_out;
      if (!sock->init ())
        goto err_out;
      sock->on_recv.set<EIBnetDiscover,&EIBnetDiscover::on_recv_cb>(this);
    }
  else
    {
      maddr.sin_port = parent->Port;
      sock = parent->sock;
    }

  mcfg.imr_multiaddr = maddr.sin_addr;
  mcfg.imr_interface.s_addr = htonl (INADDR_ANY);
  if (!sock->SetMulticast (mcfg))
    goto err_out;

  /** This causes us to ignore multicast packets sent by ourselves */
  if (!GetSourceAddress (&maddr, &sock->localaddr))
    goto err_out;
  sock->localaddr.sin_port = parent->Port;
  sock->recvall = 2;

  TRACEPRINTF (parent->t, 8, "OpenedD");
  return;

err_out:
  if (sock && port)
    delete (sock);
  sock = 0;
  return;
}

bool
EIBnetDiscover::init ()
{
  if (! sock)
    return false;
  
  return true;
}

EIBnetServer::~EIBnetServer ()
{
  stop();
  TRACEPRINTF (t, 8, "Close");
}

EIBnetDiscover::~EIBnetDiscover ()
{
  TRACEPRINTF (parent->t, 8, "CloseD");
  if (sock && parent->sock && parent->sock != sock)
    delete sock;
}

bool
EIBnetServer::setup (const char *multicastaddr, const int port, const char *intf,
                     const bool tunnel, const bool route,
                     const bool discover, const bool single_port)
{
  struct sockaddr_in baddr;

  TRACEPRINTF (t, 8, "Open");
  sock_mac = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock_mac < 0)
  {
    ERRORPRINTF (t, E_ERROR | 27, "Lookup socket creation failed");
    goto err_out0;
  }
  memset (&baddr, 0, sizeof (baddr));
#ifdef HAVE_SOCKADDR_IN_LEN
  baddr.sin_len = sizeof (baddr);
#endif
  baddr.sin_family = AF_INET;
  baddr.sin_addr.s_addr = htonl (INADDR_ANY);
  baddr.sin_port = single_port ? htons(port) : 0;

  sock = new EIBNetIPSocket (baddr, 1, t);
  if (!sock)
  {
    ERRORPRINTF (t, E_ERROR | 41, "EIBNetIPSocket creation failed");
    goto err_out1;
  }
  if (intf)
    sock->SetInterface(intf);

  if (!sock->init ())
    goto err_out2;

  sock->on_recv.set<EIBnetServer,&EIBnetServer::on_recv_cb>(this);

  sock->recvall = 1;
  Port = sock->port ();

  mcast = new EIBnetDiscover (this, multicastaddr, single_port ? 0 : port, intf);
  if (!mcast)
  {
    ERRORPRINTF (t, E_ERROR | 42, "EIBnetDiscover creation failed");
    goto err_out2;
  }
  if (!mcast->init ())
    goto err_out3;

  this->tunnel = tunnel;
  this->route = route;
  this->discover = discover;
  this->single_port = single_port;
  if (this->route || this->tunnel)
    addGroupAddress(0);

  TRACEPRINTF (t, 8, "Opened");

  return true;

err_out4:
  stop();
err_out3:
  delete mcast;
  mcast = NULL;
err_out2:
  delete sock;
  sock = NULL;
err_out1:
  close (sock_mac);
  sock_mac = -1;
err_out0:
  return false;
}

void EIBnetDiscover::Send (EIBNetIPPacket p, struct sockaddr_in addr)
{
  if (sock)
    sock->Send (p, addr);
}

void
EIBnetServer::send_L_Data (LDataPtr l)
{
  if (route)
    {
      TRACEPRINTF (t, 8, "Send_Route %s", l->Decode ().c_str());
      EIBNetIPPacket p;
      p.service = ROUTING_INDICATION;
      p.data = L_Data_ToCEMI (0x29, l);
      Send (p);
    }
}

bool ConnState::init()
{
  if (! Layer2mixin::init(parent->l3))
    return false;
  if (! addGroupAddress(0))
    return false;
  if (type == CT_BUSMONITOR && ! l3->registerVBusmonitor(this))
    return false;
  l3 = parent->l3->registerLayer2(shared_from_this());

  addAddress(remoteAddr);
  TRACEPRINTF (parent->t, 8, "Start Conn %d", channel);
  return true;
}

void ConnState::send_L_Busmonitor (LBusmonPtr l)
{
  if (type == CT_BUSMONITOR)
    {
      out.put (Busmonitor_to_CEMI (0x2B, l, no++));
      if (! retries)
	send_trigger.send();
    }
}

void ConnState::send_L_Data (LDataPtr l)
{
  if (type == CT_STANDARD)
    {
      out.put (L_Data_ToCEMI (0x29, l));
      if (! retries)
	send_trigger.send();
    }
}

int
EIBnetServer::addClient (ConnType type, const EIBnet_ConnectRequest & r1,
                         eibaddr_t addr)
{
  unsigned int i;
  int id = 1;
rt:
  ITER(i, connections)
    if ((*i)->channel == id)
      {
	id++;
	goto rt;
      }
  if (id <= 0xff)
    {
      ConnStatePtr s = ConnStatePtr(new ConnState (this, addr));
      s->channel = id;
      s->daddr = r1.daddr;
      s->caddr = r1.caddr;
      s->retries = 0;
      s->sno = 0;
      s->rno = 0;
      s->no = 1;
      s->type = type;
      s->nat = r1.nat;
      if(!s->init())
        return -1;

      connections.push_back(s);
    }
  return id;
}

ConnState::ConnState (EIBnetServer *p, eibaddr_t addr)
  : Layer2mixin (TracePtr(new Trace(*(p->t),p->t->name+':'+FormatEIBAddr(addr))))
{
  parent = p;
  timeout.set <ConnState,&ConnState::timeout_cb> (this);
  sendtimeout.set <ConnState,&ConnState::sendtimeout_cb> (this);
  send_trigger.set<ConnState,&ConnState::send_trigger_cb>(this);
  send_trigger.start();
  remoteAddr = addr;
  TRACEPRINTF (t, 9, "has %s", FormatEIBAddr (addr).c_str());
}

void ConnState::sendtimeout_cb(ev::timer &w, int revents)
{
  if (++retries <= 5)
    {
      send_trigger.send();
      return;
    }
  CArray p = out.get ();
  t->TracePacket (2, "dropped no-ACK", p.size(), p.data());
  stop();
}

void ConnState::send_trigger_cb(ev::async &w, int revents)
{
  if (out.isempty ())
    return;
  EIBNetIPPacket p;
  if (type == CT_CONFIG)
    {
      EIBnet_ConfigRequest r;
      r.channel = channel;
      r.seqno = sno;
      r.CEMI = out.front ();
      p = r.ToPacket ();
    }
  else
    {
      EIBnet_TunnelRequest r;
      r.channel = channel;
      r.seqno = sno;
      r.CEMI = out.front ();
      p = r.ToPacket ();
    }
  retries ++;
  sendtimeout.start(1,0);
  parent->mcast->Send (p, daddr);
}

void ConnState::timeout_cb(ev::timer &w, int revents)
{
  if (channel > 0)
    {
      EIBnet_DisconnectRequest r;
      r.channel = channel;
      if (GetSourceAddress (&caddr, &r.caddr))
        {
          r.caddr.sin_port = parent->Port;
          r.nat = nat;
          parent->Send (r.ToPacket (), caddr);
        }
    }
  stop();
}

void ConnState::stop()
{
  TRACEPRINTF (t, 8, "Stop Conn %d", channel);
  if (type == CT_BUSMONITOR)
    l3->deregisterVBusmonitor(this);
  timeout.stop();
  sendtimeout.stop();
  send_trigger.stop();
  retries = 0;
  parent->drop_connection (std::static_pointer_cast<ConnState>(shared_from_this()));
  Layer2::stop();
  if (remoteAddr && l3)
    {
      l3->release_client_addr(remoteAddr);
      remoteAddr = 0;
    }
}

void EIBnetServer::drop_connection (ConnStatePtr s)
{
  drop_q.put(std::move(s));
  drop_trigger.send();
}

void EIBnetServer::drop_trigger_cb(ev::async &w, int revents)
{
  while (!drop_q.isempty())
    {
      ConnStatePtr s = drop_q.get();
      ITER(i,connections)
        if (*i == s)
          {
            connections.erase (i);
            break;
          }
    }
}

ConnState::~ConnState()
{
  TRACEPRINTF (parent->t, 8, "CloseS");
}

void ConnState::reset_timer()
{
  timeout.set(120,0);
}

void
EIBnetServer::handle_packet (EIBNetIPPacket *p1, EIBNetIPSocket *isock)
{
  /* Get MAC Address */
  /* TODO: cache all of this, and ask at most once per seoncd */

  struct ifreq ifr;
  struct ifconf ifc;
  char buf[1024];
  unsigned char mac_address[IFHWADDRLEN]= {0,0,0,0,0,0};

  if (sock_mac != -1 && discover &&
      (p1->service == DESCRIPTION_REQUEST || p1->service == SEARCH_REQUEST))
    {
      ifc.ifc_len = sizeof(buf);
      ifc.ifc_buf = buf;
      if (ioctl(sock_mac, SIOCGIFCONF, &ifc) != -1)
	{
	  struct ifreq* it = ifc.ifc_req;
	  const struct ifreq* const end = it + (ifc.ifc_len / sizeof(struct ifreq));

	  for (; it != end; ++it)
	    {
	      strcpy(ifr.ifr_name, it->ifr_name);
	      if (ioctl(sock_mac, SIOCGIFFLAGS, &ifr))
		continue;
	      if (ifr.ifr_flags & IFF_LOOPBACK) // don't count loopback
		continue;
	      if (ioctl(sock_mac, SIOCGIFHWADDR, &ifr))
		continue;
	      if (ifr.ifr_hwaddr.sa_family != ARPHRD_ETHER)
		continue;
	      memcpy(mac_address, ifr.ifr_hwaddr.sa_data, sizeof(mac_address));
	      break;
	    }
	}
    }
  /* End MAC Address */

  if (p1->service == SEARCH_REQUEST)
    {
      EIBnet_SearchRequest r1;
      EIBnet_SearchResponse r2;
      DIB_service_Entry d;
      if (parseEIBnet_SearchRequest (*p1, r1))
        {
          t->TracePacket (2, "unparseable SEARCH_REQUEST", p1->data);
          goto out;
        }
      TRACEPRINTF (t, 8, "SEARCH_REQ");
      if (!discover)
        goto out;

      r2.KNXmedium = 2;
      r2.devicestatus = 0;
      r2.individual_addr = l3->getDefaultAddr();
      r2.installid = 0;
      r2.multicastaddr = mcast->maddr.sin_addr;
      r2.serial[0]=1;
      r2.serial[1]=2;
      r2.serial[2]=3;
      r2.serial[3]=4;
      r2.serial[4]=5;
      r2.serial[5]=6;
      //FIXME: Hostname, MAC-addr
      memcpy(r2.MAC, mac_address, sizeof(r2.MAC));
      //FIXME: Hostname, indiv. address
      strncpy ((char *) r2.name, name.c_str(), sizeof(r2.name));
      d.version = 1;
      d.family = 2; // core
      r2.services.push_back (d);
      //d.family = 3; // device management
      //r2.services.add (d);
      d.family = 4;
      if (tunnel)
	r2.services.push_back (d);
      d.family = 5;
      if (route)
	r2.services.push_back (d);
      if (!GetSourceAddress (&r1.caddr, &r2.caddr))
	goto out;
      r2.caddr.sin_port = Port;
      isock->Send (r2.ToPacket (), r1.caddr);
      goto out;
    }

  if (p1->service == DESCRIPTION_REQUEST)
    {
      EIBnet_DescriptionRequest r1;
      EIBnet_DescriptionResponse r2;
      DIB_service_Entry d;
      if (parseEIBnet_DescriptionRequest (*p1, r1))
        {
          t->TracePacket (2, "unparseable DESCRIPTION_REQUEST", p1->data);
          goto out;
        }
      if (!discover)
        goto out;
      TRACEPRINTF (t, 8, "DESCRIBE");
      r2.KNXmedium = 2;
      r2.devicestatus = 0;
      r2.individual_addr = l3->getDefaultAddr();
      r2.installid = 0;
      r2.multicastaddr = mcast->maddr.sin_addr;
      memcpy(r2.MAC, mac_address, sizeof(r2.MAC));
      //FIXME: Hostname, indiv. address
      strncpy ((char *) r2.name, name.c_str(), sizeof(r2.name));
      d.version = 1;
      d.family = 2;
      if (discover)
	r2.services.push_back (d);
      d.family = 3;
      r2.services.push_back (d);
      d.family = 4;
      if (tunnel)
	r2.services.push_back (d);
      d.family = 5;
      if (route)
	r2.services.push_back (d);
      isock->Send (r2.ToPacket (), r1.caddr);
      goto out;
    }
  if (p1->service == ROUTING_INDICATION)
    {
      if (p1->data.size() < 2 || p1->data[0] != 0x29)
        {
          t->TracePacket (2, "unparseable ROUTING_INDICATION", p1->data);
          goto out;
        }
      LDataPtr c = CEMI_to_L_Data (p1->data, shared_from_this());
      if (!c)
        t->TracePacket (2, "unCEMIable ROUTING_INDICATION", p1->data);
      else if (route)
	{
	  TRACEPRINTF (t, 8, "Recv_Route %s", c->Decode ().c_str());
          l3->recv_L_Data (std::move(c));
	}
      goto out;
    }
  if (p1->service == CONNECTIONSTATE_REQUEST)
    {
      EIBnet_ConnectionStateRequest r1;
      EIBnet_ConnectionStateResponse r2;
      if (parseEIBnet_ConnectionStateRequest (*p1, r1))
        {
          t->TracePacket (2, "unparseable CONNECTIONSTATE_REQUEST", p1->data);
          goto out;
        }
      r2.channel = r1.channel;
      r2.status = 0x21;
      ITER(i, connections)
	if ((*i)->channel == r1.channel)
	  {
            TRACEPRINTF ((*i)->t, 8, "CONNECTIONSTATE_REQUEST on %d", r1.channel);
            r2.status = 0;
            (*i)->reset_timer();
	    break;
	  }
      if (r2.status)
        TRACEPRINTF (t, 2, "Unknown connection %d", r2.channel);
        
      isock->Send (r2.ToPacket (), r1.caddr);
      goto out;
    }
  if (p1->service == DISCONNECT_REQUEST)
    {
      EIBnet_DisconnectRequest r1;
      EIBnet_DisconnectResponse r2;
      if (parseEIBnet_DisconnectRequest (*p1, r1))
        {
          t->TracePacket (2, "unparseable DISCONNECT_REQUEST", p1->data);
          goto out;
        }
      r2.status = 0x21;
      r2.channel = r1.channel;
      ITER(i,connections)
	if ((*i)->channel == r1.channel)
	  {
            r2.status = 0;
            TRACEPRINTF ((*i)->t, 8, "DISCONNECT_REQUEST");
            (*i)->stop();
            break;
	  }
      if (r2.status)
        TRACEPRINTF (t, 8, "DISCONNECT_REQUEST on %d", r1.channel);
      isock->Send (r2.ToPacket (), r1.caddr);
      goto out;
    }
  if (p1->service == CONNECTION_REQUEST)
    {
      EIBnet_ConnectRequest r1;
      EIBnet_ConnectResponse r2;
      if (parseEIBnet_ConnectRequest (*p1, r1))
        {
          t->TracePacket (2, "unparseable CONNECTION_REQUEST", p1->data);
          goto out;
        }
      r2.status = 0x22;
      if (r1.CRI.size() == 3 && r1.CRI[0] == 4)
	{
	  eibaddr_t a = tunnel ? l3->get_client_addr (t) : 0;
	  r2.CRD.resize (3);
	  r2.CRD[0] = 0x04;
          if (tunnel)
            TRACEPRINTF (t, 8, "Tunnel CONNECTION_REQ with %s", FormatEIBAddr(a).c_str());
	  r2.CRD[1] = (a >> 8) & 0xFF;
	  r2.CRD[2] = (a >> 0) & 0xFF;
          if (!tunnel)
            TRACEPRINTF (t, 8, "Tunnel CONNECTION_REQ, ignored, not tunneling");
          else if (!a)
            TRACEPRINTF (t, 8, "Tunnel CONNECTION_REQ, ignored, no free addresses");
          else if (r1.CRI[1] == 0x02 || r1.CRI[1] == 0x80)
	    {
	      int id = addClient ((r1.CRI[1] == 0x80) ? CT_BUSMONITOR : CT_STANDARD, r1, a);
	      if (id <= 0xff)
		{
		  r2.channel = id;
		  r2.status = 0;
		}
	    }
          else
            TRACEPRINTF (t, 8, "bad CONNECTION_REQ: [1] x%02x", r1.CRI[1]);
	}
      else if (r1.CRI.size() == 1 && r1.CRI[0] == 3)
	{
	  r2.CRD.resize (1);
	  r2.CRD[0] = 0x03;
	  TRACEPRINTF (t, 8, "Tunnel CONNECTION_REQ");
	  int id = addClient (CT_CONFIG, r1, 0);
	  if (id <= 0xff)
	    {
	      r2.channel = id;
	      r2.status = 0;
	    }
	}
      else
        {
          TRACEPRINTF (t, 8, "bad CONNECTION_REQ: size %d, [0] x%02x", r1.CRI.size(), r1.CRI[0]);
          // XXX set status to something more reasonable
        }
      if (!GetSourceAddress (&r1.caddr, &r2.daddr))
	goto out;
      if (tunnel && r2.status)
        TRACEPRINTF (t, 8, "CONNECTION_REQ: no free channel");
      r2.daddr.sin_port = Port;
      r2.nat = r1.nat;
      isock->Send (r2.ToPacket (), r1.caddr);
      goto out;
    }
  if (p1->service == TUNNEL_REQUEST)
    {
      EIBnet_TunnelRequest r1;
      EIBnet_TunnelACK r2;
      if (parseEIBnet_TunnelRequest (*p1, r1))
        {
          t->TracePacket (2, "unparseable TUNNEL_REQUEST", p1->data);
          goto out;
        }
      if (tunnel)
        ITER(i,connections)
          if ((*i)->channel == r1.channel)
            {
              (*i)->tunnel_request(r1, isock);
              goto out;
            }
      TRACEPRINTF (t, 8, "TUNNEL_REQ on unknown %d", r1.channel);
      goto out;
    }
  if (p1->service == TUNNEL_RESPONSE)
    {
      EIBnet_TunnelACK r1;
      if (parseEIBnet_TunnelACK (*p1, r1))
        {
          t->TracePacket (2, "unparseable TUNNEL_RESPONSE", p1->data);
          goto out;
        }
      if (tunnel)
        ITER(i, connections)
          if ((*i)->channel == r1.channel)
            {
              (*i)->tunnel_response (r1);
              goto out;
            }
      TRACEPRINTF (t, 8, "TUNNEL_ACK on unknown %d",r1.channel);
      goto out;
    }
  if (p1->service == DEVICE_CONFIGURATION_REQUEST)
    {
      EIBnet_ConfigRequest r1;
      EIBnet_ConfigACK r2;
      if (parseEIBnet_ConfigRequest (*p1, r1))
        {
          t->TracePacket (2, "unparseable DEVICE_CONFIGURATION_REQUEST", p1->data);
          goto out;
        }
      TRACEPRINTF (t, 8, "CONFIG_REQ on %d",r1.channel);
      ITER(i, connections)
	if ((*i)->channel == r1.channel)
	  {
	    (*i)->config_request (r1, isock);
	    break;
	  }
      goto out;
    }
  if (p1->service == DEVICE_CONFIGURATION_ACK)
    {
      EIBnet_ConfigACK r1;
      if (parseEIBnet_ConfigACK (*p1, r1))
        {
          t->TracePacket (2, "unparseable DEVICE_CONFIGURATION_ACK", p1->data);
          goto out;
        }
      ITER(i, connections)
	if ((*i)->channel == r1.channel)
	  {
	    (*i)->config_response (r1);
	    goto out;
	  }
      TRACEPRINTF (t, 8, "CONFIG_ACK on unknown channel %d",r1.channel);
      goto out;
    }
  TRACEPRINTF (t, 8, "Unexpected service type: %04x", p1->service);
out:
  delete p1;
}

void
EIBnetServer::on_recv_cb (EIBNetIPPacket *p)
{
  handle_packet (p, this->sock);
}

//void
//EIBnetServer::error_cb ()
//{
//  TRACEPRINTF (t, 8, "got an error");
//  stop();
//}

void
EIBnetServer::stop()
{
  drop_trigger.stop();

  R_ITER(i,connections)
    (*i)->stop();

  if (mcast)
  {
    delete mcast;
    mcast = 0;
  }
  if (sock)
  {
    delete sock;
    sock = 0;
  }
  if (sock_mac >= 0)
  {
    close (sock_mac);
    sock_mac = -1;
  }
  Layer2mixin::stop();
}

void
EIBnetDiscover::on_recv_cb (EIBNetIPPacket *p)
{
  parent->handle_packet (p, this->sock);
}

void ConnState::tunnel_request(EIBnet_TunnelRequest &r1, EIBNetIPSocket *isock)
{
  EIBnet_TunnelACK r2;
  r2.channel = r1.channel;
  r2.seqno = r1.seqno;

  if (rno == ((r1.seqno + 1) & 0xff))
    {
      TRACEPRINTF (t, 8, "Lost ACK for %d", rno);
      isock->Send (r2.ToPacket (), daddr);
      return;
    }
  if (rno != r1.seqno)
    {
      TRACEPRINTF (t, 8, "Wrong sequence %d<->%d",
		   r1.seqno, rno);
      return;
    }
  if (type == CT_STANDARD)
    {
      TRACEPRINTF (t, 8, "TUNNEL_REQ");
      LDataPtr c = CEMI_to_L_Data (r1.CEMI, shared_from_this());
      if (c)
	{
	  r2.status = 0;
          if (r1.CEMI[0] == 0x11)
            {
              out.put (L_Data_ToCEMI (0x2E, c));
              if (! retries)
		send_trigger.send();
            }
          if (c->source == 0)
            c->source = remoteAddr;
          if (r1.CEMI[0] == 0x11 || r1.CEMI[0] == 0x29)
            l3->recv_L_Data (std::move(c));
          else
            TRACEPRINTF (t, 8, "Wrong leader x%02x", r1.CEMI[0]);
	}
      else
	r2.status = 0x29;
    }
  else
    {
      TRACEPRINTF (t, 8, "Type not CT_STANDARD (%d)", type);
      r2.status = 0x29;
    }
  rno++;
  isock->Send (r2.ToPacket (), daddr);

  reset_timer(); // presumably the client is alive if it can send
}

void ConnState::tunnel_response (EIBnet_TunnelACK &r1)
{
  TRACEPRINTF (t, 8, "TUNNEL_ACK");
  if (sno != r1.seqno)
    {
      TRACEPRINTF (t, 8, "Wrong sequence %d<->%d",
		   r1.seqno, sno);
      return;
    }
  if (r1.status != 0)
    {
      TRACEPRINTF (t, 8, "Wrong status %d", r1.status);
      return;
    }
  if (! retries)
    {
      TRACEPRINTF (t, 8, "Unexpected ACK 1");
      return;
    }
  if (type != CT_STANDARD && type != CT_BUSMONITOR)
    {
      TRACEPRINTF (t, 8, "Unexpected Connection Type");
      return;
    }
  sno++;

  out.get ();
  sendtimeout.stop();
  reset_timer(); // presumably the client is alive if it can ack
  retries = 0;
  if (!out.isempty())
    send_trigger.send();
}

void ConnState::config_request(EIBnet_ConfigRequest &r1, EIBNetIPSocket *isock)
{
  EIBnet_ConfigACK r2;
  if (rno == ((r1.seqno + 1) & 0xff))
    {
      r2.channel = r1.channel;
      r2.seqno = r1.seqno;
      isock->Send (r2.ToPacket (), daddr);
      return;
    }
  if (rno != r1.seqno)
    {
      TRACEPRINTF (t, 8, "Wrong sequence %d<->%d",
		   r1.seqno, rno);
      return;
    }
  r2.channel = r1.channel;
  r2.seqno = r1.seqno;
  if (type == CT_CONFIG && r1.CEMI.size() > 1)
    {
      if (r1.CEMI[0] == 0xFC)
	{
	  if (r1.CEMI.size() == 7)
	    {
	      CArray res, CEMI;
	      int obj = (r1.CEMI[1] << 8) | r1.CEMI[2];
	      int objno = r1.CEMI[3];
	      int prop = r1.CEMI[4];
	      int count = (r1.CEMI[5] >> 4) & 0x0f;
	      int start = (r1.CEMI[5] & 0x0f) | r1.CEMI[6];
	      res.resize (1);
	      res[0] = 0;
	      if (obj == 0 && objno == 0)
		{
		  if (prop == 0)
		    {
		      res.resize (2);
		      res[0] = 0;
		      res[1] = 0;
		      start = 0;
		    }
		  else
		    count = 0;
		}
	      else
		count = 0;
	      CEMI.resize (6 + res.size());
	      CEMI[0] = 0xFB;
	      CEMI[1] = (obj >> 8) & 0xff;
	      CEMI[2] = obj & 0xff;
	      CEMI[3] = objno;
	      CEMI[4] = prop;
	      CEMI[5] = ((count & 0x0f) << 4) | (start >> 8);
	      CEMI[6] = start & 0xff;
	      CEMI.setpart (res, 7);
	      r2.status = 0x00;

	      out.push (CEMI);
              if (! retries)
		send_trigger.send();
	    }
	  else
	    r2.status = 0x26;
	}
      else
	r2.status = 0x26;
    }
  else
    r2.status = 0x29;
  rno++;
  isock->Send (r2.ToPacket (), daddr);
}

void ConnState::config_response (EIBnet_ConfigACK &r1)
{
  TRACEPRINTF (t, 8, "CONFIG_ACK");
  if (sno != r1.seqno)
    {
      TRACEPRINTF (t, 8, "Wrong sequence %d<->%d",
		   r1.seqno, sno);
      return;
    }
  if (r1.status != 0)
    {
      TRACEPRINTF (t, 8, "Wrong status %d", r1.status);
      return;
    }
  if (!retries)
    {
      TRACEPRINTF (t, 8, "Unexpected ACK 2");
      return;
    }
  if (type != CT_CONFIG)
    {
      TRACEPRINTF (t, 8, "Unexpected Connection Type");
      return;
    }
  sno++;
  sendtimeout.stop();

  out.get ();
  retries = 0;
  if (!out.isempty())
    send_trigger.send();
}

