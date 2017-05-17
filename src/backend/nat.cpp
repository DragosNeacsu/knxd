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

#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "nat.h"
#include "layer3.h"

NatL2Filter::NatL2Filter (L2options *opt, Layer2Ptr l2) : Layer23 (l2)
{
  TRACEPRINTF (t, 2, "OpenFilter");
}

bool
NatL2Filter::init (Layer3 *l3)
{
  addr = l3->getDefaultAddr();
  return Layer23::init(l3);
}

NatL2Filter::~NatL2Filter ()
{
  TRACEPRINTF (t, 2, "CloseFilter");
}

Layer2Ptr
NatL2Filter::clone (Layer2Ptr l2)
{
  Layer2Ptr c = Layer2Ptr(new NatL2Filter(NULL, l2));
  // now copy our settings to c. In this case there's nothing to copy,
  // because each interface has its own NAT table.
  return c;
}

void
NatL2Filter::send_L_Data (LDataPtr  l)
{
  /* Sending a packet to this interface: record address pair, clear source */
  if (l->getType () == L_Data)
    {
      if (l->AddrType == IndividualAddress)
        addReverseAddress (l->source, l->dest);
      l->source = addr;
    }
  l2->send_L_Data (std::move(l));
}


void
NatL2Filter::recv_L_Data (LDataPtr  l)
{
  /* Receiving a packet from this interface: reverse-lookup real destination from source */
  if (l->source == addr)
    {
      TRACEPRINTF (t, 5, "drop packet from %s", FormatEIBAddr (l->source).c_str());
      return;
    }
  if (l->AddrType == IndividualAddress)
    l->dest = getDestinationAddress (l->source);
  Layer23::recv_L_Data (std::move(l));
}

void NatL2Filter::addReverseAddress (eibaddr_t src, eibaddr_t dest)
{
  ITER(i,revaddr)
    if (i->dest == dest)
      {
        if (i->src != src)
	  {
	    TRACEPRINTF (t, 5, "from %s to %s", FormatEIBAddr (src).c_str(), FormatEIBAddr (dest).c_str());
            i->src = src;
	  }
        return;
      }

  TRACEPRINTF (t, 5, "from %s to %s", FormatEIBAddr (src).c_str(), FormatEIBAddr (dest).c_str());
  phys_comm srcdest = (phys_comm) { .src=src, .dest=dest };
  revaddr.push_back(srcdest);
}

eibaddr_t NatL2Filter::getDestinationAddress (eibaddr_t src)
{
  ITER(i,revaddr)
    if (i->dest == src)
      return i->src;

  return 0;
}

