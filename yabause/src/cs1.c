/*  Copyright 2003-2005 Guillaume Duhamel
    Copyright 2005 Theo Berkau

    This file is part of Yabause.

    Yabause is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Yabause is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Yabause; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

/*! \file cs1.c
    \brief A-Bus CS1 emulation functions.
*/

#include <stdlib.h>
#include "cs1.h"
#include "cs0.h"

/* The Saturn cart ID register lives at the top of A-bus CS1 (0x04FFFFFC..FF).
   An ST-V ROM board puts its 315-5881 protection/decrypt port there instead, so
   the ID special cases below must not apply: they returned
   0xFF00FF00|cartid (0xFFFFFFFF while cartid is unset) for every decrypted read
   the protected titles make, which is why they filled a buffer with 0xFFFFFFFF,
   compared it against the real data and hung in a bra-self loop.  Kronos ships
   exactly this change (its cs1.c has no cart-ID special case at all). */
#define CS1_CART_ID_APPLIES() \
   (CartridgeArea != NULL && CartridgeArea->carttype != CART_ROMSTV)

//////////////////////////////////////////////////////////////////////////////

u8 FASTCALL Cs1ReadByte(u32 addr)
{
   addr &= 0xFFFFFF;
               
   if (addr == 0xFFFFFF && CS1_CART_ID_APPLIES())
      return CartridgeArea->cartid;

   return CartridgeArea->Cs1ReadByte(addr);
}

//////////////////////////////////////////////////////////////////////////////

u16 FASTCALL Cs1ReadWord(u32 addr)
{
   addr &= 0xFFFFFF;

   if (addr == 0xFFFFFE && CS1_CART_ID_APPLIES())
      return (0xFF00 | CartridgeArea->cartid);

   return CartridgeArea->Cs1ReadWord(addr);
}

//////////////////////////////////////////////////////////////////////////////

u32 FASTCALL Cs1ReadLong(u32 addr)
{
   addr &= 0xFFFFFF;

   if (addr == 0xFFFFFC && CS1_CART_ID_APPLIES())
      return (0xFF00FF00 | (CartridgeArea->cartid << 16) | CartridgeArea->cartid);

   return CartridgeArea->Cs1ReadLong(addr);
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Cs1WriteByte(u32 addr, u8 val)
{
   addr &= 0xFFFFFF;

   if (addr == 0xFFFFFF && CS1_CART_ID_APPLIES())
      return;

   CartridgeArea->Cs1WriteByte(addr, val);
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Cs1WriteWord(u32 addr, u16 val)
{
   addr &= 0xFFFFFF;

   if (addr == 0xFFFFFE && CS1_CART_ID_APPLIES())
      return;

   CartridgeArea->Cs1WriteWord(addr, val);
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL Cs1WriteLong(u32 addr, u32 val)
{
   addr &= 0xFFFFFF;

   if (addr == 0xFFFFFC && CS1_CART_ID_APPLIES())
      return;

   CartridgeArea->Cs1WriteLong(addr, val);
}

//////////////////////////////////////////////////////////////////////////////
