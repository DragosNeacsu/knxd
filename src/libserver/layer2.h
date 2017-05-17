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

#ifndef LAYER2_H
#define LAYER2_H

#include "layer2common.h"
#include "common.h"
#include "lpdu.h"
#include <memory>

class Layer3;

class Layer2shim : public std::enable_shared_from_this<Layer2shim>
{
public:
  Layer2shim(L2options *opt, TracePtr tr);
  virtual ~Layer2shim();
  virtual const char *Name() { return "?-L2"; }

  /** debug output */
  TracePtr t;
  /** our layer-3 (to send packets to) */
  Layer3 *l3;
  /** connect to the "real" layer3 */
  virtual bool init (Layer3 *l3);
  /** disconnect from L3, shutdown, whatever */
  virtual void stop ();

  /** sends a Layer 2 frame to this interface */
  virtual void send_L_Data (LDataPtr l) = 0;

  /** try to add the individual addr to the device, return true if successful */
  virtual bool addAddress (eibaddr_t addr) = 0;
  /** try to add the group address addr to the device, return true if successful */
  virtual bool addGroupAddress (eibaddr_t addr) = 0;
  /** try to remove the individual address addr to the device, return true if successful */
  virtual bool removeAddress (eibaddr_t addr) = 0;
  /** try to remove the group address addr to the device, return true if successful */
  virtual bool removeGroupAddress (eibaddr_t addr) = 0;
  /** individual address known? */
  virtual bool hasAddress (eibaddr_t addr) = 0;
  /** group address known? */
  virtual bool hasGroupAddress (eibaddr_t addr) = 0;
  /** my remote address, if any? */
  virtual eibaddr_t getRemoteAddr() = 0;

  /** try to enter the busmonitor mode, return true if successful */
  virtual bool enterBusmonitor () = 0;
  /** try to leave the busmonitor mode, return true if successful */
  virtual bool leaveBusmonitor () = 0;

  /** try to enter the normal operation mode, return true if successful */
  virtual bool Open () = 0;
  /** try to leave the normal operation mode, return true if successful */
  virtual bool Close () = 0;
};

/** generic interface for an Layer 2 driver */
class Layer2 : public Layer2shim
{
public:
  Layer2 (L2options *opt, TracePtr tr = 0);

protected:
  /** my individual addresses */
  Array < eibaddr_t > indaddr;
  /** my group addresses */
  Array < eibaddr_t > groupaddr;

  bool allow_monitor;

  /** auto-assigned. NON-bus connections only! */
  eibaddr_t remoteAddr = 0;

  busmode_t mode;

public:
  /** implement all of Layer2shim */
  virtual bool addAddress (eibaddr_t addr);
  virtual bool addGroupAddress (eibaddr_t addr);
  virtual bool removeAddress (eibaddr_t addr);
  virtual bool removeGroupAddress (eibaddr_t addr);
  virtual bool hasAddress (eibaddr_t addr);
  virtual bool hasGroupAddress (eibaddr_t addr);
  eibaddr_t getRemoteAddr() { return remoteAddr; };

  virtual bool enterBusmonitor ();
  virtual bool leaveBusmonitor ();

  virtual bool Open ();
  virtual bool Close ();
};



/** Layer2 class for network interfaces
 * without "real" hardware behind them
 */
class Layer2mixin:public Layer2
{
public:
  Layer2mixin (TracePtr tr) : Layer2 (NULL, tr)
    {
      t = tr;
    }
  virtual void send_L_Data (LDataPtr l) = 0;
  bool enterBusmonitor () { return 0; }
  bool leaveBusmonitor () { return 0; }
  bool Open () { return 1; }
  bool Close () { return 1; }
};

class Layer2single:public Layer2mixin
{
public:
  Layer2single (TracePtr tr) : Layer2mixin (tr) {};
  bool hasAddress(eibaddr_t addr);
  bool addAddress(eibaddr_t addr);
  bool removeAddress(eibaddr_t addr);
};

/** Layer2 class for interfaces
 * which don't ever do anything,
 * e.g. server sockets used to accept() connections
 *
 * TODO these should probably be wholly separate
 */
class Layer2virtual:public Layer2mixin
{
public:
  Layer2virtual (TracePtr tr) : Layer2mixin (tr) { }
  void send_L_Data (LDataPtr l) { }
  bool hasAddress (eibaddr_t addr UNUSED) { return false; }
  bool addAddress (eibaddr_t addr UNUSED) { return false; }
  bool hasGroupAddress (eibaddr_t addr UNUSED) { return false; }
  bool addGroupAddress (eibaddr_t addr UNUSED) { return false; }
  bool removeAddress (eibaddr_t addr UNUSED) { return false; }
  bool removeGroupAddress (eibaddr_t addr UNUSED) { return false; }
};

#endif
