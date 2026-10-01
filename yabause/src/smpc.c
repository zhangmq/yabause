/*  Copyright 2003-2005 Guillaume Duhamel
    Copyright 2004-2006 Theo Berkau

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
/*
        Copyright 2019 devMiyax(smiyaxdev@gmail.com)

This file is part of YabaSanshiro.

        YabaSanshiro is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

YabaSanshiro is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
along with YabaSanshiro; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

/*! \file smpc.c
    \brief SMPC emulation functions.
*/

#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include "smpc.h"
#include "eeprom.h"
#include "cs2.h"
#include "debug.h"
#include "peripheral.h"
#include "scsp.h"
#include "scu.h"
#include "sh2core.h"
#include "vdp1.h"
#include "vdp2.h"
#include "yabause.h"
#include "movie.h"

#ifdef _arch_dreamcast
# include "dreamcast/localtime.h"
#endif
#ifdef PSP
# include "psp/localtime.h"
#endif

Smpc * SmpcRegs;
u8 * SmpcRegsT;
SmpcInternal * SmpcInternalVars = NULL;
int intback_wait_for_line = 0;
u8 bustmp = 0;

/* ST-V: the 68k sound CPU is started/stopped through PDR2 bit 0x10
   (ported from libretro/yabause@kronos smpc.c; see critical fact 26). */
static u8 m_pdr1_readback = 0;
static u8 m_pdr2_readback = 0;
#ifdef YAB_STV_DEBUG
static unsigned int stv_smpc_dbg = 0;
#endif

//////////////////////////////////////////////////////////////////////////////

int SmpcInit(u8 regionid, int clocksync, u32 basetime) {
   if ((SmpcRegsT = (u8 *) calloc(1, sizeof(Smpc))) == NULL)
      return -1;
 
   SmpcRegs = (Smpc *) SmpcRegsT;

   if ((SmpcInternalVars = (SmpcInternal *) calloc(1, sizeof(SmpcInternal))) == NULL)
      return -1;
  
   SmpcInternalVars->regionsetting = regionid;
   SmpcInternalVars->regionid = regionid;
   SmpcInternalVars->clocksync = clocksync;
   SmpcInternalVars->basetime = basetime ? basetime : time(NULL);

   return 0;
}

int SmpcSetClockSync(int clocksync, u32 basetime) {
  if (SmpcInternalVars == NULL) return -1;
  SmpcInternalVars->clocksync = clocksync;
  SmpcInternalVars->basetime = basetime ? basetime : time(NULL);
}

//////////////////////////////////////////////////////////////////////////////

void SmpcDeInit(void) {
   if (SmpcRegsT)
      free(SmpcRegsT);
   SmpcRegsT = NULL;

   if (SmpcInternalVars)
      free(SmpcInternalVars);
   SmpcInternalVars = NULL;
}

//////////////////////////////////////////////////////////////////////////////

void SmpcRecheckRegion(void) {
   if (SmpcInternalVars == NULL)
      return;

   if (SmpcInternalVars->regionsetting == REGION_AUTODETECT)
   {
      // Time to autodetect the region using the cd block
      SmpcInternalVars->regionid = Cs2GetRegionID();

      // Since we couldn't detect the region from the CD, let's assume
      // it's japanese
      if (SmpcInternalVars->regionid == 0)
         SmpcInternalVars->regionid = 1;
   }
   else
      Cs2GetIP(0);
}

//////////////////////////////////////////////////////////////////////////////

void SmpcReset(void) {
   memset((void *)SmpcRegs, 0, sizeof(Smpc));
   memset((void *)SmpcInternalVars->SMEM, 0, 4);

   SmpcRecheckRegion();

   SmpcInternalVars->dotsel = 0;
   SmpcInternalVars->mshnmi = 0;
   SmpcInternalVars->sysres = 0;
   SmpcInternalVars->sndres = 0;
   SmpcInternalVars->cdres = 0;
   SmpcInternalVars->resd = 1;
   SmpcInternalVars->ste = 0;
   SmpcInternalVars->resb = 0;

   SmpcInternalVars->intback=0;
   SmpcInternalVars->intbackIreg0=0;
   SmpcInternalVars->firstPeri=0;

   SmpcInternalVars->timing=0;

   memset((void *)&SmpcInternalVars->port1, 0, sizeof(PortData_struct));
   memset((void *)&SmpcInternalVars->port2, 0, sizeof(PortData_struct));
   /* Kronos sets this in SmpcReset; without it OREG[31] stays 0 after the
      memset and the ST-V BIOS never sees the 0xD it polls for. */
   SmpcRegs->OREG[31] = 0xD;
}

//////////////////////////////////////////////////////////////////////////////

static void SmpcSSHON(void) {
   YabauseStartSlave();
}

//////////////////////////////////////////////////////////////////////////////

static void SmpcSSHOFF(void) {
   YabauseStopSlave();
}

//////////////////////////////////////////////////////////////////////////////

static void SmpcSNDON(void) {
   if (!yabsys.isSTV) M68KStart(); // C68k wire is controlled by pdr2 on STV
   SmpcRegs->OREG[31] = 0x6;
}

//////////////////////////////////////////////////////////////////////////////

/* SYSRES: on the Saturn this would reset the console; the SMPC registers
   themselves survive, so OREG[31] becomes 0x0D and the BIOS's next boot sees
   it and continues past the check at 0xd24.  Kronos does exactly this
   (SmpcSYSRES + SF = 0) and it was missing from our port. */
static void SmpcSYSRES(void) {
   SmpcRegs->OREG[31] = 0xD;
}

static void SmpcSNDOFF(void) {
   if (!yabsys.isSTV) M68KStop(); // C68k wire is controlled by pdr2 on STV
   SmpcRegs->OREG[31] = 0x7;
}

//////////////////////////////////////////////////////////////////////////////

void SmpcCKCHG352(void) {
   // Reset VDP1, VDP2, SCU, and SCSP
   Vdp1Reset();  
   Vdp2Reset();  
   ScuReset(0);  
   ScspReset();  

   // Clear VDP1/VDP2 ram

   YabauseStopSlave();

   // change clock
   YabauseChangeTiming(CLKTYPE_28MHZ);

   // Set DOTSEL
   SmpcInternalVars->dotsel = 1;

   // Send NMI
   SH2NMI(MSH2);
}

//////////////////////////////////////////////////////////////////////////////

void SmpcCKCHG320(void) {
   // Reset VDP1, VDP2, SCU, and SCSP
   Vdp1Reset();  
   Vdp2Reset();  
   ScuReset(0);  
   ScspReset();  

   // Clear VDP1/VDP2 ram

   YabauseStopSlave();

   // change clock
   YabauseChangeTiming(CLKTYPE_26MHZ);

   // Set DOTSEL
   SmpcInternalVars->dotsel = 0;

   // Send NMI
   SH2NMI(MSH2);
}

struct movietime {

	int tm_year;
	int tm_wday;
	int tm_mon;
	int tm_mday;
	int tm_hour;
	int tm_min;
	int tm_sec;
};

static struct movietime movietime;
int totalseconds;
int noon= 43200;

//////////////////////////////////////////////////////////////////////////////

static void SmpcINTBACKStatus(void) {
   // return time, cartidge, zone, etc. data
   int i;
   struct tm times;
   u8 year[4];
   time_t tmp;

   SmpcRegs->OREG[0] = 0x80 | (SmpcInternalVars->resd << 6);   // goto normal startup
   //SmpcRegs->OREG[0] = 0x0 | (SmpcInternalVars->resd << 6);  // goto setclock/setlanguage screen
    
   // write time data in OREG1-7
   if (SmpcInternalVars->clocksync) {
      tmp = SmpcInternalVars->basetime + ((u64)yabsys.frame_count * 1001 / 60000);
   } else {
      tmp = time(NULL);
   }
#ifdef WIN32
   memcpy(&times, localtime(&tmp), sizeof(times));
#elif defined(_arch_dreamcast) || defined(PSP)
   internal_localtime_r(&tmp, &times);
#else
   localtime_r(&tmp, &times);
#endif
   year[0] = (1900 + times.tm_year) / 1000;
   year[1] = ((1900 + times.tm_year) % 1000) / 100;
   year[2] = (((1900 + times.tm_year) % 1000) % 100) / 10;
   year[3] = (((1900 + times.tm_year) % 1000) % 100) % 10;
   SmpcRegs->OREG[1] = (year[0] << 4) | year[1];
   SmpcRegs->OREG[2] = (year[2] << 4) | year[3];
   SmpcRegs->OREG[3] = (times.tm_wday << 4) | (times.tm_mon + 1);
   SmpcRegs->OREG[4] = ((times.tm_mday / 10) << 4) | (times.tm_mday % 10);
   SmpcRegs->OREG[5] = ((times.tm_hour / 10) << 4) | (times.tm_hour % 10);
   SmpcRegs->OREG[6] = ((times.tm_min / 10) << 4) | (times.tm_min % 10);
   SmpcRegs->OREG[7] = ((times.tm_sec / 10) << 4) | (times.tm_sec % 10);

   if(Movie.Status == Recording || Movie.Status == Playback) {
	   movietime.tm_year=0x62;
	   movietime.tm_wday=0x04;
	   movietime.tm_mday=0x01;
	   movietime.tm_mon=0;
	   totalseconds = ((framecounter / 60) + noon);

	   movietime.tm_sec=totalseconds % 60;
	   movietime.tm_min=totalseconds/60;
	   movietime.tm_hour=movietime.tm_min/60;

	   //convert to sane numbers
	   movietime.tm_min=movietime.tm_min % 60;
	   movietime.tm_hour=movietime.tm_hour % 24;

	   year[0] = (1900 + movietime.tm_year) / 1000;
	   year[1] = ((1900 + movietime.tm_year) % 1000) / 100;
	   year[2] = (((1900 + movietime.tm_year) % 1000) % 100) / 10;
	   year[3] = (((1900 + movietime.tm_year) % 1000) % 100) % 10;
	   SmpcRegs->OREG[1] = (year[0] << 4) | year[1];
	   SmpcRegs->OREG[2] = (year[2] << 4) | year[3];
	   SmpcRegs->OREG[3] = (movietime.tm_wday << 4) | (movietime.tm_mon + 1);
	   SmpcRegs->OREG[4] = ((movietime.tm_mday / 10) << 4) | (movietime.tm_mday % 10);
	   SmpcRegs->OREG[5] = ((movietime.tm_hour / 10) << 4) | (movietime.tm_hour % 10);
	   SmpcRegs->OREG[6] = ((movietime.tm_min / 10) << 4) | (movietime.tm_min % 10);
	   SmpcRegs->OREG[7] = ((movietime.tm_sec / 10) << 4) | (movietime.tm_sec % 10);
   }

   // write cartidge data in OREG8
   SmpcRegs->OREG[8] = 0; // FIXME : random value
    
   // write zone data in OREG9 bits 0-7
   // 1 -> japan
   // 2 -> asia/ntsc
   // 4 -> north america
   // 5 -> central/south america/ntsc
   // 6 -> corea
   // A -> asia/pal
   // C -> europe + others/pal
   // D -> central/south america/pal
   SmpcRegs->OREG[9] = SmpcInternalVars->regionid;

   // system state, first part in OREG10, bits 0-7
   // bit | value  | comment
   // ---------------------------
   // 7   | 0      |
   // 6   | DOTSEL |
   // 5   | 1      |
   // 4   | 1      |
   // 3   | MSHNMI |
   // 2   | 1      |
   // 1   | SYSRES | 
   // 0   | SNDRES |
   SmpcRegs->OREG[10] = 0x34|(SmpcInternalVars->dotsel<<6)|(SmpcInternalVars->mshnmi<<3)|(SmpcInternalVars->sysres<<1)|SmpcInternalVars->sndres;
    
   // system state, second part in OREG11, bit 6
   // bit 6 -> CDRES
   SmpcRegs->OREG[11] = SmpcInternalVars->cdres << 6; // FIXME
    
   // SMEM
   for(i = 0;i < 4;i++)
      SmpcRegs->OREG[12+i] = SmpcInternalVars->SMEM[i];
    
   SmpcRegs->OREG[31] = 0x10; // set to intback command
}

//////////////////////////////////////////////////////////////////////////////

static void SmpcINTBACKPeripheral(void) {
  {  /* Probe: what the SMPC hands the game (START travels through here). */
     static int on=-1; static FILE *fp=NULL;
     if(on<0){ on=(access("/mnt/sdcard/smpc.on",F_OK)==0); if(on) fp=fopen("/mnt/sdcard/smpcresp.log","w"); }
     if(fp){
       fprintf(fp,"frame=%u SR=%02X SF=%02X COMREG=%02X OREG=",(unsigned)yabsys.frame_count,
               SmpcRegs->SR,SmpcRegs->SF,SmpcRegs->COMREG);
       for(int k=0;k<12;k++) fprintf(fp,"%02X",SmpcRegs->OREG[k]);
       fprintf(fp," SMEM=");
       for(int k=0;k<12;k++) fprintf(fp,"%02X",SmpcInternalVars->SMEM[k]);
       fprintf(fp,"\n"); fflush(fp);
     }
  }
  int oregoffset;
  PortData_struct *port1, *port2;

  if (SmpcInternalVars->firstPeri)
    SmpcRegs->SR = 0xC0 | (SmpcRegs->IREG[1] >> 4);
  else
    SmpcRegs->SR = 0x80 | (SmpcRegs->IREG[1] >> 4);

  SmpcInternalVars->firstPeri = 0;

  /* Port Status:
  0x04 - Sega-tap is connected
  0x16 - Multi-tap is connected
  0x21-0x2F - Clock serial peripheral is connected
  0xF0 - Not Connected or Unknown Device
  0xF1 - Peripheral is directly connected */

  /* PeripheralID:
  0x02 - Digital Device Standard Format
  0x13 - Racing Device Standard Format
  0x15 - Analog Device Standard Format
  0x23 - Pointing Device Standard Format
  0x23 - Shooting Device Standard Format
  0x34 - Keyboard Device Standard Format
  0xE1 - Mega Drive 3-Button Pad
  0xE2 - Mega Drive 6-Button Pad
  0xE3 - Saturn Mouse
  0xFF - Not Connected */

  /* Special Notes(for potential future uses):

  If a peripheral is disconnected from a port, you only return 1 byte for
  that port(which is the port status 0xF0), at the next OREG you then return
  the port status of the next port.

  e.g. If Port 1 has nothing connected, and Port 2 has a controller
       connected:

  OREG0 = 0xF0
  OREG1 = 0xF1
  OREG2 = 0x02
  etc.
  */

  oregoffset=0;

  if (SmpcInternalVars->port1.size == 0 && SmpcInternalVars->port2.size == 0)
  {
     // Request data from the Peripheral Interface
     port1 = &PORTDATA1;
     port2 = &PORTDATA2;
     memcpy(&SmpcInternalVars->port1, port1, sizeof(PortData_struct));
     memcpy(&SmpcInternalVars->port2, port2, sizeof(PortData_struct));
     PerFlush(&PORTDATA1);
     PerFlush(&PORTDATA2);
     SmpcInternalVars->port1.offset = 0;
     SmpcInternalVars->port2.offset = 0;
     LagFrameFlag=0;
  }

  // Port 1
  if (SmpcInternalVars->port1.size > 0)
  {
     if ((SmpcInternalVars->port1.size-SmpcInternalVars->port1.offset) < 32)
     {
        memcpy(SmpcRegs->OREG, SmpcInternalVars->port1.data+SmpcInternalVars->port1.offset, SmpcInternalVars->port1.size-SmpcInternalVars->port1.offset);
        oregoffset += SmpcInternalVars->port1.size-SmpcInternalVars->port1.offset;
        SmpcInternalVars->port1.size = 0;
     }
     else
     {
        memcpy(SmpcRegs->OREG, SmpcInternalVars->port1.data, 32);
        oregoffset += 32;
        SmpcInternalVars->port1.offset += 32;
     }
  }
  // Port 2
  if (SmpcInternalVars->port2.size > 0 && oregoffset < 32)
  {
     if ((SmpcInternalVars->port2.size-SmpcInternalVars->port2.offset) < (32 - oregoffset))
     {
        memcpy(SmpcRegs->OREG + oregoffset, SmpcInternalVars->port2.data+SmpcInternalVars->port2.offset, SmpcInternalVars->port2.size-SmpcInternalVars->port2.offset);
        SmpcInternalVars->port2.size = 0;
     }
     else
     {
        memcpy(SmpcRegs->OREG + oregoffset, SmpcInternalVars->port2.data, 32 - oregoffset);
        SmpcInternalVars->port2.offset += 32 - oregoffset;
     }
  }

/*
  Use this as a reference for implementing other peripherals
  // Port 1
  SmpcRegs->OREG[0] = 0xF1; //Port Status(Directly Connected)
  SmpcRegs->OREG[1] = 0xE3; //PeripheralID(Shuttle Mouse)
  SmpcRegs->OREG[2] = 0x00; //First Data
  SmpcRegs->OREG[3] = 0x00; //Second Data
  SmpcRegs->OREG[4] = 0x00; //Third Data

  // Port 2
  SmpcRegs->OREG[5] = 0xF0; //Port Status(Not Connected)
*/
}

//////////////////////////////////////////////////////////////////////////////

static void SmpcINTBACK(void) {
  {  /* Probe: every INTBACK request the game makes (which branch is chosen and
      * with what arguments).  Enabled while /mnt/sdcard/smpc.on exists. */
     static int on=-1; static FILE *fp=NULL;
     if(on<0){ on=(access("/mnt/sdcard/smpc.on",F_OK)==0); if(on) fp=fopen("/mnt/sdcard/smpcreq.log","w"); }
     if(fp){
       fprintf(fp,"frame=%u IREG=%02X%02X%02X%02X SR=%02X SF=%02X firstPeri=%d\n",
               (unsigned)yabsys.frame_count,SmpcRegs->IREG[0],SmpcRegs->IREG[1],
               SmpcRegs->IREG[2],SmpcRegs->IREG[3],SmpcRegs->SR,SmpcRegs->SF,
               (int)SmpcInternalVars->firstPeri); fflush(fp);
     }
  }
   SmpcRegs->SF = 1;
   /* Peripheral (pad) data must be available in a CONTINUOUS mode: Kronos keeps
    * returning it while firstPeri == 1, whereas this tree cleared "intback" at
    * every INTBACK end and then went silent for requests that ask for neither
    * status bit 0 nor peripheral data -- measured: ZERO peripheral responses in
    * a whole run, which is why the pad (and START) never reached the game. */
   if (SmpcInternalVars->firstPeri == 1) {
      SmpcINTBACKPeripheral();
      ScuSendSystemManager();
      return;
   }

   //we think rayman sets 0x40 so that it breaks the intback command immediately when it blocks, 
   //rather than having to set 0x40 in response to an interrupt
   if ((SmpcInternalVars->intbackIreg0 = (SmpcRegs->IREG[0] & 1))) {
      // Return non-peripheral data
      SmpcInternalVars->firstPeri = (SmpcRegs->IREG[1] & 0x8) >> 3; // only if the program wants peripheral data
      SmpcInternalVars->intback = (SmpcRegs->IREG[1] & 0x8) >> 3;
      SmpcINTBACKStatus();
      SmpcRegs->SR = 0x4F | (SmpcInternalVars->intback << 5); // the low nibble is undefined(or 0xF)
      ScuSendSystemManager();
      return;
   }
   if (SmpcRegs->IREG[1] & 0x8) {
      SmpcInternalVars->firstPeri = 1;
      SmpcInternalVars->intback = 1;
      SmpcRegs->SR = 0x40;
      SmpcINTBACKPeripheral();
      SmpcRegs->OREG[31] = 0x10; // may need to be changed
      ScuSendSystemManager();
      return;
   }
}

//////////////////////////////////////////////////////////////////////////////

void SmpcINTBACKEnd(void) {
   SmpcInternalVars->intback = 0;
}

//////////////////////////////////////////////////////////////////////////////

static void SmpcSETSMEM(void) {
   int i;

   for(i = 0;i < 4;i++)
      SmpcInternalVars->SMEM[i] = SmpcRegs->IREG[i];

   SmpcRegs->OREG[31] = 0x17;
}

//////////////////////////////////////////////////////////////////////////////

static void SmpcNMIREQ(void) {
   SH2SendInterrupt(MSH2, 0xB, 16);
   SmpcRegs->OREG[31] = 0x18;
}

//////////////////////////////////////////////////////////////////////////////

void SmpcResetButton(void) {
   // If RESD isn't set, send an NMI request to the MSH2.
   if (SmpcInternalVars->resd)
      return;

   SH2SendInterrupt(MSH2, 0xB, 16);
}

//////////////////////////////////////////////////////////////////////////////

static void SmpcRESENAB(void) {
  SmpcInternalVars->resd = 0;
  SmpcRegs->OREG[31] = 0x19;
}

//////////////////////////////////////////////////////////////////////////////

static void SmpcRESDISA(void) {
  SmpcInternalVars->resd = 1;
  SmpcRegs->OREG[31] = 0x1A;
}

//////////////////////////////////////////////////////////////////////////////

void SmpcExec(s32 t) {
   if (SmpcInternalVars->timing > 0) {

      if (intback_wait_for_line)
      {
         if (yabsys.LineCount == 207)
         {
            SmpcInternalVars->timing = -1;
            intback_wait_for_line = 0;
         }
      }

      SmpcInternalVars->timing -= t;
      if (SmpcInternalVars->timing <= 0) {
#ifdef YAB_STV_DEBUG
         if (yabsys.isSTV) printf("[SMPCDBG] exec COMREG=%02x\n", SmpcRegs->COMREG);
#endif
         switch(SmpcRegs->COMREG) {
            case 0x0:
               SMPCLOG("smpc\t: MSHON not implemented\n");
               SmpcRegs->OREG[31] = 0x0;
               SmpcRegs->SF = 0;
               break;
            case 0x2:
               SMPCLOG("smpc\t: SSHON\n");
               SmpcSSHON();
               break;
            case 0x3:
               SMPCLOG("smpc\t: SSHOFF\n");
               SmpcSSHOFF();
               break;
            case 0x6:
               SMPCLOG("smpc\t: SNDON\n");
               SmpcSNDON();
               break;
            case 0x7:
               SMPCLOG("smpc\t: SNDOFF\n");
               SmpcSNDOFF();
               break;
            case 0x8:
               SMPCLOG("smpc\t: CDON not implemented\n");
               SmpcRegs->SF = 0;
               break;
            case 0x9:
               SMPCLOG("smpc\t: CDOFF not implemented\n");
               SmpcRegs->SF = 0;
               break;
            case 0xD:
               SMPCLOG("smpc\t: SYSRES not implemented\n");
               SmpcSYSRES();
               SmpcRegs->SF = 0;
               break;
            case 0xE:
               SMPCLOG("smpc\t: CKCHG352\n");
               SmpcCKCHG352();
               break;
            case 0xF:
               SMPCLOG("smpc\t: CKCHG320\n");
               SmpcCKCHG320();
               break;
            case 0x10:
               SMPCLOG("smpc\t: INTBACK\n");
               SmpcINTBACK();
               break;
            case 0x17:
               SMPCLOG("smpc\t: SETSMEM\n");
               SmpcSETSMEM();
               break;
            case 0x18:
               SMPCLOG("smpc\t: NMIREQ\n");
               SmpcNMIREQ();
               break;
            case 0x19:
               SMPCLOG("smpc\t: RESENAB\n");
               SmpcRESENAB();
               break;
            case 0x1A:
               SMPCLOG("smpc\t: RESDISA\n");
               SmpcRESDISA();
               break;
            default:
               SMPCLOG("smpc\t: Command %02X not implemented\n", SmpcRegs->COMREG);
               break;
         }
  
         SmpcRegs->SF = 0;
      }
   }
}

//////////////////////////////////////////////////////////////////////////////

/* Probe wrapper: log every SMPC register read (which registers the game polls
 * and what it gets).  Enabled while /mnt/sdcard/smpc.on exists. */
static u8 FASTCALL SmpcReadByte_inner(u32 addr);
u8 FASTCALL SmpcReadByte(u32 addr) {
   u8 v = SmpcReadByte_inner(addr);
   {
      static int on=-1; static FILE *fp=NULL;
      if(on<0){ on=(access("/mnt/sdcard/smpc.on",F_OK)==0); if(on) fp=fopen("/mnt/sdcard/smpcread.log","w"); }
      if(fp){ fprintf(fp,"frame=%u addr=%02X val=%02X\n",(unsigned)yabsys.frame_count,addr,v); fflush(fp); }
   }
   return v;
}
static u8 FASTCALL SmpcReadByte_inner(u32 addr) {
   addr &= 0x7F;
   if (addr == 0x05F && yabsys.isSTV) {
      /* Return the real OREG[31].  A hardcoded 0xF0 here (the old hack) made
         the BIOS boot but also masked every handshake value the game polls
         for (0x10/0x17/0x18/0x19/0x1A), so coin credits never updated. */
      return SmpcRegs->OREG[31];
   }
     if (addr == 0x077) {
        /* PDR2 read-back.  Kronos reads the EEPROM DO bit here (not from PDR1):
           when DDR2 is 0x18 the byte carries eeprom_do_read() in bit 0.  The ST-V
           BIOS bit-bangs the EEPROM through PDR1 and polls the DO line through
           PDR2, so without this it never sees a 1. */
        if ((SmpcRegs->DDR[1] & 0x7F) == 0x18) {
           return (u8)((((0x67 & ~0x19) | 0x18 | (eeprom_do_read() << 0)) & ~SmpcRegs->DDR[1]) | m_pdr2_readback);
        }
        return SmpcRegsT[addr >> 1];
     }
     if (addr == 0x075) {
        /* PDR1 read-back: the ST-V BIOS polls this for the EEPROM DO bit (bit 0).
           Ported from Kronos -- without it the poll at 0x4ed8 never sees a 1. */
        if ((SmpcRegs->DDR[0] & 0x7F) == 0x3f) {
           return (u8)((((0x40 & 0x40) | 0x3f) & ~SmpcRegs->DDR[0]) | m_pdr1_readback);
        }
        return SmpcRegsT[addr >> 1];
     }
   if (addr == 0x063) {
     /* Kronos semantics: the 0x63 read returns the register array byte with the
        SF flag in bit 0, NOT the last written byte.  Measured at frame 8: the
        two builds hand the BIOS completely different handshake values here
        (old: 10 01 02 02 01 1A 0E ... ; ported: 00 01 00 00 01 00 ...), and that
        first divergence is where the game decides whether to use INTBACK. */
     bustmp = SmpcRegsT[addr >> 1] & 0xFE;
     bustmp |= SmpcRegs->SF;
     return bustmp;
   }
   return SmpcRegsT[addr >> 1];
}

//////////////////////////////////////////////////////////////////////////////

u16 FASTCALL SmpcReadWord(USED_IF_SMPC_DEBUG u32 addr) {
   // byte access only
   SMPCLOG("smpc\t: SMPC register read word - %08X\n", addr);
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

u32 FASTCALL SmpcReadLong(USED_IF_SMPC_DEBUG u32 addr) {
   // byte access only
   SMPCLOG("smpc\t: SMPC register read long - %08X\n", addr);
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

static void SmpcSetTiming(void) {
   switch(SmpcRegs->COMREG) {
      case 0x0:
         SMPCLOG("smpc\t: MSHON not implemented\n");
         SmpcInternalVars->timing = 1;
         return;
      case 0x8:
         SMPCLOG("smpc\t: CDON not implemented\n");
         SmpcInternalVars->timing = 1;
         return;
      case 0x9:
         SMPCLOG("smpc\t: CDOFF not implemented\n");
         SmpcInternalVars->timing = 1;
         return;
      case 0xD:
      case 0xE:
      case 0xF:
         SmpcInternalVars->timing = 1; // this has to be tested on a real saturn
         return;
      case 0x10:
         if (SmpcInternalVars->intback)//continue was issued
         {
            SmpcInternalVars->timing = 16000;
            intback_wait_for_line = 1;
         }
         else {
            // Calculate timing based on what data is being retrieved

            if ((SmpcRegs->IREG[0] == 0x01) && (SmpcRegs->IREG[1] & 0x8))
            {
               //status followed by peripheral data
               SmpcInternalVars->timing = 250;
            }
            else if ((SmpcRegs->IREG[0] == 0x01) && ((SmpcRegs->IREG[1] & 0x8) == 0))
            {
               //status only
               SmpcInternalVars->timing = 250;
            }
            else if ((SmpcRegs->IREG[0] == 0) && (SmpcRegs->IREG[1] & 0x8))
            {
               //peripheral only
               SmpcInternalVars->timing = 16000;
               intback_wait_for_line = 1;
            }
            else {
              /* Any other IREG[0] still has to be given a timing.  cotton2 asks
                 for INTBACK with IREG[0] = 0xFF / 0x80 (bit 0 = the status
                 request, the high nibble = the port mode), which matches
                 neither "0x01" nor "0" above.  The old code fell through here
                 and left `timing` at 0, so SmpcExec never ran the command:
                 SmpcINTBACK() was never called, SR stayed 0, and the game span
                 forever on SR(0x61) instead of reading the INTBACK payload --
                 which is exactly why the pad data (and START) never arrived.
                 Kronos gives this branch `timing = 10` in its own 250us unit,
                 i.e. ~2.5ms; we use our status-path value (250us) so the
                 command always completes well inside the frame the game polls. */
              SMPCLOG("smpc\t: unimplemented command: %02X\n", SmpcRegs->COMREG);
              SmpcInternalVars->timing = 250;
              SmpcRegs->SF = 0;
            }
         }
         return;
      case 0x17:
         SmpcInternalVars->timing = 1;
         return;
      case 0x2:
         SmpcInternalVars->timing = 1;
         return;
      case 0x3:
         SmpcInternalVars->timing = 1;                        
         return;
      case 0x6:
      case 0x7:
      case 0x18:
      case 0x19:
      case 0x1A:
         SmpcInternalVars->timing = 1;
         return;
      default:
         SMPCLOG("smpc\t: unimplemented command: %02X\n", SmpcRegs->COMREG);
         SmpcRegs->SF = 0;
         break;
   }
}

//////////////////////////////////////////////////////////////////////////////

//acquiring megadrive id
//world heroes perfect wants to find a saturn pad
//id = 0xb
u8 do_th_mode(u8 val)
{
   switch (val & 0x40) {
   case 0x40:
      return 0x70 | ((PORTDATA1.data[3] & 0xF) & 0xc);
      break;
   case 0x00:
      return 0x30 | ((PORTDATA1.data[2] >> 4) & 0xf);
      break;
   }

   //should not happen
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

static void FASTCALL SmpcWriteByte_inner(u32 addr, u8 val);
void FASTCALL SmpcWriteByte(u32 addr, u8 val) {
   { static int on=-1; static FILE *fp=NULL;
     if(on<0){ on=(access("/mnt/sdcard/smpc.on",F_OK)==0); if(on) fp=fopen("/mnt/sdcard/smpcwrite.log","w"); }
     if(fp){ fprintf(fp,"frame=%u addr=%02X val=%02X\n",(unsigned)yabsys.frame_count,addr&0x7F,val); fflush(fp); } }
   SmpcWriteByte_inner(addr, val);
}
static void FASTCALL SmpcWriteByte_inner(u32 addr, u8 val) {
#ifdef YAB_STV_DEBUG
   if (yabsys.isSTV && stv_smpc_dbg < 300) {
      printf("[SMPCDBG] w %02x = %02x (COMREG=%02x SF=%02x DDR=%02x,%02x PDR=%02x,%02x)\n",
             addr & 0x7F, val, SmpcRegs->COMREG, SmpcRegs->SF,
             SmpcRegs->DDR[0], SmpcRegs->DDR[1], SmpcRegs->PDR[0], SmpcRegs->PDR[1]);
      stv_smpc_dbg++;
   }
#endif
   addr &= 0x7F;
   bustmp = val;
   SmpcRegsT[addr >> 1] = val;

   switch(addr) {
      case 0x01: // Maybe an INTBACK continue/break request
         /* Aligned with Kronos (see .notes/smpc-port/smpc.c.kronos-ported).
            The old gate was "if (intback)", and intback is cleared at every
            INTBACK end, so cotton2's continue/break writes were dropped; the
            game then stopped using INTBACK altogether (measured: ZERO INTBACK
            requests in a whole run) and fell back to register polling, where
            START never arrives.  Gate on firstPeri/timing as Kronos does, clear
            SF on break, and do not rewrite COMREG on continue. */
         if ((SmpcInternalVars->firstPeri != 0) && (SmpcInternalVars->timing <= 0))
         {
            if (SmpcRegs->IREG[0] & 0x40) {
               // Break
               SmpcInternalVars->firstPeri = 0;
               SmpcRegs->SR &= 0x0F;
               SmpcRegs->SF = 0;
               break;
            }
            else if (SmpcRegs->IREG[0] & 0x80) {
               // Continue
               SmpcSetTiming();
               SmpcRegs->SF = 1;
            }
         }
         return;
      case 0x1F:
         SmpcSetTiming();
         return;
      case 0x63:
         SmpcRegs->SF &= val;
         return;
      case 0x75: // PDR1
         // FIX ME (should support other peripherals)
         switch (SmpcRegs->DDR[0] & 0x7F) { // Which Control Method do we use?
            case 0x00:
               if (PORTDATA1.data[1] == PERGUN && (val & 0x7F) == 0x7F)
                  SmpcRegs->PDR[0] = PORTDATA1.data[2];
               break;
            //th control mode (acquire id)
            case 0x40:
               SmpcRegs->PDR[0] = do_th_mode(val);
               break;
            //th tr control mode
            case 0x60:
               switch (val & 0x60) {
                  case 0x60: // 1st Data
                     val = (val & 0x80) | 0x14 | (PORTDATA1.data[3] & 0x8);
                     break;
                  case 0x20: // 2nd Data
                     val = (val & 0x80) | 0x10 | ((PORTDATA1.data[2] >> 4) & 0xF);
                     break;
                  case 0x40: // 3rd Data
                     val = (val & 0x80) | 0x10 | (PORTDATA1.data[2] & 0xF);
                     break;
                  case 0x00: // 4th Data
                     val = (val & 0x80) | 0x10 | ((PORTDATA1.data[3] >> 4) & 0xF);
                     break;
                  default: break;
               }

               SmpcRegs->PDR[0] = val;
               break;
            case 0x3f: /* EEPROM bit-bang.  ST-V needs it: the BIOS polls the DO line
                          at 0x4ed8 and spins forever when it never reads 1.  Kronos
                          drives the eeprom here and reads it back in SmpcReadByte(0x75). */
               m_pdr1_readback = (val & SmpcRegs->DDR[0]) & 0x7f;
               eeprom_set_clk((val & 0x08) ? 1 : 0);
               eeprom_set_di((val >> 4) & 1);
               eeprom_set_cs((val & 0x04) ? 1 : 0);
               SmpcRegs->PDR[0] = m_pdr1_readback;
               m_pdr1_readback |= (val & 0x80);
               break;
            default:
               SMPCLOG("smpc\t: Peripheral Unknown Control Method not implemented\n");
               break;
         }
			break;
	  case 0x77: // PDR2
		  // FIX ME (should support other peripherals)
		  switch (SmpcRegs->DDR[1] & 0x7F) { // Which Control Method do we use?
		  case 0x00:
			  if (PORTDATA2.data[1] == PERGUN && (val & 0x7F) == 0x7F)
				  SmpcRegs->PDR[1] = PORTDATA2.data[2];
			  break;
		  case 0x18: /* ST-V sound-CPU wire: PDR2 bit 0x10 stops the 68k */
			  m_pdr2_readback = (val & SmpcRegs->DDR[1]) & 0x7F;
			  if (m_pdr2_readback & 0x10) {
				  M68KStop();
			  } else {
				  M68KStart();
			  }
			  SmpcRegs->PDR[1] = m_pdr2_readback;
			  m_pdr2_readback |= val & 0x80;
			  break;
		  case 0x60:
			  switch (val & 0x60) {
			  case 0x60: // 1st Data
				  val = (val & 0x80) | 0x14 | (PORTDATA2.data[3] & 0x8);
				  break;
			  case 0x20: // 2nd Data
				  val = (val & 0x80) | 0x10 | ((PORTDATA2.data[2] >> 4) & 0xF);
				  break;
			  case 0x40: // 3rd Data
				  val = (val & 0x80) | 0x10 | (PORTDATA2.data[2] & 0xF);
				  break;
			  case 0x00: // 4th Data
				  val = (val & 0x80) | 0x10 | ((PORTDATA2.data[3] >> 4) & 0xF);
				  break;
			  default: break;
			  }

			  SmpcRegs->PDR[1] = val;
			  break;
		  default:
			  SMPCLOG("smpc\t: Peripheral Unknown Control Method not implemented\n");
			  break;
		  }
		  break;
	  case 0x79: // DDR1
         switch (SmpcRegs->DDR[0] & 0x7F) { // Which Control Method do we use?
            case 0x00: // Low Nibble of Peripheral ID
            case 0x40: // High Nibble of Peripheral ID
               switch (PORTDATA1.data[0])
               {
                  case 0xA0:
                  {
                     if (PORTDATA1.data[1] == PERGUN)
                        SmpcRegs->PDR[0] = 0x7C;
                           break;
                  }
                  case 0xF0:
                     SmpcRegs->PDR[0] = 0x7F;
                     break;
                  case 0xF1:
                  {
                     switch(PORTDATA1.data[1])
                     {
                        case PERPAD:
                           SmpcRegs->PDR[0] = 0x7C;
                           break;
                        case PER3DPAD:
                        case PERKEYBOARD:
                           SmpcRegs->PDR[0] = 0x71;
                           break;
                        case PERMOUSE:
                           SmpcRegs->PDR[0] = 0x70;
                           break;
                        case PERWHEEL:
                        case PERMISSIONSTICK:
                        case PERTWINSTICKS:
                        default: 
                           SMPCLOG("smpc\t: Peripheral TH Control Method not supported for peripherl id %02X\n", PORTDATA1.data[1]);
                           break;
                     }
                     break;
                  }
                  default: 
                     SmpcRegs->PDR[0] = 0x71;
                     break;
               }

               break;
            default: break;
         }
         break;
	  case 0x7D: // IOSEL
		  SmpcRegs->IOSEL = val;
		  break;
	  case 0x7F: // EXLE
		  SmpcRegs->EXLE = val;
		  break;
      default:
         return;
   }
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL SmpcWriteWord(USED_IF_SMPC_DEBUG u32 addr, UNUSED u16 val) {
   // byte access only
   SMPCLOG("smpc\t: SMPC register write word - %08X\n", addr);
}

//////////////////////////////////////////////////////////////////////////////

void FASTCALL SmpcWriteLong(USED_IF_SMPC_DEBUG u32 addr, UNUSED u32 val) {
   // byte access only
   SMPCLOG("smpc\t: SMPC register write long - %08X\n", addr);
}

//////////////////////////////////////////////////////////////////////////////

int SmpcSaveState(FILE *fp)
{
   int offset;
   IOCheck_struct check = { 0, 0 };

   offset = StateWriteHeader(fp, "SMPC", 3);

   // Write registers
   ywrite(&check, (void *)SmpcRegs->IREG, sizeof(u8), 7, fp);
   ywrite(&check, (void *)&SmpcRegs->COMREG, sizeof(u8), 1, fp);
   ywrite(&check, (void *)SmpcRegs->OREG, sizeof(u8), 32, fp);
   ywrite(&check, (void *)&SmpcRegs->SR, sizeof(u8), 1, fp);
   ywrite(&check, (void *)&SmpcRegs->SF, sizeof(u8), 1, fp);
   ywrite(&check, (void *)SmpcRegs->PDR, sizeof(u8), 2, fp);
   ywrite(&check, (void *)SmpcRegs->DDR, sizeof(u8), 2, fp);
   ywrite(&check, (void *)&SmpcRegs->IOSEL, sizeof(u8), 1, fp);
   ywrite(&check, (void *)&SmpcRegs->EXLE, sizeof(u8), 1, fp);

   // Write internal variables
   ywrite(&check, (void *)SmpcInternalVars, sizeof(SmpcInternal), 1, fp);

   // Write ID's of currently emulated peripherals(fix me)

   return StateFinishHeader(fp, offset);
}

//////////////////////////////////////////////////////////////////////////////

int SmpcLoadState(FILE *fp, int version, int size)
{
   IOCheck_struct check = { 0, 0 };
   int internalsizev2 = sizeof(SmpcInternal) - 8;

   // Read registers
   yread(&check, (void *)SmpcRegs->IREG, sizeof(u8), 7, fp);
   yread(&check, (void *)&SmpcRegs->COMREG, sizeof(u8), 1, fp);
   yread(&check, (void *)SmpcRegs->OREG, sizeof(u8), 32, fp);
   yread(&check, (void *)&SmpcRegs->SR, sizeof(u8), 1, fp);
   yread(&check, (void *)&SmpcRegs->SF, sizeof(u8), 1, fp);
   yread(&check, (void *)SmpcRegs->PDR, sizeof(u8), 2, fp);
   yread(&check, (void *)SmpcRegs->DDR, sizeof(u8), 2, fp);
   yread(&check, (void *)&SmpcRegs->IOSEL, sizeof(u8), 1, fp);
   yread(&check, (void *)&SmpcRegs->EXLE, sizeof(u8), 1, fp);

   // Read internal variables
   if (version == 1)
   {
      // This handles the problem caused by the version not being incremented
      // when SmpcInternal was changed
      if ((size - 48) == internalsizev2)
         yread(&check, (void *)SmpcInternalVars, internalsizev2, 1, fp);
      else if ((size - 48) == 24)
         yread(&check, (void *)SmpcInternalVars, 24, 1, fp);
      else
         fseek(fp, size - 48, SEEK_CUR);
   }
   else if (version == 2)
      yread(&check, (void *)SmpcInternalVars, internalsizev2, 1, fp);
   else
      yread(&check, (void *)SmpcInternalVars, sizeof(SmpcInternal), 1, fp);

   // Read ID's of currently emulated peripherals(fix me)

   return size;
}

//////////////////////////////////////////////////////////////////////////////
u32 g_pdr2_writes = 0, g_pdr2_stops = 0, g_pdr2_starts = 0;
u8 g_pdr2_last = 0, g_ddr1_last = 0;
