/*
    EIBD eib bus access and management daemon
    Copyright (C) 2005-2011 Martin Koegler <mkoegler@auto.tuwien.ac.at>
 
    cEMI support for USB
    Copyright (C) 2013 Meik Felser <felser@cs.fau.de>

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

#ifndef EIB_FT12CEMI_H
#define EIB_FT12CEMI_H

#include "cemi.h"

/** CEMI backend */
class FT12CEMILayer2 : public CEMILayer2
{
  void cmdOpen(); 
public:
  FT12CEMILayer2 (LowLevelDriver * i, L2options *opt) : CEMILayer2(i,opt) {}
  ~FT12CEMILayer2 ();
};

#endif  /* EIB_CEMI_H */
