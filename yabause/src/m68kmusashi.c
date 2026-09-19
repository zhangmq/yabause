/*  Copyright 2007 Guillaume Duhamel

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

/*! \file m68kmusashi.c
\brief Musashi 68000 interface.
*/

#include "m68kmusashi.h"
#include "musashi/m68k.h"
#include "m68kcore.h"
#include "musashi/m68kcpu.h"

struct ReadWriteFuncs
{
   M68K_READ  *r_8;
   M68K_READ  *r_16;
   M68K_WRITE *w_8;
   M68K_WRITE *w_16;
}rw_funcs;

static int M68KMusashiInit(void) {

   m68k_init();
   m68k_set_reset_instr_callback(m68k_pulse_reset);
   m68k_set_cpu_type(M68K_CPU_TYPE_68000);

   return 0;
}

static void M68KMusashiDeInit(void) {
}

static void M68KMusashiReset(void) {
   m68k_pulse_reset();
}

static s32 FASTCALL M68KMusashiExec(s32 cycle) {
   return m68k_execute(cycle);
}

static void M68KMusashiSync(void) {
}

static u32 M68KMusashiGetDReg(u32 num) {
   return m68k_get_reg(NULL, M68K_REG_D0 + num);
}

static u32 M68KMusashiGetAReg(u32 num) {
   return m68k_get_reg(NULL, M68K_REG_A0 + num);
}

static u32 M68KMusashiGetPC(void) {
   return m68k_get_reg(NULL, M68K_REG_PC);
}

static u32 M68KMusashiGetSR(void) {
   return m68k_get_reg(NULL, M68K_REG_SR);
}

static u32 M68KMusashiGetUSP(void) {
   return m68k_get_reg(NULL, M68K_REG_USP);
}

static u32 M68KMusashiGetMSP(void) {
   return m68k_get_reg(NULL, M68K_REG_MSP);
}

static void M68KMusashiSetDReg(u32 num, u32 val) {
   m68k_set_reg(M68K_REG_D0 + num, val);
}

static void M68KMusashiSetAReg(u32 num, u32 val) {
   m68k_set_reg(M68K_REG_A0 + num, val);
}

static void M68KMusashiSetPC(u32 val) {
   m68k_set_reg(M68K_REG_PC, val);
}

static void M68KMusashiSetSR(u32 val) {
   m68k_set_reg(M68K_REG_SR, val);
}

static void M68KMusashiSetUSP(u32 val) {
   m68k_set_reg(M68K_REG_USP, val);
}

static void M68KMusashiSetMSP(u32 val) {
   m68k_set_reg(M68K_REG_MSP, val);
}

static void M68KMusashiSetFetch(u32 low_adr, u32 high_adr, pointer fetch_adr) {
}

static void FASTCALL M68KMusashiSetIRQ(s32 level) {
   if (level > 0)
      m68k_set_irq(level);
}

static void FASTCALL M68KMusashiWriteNotify(u32 address, u32 size) {
}

unsigned int  m68k_read_memory_8(unsigned int address)
{
   return rw_funcs.r_8(address);
}

unsigned int  m68k_read_memory_16(unsigned int address)
{
   return rw_funcs.r_16(address);
}

unsigned int  m68k_read_memory_32(unsigned int address)
{
   u16 val1 = rw_funcs.r_16(address);

   return (val1 << 16 | rw_funcs.r_16(address + 2));
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
   rw_funcs.w_8(address, value);
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
   rw_funcs.w_16(address, value);
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
   rw_funcs.w_16(address, value >> 16 );
   rw_funcs.w_16(address + 2, value & 0xffff);
}

static void M68KMusashiSetReadB(M68K_READ *Func) {
   rw_funcs.r_8 = Func;
}

static void M68KMusashiSetReadW(M68K_READ *Func) {
   rw_funcs.r_16 = Func;
}

static void M68KMusashiSetWriteB(M68K_WRITE *Func) {
   rw_funcs.w_8 = Func;
}

static void M68KMusashiSetWriteW(M68K_WRITE *Func) {
   rw_funcs.w_16 = Func;
}

/* Sound-CPU register block, written inside the SCSP savestate chunk.
 *
 * These two hooks used to be empty, so a savestate carried no sound-CPU state
 * at all: the 68k was simply left wherever the live machine happened to have it
 * when the state was loaded.  On a cold start (the frontend's "resume on boot"
 * flow) that is the freshly reset CPU with PC=0, so the SCSP sound driver never
 * ran again and the machine stayed silent for good; on a warm machine it only
 * worked by accident, when the live 68k happened to sit in a state compatible
 * with the loaded SCSP state -- which is why a fixed number of warm-up frames
 * never made loading reliable.
 *
 * Layout: 23 u32 words -- dar[0..15] (D0-D7, A0-A7), sp[USP/ISP/MSP], PC, SR,
 * stopped, int_cycles.  Raw struct fields are used on purpose: the public
 * accessors only expose the *active* stack pointer, so going through them would
 * lose the inactive ones.
 */
#define M68K_STATE_WORDS 23

static void M68KMusashiSaveState(FILE *fp) {
   u32 words[M68K_STATE_WORDS];
   int i;

   for (i = 0; i < 16; i++)
      words[i] = m68ki_cpu.dar[i];
   words[16] = m68ki_cpu.sp[0];   /* USP */
   words[17] = m68ki_cpu.sp[4];   /* ISP */
   words[18] = m68ki_cpu.sp[6];   /* MSP */
   words[19] = m68ki_cpu.pc;
   words[20] = m68ki_get_sr();    /* packs the condition codes too */
   words[21] = m68ki_cpu.stopped;
   words[22] = m68ki_cpu.int_cycles;

   fwrite(words, sizeof(u32), M68K_STATE_WORDS, fp);
}

static void M68KMusashiLoadState(FILE *fp) {
   u32 words[M68K_STATE_WORDS];
   int i;

   if (fread(words, sizeof(u32), M68K_STATE_WORDS, fp) != M68K_STATE_WORDS)
      return;

   /* SR first, and *without* touching the stack pointer: m68ki_set_sr() and
    * m68ki_set_sr_noint() go through m68ki_set_sm_flag(), which backs the
    * current SP up into the slot of the mode it is leaving and loads the new
    * mode's SP.  Setting SR after restoring the register file would therefore
    * swap the stack pointers whenever the current mode differs from the saved
    * one -- exactly the cold-start case, where the 68k still sits in user mode
    * after reset.  The 68k then ran off into the wrong code with interrupt mask
    * 7 (all interrupts blocked) and the SCSP never produced another sample. */
   m68ki_set_sr_noint_nosp(words[20]);

   for (i = 0; i < 16; i++)
      m68ki_cpu.dar[i] = words[i];
   m68ki_cpu.sp[0] = words[16];
   m68ki_cpu.sp[4] = words[17];
   m68ki_cpu.sp[6] = words[18];
   m68ki_cpu.pc   = words[19];
   m68ki_cpu.ppc  = words[19];
   m68ki_cpu.stopped    = words[21];
   m68ki_cpu.int_cycles = words[22];
}

M68K_struct M68KMusashi = {
   3,
   "Musashi Interface",
   M68KMusashiInit,
   M68KMusashiDeInit,
   M68KMusashiReset,
   M68KMusashiExec,
   M68KMusashiSync,
   M68KMusashiGetDReg,
   M68KMusashiGetAReg,
   M68KMusashiGetPC,
   M68KMusashiGetSR,
   M68KMusashiGetUSP,
   M68KMusashiGetMSP,
   M68KMusashiSetDReg,
   M68KMusashiSetAReg,
   M68KMusashiSetPC,
   M68KMusashiSetSR,
   M68KMusashiSetUSP,
   M68KMusashiSetMSP,
   M68KMusashiSetFetch,
   M68KMusashiSetIRQ,
   M68KMusashiWriteNotify,
   M68KMusashiSetReadB,
   M68KMusashiSetReadW,
   M68KMusashiSetWriteB,
   M68KMusashiSetWriteW,
   M68KMusashiSaveState,
   M68KMusashiLoadState
};