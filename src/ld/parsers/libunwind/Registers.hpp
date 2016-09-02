/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2007-2011 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */
 
//
//	C++ interface to lower levels of libuwind 
//

#ifndef __REGISTERS_HPP__
#define __REGISTERS_HPP__

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <mach-o/loader.h>
#include <mach-o/getsect.h>
#include <mach/i386/thread_status.h>

#include "libunwind.h"
#include "InternalMacros.h"

namespace libunwind {


///
/// Registers_x86 holds the register state of a thread in a 32-bit intel process.  
///
class Registers_x86
{
public:	
					Registers_x86();
					Registers_x86(const void* registers);

	bool			validRegister(int num) const;
	uint32_t		getRegister(int num) const;
	void			setRegister(int num, uint32_t value);
	bool			validFloatRegister(int num) const { return false; }
	double			getFloatRegister(int num) const;
	void			setFloatRegister(int num, double value); 
	bool			validVectorRegister(int num) const { return false; }
	v128			getVectorRegister(int num) const;
	void			setVectorRegister(int num, v128 value);
	const char*		getRegisterName(int num);
	void			jumpto();
	
	uint32_t		getSP() const			{ return fRegisters.__esp; }
	void			setSP(uint32_t value)	{ fRegisters.__esp = value; }
	uint32_t		getIP()	const			{ return fRegisters.__eip; }
	void			setIP(uint32_t value)	{ fRegisters.__eip = value; }
	uint32_t		getEBP() const			{ return fRegisters.__ebp; }
	void			setEBP(uint32_t value)	{ fRegisters.__ebp = value; }
	uint32_t		getEBX() const			{ return fRegisters.__ebx; }
	void			setEBX(uint32_t value)	{ fRegisters.__ebx = value; }
	uint32_t		getECX() const			{ return fRegisters.__ecx; }
	void			setECX(uint32_t value)	{ fRegisters.__ecx = value; }
	uint32_t		getEDX() const			{ return fRegisters.__edx; }
	void			setEDX(uint32_t value)	{ fRegisters.__edx = value; }
	uint32_t		getESI() const			{ return fRegisters.__esi; }
	void			setESI(uint32_t value)	{ fRegisters.__esi = value; }
	uint32_t		getEDI() const			{ return fRegisters.__edi; }
	void			setEDI(uint32_t value)	{ fRegisters.__edi = value; }
	
private:
	i386_thread_state_t  fRegisters;
};

inline Registers_x86::Registers_x86(const void* registers)
{
	COMPILE_TIME_ASSERT( sizeof(Registers_x86) < sizeof(unw_context_t) );
	fRegisters = *((i386_thread_state_t*)registers); 
}

inline Registers_x86::Registers_x86()
{
	bzero(&fRegisters, sizeof(fRegisters)); 
}


inline bool Registers_x86::validRegister(int regNum) const
{
	if ( regNum == UNW_REG_IP )
		return true;
	if ( regNum == UNW_REG_SP )
		return true;
	if ( regNum < 0 )
		return false;
	if ( regNum > 7 )
		return false;
	return true;
}

inline uint32_t Registers_x86::getRegister(int regNum) const
{
	switch ( regNum ) {
		case UNW_REG_IP:
			return fRegisters.__eip;
		case UNW_REG_SP:
			return fRegisters.__esp;
		case UNW_X86_EAX:
			return fRegisters.__eax;
		case UNW_X86_ECX:
			return fRegisters.__ecx;
		case UNW_X86_EDX:
			return fRegisters.__edx;
		case UNW_X86_EBX:
			return fRegisters.__ebx;
		case UNW_X86_EBP:
			return fRegisters.__ebp;
		case UNW_X86_ESP:
			return fRegisters.__esp;
		case UNW_X86_ESI:
			return fRegisters.__esi;
		case UNW_X86_EDI:
			return fRegisters.__edi;
	}
	ABORT("unsupported x86 register");
}

inline void Registers_x86::setRegister(int regNum, uint32_t value)
{
	switch ( regNum ) {
		case UNW_REG_IP:
			fRegisters.__eip = value;
			return;
		case UNW_REG_SP:
			fRegisters.__esp = value;
			return;
		case UNW_X86_EAX:
			fRegisters.__eax = value;
			return;
		case UNW_X86_ECX:
			fRegisters.__ecx = value;
			return;
		case UNW_X86_EDX:
			fRegisters.__edx = value;
			return;
		case UNW_X86_EBX:
			fRegisters.__ebx = value;
			return;
		case UNW_X86_EBP:
			fRegisters.__ebp = value;
			return;
		case UNW_X86_ESP:
			fRegisters.__esp = value;
			return;
		case UNW_X86_ESI:
			fRegisters.__esi = value;
			return;
		case UNW_X86_EDI:
			fRegisters.__edi = value;
			return;
	}
	ABORT("unsupported x86 register");
}

inline const char* Registers_x86::getRegisterName(int regNum)
{
	switch ( regNum ) {
		case UNW_REG_IP:
			return "ip";
		case UNW_REG_SP:
			return "esp";
		case UNW_X86_EAX:
			return "eax";
		case UNW_X86_ECX:
			return "ecx";
		case UNW_X86_EDX:
			return "edx";
		case UNW_X86_EBX:
			return "ebx";
		case UNW_X86_EBP:
			return "ebp";
		case UNW_X86_ESP:
			return "esp";
		case UNW_X86_ESI:
			return "esi";
		case UNW_X86_EDI:
			return "edi";
		default:
			return "unknown register";
	}
}

inline double Registers_x86::getFloatRegister(int num) const
{
	ABORT("no x86 float registers");
}

inline void Registers_x86::setFloatRegister(int num, double value)
{
	ABORT("no x86 float registers");
}

inline v128 Registers_x86::getVectorRegister(int num) const
{
	ABORT("no x86 vector registers");
}

inline void Registers_x86::setVectorRegister(int num, v128 value)
{
	ABORT("no x86 vector registers");
}




///
/// Registers_x86_64  holds the register state of a thread in a 64-bit intel process.  
///
class Registers_x86_64
{
public:	
					Registers_x86_64();
					Registers_x86_64(const void* registers); 

	bool			validRegister(int num) const;
	uint64_t		getRegister(int num) const;
	void			setRegister(int num, uint64_t value);
	bool			validFloatRegister(int num) const{ return false; }
	double			getFloatRegister(int num) const;
	void			setFloatRegister(int num, double value);
	bool			validVectorRegister(int num) const { return false; }
	v128			getVectorRegister(int num) const;
	void			setVectorRegister(int num, v128 value);
	const char*		getRegisterName(int num);
	void			jumpto();
	uint64_t		getSP()	const			{ return fRegisters.__rsp; }
	void			setSP(uint64_t value)	{ fRegisters.__rsp = value; }
	uint64_t		getIP()	const			{ return fRegisters.__rip; }
	void			setIP(uint64_t value)	{ fRegisters.__rip = value; }
	uint64_t		getRBP() const			{ return fRegisters.__rbp; }
	void			setRBP(uint64_t value)	{ fRegisters.__rbp = value; }
	uint64_t		getRBX() const			{ return fRegisters.__rbx; }
	void			setRBX(uint64_t value)	{ fRegisters.__rbx = value; }
	uint64_t		getR12() const			{ return fRegisters.__r12; }
	void			setR12(uint64_t value)	{ fRegisters.__r12 = value; }
	uint64_t		getR13() const			{ return fRegisters.__r13; }
	void			setR13(uint64_t value)	{ fRegisters.__r13 = value; }
	uint64_t		getR14() const			{ return fRegisters.__r14; }
	void			setR14(uint64_t value)	{ fRegisters.__r14 = value; }
	uint64_t		getR15() const			{ return fRegisters.__r15; }
	void			setR15(uint64_t value)	{ fRegisters.__r15 = value; }
private:
	x86_thread_state64_t fRegisters;
};

inline Registers_x86_64::Registers_x86_64(const void* registers)
{
	COMPILE_TIME_ASSERT( sizeof(Registers_x86_64) < sizeof(unw_context_t) );
	fRegisters = *((x86_thread_state64_t*)registers); 
}

inline Registers_x86_64::Registers_x86_64()
{
	bzero(&fRegisters, sizeof(fRegisters)); 
}


inline bool Registers_x86_64::validRegister(int regNum) const
{
	if ( regNum == UNW_REG_IP )
		return true;
	if ( regNum == UNW_REG_SP )
		return true;
	if ( regNum < 0 )
		return false;
	if ( regNum > 15 )
		return false;
	return true;
}

inline uint64_t Registers_x86_64::getRegister(int regNum) const
{
	switch ( regNum ) {
		case UNW_REG_IP:
			return fRegisters.__rip;
		case UNW_REG_SP:
			return fRegisters.__rsp;
		case UNW_X86_64_RAX:
			return fRegisters.__rax;
		case UNW_X86_64_RDX:
			return fRegisters.__rdx;
		case UNW_X86_64_RCX:
			return fRegisters.__rcx;
		case UNW_X86_64_RBX:
			return fRegisters.__rbx;
		case UNW_X86_64_RSI:
			return fRegisters.__rsi;
		case UNW_X86_64_RDI:
			return fRegisters.__rdi;
		case UNW_X86_64_RBP:
			return fRegisters.__rbp;
		case UNW_X86_64_RSP:
			return fRegisters.__rsp;
		case UNW_X86_64_R8:
			return fRegisters.__r8;
		case UNW_X86_64_R9:
			return fRegisters.__r9;
		case UNW_X86_64_R10:
			return fRegisters.__r10;
		case UNW_X86_64_R11:
			return fRegisters.__r11;
		case UNW_X86_64_R12:
			return fRegisters.__r12;
		case UNW_X86_64_R13:
			return fRegisters.__r13;
		case UNW_X86_64_R14:
			return fRegisters.__r14;
		case UNW_X86_64_R15:
			return fRegisters.__r15;
	}
	ABORT("unsupported x86_64 register");
}

inline void Registers_x86_64::setRegister(int regNum, uint64_t value)
{
	switch ( regNum ) {
		case UNW_REG_IP:
			fRegisters.__rip = value;
			return;
		case UNW_REG_SP:
			fRegisters.__rsp = value;
			return;
		case UNW_X86_64_RAX:
			fRegisters.__rax = value;
			return;
		case UNW_X86_64_RDX:
			fRegisters.__rdx = value;
			return;
		case UNW_X86_64_RCX:
			fRegisters.__rcx = value;
			return;
		case UNW_X86_64_RBX:
			fRegisters.__rbx = value;
			return;
		case UNW_X86_64_RSI:
			fRegisters.__rsi = value;
			return;
		case UNW_X86_64_RDI:
			fRegisters.__rdi = value;
			return;
		case UNW_X86_64_RBP:
			fRegisters.__rbp = value;
			return;
		case UNW_X86_64_RSP:
			fRegisters.__rsp = value;
			return;
		case UNW_X86_64_R8:
			fRegisters.__r8 = value;
			return;
		case UNW_X86_64_R9:
			fRegisters.__r9 = value;
			return;
		case UNW_X86_64_R10:
			fRegisters.__r10 = value;
			return;
		case UNW_X86_64_R11:
			fRegisters.__r11 = value;
			return;
		case UNW_X86_64_R12:
			fRegisters.__r12 = value;
			return;
		case UNW_X86_64_R13:
			fRegisters.__r13 = value;
			return;
		case UNW_X86_64_R14:
			fRegisters.__r14 = value;
			return;
		case UNW_X86_64_R15:
			fRegisters.__r15 = value;
			return;
	}
	ABORT("unsupported x86_64 register");
}

inline const char* Registers_x86_64::getRegisterName(int regNum)
{
	switch ( regNum ) {
		case UNW_REG_IP:
			return "rip";
		case UNW_REG_SP:
			return "rsp";
		case UNW_X86_64_RAX:
			return "rax";
		case UNW_X86_64_RDX:
			return "rdx";
		case UNW_X86_64_RCX:
			return "rcx";
		case UNW_X86_64_RBX:
			return "rbx";
		case UNW_X86_64_RSI:
			return "rsi";
		case UNW_X86_64_RDI:
			return "rdi";
		case UNW_X86_64_RBP:
			return "rbp";
		case UNW_X86_64_RSP:
			return "rsp";
		case UNW_X86_64_R8:
			return "r8";
		case UNW_X86_64_R9:
			return "r9";
		case UNW_X86_64_R10:
			return "r10";
		case UNW_X86_64_R11:
			return "r11";
		case UNW_X86_64_R12:
			return "r12";
		case UNW_X86_64_R13:
			return "r13";
		case UNW_X86_64_R14:
			return "r14";
		case UNW_X86_64_R15:
			return "r15";
		default:
			return "unknown register";
	}
}

double Registers_x86_64::getFloatRegister(int num) const
{
	ABORT("no x86_64 float registers");
}

void Registers_x86_64::setFloatRegister(int num, double value)
{
	ABORT("no x86_64 float registers");
}

inline v128 Registers_x86_64::getVectorRegister(int num) const
{
	ABORT("no x86_64 vector registers");
}

inline void Registers_x86_64::setVectorRegister(int num, v128 value)
{
	ABORT("no x86_64 vector registers");
}


///
/// Registers_ppc holds the register state of a thread in a 32-bit PowerPC process.  
///
class Registers_ppc
{
public:	
					Registers_ppc();
					Registers_ppc(const void* registers);

	bool			validRegister(int num) const;
	uint32_t		getRegister(int num) const;
	void			setRegister(int num, uint32_t value);
	bool			validFloatRegister(int num) const;
	double			getFloatRegister(int num) const;
	void			setFloatRegister(int num, double value);
	bool			validVectorRegister(int num) const;
	v128			getVectorRegister(int num) const;
	void			setVectorRegister(int num, v128 value);
	void			jumpto();
	const char*		getRegisterName(int num);
	uint64_t		getSP() const			{ return fRegisters.__r1; }
	void			setSP(uint64_t value)	{ fRegisters.__r1 = value; }
	uint64_t		getIP() const			{ return fRegisters.__srr0; }
	void			setIP(uint64_t value)	{ fRegisters.__srr0 = value; }
private:
	struct ppc_thread_state_t
	{
		unsigned int __srr0;	/* Instruction address register (PC) */
		unsigned int __srr1;	/* Machine state register (supervisor) */
		unsigned int __r0;
		unsigned int __r1;
		unsigned int __r2;
		unsigned int __r3;
		unsigned int __r4;
		unsigned int __r5;
		unsigned int __r6;
		unsigned int __r7;
		unsigned int __r8;
		unsigned int __r9;
		unsigned int __r10;
		unsigned int __r11;
		unsigned int __r12;
		unsigned int __r13;
		unsigned int __r14;
		unsigned int __r15;
		unsigned int __r16;
		unsigned int __r17;
		unsigned int __r18;
		unsigned int __r19;
		unsigned int __r20;
		unsigned int __r21;
		unsigned int __r22;
		unsigned int __r23;
		unsigned int __r24;
		unsigned int __r25;
		unsigned int __r26;
		unsigned int __r27;
		unsigned int __r28;
		unsigned int __r29;
		unsigned int __r30;
		unsigned int __r31;
		unsigned int __cr;	/* Condition register */
		unsigned int __xer;	/* User's integer exception register */
		unsigned int __lr;	/* Link register */
		unsigned int __ctr;	/* Count register */
		unsigned int __mq;	/* MQ register (601 only) */
		unsigned int __vrsave;	/* Vector Save Register */
	};
	
	struct ppc_float_state_t
	{
		double  __fpregs[32];

		unsigned int __fpscr_pad; /* fpscr is 64 bits, 32 bits of rubbish */
		unsigned int __fpscr;	/* floating point status register */
	};

	ppc_thread_state_t	fRegisters;
	ppc_float_state_t	fFloatRegisters;
	v128				fVectorRegisters[32];	// offset 424 
};



inline Registers_ppc::Registers_ppc(const void* registers) 
{
	COMPILE_TIME_ASSERT( sizeof(Registers_ppc) < sizeof(unw_context_t) );
	fRegisters = *((ppc_thread_state_t*)registers); 
	fFloatRegisters = *((ppc_float_state_t*)((char*)registers+160));
	memcpy(fVectorRegisters, ((char*)registers+424), sizeof(fVectorRegisters));
}

inline Registers_ppc::Registers_ppc() 
{ 
	bzero(&fRegisters, sizeof(fRegisters)); 
	bzero(&fFloatRegisters, sizeof(fFloatRegisters)); 
	bzero(&fVectorRegisters, sizeof(fVectorRegisters)); 
}


inline bool Registers_ppc::validRegister(int regNum) const
{
	if ( regNum == UNW_REG_IP )
		return true;
	if ( regNum == UNW_REG_SP )
		return true;
	if ( regNum == UNW_PPC_VRSAVE )
		return true;
	if ( regNum < 0 )
		return false;
	if ( regNum <= UNW_PPC_R31 )
		return true;
	if ( regNum == UNW_PPC_MQ )
		return true;
	if ( regNum == UNW_PPC_LR )
		return true;
	if ( regNum == UNW_PPC_CTR )
		return true;
	if ( (UNW_PPC_CR0 <= regNum) && (regNum <= UNW_PPC_CR7) )
		return true;
	return false;
}


inline uint32_t Registers_ppc::getRegister(int regNum) const
{
	switch ( regNum ) {
		case UNW_REG_IP:
			return fRegisters.__srr0;
		case UNW_REG_SP:
			return fRegisters.__r1;
		case UNW_PPC_R0:
			return fRegisters.__r0;
		case UNW_PPC_R1:
			return fRegisters.__r1;
		case UNW_PPC_R2:
			return fRegisters.__r2;
		case UNW_PPC_R3:
			return fRegisters.__r3;
		case UNW_PPC_R4:
			return fRegisters.__r4;
		case UNW_PPC_R5:
			return fRegisters.__r5;
		case UNW_PPC_R6:
			return fRegisters.__r6;
		case UNW_PPC_R7:
			return fRegisters.__r7;
		case UNW_PPC_R8:
			return fRegisters.__r8;
		case UNW_PPC_R9:
			return fRegisters.__r9;
		case UNW_PPC_R10:
			return fRegisters.__r10;
		case UNW_PPC_R11:
			return fRegisters.__r11;
		case UNW_PPC_R12:
			return fRegisters.__r12;
		case UNW_PPC_R13:
			return fRegisters.__r13;
		case UNW_PPC_R14:
			return fRegisters.__r14;
		case UNW_PPC_R15:
			return fRegisters.__r15;
		case UNW_PPC_R16:
			return fRegisters.__r16;
		case UNW_PPC_R17:
			return fRegisters.__r17;
		case UNW_PPC_R18:
			return fRegisters.__r18;
		case UNW_PPC_R19:
			return fRegisters.__r19;
		case UNW_PPC_R20:
			return fRegisters.__r20;
		case UNW_PPC_R21:
			return fRegisters.__r21;
		case UNW_PPC_R22:
			return fRegisters.__r22;
		case UNW_PPC_R23:
			return fRegisters.__r23;
		case UNW_PPC_R24:
			return fRegisters.__r24;
		case UNW_PPC_R25:
			return fRegisters.__r25;
		case UNW_PPC_R26:
			return fRegisters.__r26;
		case UNW_PPC_R27:
			return fRegisters.__r27;
		case UNW_PPC_R28:
			return fRegisters.__r28;
		case UNW_PPC_R29:
			return fRegisters.__r29;
		case UNW_PPC_R30:
			return fRegisters.__r30;
		case UNW_PPC_R31:
			return fRegisters.__r31;
		case UNW_PPC_LR:
			return fRegisters.__lr;
		case UNW_PPC_CR0:
			return (fRegisters.__cr & 0xF0000000);
		case UNW_PPC_CR1:
			return (fRegisters.__cr & 0x0F000000);
		case UNW_PPC_CR2:
			return (fRegisters.__cr & 0x00F00000);
		case UNW_PPC_CR3:
			return (fRegisters.__cr & 0x000F0000);
		case UNW_PPC_CR4:
			return (fRegisters.__cr & 0x0000F000);
		case UNW_PPC_CR5:
			return (fRegisters.__cr & 0x00000F00);
		case UNW_PPC_CR6:
			return (fRegisters.__cr & 0x000000F0);
		case UNW_PPC_CR7:
			return (fRegisters.__cr & 0x0000000F);
		case UNW_PPC_VRSAVE:
			return fRegisters.__vrsave;
	}
	ABORT("unsupported ppc register");
}


inline void Registers_ppc::setRegister(int regNum, uint32_t value)
{
	//fprintf(stderr, "Registers_ppc::setRegister(%d, 0x%08X)\n", regNum, value);	
	switch ( regNum ) {
		case UNW_REG_IP:
			fRegisters.__srr0 = value;
			return;
		case UNW_REG_SP:
			fRegisters.__r1 = value;
			return;
		case UNW_PPC_R0:
			fRegisters.__r0 = value;
			return;
		case UNW_PPC_R1:
			fRegisters.__r1 = value;
			return;
		case UNW_PPC_R2:
			fRegisters.__r2 = value;
			return;
		case UNW_PPC_R3:
			fRegisters.__r3 = value;
			return;
		case UNW_PPC_R4:
			fRegisters.__r4 = value;
			return;
		case UNW_PPC_R5:
			fRegisters.__r5 = value;
			return;
		case UNW_PPC_R6:
			fRegisters.__r6 = value;
			return;
		case UNW_PPC_R7:
			fRegisters.__r7 = value;
			return;
		case UNW_PPC_R8:
			fRegisters.__r8 = value;
			return;
		case UNW_PPC_R9:
			fRegisters.__r9 = value;
			return;
		case UNW_PPC_R10:
			fRegisters.__r10 = value;
			return;
		case UNW_PPC_R11:
			fRegisters.__r11 = value;
			return;
		case UNW_PPC_R12:
			fRegisters.__r12 = value;
			return;
		case UNW_PPC_R13:
			fRegisters.__r13 = value;
			return;
		case UNW_PPC_R14:
			fRegisters.__r14 = value;
			return;
		case UNW_PPC_R15:
			fRegisters.__r15 = value;
			return;
		case UNW_PPC_R16:
			fRegisters.__r16 = value;
			return;
		case UNW_PPC_R17:
			fRegisters.__r17 = value;
			return;
		case UNW_PPC_R18:
			fRegisters.__r18 = value;
			return;
		case UNW_PPC_R19:
			fRegisters.__r19 = value;
			return;
		case UNW_PPC_R20:
			fRegisters.__r20 = value;
			return;
		case UNW_PPC_R21:
			fRegisters.__r21 = value;
			return;
		case UNW_PPC_R22:
			fRegisters.__r22 = value;
			return;
		case UNW_PPC_R23:
			fRegisters.__r23 = value;
			return;
		case UNW_PPC_R24:
			fRegisters.__r24 = value;
			return;
		case UNW_PPC_R25:
			fRegisters.__r25 = value;
			return;
		case UNW_PPC_R26:
			fRegisters.__r26 = value;
			return;
		case UNW_PPC_R27:
			fRegisters.__r27 = value;
			return;
		case UNW_PPC_R28:
			fRegisters.__r28 = value;
			return;
		case UNW_PPC_R29:
			fRegisters.__r29 = value;
			return;
		case UNW_PPC_R30:
			fRegisters.__r30 = value;
			return;
		case UNW_PPC_R31:
			fRegisters.__r31 = value;
			return;
		case UNW_PPC_MQ:
			fRegisters.__mq = value;
			return;
		case UNW_PPC_LR:
			fRegisters.__lr = value;
			return;
		case UNW_PPC_CTR:
			fRegisters.__ctr = value;
			return;
		case UNW_PPC_CR0:
			fRegisters.__cr &= 0x0FFFFFFF;
			fRegisters.__cr |= (value & 0xF0000000);
			return;
		case UNW_PPC_CR1:
			fRegisters.__cr &= 0xF0FFFFFF;
			fRegisters.__cr |= (value & 0x0F000000);
			return;
		case UNW_PPC_CR2:
			fRegisters.__cr &= 0xFF0FFFFF;
			fRegisters.__cr |= (value & 0x00F00000);
			return;
		case UNW_PPC_CR3:
			fRegisters.__cr &= 0xFFF0FFFF;
			fRegisters.__cr |= (value & 0x000F0000);
			return;
		case UNW_PPC_CR4:
			fRegisters.__cr &= 0xFFFF0FFF;
			fRegisters.__cr |= (value & 0x0000F000);
			return;
		case UNW_PPC_CR5:
			fRegisters.__cr &= 0xFFFFF0FF;
			fRegisters.__cr |= (value & 0x00000F00);
			return;
		case UNW_PPC_CR6:
			fRegisters.__cr &= 0xFFFFFF0F;
			fRegisters.__cr |= (value & 0x000000F0);
			return;
		case UNW_PPC_CR7:
			fRegisters.__cr &= 0xFFFFFFF0;
			fRegisters.__cr |= (value & 0x0000000F);
			return;
		case UNW_PPC_VRSAVE:
			fRegisters.__vrsave = value;
			return;
			// not saved
			return;
		case UNW_PPC_XER:
			fRegisters.__xer = value;
			return;
		case UNW_PPC_AP:
		case UNW_PPC_VSCR:
		case UNW_PPC_SPEFSCR:
			// not saved
			return;
	}
	ABORT("unsupported ppc register");
}

inline bool Registers_ppc::validFloatRegister(int regNum) const
{
	if ( regNum < UNW_PPC_F0 )
		return false;
	if ( regNum > UNW_PPC_F31 )
		return false;
	return true;
}

inline double Registers_ppc::getFloatRegister(int regNum) const
{
	assert(validFloatRegister(regNum));
	return fFloatRegisters.__fpregs[regNum-UNW_PPC_F0];
}

inline void Registers_ppc::setFloatRegister(int regNum, double value)
{
	//fprintf(stderr, "Registers_ppc::setFloatRegister(%d, %g))\n", regNum, value);
	assert(validFloatRegister(regNum));
	fFloatRegisters.__fpregs[regNum-UNW_PPC_F0] = value;
}


inline bool Registers_ppc::validVectorRegister(int regNum) const
{
	if ( regNum < UNW_PPC_V0 )
		return false;
	if ( regNum > UNW_PPC_V31 )
		return false;
	return true;
}

v128 Registers_ppc::getVectorRegister(int regNum) const
{
	assert(validVectorRegister(regNum));
	v128 result = fVectorRegisters[regNum-UNW_PPC_V0];
	//fprintf(stderr, "Registers_ppc::getVectorRegister(this=%p, %d) => <0x%08X, 0x%08X, 0x%08X, 0x%08X> \n", 
	//		this, regNum, result.vec[0], result.vec[1], result.vec[2], result.vec[3]);
	return result;
}

void Registers_ppc::setVectorRegister(int regNum, v128 value) 
{
	assert(validVectorRegister(regNum));
	//fprintf(stderr, "Registers_ppc::setVectorRegister(this=%p, %d) <0x%08X, 0x%08X, 0x%08X, 0x%08X> => <0x%08X, 0x%08X, 0x%08X, 0x%08X> \n", 
	//		this, regNum, fVectorRegisters[regNum-UNW_PPC_V0].vec[0], fVectorRegisters[regNum-UNW_PPC_V0].vec[1], fVectorRegisters[regNum-UNW_PPC_V0].vec[2], 
	//			fVectorRegisters[regNum-UNW_PPC_V0].vec[3], value.vec[0], value.vec[1], value.vec[2], value.vec[3]);
	fVectorRegisters[regNum-UNW_PPC_V0] = value;
}


inline const char* Registers_ppc::getRegisterName(int regNum)
{
	switch ( regNum ) {
		case UNW_REG_IP:
			return "ip";
		case UNW_REG_SP:
			return "sp";
		case UNW_PPC_R0:
			return "r0";
		case UNW_PPC_R1:
			return "r1";
		case UNW_PPC_R2:
			return "r2";
		case UNW_PPC_R3:
			return "r3";
		case UNW_PPC_R4:
			return "r4";
		case UNW_PPC_R5:
			return "r5";
		case UNW_PPC_R6:
			return "r6";
		case UNW_PPC_R7:
			return "r7";
		case UNW_PPC_R8:
			return "r8";
		case UNW_PPC_R9:
			return "r9";
		case UNW_PPC_R10:
			return "r10";
		case UNW_PPC_R11:
			return "r11";
		case UNW_PPC_R12:
			return "r12";
		case UNW_PPC_R13:
			return "r13";
		case UNW_PPC_R14:
			return "r14";
		case UNW_PPC_R15:
			return "r15";
		case UNW_PPC_R16:
			return "r16";
		case UNW_PPC_R17:
			return "r17";
		case UNW_PPC_R18:
			return "r18";
		case UNW_PPC_R19:
			return "r19";
		case UNW_PPC_R20:
			return "r20";
		case UNW_PPC_R21:
			return "r21";
		case UNW_PPC_R22:
			return "r22";
		case UNW_PPC_R23:
			return "r23";
		case UNW_PPC_R24:
			return "r24";
		case UNW_PPC_R25:
			return "r25";
		case UNW_PPC_R26:
			return "r26";
		case UNW_PPC_R27:
			return "r27";
		case UNW_PPC_R28:
			return "r28";
		case UNW_PPC_R29:
			return "r29";
		case UNW_PPC_R30:
			return "r30";
		case UNW_PPC_R31:
			return "r31";
		case UNW_PPC_F0:
			return "fp0";
		case UNW_PPC_F1:
			return "fp1";
		case UNW_PPC_F2:
			return "fp2";
		case UNW_PPC_F3:
			return "fp3";
		case UNW_PPC_F4:
			return "fp4";
		case UNW_PPC_F5:
			return "fp5";
		case UNW_PPC_F6:
			return "fp6";
		case UNW_PPC_F7:
			return "fp7";
		case UNW_PPC_F8:
			return "fp8";
		case UNW_PPC_F9:
			return "fp9";
		case UNW_PPC_F10:
			return "fp10";
		case UNW_PPC_F11:
			return "fp11";
		case UNW_PPC_F12:
			return "fp12";
		case UNW_PPC_F13:
			return "fp13";
		case UNW_PPC_F14:
			return "fp14";
		case UNW_PPC_F15:
			return "fp15";
		case UNW_PPC_F16:
			return "fp16";
		case UNW_PPC_F17:
			return "fp17";
		case UNW_PPC_F18:
			return "fp18";
		case UNW_PPC_F19:
			return "fp19";
		case UNW_PPC_F20:
			return "fp20";
		case UNW_PPC_F21:
			return "fp21";
		case UNW_PPC_F22:
			return "fp22";
		case UNW_PPC_F23:
			return "fp23";
		case UNW_PPC_F24:
			return "fp24";
		case UNW_PPC_F25:
			return "fp25";
		case UNW_PPC_F26:
			return "fp26";
		case UNW_PPC_F27:
			return "fp27";
		case UNW_PPC_F28:
			return "fp28";
		case UNW_PPC_F29:
			return "fp29";
		case UNW_PPC_F30:
			return "fp30";
		case UNW_PPC_F31:
			return "fp31";
		case UNW_PPC_LR:
			return "lr";
		default:
			return "unknown register";
	}

}





struct arm_thread_state64_t
{
	__uint64_t    __x[29];	/* General purpose registers x0-x28 */
	__uint64_t    __fp;		/* Frame pointer x29 */
	__uint64_t    __lr;		/* Link register x30 */
	__uint64_t    __sp;		/* Stack pointer x31 */
	__uint64_t    __pc;		/* Program counter */
	__uint32_t    __cpsr;	/* Current program status register */
	__uint32_t	  padding[3]; /* round up struct size to be 0x110 */
};


///
/// Registers_arm64  holds the register state of a thread in a 64-bit intel process.  
///
class Registers_arm64
{
public:	
					Registers_arm64();
					Registers_arm64(const void* registers); 

	bool			validRegister(int num) const;
	uint64_t		getRegister(int num) const;
	void			setRegister(int num, uint64_t value);
	bool			validFloatRegister(int num) const;
	double			getFloatRegister(int num) const;
	void			setFloatRegister(int num, double value);
	bool			validVectorRegister(int num) const;
	v128			getVectorRegister(int num) const;
	void			setVectorRegister(int num, v128 value);
	const char*		getRegisterName(int num);
	void			jumpto();
	uint64_t		getSP()	const			{ return fRegisters.__sp; }
	void			setSP(uint64_t value)	{ fRegisters.__sp = value; }
	uint64_t		getIP()	const			{ return fRegisters.__pc; }
	void			setIP(uint64_t value)	{ fRegisters.__pc = value; }
	uint64_t		getFP() const			{ return fRegisters.__fp; }
	void			setFP(uint64_t value)	{ fRegisters.__fp = value; }
private:
	arm_thread_state64_t	fRegisters;
	double					fHalfVectorRegisters[32];
	// Currently only the lower double in 128-bit vectore registers
	// is perserved during unwinding.  We could define new register
	// numbers (> 96) which mean whole vector registers, then this
	// struct would need to change to contain whole vector registers.
};

inline Registers_arm64::Registers_arm64(const void* registers)
{
	COMPILE_TIME_ASSERT( sizeof(Registers_arm64) < sizeof(unw_context_t) );
	fRegisters = *((arm_thread_state64_t*)registers); 
}

inline Registers_arm64::Registers_arm64()
{
	bzero(&fRegisters, sizeof(fRegisters)); 
	bzero(&fRegisters, sizeof(fHalfVectorRegisters)); 
}


inline bool Registers_arm64::validRegister(int regNum) const
{
	if ( regNum == UNW_REG_IP )
		return true;
	if ( regNum == UNW_REG_SP )
		return true;
	if ( regNum < 0 )
		return false;
	if ( regNum > 95 )
		return false;
	if ( (regNum > 31) && (regNum < 64) )
		return false;
	return true;
}

inline uint64_t Registers_arm64::getRegister(int regNum) const
{
	if ( regNum == UNW_REG_IP )
		return fRegisters.__pc;
	if ( regNum == UNW_REG_SP )
		return fRegisters.__sp;
	if ( (regNum >= 0) && (regNum < 32) )
		return fRegisters.__x[regNum];
	ABORT("unsupported arm64 register");
}

inline void Registers_arm64::setRegister(int regNum, uint64_t value)
{
	if ( regNum == UNW_REG_IP )
		fRegisters.__pc = value;
	else if ( regNum == UNW_REG_SP )
		fRegisters.__sp = value;
	else if ( (regNum >= 0) && (regNum < 32) )
		fRegisters.__x[regNum] = value;
	else
		ABORT("unsupported arm64 register");
}

inline const char* Registers_arm64::getRegisterName(int regNum)
{
	switch (regNum) {
		case UNW_REG_IP:
			return "pc";
		case UNW_REG_SP:
			return "sp";
		case UNW_ARM64_X0:
			return "x0";
		case UNW_ARM64_X1:
			return "x1";
		case UNW_ARM64_X2:
			return "x2";
		case UNW_ARM64_X3:
			return "x3";
		case UNW_ARM64_X4:
			return "x4";
		case UNW_ARM64_X5:
			return "x5";
		case UNW_ARM64_X6:
			return "x6";
		case UNW_ARM64_X7:
			return "x7";
		case UNW_ARM64_X8:
			return "x8";
		case UNW_ARM64_X9:
			return "x9";
		case UNW_ARM64_X10:
			return "x10";
		case UNW_ARM64_X11:
			return "x11";
		case UNW_ARM64_X12:
			return "x12";
		case UNW_ARM64_X13:
			return "x13";
		case UNW_ARM64_X14:
			return "x14";
		case UNW_ARM64_X15:
			return "x15";
		case UNW_ARM64_X16:
			return "x16";
		case UNW_ARM64_X17:
			return "x17";
		case UNW_ARM64_X18:
			return "x18";
		case UNW_ARM64_X19:
			return "x19";
		case UNW_ARM64_X20:
			return "x20";
		case UNW_ARM64_X21:
			return "x21";
		case UNW_ARM64_X22:
			return "x22";
		case UNW_ARM64_X23:
			return "x23";
		case UNW_ARM64_X24:
			return "x24";
		case UNW_ARM64_X25:
			return "x25";
		case UNW_ARM64_X26:
			return "x26";
		case UNW_ARM64_X27:
			return "x27";
		case UNW_ARM64_X28:
			return "x28";
		case UNW_ARM64_X29:
			return "fp";
		case UNW_ARM64_X30:
			return "lr";
		case UNW_ARM64_X31:
			return "sp";
		case UNW_ARM64_D0:
			return "d0";
		case UNW_ARM64_D1:
			return "d1";
		case UNW_ARM64_D2:
			return "d2";
		case UNW_ARM64_D3:
			return "d3";
		case UNW_ARM64_D4:
			return "d4";
		case UNW_ARM64_D5:
			return "d5";
		case UNW_ARM64_D6:
			return "d6";
		case UNW_ARM64_D7:
			return "d7";
		case UNW_ARM64_D8:
			return "d8";
		case UNW_ARM64_D9:
			return "d9";
		case UNW_ARM64_D10:
			return "d10";
		case UNW_ARM64_D11:
			return "d11";
		case UNW_ARM64_D12:
			return "d12";
		case UNW_ARM64_D13:
			return "d13";
		case UNW_ARM64_D14:
			return "d14";
		case UNW_ARM64_D15:
			return "d15";
		case UNW_ARM64_D16:
			return "d16";
		case UNW_ARM64_D17:
			return "d17";
		case UNW_ARM64_D18:
			return "d18";
		case UNW_ARM64_D19:
			return "d19";
		case UNW_ARM64_D20:
			return "d20";
		case UNW_ARM64_D21:
			return "d21";
		case UNW_ARM64_D22:
			return "d22";
		case UNW_ARM64_D23:
			return "d23";
		case UNW_ARM64_D24:
			return "d24";
		case UNW_ARM64_D25:
			return "d25";
		case UNW_ARM64_D26:
			return "d26";
		case UNW_ARM64_D27:
			return "d27";
		case UNW_ARM64_D28:
			return "d28";
		case UNW_ARM64_D29:
			return "d29";
		case UNW_ARM64_D30:
			return "d30";
		case UNW_ARM64_D31:
			return "d31";
		default:
			return "unknown register";
	}
}

bool Registers_arm64::validFloatRegister(int regNum) const
{
	if ( regNum < UNW_ARM64_D0 )
		return false;
	if ( regNum > UNW_ARM64_D31 )
		return false;
	return true;
}


double Registers_arm64::getFloatRegister(int regNum) const
{
	assert(validFloatRegister(regNum));
	return fHalfVectorRegisters[regNum-UNW_ARM64_D0];
}

void Registers_arm64::setFloatRegister(int regNum, double value)
{
	assert(validFloatRegister(regNum));
	fHalfVectorRegisters[regNum-UNW_ARM64_D0] = value;
}

inline bool Registers_arm64::validVectorRegister(int regNum) const
{
	return false;
}

inline v128 Registers_arm64::getVectorRegister(int regNum) const
{
	ABORT("no arm64 vector register support yet");
}

inline void Registers_arm64::setVectorRegister(int regNum, v128 value)
{
	ABORT("no arm64 vector register support yet");
}


///
/// Registers_arm holds the register state of a thread in a 32-bit arm process.  
///
class Registers_arm {
public:
                Registers_arm();

    bool        validRegister(int num) const;
    uint32_t    getRegister(int num) const;
    void        setRegister(int num, uint32_t value);
    bool        validFloatRegister(int num) const;
    unw_fpreg_t getFloatRegister(int num) const;
    void        setFloatRegister(int num, unw_fpreg_t value);
    bool        validVectorRegister(int num) const;
    v128        getVectorRegister(int num) const;
    void        setVectorRegister(int num, v128 value);
    const char *getRegisterName(int num);
    void        jumpto();

    uint32_t    getSP() const         { return _registers[13]; }
    void        setSP(uint32_t value) { _registers[13] = value; }
    uint32_t    getIP() const         { return _registers[15]; }
    void        setIP(uint32_t value) { _registers[15] = value; }

private:
  uint32_t    _registers[16];
};

inline Registers_arm::Registers_arm() {
  bzero(&_registers, sizeof(_registers));
}


} // namespace libunwind 



#endif // __REGISTERS_HPP__




