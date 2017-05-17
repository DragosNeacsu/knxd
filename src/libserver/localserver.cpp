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

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <errno.h>
#include "localserver.h"

LocalServer::LocalServer (TracePtr tr, const char *path):
Server (tr)
{
  struct sockaddr_un addr;
  TRACEPRINTF (tr, 8, "OpenLocalSocket %s", path);
  addr.sun_family = AF_LOCAL;
  strncpy (addr.sun_path, path, sizeof (addr.sun_path));

  fd = socket (AF_LOCAL, SOCK_STREAM, 0);
  if (fd == -1)
    {
      ERRORPRINTF (tr, E_ERROR | 15, "OpenLocalSocket %s: socket: %s", path, strerror(errno));
      return;
    }

  if (bind (fd, (struct sockaddr *) &addr, sizeof (addr)) == -1)
    {
      /* 
       * dead file? 
       */
      if (errno == EADDRINUSE)
        {
          if (connect(fd, (struct sockaddr *) &addr, sizeof (addr)) == 0)
            {
          ex:
              ERRORPRINTF (tr, E_ERROR | 16, "OpenLocalSocket %s: bind: %s", path, strerror(errno));
              close (fd);
              fd = -1;
              return;
            }
          else if (errno == ECONNREFUSED)
            {
              unlink (path);
              if (bind (fd, (struct sockaddr *) &addr, sizeof (addr)) == -1)
                goto ex;
            }
          else
            {
              ERRORPRINTF (tr, E_ERROR | 18, "Existing socket %s: connect: %s", path, strerror(errno));
              close (fd);
              fd = -1;
              return;
            }
        }
    }

  if (listen (fd, 10) == -1)
    {
      ERRORPRINTF (tr, E_ERROR | 17, "OpenLocalSocket %s: listen: %s", path, strerror(errno));
      close (fd);
      fd = -1;
      return;
    }

  this->path = path;
  TRACEPRINTF (tr, 8, "LocalSocket opened");
}

LocalServer::~LocalServer ()
{
  if (path)
    unlink (path);
}
