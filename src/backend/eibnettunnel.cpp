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

#include "eibnettunnel.h"
#include "emi.h"
#include "layer3.h"

EIBNetIPTunnel::EIBNetIPTunnel (const char *dest, int port, int sport,
				const char *srcip, int Dataport, L2options *opt) : Layer2 (opt)
{
  TRACEPRINTF (t, 2, "Open");

  timeout.set <EIBNetIPTunnel,&EIBNetIPTunnel::timeout_cb> (this);
  conntimeout.set <EIBNetIPTunnel,&EIBNetIPTunnel::conntimeout_cb> (this);
  sendtimeout.set <EIBNetIPTunnel,&EIBNetIPTunnel::sendtimeout_cb> (this);
  trigger.set <EIBNetIPTunnel,&EIBNetIPTunnel::trigger_cb> (this);

  trigger.start();

  if (opt && opt->send_delay) {
    send_delay = float(opt->send_delay) / 1000;
    opt->send_delay = 0;
  } else
    send_delay = 0;

  sock = 0;
  if (!GetHostIP (t, &caddr, dest))
    return;
  caddr.sin_port = htons (port);
  if (!GetSourceAddress (&caddr, &raddr))
    return;
  raddr.sin_port = htons (sport);
  NAT = false;
  dataport = Dataport;
  sock = new EIBNetIPSocket (raddr, 0, t);
  if (!sock->init ())
    {
      delete sock;
      sock = 0;
      return;
    }
  sock->on_recv.set<EIBNetIPTunnel,&EIBNetIPTunnel::on_recv_cb>(this);

  if (srcip)
    {
      if (!GetHostIP (t, &saddr, srcip))
	{
	  delete sock;
	  sock = 0;
	  return;
	}
      saddr.sin_port = htons (sport);
      NAT = true;
    }
  else
    saddr = raddr;
  sock->sendaddr = caddr;
  sock->recvaddr = caddr;
  sock->recvall = 0;
  support_busmonitor = true;
  connect_busmonitor = false;
  TRACEPRINTF (t, 2, "Opened");
}

EIBNetIPTunnel::~EIBNetIPTunnel ()
{
  TRACEPRINTF (t, 2, "Close");
  if (sock && channel >= 0)
    {
      EIBnet_DisconnectRequest dreq;
      dreq.nat = saddr.sin_addr.s_addr == 0;
      dreq.caddr = saddr;
      dreq.channel = channel;

      EIBNetIPPacket p = dreq.ToPacket ();
      sock->Send (p, caddr);
    }
  timeout.stop();
  sendtimeout.stop();
  conntimeout.stop();
  trigger.stop();
  delete sock;
}

bool EIBNetIPTunnel::init (Layer3 *l3)
{
  if (sock == 0)
    return false;
  if (! addGroupAddress(0))
    return false;
  if (! Layer2::init (l3))
    return false;

  EIBnet_ConnectRequest creq = get_creq();
  EIBNetIPPacket p = creq.ToPacket ();
  sock->sendaddr = caddr;
  sock->Send (p);
  conntimeout.start(10,0);

  return true;
}

void
EIBNetIPTunnel::send_L_Data (LDataPtr l)
{
  TRACEPRINTF (t, 2, "Send %s", l->Decode ().c_str());

  send_q.put (L_Data_ToCEMI (0x11, l));
  trigger.send();
}

bool
EIBNetIPTunnel::enterBusmonitor ()
{
  if (!Layer2::enterBusmonitor ())
    return false;
  if (support_busmonitor)
    connect_busmonitor = true;
  return 1;
}

bool
EIBNetIPTunnel::leaveBusmonitor ()
{
  if (!Layer2::leaveBusmonitor ())
    return false;
  connect_busmonitor = false;
  return 1;
}

void
EIBNetIPTunnel::on_recv_cb (EIBNetIPPacket *p1)
{
  LDataPtr c;

  switch (p1->service)
    {
    case CONNECTION_RESPONSE:
      {
	EIBnet_ConnectResponse cresp;
	if (mod)
	  goto err;
	if (parseEIBnet_ConnectResponse (*p1, cresp))
	  {
	    TRACEPRINTF (t, 1, "Recv wrong connection response");
	    break;
	  }
	if (cresp.status != 0)
	  {
	    TRACEPRINTF (t, 1, "Connect failed with error %02X",
			  cresp.status);
	    if (cresp.status == 0x23 && support_busmonitor && connect_busmonitor)
	      {
		TRACEPRINTF (t, 1, "Disable busmonitor support");
		support_busmonitor = false;
		connect_busmonitor = false;

		EIBnet_ConnectRequest creq = get_creq();
		creq.CRI[1] = 0x02;

		EIBNetIPPacket p = creq.ToPacket ();
		TRACEPRINTF (t, 1, "Connectretry");
		sock->Send (p, caddr);
		conntimeout.start(10,0);
	      }
	    break;
	  }
	if (cresp.CRD.size() != 3)
	  {
	    TRACEPRINTF (t, 1, "Recv wrong connection response");
	    break;
	  }
	addAddress((cresp.CRD[1] << 8) | cresp.CRD[2]);
	daddr = cresp.daddr;
	if (!cresp.nat)
	  {
	    if (NAT)
	      daddr.sin_addr = caddr.sin_addr;
	    if (dataport != -1)
	      daddr.sin_port = htons (dataport);
	  }
	channel = cresp.channel;
	mod = 1; trigger.send();
	sno = 0;
	rno = 0;
	sock->recvaddr2 = daddr;
	sock->recvall = 3;
	conntimeout.start(30,0);
	heartbeat = 0;
	break;
      }
    case TUNNEL_REQUEST:
      {
	EIBnet_TunnelRequest treq;
	if (mod == 0)
	  {
	    TRACEPRINTF (t, 1, "Not connected");
	    goto err;
	  }
	if (parseEIBnet_TunnelRequest (*p1, treq))
	  {
	    TRACEPRINTF (t, 1, "Invalid request");
	    break;
	  }
	if (treq.channel != channel)
	  {
	    TRACEPRINTF (t, 1, "Not for us (treq.chan %d != %d)", treq.channel,channel);
	    break;
	  }
	if (((treq.seqno + 1) & 0xff) == rno)
	  {
	    EIBnet_TunnelACK tresp;
	    tresp.status = 0;
	    tresp.channel = channel;
	    tresp.seqno = treq.seqno;

	    EIBNetIPPacket p = tresp.ToPacket ();
	    sock->Send (p, daddr);
	    sock->recvall = 0;
	    break;
	  }
	if (treq.seqno != rno)
	  {
	    TRACEPRINTF (t, 1, "Wrong sequence %d<->%d",
			  treq.seqno, rno);
	    if (treq.seqno < rno)
	      treq.seqno += 0x100;
	    if (treq.seqno >= rno + 5)
	      {
		EIBnet_DisconnectRequest dreq;
		dreq.nat = saddr.sin_addr.s_addr == 0;
		dreq.caddr = saddr;
		dreq.channel = channel;

		EIBNetIPPacket p = dreq.ToPacket ();
		sock->Send (p, caddr);
		sock->recvall = 0;
		mod = 0;
	      }
	    break;
	  }
	rno++;
	if (rno > 0xff)
	  rno = 0;
	EIBnet_TunnelACK tresp;
	tresp.status = 0;
	tresp.channel = channel;
	tresp.seqno = treq.seqno;

	EIBNetIPPacket p = tresp.ToPacket ();
	sock->Send (p, daddr);

	//Confirmation
	if (treq.CEMI[0] == 0x2E)
	  {
	    if (mod == 3)
	      {
		mod = 1; trigger.send();
	      }
	    break;
	  }
	if (treq.CEMI[0] == 0x2B)
	  {
	    LBusmonPtr l2 = CEMI_to_Busmonitor (treq.CEMI, shared_from_this());
	    l3->recv_L_Busmonitor (std::move(l2));
	    break;
	  }
	if (treq.CEMI[0] != 0x29)
	  {
	    TRACEPRINTF (t, 1, "Unexpected CEMI Type %02X",
			  treq.CEMI[0]);
	    break;
	  }
	c = CEMI_to_L_Data (treq.CEMI, shared_from_this());
	if (c)
	  {
	    TRACEPRINTF (t, 1, "Recv %s", c->Decode ().c_str());
	    if (mode != BUSMODE_MONITOR)
	      {
		l3->recv_L_Data (std::move(c));
		break;
	      }
	    LBusmonPtr p1 = LBusmonPtr(new L_Busmonitor_PDU (shared_from_this()));
	    p1->pdu = c->ToPacket ();
	    l3->recv_L_Busmonitor (std::move(p1));
	    break;
	  }
	TRACEPRINTF (t, 1, "Unknown CEMI");
	break;
      }
    case TUNNEL_RESPONSE:
      {
	EIBnet_TunnelACK tresp;
	if (mod == 0)
	  {
	    TRACEPRINTF (t, 1, "Not connected");
	    goto err;
	  }
	if (parseEIBnet_TunnelACK (*p1, tresp))
	  {
	    TRACEPRINTF (t, 1, "Invalid response");
	    break;
	  }
	if (tresp.channel != channel)
	  {
	    TRACEPRINTF (t, 1, "Not for us (tresp.chan %d != %d)", tresp.channel,channel);
	    break;
	  }
	if (tresp.seqno != sno)
	  {
	    TRACEPRINTF (t, 1, "Wrong sequence %d<->%d",
			  tresp.seqno, sno);
	    break;
	  }
	if (tresp.status)
	  {
	    TRACEPRINTF (t, 1, "Error in ACK %d", tresp.status);
	    break;
	  }
	if (mod == 2)
	  {
	    sno++;
	    if (sno > 0xff)
	      sno = 0;
	    send_q.get ();
	    if (send_delay)
	      {
		mod = 3;
		sendtimeout.start(send_delay, 0);
	      }
	    else
	      {
		mod = 1; trigger.send();
	      }
	    drop = 0;
	    retry = 0;
	  }
	else
	  TRACEPRINTF (t, 1, "Unexpected ACK mod=%d",mod);
	break;
      }
    case CONNECTIONSTATE_RESPONSE:
      {
	EIBnet_ConnectionStateResponse csresp;
	if (parseEIBnet_ConnectionStateResponse (*p1, csresp))
	  {
	    TRACEPRINTF (t, 1, "Invalid response");
	    break;
	  }
	if (csresp.channel != channel)
	  {
	    TRACEPRINTF (t, 1, "Not for us (csresp.chan %d != %d)", csresp.channel,channel);
	    break;
	  }
	if (csresp.status == 0)
	  {
	    if (heartbeat > 0)
	      heartbeat--;
	    else
	      TRACEPRINTF (t, 1,
			    "Duplicate Connection State Response");
	  }
	else if (csresp.status == 0x21)
	  {
	    TRACEPRINTF (t, 1,
			  "Connection State Response not connected");
	    EIBnet_DisconnectRequest dreq;
	    dreq.nat = saddr.sin_addr.s_addr == 0;
	    dreq.caddr = saddr;
	    dreq.channel = channel;

	    EIBNetIPPacket p = dreq.ToPacket ();
	    sock->Send (p, caddr);
	    sock->recvall = 0;
	    mod = 0;
	  }
	else
	  TRACEPRINTF (t, 1,
			"Connection State Response Error %02x",
			csresp.status);
	break;
      }
    case DISCONNECT_REQUEST:
      {
	EIBnet_DisconnectRequest dreq;
	if (mod == 0)
	  {
	    TRACEPRINTF (t, 1, "Not connected");
	    goto err;
	  }
	if (parseEIBnet_DisconnectRequest (*p1, dreq))
	  {
	    TRACEPRINTF (t, 1, "Invalid request");
	    break;
	  }
	if (dreq.channel != channel)
	  {
	    TRACEPRINTF (t, 1, "Not for us (dreq.chan %d != %d)", dreq.channel,channel);
	    break;
	  }

	EIBnet_DisconnectResponse dresp;
	dresp.channel = channel;
	dresp.status = 0;

	EIBNetIPPacket p = dresp.ToPacket ();
	t->TracePacket (1, "SendDis", p.data);
	sock->Send (p, caddr);
	sock->recvall = 0;
	mod = 0;
	break;
      }
    case DISCONNECT_RESPONSE:
      {
	EIBnet_DisconnectResponse dresp;
	if (mod == 0)
	  {
	    TRACEPRINTF (t, 1, "Not connected");
	    break;
	  }
	if (parseEIBnet_DisconnectResponse (*p1, dresp))
	  {
	    TRACEPRINTF (t, 1, "Invalid request");
	    break;
	  }
	if (dresp.channel != channel)
	  {
	    TRACEPRINTF (t, 1, "Not for us (dresp.chan %d != %d)", dresp.channel,channel);
	    break;
	  }
	mod = 0;
	sock->recvall = 0;
	TRACEPRINTF (t, 1, "Disconnected");
	conntimeout.start(0.1,0);
	break;
      }
    default:
    err:
      TRACEPRINTF (t, 1, "Recv unexpected service %04X",
		    p1->service);
    }
  delete p1;
}

void EIBNetIPTunnel::trigger_cb(ev::async &w, int revents)
{
  if (mod != 1 || send_q.isempty ())
    return;

  EIBnet_TunnelRequest treq;
  treq.channel = channel;
  treq.seqno = sno;
  treq.CEMI = send_q.front ();

  EIBNetIPPacket p = treq.ToPacket ();
  t->TracePacket (1, "SendTunnel", p.data);
  sock->Send (p, daddr);
  mod = 2; timeout.start(1,0);
}

void EIBNetIPTunnel::conntimeout_cb(ev::timer &w, int revents)
{
  if (mod)
    {
      if (heartbeat < 5)
	{
	  EIBnet_ConnectionStateRequest csreq;
	  csreq.nat = saddr.sin_addr.s_addr == 0;
	  csreq.caddr = saddr;
	  csreq.channel = channel;

	  EIBNetIPPacket p = csreq.ToPacket ();
	  TRACEPRINTF (t, 1, "Heartbeat");
	  sock->Send (p, caddr);
	  heartbeat++;
          conntimeout.start(30,0);
	}
      else
	{
	  TRACEPRINTF (t, 1, "Disconnection because of errors");
	  EIBnet_DisconnectRequest dreq;
	  dreq.caddr = saddr;
	  dreq.channel = channel;

	  if (channel != -1)
	    {
	      EIBNetIPPacket p = dreq.ToPacket ();
	      sock->Send (p, caddr);
	    }
	  sock->recvall = 0;
	  mod = 0;
	}
    }
  else
    {
      EIBnet_ConnectRequest creq = get_creq();
      creq.CRI[1] =
	((connect_busmonitor && support_busmonitor) ? 0x80 : 0x02);

      TRACEPRINTF (t, 1, "Connectretry");
      EIBNetIPPacket p = creq.ToPacket ();
      sock->Send (p, caddr);
      conntimeout.start(10,0);
    }
}

void
EIBNetIPTunnel::sendtimeout_cb(ev::timer &w, int revents)
{
  if (mod == 3)
    {
      mod = 1; trigger.send();
    }
}

void
EIBNetIPTunnel::timeout_cb(ev::timer &w, int revents)
{
  if (mod != 2)
    return;
  if (retry++ > 3)
    {
      TRACEPRINTF (t, 1, "Drop");
      send_q.get ();
      drop++;
      if (drop >= 3)
	{
	  TRACEPRINTF (t, 1, "Too many drops, disconnecting");
	  EIBnet_DisconnectRequest dreq;
	  dreq.caddr = saddr;
	  dreq.channel = channel;
	  EIBNetIPPacket p = dreq.ToPacket ();
	  sock->Send (p, caddr);
	  sock->recvall = 0;
	  mod = 0;
	  return;
	}
      else
	TRACEPRINTF (t, 1, "Drop");
    }
  else
    TRACEPRINTF (t, 1, "Retry");
  mod = 1; trigger.send();
}

