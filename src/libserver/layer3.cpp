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

#include "layer3.h"
#include "layer2.h"
#include "server.h"
#include <typeinfo>

Layer3::Layer3() { }
Layer3::~Layer3() { }

Layer3real::Layer3real (eibaddr_t addr, TracePtr tr, bool force_broadcast)
    : Layer3()
{
  _tr = tr;
  defaultAddr = addr;
  this->force_broadcast = force_broadcast;
  TRACEPRINTF (_tr, 3, "Open");
  running = true;
  trigger.set<Layer3real, &Layer3real::trigger_cb>(this);
  trigger.start();
  mtrigger.set<Layer3real, &Layer3real::mtrigger_cb>(this);
  mtrigger.start();
  cleanup.set<Layer3real, &Layer3real::cleanup_cb>(this);
  cleanup.start();

  TRACEPRINTF (_tr, 3, "L3 started");
}

Layer3real::~Layer3real ()
{
  TRACEPRINTF (tr(), 3, "L3 stopping");
  running = false;
  cache = nullptr;
  trigger.stop();
  mtrigger.stop();

  R_ITER(i,layer2)
    (*i)->stop();
  ITER(i,layer2)
    ERRORPRINTF (tr(), E_WARNING | 54, "Layer2 '%s' didn't de-register!", (*i)->Name());

  layer2.clear();
  servers.clear();

  R_ITER(i,vbusmonitor)
    ERRORPRINTF (tr(), E_WARNING | 55, "VBusmonitor '%s' didn't de-register!", i->cb->Name());
  vbusmonitor.clear();

  R_ITER(i,busmonitor)
    ERRORPRINTF (tr(), E_WARNING | 56, "Busmonitor '%s' didn't de-register!", i->cb->Name());
  busmonitor.clear();

  cleanup.stop();
  TRACEPRINTF (tr(), 3, "Closed");
}

void
Layer3real::recv_L_Data (LDataPtr l)
{
  if (running)
    {
      TRACEPRINTF (l->l2->t, 9, "Queue %s", l->Decode ().c_str());
      buf.push (std::move(l));
      trigger.send();
    }
  else
    TRACEPRINTF (l->l2->t, 9, "Queue: discard (not running) %s", l->Decode ().c_str());
}

void
Layer3real::recv_L_Busmonitor (LBusmonPtr l)
{
  if (running)
    {
      TRACEPRINTF (tr(), 9, "MonQueue %s", l->Decode ().c_str());
      mbuf.push (std::move(l));
      mtrigger.send();
    }
  else
    TRACEPRINTF (tr(), 9, "MonQueue: discard (not running) %s", l->Decode ().c_str());
}

bool
Layer3real::deregisterBusmonitor (L_Busmonitor_CallBack * c)
{
  unsigned i;
  for (i = 0; i < busmonitor.size(); i++)
    if (busmonitor[i].cb == c)
      {
	busmonitor.erase(busmonitor.begin()+i);
	if (busmonitor.size() == 0)
          for (unsigned int i = 0; i < layer2.size(); i++)
            if (layer2[i]->leaveBusmonitor ())
              layer2[i]->Open ();
	TRACEPRINTF (tr(), 3, "deregisterBusmonitor %08X = 1", c);
	return true;
      }
  TRACEPRINTF (tr(), 3, "deregisterBusmonitor %08X = 0", c);
  return false;
}

void
Layer3real::deregisterServer (BaseServer * s)
{
  ITER(i,servers)
    if (*i == s)
      {
	TRACEPRINTF (tr(), 3, "deregisterServer %d:%s = 1", s->t->seq, s->t->name.c_str());
	servers.erase(i);
	return;
      }
  TRACEPRINTF (tr(), 3, "deregisterServer %d:%s = 0", s->t->seq, s->t->name.c_str());
}

bool
Layer3real::deregisterVBusmonitor (L_Busmonitor_CallBack * c)
{
  ITER(i,vbusmonitor)
    if (i->cb == c)
      {
	TRACEPRINTF (tr(), 3, "deregisterVBusmonitor %08X = 1", c);
	vbusmonitor.erase(i);
	return true;
      }
  TRACEPRINTF (tr(), 3, "deregisterVBusmonitor %08X = 0", c);
  return false;
}

bool
Layer3real::deregisterLayer2 (Layer2Ptr l2)
{
  TRACEPRINTF (l2->t, 3, "deregisterLayer2 %d", l2->t->seq);
  cleanup_q.push(l2);
  cleanup.send();
}

void
Layer3real::cleanup_cb (ev::async &w, int revents)
{
  while (!cleanup_q.isempty())
    {
      Layer2Ptr l2 = cleanup_q.get();

      ITER(i,layer2)
        if (*i == l2)
          {
            TRACEPRINTF (l2->t, 3, "deregisterLayer2 %d OK", l2->t->seq);
            layer2.erase(i);
            goto out;
          }
      ERRORPRINTF (l2->t, E_WARNING | 60, "deregisterLayer2 %d: not found", l2->t->seq);
    out:;
    }
}

bool
Layer3real::registerBusmonitor (L_Busmonitor_CallBack * c)
{
  TRACEPRINTF (tr(), 3, "registerBusmonitor %08X", c);
  if (!busmonitor.size()) 
    {
      bool have_monitor = false;
      ITER (i, layer2)
        if (i->get()->Close ()) 
          {
            if (i->get()->enterBusmonitor ())
              have_monitor = true;
            else
              i->get()->Open ();
          }
      if (! have_monitor)
        return false;
    }
  busmonitor.push_back((Busmonitor_Info){.cb=c});
  TRACEPRINTF (tr(), 3, "registerBusmontitr %08X = 1", c);
  return true;
}

bool
Layer3real::registerVBusmonitor (L_Busmonitor_CallBack * c)
{
  vbusmonitor.push_back((Busmonitor_Info){.cb=c});
  TRACEPRINTF (tr(), 3, "registerVBusmonitor %08X", c);
  return true;
}

Layer3 *
Layer3real::registerLayer2 (Layer2Ptr l2)
{
  TRACEPRINTF (tr(), 3, "registerLayer2 %d:%s", l2->t->seq, l2->t->name.c_str());
  if (! busmonitor.size() || ! l2->enterBusmonitor ())
    if (! l2->Open ())
      {
        TRACEPRINTF (tr(), 3, "registerLayer2 %d:%s = 0", l2->t->seq, l2->t->name.c_str());
        return nullptr;
      }
  layer2.push_back(l2);
  TRACEPRINTF (tr(), 3, "registerLayer2 %d:%s = 1", l2->t->seq, l2->t->name.c_str());
  return this;
}

bool
Layer3real::hasAddress (eibaddr_t addr, Layer2Ptr l2)
{
  TracePtr t = l2 ? l2->t : tr();
  if (addr == defaultAddr)
    {
      TRACEPRINTF (t, 8, "default addr %s", FormatEIBAddr (addr).c_str());
      return l2 != nullptr;
    }

  if (l2 && l2->hasAddress(addr))
    {
    on_this_interface:
      TRACEPRINTF (t, 8, "own addr %s", FormatEIBAddr (addr).c_str());
      return false;
    }

  ITER(i,layer2)
    {
      if (*i == l2)
        continue;
      if ((*i)->hasAddress (addr))
        {
          if (*i == l2)
            {
              TRACEPRINTF (l2->t, 8, "local addr %s", FormatEIBAddr (addr).c_str());
              return false;
            }
          TRACEPRINTF ((*i)->t, 8, "found addr %s", FormatEIBAddr (addr).c_str());
          return true;
        }
    }

  TRACEPRINTF (t, 8, "unknown addr %s", FormatEIBAddr (addr).c_str());
  return false;
}

bool
Layer3real::hasGroupAddress (eibaddr_t addr, Layer2Ptr l2 UNUSED)
{
  if (addr == 0) // always accept broadcast
    return true;

  ITER(i, layer2)
    if ((*i)->hasGroupAddress (addr))
      return true;

  return false;
}

void
Layer3real::set_client_block (eibaddr_t r_start, int r_len)
{
  client_addrs_start = r_start;
  client_addrs_len = r_len;
  client_addrs_pos = r_len-1; // starts at the first address
  client_addrs.resize(r_len);
  ITER(i,client_addrs)
    *i = false;
}

eibaddr_t
Layer3real::get_client_addr (TracePtr t)
{
  /*
   * Start allocating after the last-assigned address.
   * This leaves a buffer for delayed replies so that they don't get sent
   * to a new client.
   *
   * client_addrs_pos is set to len-1 in set_client_block() so that allocation
   * still starts at the first free address when starting up.
   */
  for (int i = 1; i <= client_addrs_len; i++)
    {
      unsigned int pos = (client_addrs_pos + i) % client_addrs_len;
      if (client_addrs[pos])
        continue;
      eibaddr_t a = client_addrs_start + pos;
      if (a != defaultAddr && !hasAddress (a))
        {
          TRACEPRINTF (t, 3, "Allocate %s", FormatEIBAddr (a).c_str());
          /* remember for next pass */
          client_addrs_pos = pos;
          client_addrs[pos] = true;
          return a;
        }
    }

  /* no more … */
  ERRORPRINTF (t, E_WARNING | 59, "Allocate: no more free addresses!");
  return 0;
}

void
Layer3real::release_client_addr(eibaddr_t addr)
{
  TRACEPRINTF (tr(), 3, "Release %s", FormatEIBAddr (addr).c_str());
  if (addr < client_addrs_start)
    return;
  unsigned int pos = addr - client_addrs_start;
  if (pos >= client_addrs_len)
    return;
  client_addrs[pos] = false;
}

void
Layer3real::trigger_cb (ev::async &w, int revents)
{
  unsigned i;

  while (!buf.isempty())
    {
      LDataPtr l1 = buf.get ();

      Layer2Ptr l2 = l1->l2;

      if (l1->source == 0)
        l1->source = l2->getRemoteAddr();
      if (l1->source == 0)
        l1->source = defaultAddr;
      if (l1->source == defaultAddr) { /* locally generated, do nothing */ }
      // Cases:
      // * address is unknown: associate with input IF not from local range and not programming addr
      // * address is known to input: OK
      // * address is on another 
      else if (hasAddress (l1->source))
        {
          if (! l2->hasAddress (l1->source))
            {
              TRACEPRINTF (l2->t, 3, "Packet not from %d:%s: %s", l2->t->seq, l2->t->name.c_str(), l1->Decode ().c_str());
              goto next;
            }
        }
      else if (client_addrs_start && (l1->source >= client_addrs_start) &&
          (l1->source < client_addrs_start+client_addrs_len))
        { // late arrival to an already-freed client
          TRACEPRINTF (l2->t, 3, "Packet from client: %s", l1->Decode ().c_str());
          goto next;
        }
      else if (l1->source != 0xFFFF)
        l2->addAddress (l1->source);

      if (vbusmonitor.size())
        {
          LBusmonPtr l2 = LBusmonPtr(new L_Busmonitor_PDU (l1->l2));
          l2->pdu.set (l1->ToPacket ());

          ITER(i,vbusmonitor)
            i->cb->send_L_Busmonitor (LBusmonPtr(new L_Busmonitor_PDU (*l2)));
        }
      if (!l1->hopcount)
        {
          TRACEPRINTF (tr(), 3, "Hopcount zero: %s", l1->Decode ().c_str());
          goto next;
        }
      if (l1->hopcount < 7 || !force_broadcast)
        l1->hopcount--;

      if (l1->repeated)
        {
          CArray d1 = l1->ToPacket ();
          ITER (i,ignore)
            if (d1 == i->data)
              {
                TRACEPRINTF (tr(), 9, "Repeated, discarded");
                goto next;
              }
        }
      l1->repeated = 1;
      ignore.push_back((IgnoreInfo){.data = l1->ToPacket (), .end = getTime () + 1000000});
      l1->repeated = 0;

      if (l1->AddrType == IndividualAddress
          && l1->dest == defaultAddr)
        l1->dest = 0;
      TRACEPRINTF (tr(), 3, "Route %s", l1->Decode ().c_str());

      if (l1->AddrType == GroupAddress)
        {
          // This is easy: send to all other L2 which subscribe to the
          // group.
          ITER(i, layer2)
            {
              if (*i == l1->l2)
                continue;
              if (l1->hopcount == 7 || (*i)->hasGroupAddress(l1->dest))
                (*i)->send_L_Data (LDataPtr(new L_Data_PDU (*l1)));
            }
        }
      if (l1->AddrType == IndividualAddress)
        {
	  if (!l1->dest)
	    {
	      // Common problem with things that are not true gateways
	      ERRORPRINTF (l1->l2->t, E_WARNING | 57, "Message without destination. Use the single-node filter ('-B single')?");
	      goto next;
	    }

          // we want to send to the interface on which the address
          // has appeared. If it hasn't been seen yet, we send to all
          // interfaces.
          // Address ~0 is special; it's used for programming
          // so can be on different interfaces. Always broadcast these.
          // Packets to knxd itself aren't forwarded.
          bool found = (l1->dest == defaultAddr);
          if (l1->dest != 0xFFFF)
            ITER(i, layer2)
              {
                if (*i == l1->l2)
                  continue;
                if ((*i)->hasAddress (l1->dest))
                  {
                    found = true;
                    break;
                  }
              }
          ITER (i, layer2)
            {
              if (*i == l1->l2)
                continue;
              if (l1->hopcount == 7 || !found || (*i)->hasAddress (l1->dest))
                (*i)->send_L_Data (LDataPtr(new L_Data_PDU (*l1)));
            }
        }
    next:;
    }

  // Timestamps are ordered, so we scan for the first 
  timestamp_t tm = getTime ();
  ITER (i, ignore)
    if (i->end >= tm)
      {
        ignore.erase (ignore.begin(), i);
        break;
      }
}

void
Layer3real::mtrigger_cb (ev::async &w, int revents)
{
  unsigned i;

  while (!mbuf.isempty())
    {
      LBusmonPtr l1 = mbuf.get ();

      TRACEPRINTF (tr(), 3, "RecvMon %s", l1->Decode ().c_str());
      ITER (i, busmonitor)
        i->cb->send_L_Busmonitor (LBusmonPtr(new L_Busmonitor_PDU (*l1)));
    }
}
