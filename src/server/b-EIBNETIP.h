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

#ifndef C_EIBNETIP_H
#define C_EIBNETIP_H

#include <stdlib.h>
#include "eibnetrouter.h"

#define EIBNETIP_URL "ip:[multicast_addr[:port[:interface]]]\n"
#define EIBNETIP_DOC "ip uses multicast to talk to an EIBnet/IP gateway. The gateway must be configured to route your addresses to multicast.\n\n"

#define EIBNETIP_PREFIX "ip"
#define EIBNETIP_CREATE eibnetip_Create

inline Layer2Ptr
eibnetip_Create (const char *dev, L2options *opt)
{
  if (!*dev)
    return std::shared_ptr<EIBNetIPRouter>(new EIBNetIPRouter ("224.0.23.12", 3671, nullptr, opt));
  char *a = strdup (dev);
  char *b;
  char *c = nullptr;
  int port;
  Layer2Ptr cl;
  for (b = a; *b; b++)
    if (*b == ':')
      break;
  if (*b == ':')
    {
      *b++ = 0;
      if (!*b || *b == ':')
        {
          port = 3671;
          c = b;
        }
      else
        port = strtoul (b, &c, 0);
      if (*c == ':')
        c++;
      else 
        c = nullptr;
    }
  else
    port = 3671;
  cl = std::shared_ptr<EIBNetIPRouter>(new EIBNetIPRouter (*a ? a : "224.0.23.12", port, c, opt));
  free (a);
  return cl;
}

#endif
