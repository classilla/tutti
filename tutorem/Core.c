#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include "Core.h"

/* TMS 9995 Core.
   This only contains the endian-independent internal computations of the CPU.
   The external memory interface, interrupts, resets, CLA and endian swaps,
   as well as the internal decrementer and on-chip RAM, are still in TMS9995.c.
   
   Derived from MAME/MESS
   "TMS9900 emulator by Edward Swartz, initially converted for Mame by M.Coates, updated by R. Nabet"
   (C)2016 Cameron Kaiser */

inline void execute(TWORD opcode);
TWORD decipheraddr(TWORD opcode);
TWORD decipheraddrbyte(TWORD opcode);
void contextswitch(TWORD addr);
void field_interrupt(void);

/* Offsets for registers. */
#define R0   0
#define R1   1
#define R2   2
#define R3   3
#define R4   4
#define R5   5
#define R6   6
#define R7   7
#define R8   8
#define R9   9
#define R10 10
#define R11 11
#define R12 12
#define R13 13
#define R14 14
#define R15 15

int TMS99XX_ICOUNT = 0;
TBYTE lastparity;  /* rather than handling ST_OP directly, we copy the last value which
                                  would set it here */

#define READREG(r) TMS9995_GetRegister(r)
#define WRITEREG(r,d) TMS9995_SetRegister(r,d)
#define readword(x) TMS9995_FetchWord(x)
#define writeword(x,y) TMS9995_WriteWord(x,y)
inline TBYTE readbyte(TWORD addr) {
	TWORD value = readword(addr & 0xfffe);
	if (addr & 1)
		return (value & 0xff);
	else
		return (value >> 8);
}
#define writebyte(x,y) TMS9995_WriteByte(x,y)
#define IMASK       (CPU_Registers.ST & 0x0F)
//#define CYCLES(a,b) TMS99XX_ICOUNT += b*4
#define CYCLES(a,b) TMS99XX_ICOUNT += b+b

/************************************************************************
 * Status register functions
 ************************************************************************/
/************************************************************************

	ST register functions

************************************************************************/

/*
	remember that the OP ST bit is maintained in lastparity
*/

/*
	setstat sets the ST_OP bit according to lastparity

	It must be called before reading the ST register.
*/

void setstat(void)
{
	int i;
	TBYTE a;

	CPU_Registers.ST &= ~ ST_OP;

	/* We set the parity bit. */
	a = lastparity;

	for (i=0; i<8; i++)     /* 8 bits to test */
	{
		if (a & 1)  /* If current bit is set */
			CPU_Registers.ST ^= ST_OP;  /* we toggle the ST_OP bit */

		a >>= 1;    /* Next bit. */
	}
}

/*
	getstat sets emulator's lastparity variable according to 9900's STATUS bits.
	It must be called on interrupt return, or when, for some reason,
	the emulated program sets the STATUS register directly.
*/
void getstat(void)
{
	if (CPU_Registers.ST & ST_OP)
		lastparity = 1;
	else
		lastparity = 0;
}

/*
	A few words about the following functions.

	A big portability issue is the behavior of the ">>" instruction with the sign bit, which has
	not been normalised.  Every compiler does whatever it thinks smartest.
	My code assumed that when shifting right signed numbers, the operand is left-filled with a
	copy of sign bit, and that when shifting unsigned variables, it is left-filled with 0s.
	This is probably the most logical behaviour, and it is the behavior of CW PRO3 - most time
	(the exception is that ">>=" instructions always copy the sign bit (!)).  But some compilers
	are bound to disagree.

	So, I had to create special functions with predefined tables included, so that this code work
	on every compiler.  BUT this is a real slow-down.
	So, you might have to include a few lines in assembly to make this work better.
	Sorry about this, this problem is really unpleasant and absurd, but it is not my fault.
*/


const TWORD right_shift_mask_table[17] =
{
	0xFFFF,
	0x7FFF,
	0x3FFF,
	0x1FFF,
	0x0FFF,
	0x07FF,
	0x03FF,
	0x01FF,
	0x00FF,
	0x007F,
	0x003F,
	0x001F,
	0x000F,
	0x0007,
	0x0003,
	0x0001,
	0x0000
};

const TWORD inverted_right_shift_mask_table[17] =
{
	0x0000,
	0x8000,
	0xC000,
	0xE000,
	0xF000,
	0xF800,
	0xFC00,
	0xFE00,
	0xFF00,
	0xFF80,
	0xFFC0,
	0xFFE0,
	0xFFF0,
	0xFFF8,
	0xFFFC,
	0xFFFE,
	0xFFFF
};

inline TWORD logical_right_shift(TWORD val, int c)
{
	return((val>>c) & right_shift_mask_table[c]);
}

inline int16_t arithmetic_right_shift(int16_t val, int c)
{
	if (val < 0)
		return((val>>c) | inverted_right_shift_mask_table[c]);
	else
		return((val>>c) & right_shift_mask_table[c]);
}





/*
	Set lae
*/
inline void setst_lae(int16_t val)
{
	CPU_Registers.ST &= ~ (ST_LGT | ST_AGT | ST_EQ);

	if (val > 0)
		CPU_Registers.ST |= (ST_LGT | ST_AGT);
	else if (val < 0)
		CPU_Registers.ST |= ST_LGT;
	else
		CPU_Registers.ST |= ST_EQ;
}


/*
	Set laep (BYTE)
*/
inline void setst_byte_laep(int8_t val)
{
	CPU_Registers.ST &= ~ (ST_LGT | ST_AGT | ST_EQ);

	if (val > 0)
		CPU_Registers.ST |= (ST_LGT | ST_AGT);
	else if (val < 0)
		CPU_Registers.ST |= ST_LGT;
	else
		CPU_Registers.ST |= ST_EQ;

	lastparity = val;
}

/*
	For COC, CZC, and TB
*/
inline void setst_e(TWORD val, TWORD to)
{
	if (val == to)
		CPU_Registers.ST |= ST_EQ;
	else
		CPU_Registers.ST &= ~ ST_EQ;
}

/*
	For CI, C, CB
*/
inline void setst_c_lae(TWORD to, TWORD val)
{
	CPU_Registers.ST &= ~ (ST_LGT | ST_AGT | ST_EQ);

	if (val == to)
		CPU_Registers.ST |= ST_EQ;
	else
	{
		if ( ((int16_t) val) > ((int16_t) to) )
			CPU_Registers.ST |= ST_AGT;
		if ( ((TWORD) val) > ((TWORD) to) )
		CPU_Registers.ST |= ST_LGT;
	}
}

#define wadd(addr,expr) { int lval = setst_add_laeco(readword(addr), (expr)); writeword((addr),lval); }
#define wsub(addr,expr) { int lval = setst_sub_laeco(readword(addr), (expr)); writeword((addr),lval); }

/*
	Set laeco for add
*/
inline int16_t setst_add_laeco(int a, int b)
{
	uint32_t res;
	int16_t res2;

	CPU_Registers.ST &= ~ (ST_LGT | ST_AGT | ST_EQ | ST_C | ST_OV);

	res = (a & 0xffff) + (b & 0xffff);

	if (res & 0x10000)
		CPU_Registers.ST |= ST_C;

	if ((res ^ b) & (res ^ a) & 0x8000)
		CPU_Registers.ST |= ST_OV;

	res2 = (int16_t) res;

	if (res2 > 0)
		CPU_Registers.ST |= ST_LGT | ST_AGT;
	else if (res2 < 0)
		CPU_Registers.ST |= ST_LGT;
	else
		CPU_Registers.ST |= ST_EQ;

	return res2;
}


/*
	Set laeco for subtract
*/
inline int16_t setst_sub_laeco(int a, int b)
{
	uint32_t res;
	int16_t res2;

	CPU_Registers.ST &= ~ (ST_LGT | ST_AGT | ST_EQ | ST_C | ST_OV);

	res = (a & 0xffff) - (b & 0xffff);

	if (! (res & 0x10000))
		CPU_Registers.ST |= ST_C;

	if ((a ^ b) & (a ^ res) & 0x8000)
		CPU_Registers.ST |= ST_OV;

	res2 = (int16_t) res;

	if (res2 > 0)
		CPU_Registers.ST |= ST_LGT | ST_AGT;
	else if (res2 < 0)
		CPU_Registers.ST |= ST_LGT;
	else
		CPU_Registers.ST |= ST_EQ;

	return res2;
}


/*
	Set laecop for add (BYTE)
*/
inline int8_t setst_addbyte_laecop(int a, int b)
{
	unsigned int res;
	int8_t res2;

	CPU_Registers.ST &= ~ (ST_LGT | ST_AGT | ST_EQ | ST_C | ST_OV | ST_OP);

	res = (a & 0xff) + (b & 0xff);

	if (res & 0x100)
		CPU_Registers.ST |= ST_C;

	if ((res ^ b) & (res ^ a) & 0x80)
		CPU_Registers.ST |= ST_OV;

	res2 = (int8_t) res;

	if (res2 > 0)
		CPU_Registers.ST |= ST_LGT | ST_AGT;
	else if (res2 < 0)
		CPU_Registers.ST |= ST_LGT;
	else
		CPU_Registers.ST |= ST_EQ;

	lastparity = res2;

	return res2;
}


/*
	Set laecop for subtract (BYTE)
*/
inline int8_t setst_subbyte_laecop(int a, int b)
{
	unsigned int res;
	int8_t res2;

	CPU_Registers.ST &= ~ (ST_LGT | ST_AGT | ST_EQ | ST_C | ST_OV | ST_OP);

	res = (a & 0xff) - (b & 0xff);

	if (! (res & 0x100))
		CPU_Registers.ST |= ST_C;

	if ((a ^ b) & (a ^ res) & 0x80)
		CPU_Registers.ST |= ST_OV;

	res2 = (int8_t) res;

	if (res2 > 0)
		CPU_Registers.ST |= ST_LGT | ST_AGT;
	else if (res2 < 0)
		CPU_Registers.ST |= ST_LGT;
	else
		CPU_Registers.ST |= ST_EQ;

	lastparity = res2;

	return res2;
}

/*
	For NEG
*/
inline void setst_laeo(int16_t val)
{
	CPU_Registers.ST &= ~ (ST_LGT | ST_AGT | ST_EQ | ST_OV);

	if (val > 0)
		CPU_Registers.ST |= ST_LGT | ST_AGT;
	else if (val < 0)
	{
	CPU_Registers.ST |= ST_LGT;
	if (((TWORD) val) == 0x8000)
		CPU_Registers.ST |= ST_OV;
	}
	else
		CPU_Registers.ST |= ST_EQ;
}



/*
	Meat of SRA
*/
inline TWORD setst_sra_laec(int16_t a, int16_t c)
{
	CPU_Registers.ST &= ~ (ST_LGT | ST_AGT | ST_EQ | ST_C);

	if (c != 0)
	{
		a = arithmetic_right_shift(a, c-1);
		if (a & 1)  // The carry bit equals the last bit that is shifted out
			CPU_Registers.ST |= ST_C;
		a = arithmetic_right_shift(a, 1);
	}

	if (a > 0)
		CPU_Registers.ST |= ST_LGT | ST_AGT;
	else if (a < 0)
		CPU_Registers.ST |= ST_LGT;
	else
		CPU_Registers.ST |= ST_EQ;

	return a;
}


/*
	Meat of SRL.  Same algorithm as SRA, except that we fills in with 0s.
*/
inline TWORD setst_srl_laec(TWORD a, TWORD c)
{
	CPU_Registers.ST &= ~ (ST_LGT | ST_AGT | ST_EQ | ST_C);

	if (c != 0)
	{
		a = logical_right_shift(a, c-1);
		if (a & 1)
			CPU_Registers.ST |= ST_C;
		a = logical_right_shift(a, 1);
	}

	if (((int16_t) a) > 0)
		CPU_Registers.ST |= ST_LGT | ST_AGT;
	else if (((int16_t) a) < 0)
		CPU_Registers.ST |= ST_LGT;
	else
		CPU_Registers.ST |= ST_EQ;

	return a;
}


//
// Meat of SRC
//
inline TWORD setst_src_laec(TWORD a, TWORD c)
{
	CPU_Registers.ST &= ~ (ST_LGT | ST_AGT | ST_EQ | ST_C);

	if (c != 0)
	{
		a = logical_right_shift(a, c) | (a << (16-c));
		if (a & 0x8000) // The carry bit equals the last bit that is shifted out
			CPU_Registers.ST |= ST_C;
	}

	if (((int16_t) a) > 0)
		CPU_Registers.ST |= ST_LGT | ST_AGT;
	else if (((int16_t) a) < 0)
		CPU_Registers.ST |= ST_LGT;
	else
		CPU_Registers.ST |= ST_EQ;

	return a;
}


//
// Meat of SLA
//
inline TWORD setst_sla_laeco(TWORD a, TWORD c)
{
	CPU_Registers.ST &= ~ (ST_LGT | ST_AGT | ST_EQ | ST_C | ST_OV);

	if (c != 0)
	{
		{
			register TWORD mask;
			register TWORD ousted_bits;

			mask = 0xFFFF << (16-c-1);
			ousted_bits = a & mask;

			if (ousted_bits)        // If ousted_bits is neither all 0s
				if (ousted_bits ^ mask)   // nor all 1s,
					CPU_Registers.ST |= ST_OV;  // we set overflow
		}

		a <<= c-1;
		if (a & 0x8000) // The carry bit equals the last bit that is shifted out
			CPU_Registers.ST |= ST_C;

		a <<= 1;
	}

	if (((int16_t) a) > 0)
		CPU_Registers.ST |= ST_LGT | ST_AGT;
	else if (((int16_t) a) < 0)
		CPU_Registers.ST |= ST_LGT;
	else
		CPU_Registers.ST |= ST_EQ;

	return a;
}


/***********************************************************************
 *
 * Utility functions.
 *
 ***********************************************************************/

/* fetch : read one word at * PC, and increment PC. */

inline TWORD fetch(void)
{
	register TWORD value = readword(CPU_Registers.PC);
	CPU_Registers.PC += 2;
	return value;
}

/* field_interrupt : tell core and debugger interrupt was handled */

inline void field_interrupt()
{
	TMS9995_HandledInterrupt();
}

/* contextswitch : performs a BLWP, ie change PC, WP, and save PC, WP and ST... */
void contextswitch(TWORD addr)
{
	TWORD oldWP, oldpc;

	/* save old state */
	oldWP = CPU_Registers.WP;
	oldpc = CPU_Registers.PC;

	/* load vector */
	CPU_Registers.WP = readword(addr) & ~1;
	CPU_Registers.PC = readword(addr+2) & ~1;

	/* write old state to regs */
	WRITEREG(R13, oldWP);
	WRITEREG(R14, oldpc);
	setstat();
	WRITEREG(R15, CPU_Registers.ST);
}

/*
 * decipheraddr : compute and return the effective adress in word instructions.
 *
 * NOTA : the LSB is always ignored in word adresses,
 * but we do not set to 0 because of XOP...
 */
TWORD decipheraddr(TWORD opcode)
{
	register TWORD ts = opcode & 0x30;
	register TWORD reg = opcode & 0xF;

	reg += reg;

	if (ts == 0)
		/* Rx */
		return(reg + CPU_Registers.WP);
	else if (ts == 0x10)
	{	/* *Rx */
		CYCLES(4, 1);
		return(readword(reg + CPU_Registers.WP));
	}
	else if (ts == 0x20)
	{
		register TWORD imm;

		imm = fetch();

		if (reg)
		{	/* @>xxxx(Rx) */
			CYCLES(8, 3);
			return(readword(reg + CPU_Registers.WP) + imm);
		}
		else
		{	/* @>xxxx */
			CYCLES(8, 1);
			return(imm);
		}
	}
	else /*if (ts == 0x30)*/
	{	/* *Rx+ */
		register TWORD response;

		reg += CPU_Registers.WP;    /* reg now contains effective address */

		CYCLES(8, 3);

		response = readword(reg);
		writeword(reg, response+2); /* we increment register content */
		return(response);
	}
}

/* decipheraddrbyte : compute and return the effective address in byte instructions. */
TWORD decipheraddrbyte(TWORD opcode)
{
	register TWORD ts = opcode & 0x30;
	register TWORD reg = opcode & 0xF;

	reg += reg;

	if (ts == 0)
		/* Rx */
		return(reg + CPU_Registers.WP);
	else if (ts == 0x10)
	{	/* *Rx */
		CYCLES(4, 1);
		return(readword(reg + CPU_Registers.WP));
	}
	else if (ts == 0x20)
	{
		register TWORD imm;

		imm = fetch();

		if (reg)
		{	/* @>xxxx(Rx) */
			CYCLES(8, 3);
			return(readword(reg + CPU_Registers.WP) + imm);
		}
		else
		{	/* @>xxxx */
			CYCLES(8, 1);
			return(imm);
		}
	}
	else /*if (ts == 0x30)*/
	{	/* *Rx+ */
		register TWORD response;

		reg += CPU_Registers.WP;    /* reg now contains effective address */

		CYCLES(6, 3);

		response = readword(reg);
		writeword(reg, response+1); /* we increment register content */
		return(response);
	}
}


/*************************************************************************/

/*==========================================================================
   Illegal instructions                                        >0000->01FF (not for 9989 and later)
                                                               >0C00->0FFF (not for 99xxx)
============================================================================*/

#define HANDLE_ILLEGAL illegal(opcode)
void illegal(TWORD opcode)
{
	CPU_Registers.PC -= 2;
	fprintf(stderr, "ILLEGAL opcode! >%04x\n", readword(CPU_Registers.PC));
	TMS9995_TriggerDebugger();
}

/*==========================================================================
   Additional single-register instructions,                    >0040->00FF
 ---------------------------------------------------------------------------

     0 1 2 3-4 5 6 7+8 9 A B-C D E F
    ---------------------------------
    |     o p c o d e       | reg # |
    ---------------------------------

tms9989 and later : LST, LWP
tms99xxx : BLSK
============================================================================*/
void h0040(TWORD opcode)
{
	register TWORD addr;

	addr = opcode & 0xF;
	addr = ((addr + addr) + CPU_Registers.WP) & ~1;

	switch ((opcode & 0xF0) >> 4)
	{
	case 8:   /* LST */
		/* LST --- Load STatus register */
		/* ST = *Reg */
		CPU_Registers.ST = readword(addr);
		break;
	case 9:   /* LWP */
		/* LWP --- Load Workspace Pointer register */
		/* WP = *Reg */
		CPU_Registers.WP = readword(addr);
		break;

	default:
		HANDLE_ILLEGAL;
		break;
	}
}


/*==========================================================================
   Additionnal single-operand instructions,                    >0100->01FF
 ---------------------------------------------------------------------------

     0 1 2 3-4 5 6 7+8 9 A B-C D E F
    ---------------------------------
    |    o p c o d e    |TS |   S   |
    ---------------------------------

tms9989 and later : DIVS, MPYS
tms99xxx : BIND
============================================================================*/
void h0100(TWORD opcode)
{
	register TWORD src;

	src = decipheraddr(opcode) & ~1;

  switch ((opcode & 0xC0) >> 6)
  {
  case 2:   /* DIVS */
		/* DIVS -- DIVide Signed */
		/* R0 = (R0:R1)/S   R1 = (R0:R1)%S */
		{
			int16_t d = readword(src);
			long divq = (READREG(R0) << 16) | READREG(R1);
			long q = divq/d;

			if ((q < -32768L) || (q > 32767L))
			{
				CPU_Registers.ST |= ST_OV;
				CYCLES(24 /*don't know*/, 10);
			}
			else
			{
				CPU_Registers.ST &= ~ST_OV;
				setst_lae(q);
				WRITEREG(R0, q);
				WRITEREG(R1, divq%d);
				/* tms9995 : 33 is the worst case */
				CYCLES(102 /*don't know*/, 33);
			}
		}
		break;

  case 3:   /* MPYS */
		/* MPYS -- MultiPlY Signed */
		/* Results:  R0:R1 = R0*S */
		{
			long prod = ((long) (int16_t) READREG(R0)) * ((long) (int16_t) readword(src));

			CPU_Registers.ST &= ~ (ST_LGT | ST_AGT | ST_EQ);
			if (prod > 0)
				CPU_Registers.ST |= (ST_LGT | ST_AGT);
			else if (prod < 0)
				CPU_Registers.ST |= ST_LGT;
			else
				CPU_Registers.ST |= ST_EQ;

			WRITEREG(R0, prod >> 16);
			WRITEREG(R1, prod);
		}
		CYCLES(56 /*don't know*/, 25);
		break;

	default:
		HANDLE_ILLEGAL;
		break;
  }
}

/*==========================================================================
   Immediate, Control instructions,                            >0200->03FF
 ---------------------------------------------------------------------------

     0 1 2 3-4 5 6 7+8 9 A B-C D E F
    ---------------------------------
    |     o p c o d e     |0| reg # |
    ---------------------------------

  LI, AI, ANDI, ORI, CI, STWP, STST, LIMI, LWPI, IDLE, RSET, RTWP, CKON, CKOF, LREX
============================================================================*/
void h0200(TWORD opcode)
{
	register TWORD addr;
	register TWORD value;	/* used for anything */

	addr = opcode & 0xF;
	addr = ((addr + addr) + CPU_Registers.WP) & ~1;

	/* better instruction decoding on tms9995 */
	if (((opcode < 0x2E0) && (opcode & 0x10)) || ((opcode >= 0x2E0) && (opcode & 0x1F)))
	{
		HANDLE_ILLEGAL;
		return;
	}

	switch ((opcode & 0x1e0) >> 5)
	{
	case 0:   /* LI */
		/* LI ---- Load Immediate */
		/* *Reg = *PC+ */
		value = fetch();
		writeword(addr, value);
		setst_lae(value);
		CYCLES(12, 3);
		break;
	case 1:   /* AI */
		/* AI ---- Add Immediate */
		/* *Reg += *PC+ */
		value = fetch();
		wadd(addr, value);
		CYCLES(14, 4);
		break;
	case 2:   /* ANDI */
		/* ANDI -- AND Immediate */
		/* *Reg &= *PC+ */
		value = fetch();
		value = readword(addr) & value;
		writeword(addr, value);
		setst_lae(value);
		CYCLES(14, 4);
		break;
	case 3:   /* ORI */
		/* ORI --- OR Immediate */
		/* *Reg |= *PC+ */
		value = fetch();
		value = readword(addr) | value;
		writeword(addr, value);
		setst_lae(value);
		CYCLES(14, 4);
		break;
	case 4:   /* CI */
		/* CI ---- Compare Immediate */
		/* status = (*Reg-*PC+) */
		value = fetch();
		setst_c_lae(value, readword(addr));
		CYCLES(14, 4);
		break;
	case 5:   /* STWP */
		/* STWP -- STore Workspace Pointer */
		/* *Reg = WP */
		writeword(addr, CPU_Registers.WP);
		CYCLES(8, 3);
		break;
	case 6:   /* STST */
		/* STST -- STore STatus register */
		/* *Reg = ST */
		setstat();
		writeword(addr, CPU_Registers.ST);
		CYCLES(8, 3);
		break;
	case 7:   /* LWPI */
		/* LWPI -- Load Workspace Pointer Immediate */
		/* WP = *PC+ */
		CPU_Registers.WP = fetch();
		CYCLES(10, 4);
		break;
	case 8:   /* LIMI */
		/* LIMI -- Load Interrupt Mask Immediate */
		/* ST&15 |= (*PC+)&15 */
		value = fetch();
		CPU_Registers.ST = (CPU_Registers.ST & ~ 0xF) | (value & 0xF);
		field_interrupt();  /*IM has been modified.*/
		CYCLES(16, 5);
		break;
	case 9:   /* LMF is implemented elsewhere - when it is implemented */
		HANDLE_ILLEGAL;
		break;
	case 10:  /* IDLE */
		/* IDLE -- IDLE until a reset, interrupt, load */
		/* The TMS99000 locks until an interrupt happen (like with 68k STOP instruction),
		   and continuously performs a special CRU write (code 2). */
		//I.IDLE = 1;
		//external_instruction_notify(2);
		HANDLE_ILLEGAL;
		CYCLES(12, 7);
		/* we take care of further external_instruction_notify(2); in execute() */
		break;
	case 11:  /* RSET */
		/* RSET -- ReSET */
		/* Reset the Interrupt Mask, and perform a special CRU write (code 3). */
		/* Does not actually cause a reset, but an external circuitery could trigger one. */
		CPU_Registers.ST &= 0xFFF0; /*clear IM.*/
		field_interrupt();  /*IM has been modified.*/
		CYCLES(12, 7);
		break;
	case 12:  /* RTWP */
		/* RTWP -- Return with Workspace Pointer */
		/* WP = R13, PC = R14, ST = R15 */
		CPU_Registers.ST = READREG(R15);
		getstat();  /* set last_parity */
		CPU_Registers.PC = READREG(R14);
		CPU_Registers.WP = READREG(R13);
		field_interrupt();  /*IM has been modified.*/
		CYCLES(14, 6);
		break;
	case 13:  /* CKON */
	case 14:  /* CKOF */
	case 15:  /* LREX */
		/* CKON -- ClocK ON */
		/* Perform a special CRU write (code 5). */
		/* An external circuitery could, for instance, enable a "decrement-and-interrupt" timer. */
		/* CKOF -- ClocK OFf */
		/* Perform a special CRU write (code 6). */
		/* An external circuitery could, for instance, disable a "decrement-and-interrupt" timer. */
		/* LREX -- Load or REstart eXecution */
		/* Perform a special CRU write (code 7). */
		/* An external circuitery could, for instance, activate the LOAD* line,
		   causing a non-maskable LOAD interrupt (vector -1). */
		//external_instruction_notify((opcode & 0x00e0) >> 5);
		CYCLES(12, 7);
		break;
	}
}


/*==========================================================================
   Single-operand instructions,                                >0400->07FF
 ---------------------------------------------------------------------------

     0 1 2 3-4 5 6 7+8 9 A B-C D E F
    ---------------------------------
    |    o p c o d e    |TS |   S   |
    ---------------------------------

  BLWP, B, X, CLR, NEG, INV, INC, INCT, DEC, DECT, BL, SWPB, SETO, ABS
tms99xxx : LDD, LDS
============================================================================*/
void h0400(TWORD opcode)
{
	register TWORD addr = decipheraddr(opcode) & ~1;
	register TWORD value;  /* used for anything */

	switch ((opcode & 0x3C0) >> 6)
	{
	case 0:   /* BLWP */
		/* BLWP -- Branch and Link with Workspace Pointer */
		/* Result: WP = *S+, PC = *S */
		/*         New R13=old WP, New R14=Old PC, New R15=Old ST */
		contextswitch(addr);
		CYCLES(26, 11);
		break;
	case 1:   /* B */
		/* B ----- Branch */
		/* PC = S */
		CPU_Registers.PC = addr;
		CYCLES(8, 3);
		break;
	case 2:   /* X */
		/* X ----- eXecute */
		/* Executes instruction *S */
		execute(readword(addr));
		/* On tms9900, the X instruction actually takes 8 cycles, but we gain 4 cycles on the next
		instruction, as we don't need to fetch it. */
		CYCLES(4, 2);
		break;
	case 3:   /* CLR */
		/* CLR --- CLeaR */
		/* *S = 0 */
		writeword(addr, 0);
		CYCLES(10, 3);
		break;
	case 4:   /* NEG */
		/* NEG --- NEGate */
		/* *S = -*S */
		value = - (uint16_t) readword(addr);
		if (value)
			CPU_Registers.ST &= ~ ST_C;
		else
			CPU_Registers.ST |= ST_C;
		setst_laeo(value);
		writeword(addr, value);
		CYCLES(12, 3);
		break;
	case 5:   /* INV */
		/* INV --- INVert */
		/* *S = ~*S */
		value = ~ readword(addr);
		writeword(addr, value);
		setst_lae(value);
		CYCLES(10, 3);
		break;
	case 6:   /* INC */
		/* INC --- INCrement */
		/* (*S)++ */
		wadd(addr, 1);
		CYCLES(10, 3);
		break;
	case 7:   /* INCT */
		/* INCT -- INCrement by Two */
		/* (*S) +=2 */
		wadd(addr, 2);
		CYCLES(10, 3);
		break;
	case 8:   /* DEC */
		/* DEC --- DECrement */
		/* (*S)-- */
		wsub(addr, 1);
		CYCLES(10, 3);
		break;
	case 9:   /* DECT */
		/* DECT -- DECrement by Two */
		/* (*S) -= 2 */
		wsub(addr, 2);
		CYCLES(10, 3);
		break;
	case 10:  /* BL */
		/* BL ---- Branch and Link */
		/* IP=S, R11=old IP */
		WRITEREG(R11, CPU_Registers.PC);
		CPU_Registers.PC=addr;
 		CYCLES(12, 5);
		break;
	case 11:  /* SWPB */
		/* SWPB -- SWaP Bytes */
		/* *S = swab(*S) */
		value = readword(addr);
		value = logical_right_shift(value, 8) | (value << 8);
		writeword(addr, value);
		CYCLES(10, 13);
		break;
	case 12:  /* SETO */
		/* SETO -- SET Ones */
		/* *S = #$FFFF */
		writeword(addr, 0xFFFF);
		CYCLES(10, 3);
		break;
	case 13:  /* ABS */
		/* ABS --- ABSolute value */
		/* *S = |*S| */
		/* clearing ST_C seems to be necessary, although ABS will never set it. */
		/* tms9995 always write the result */
		CPU_Registers.ST &= ~ (ST_LGT | ST_AGT | ST_EQ | ST_C | ST_OV);
		value = readword(addr);

		CYCLES(12 /*Don't know for tms9989*/, 3);
		if (((int16_t) value) > 0)
			CPU_Registers.ST |= ST_LGT | ST_AGT;
		else if (((int16_t) value) < 0)
		{
			CPU_Registers.ST |= ST_LGT;
			if (value == 0x8000)
				CPU_Registers.ST |= ST_OV;
			value = - ((int16_t) value);
		}
		else
			CPU_Registers.ST |= ST_EQ;

		writeword(addr, value);
		break;
	default:
		/* illegal instructions */
		HANDLE_ILLEGAL;
		break;
	}
}


/*==========================================================================
   Shift instructions,                                         >0800->0BFF
  --------------------------------------------------------------------------

     0 1 2 3-4 5 6 7+8 9 A B-C D E F
    ---------------------------------
    | o p c o d e   |   C   |   W   |
    ---------------------------------

  SRA, SRL, SLA, SRC
============================================================================*/
void h0800(TWORD opcode)
{
	register TWORD addr;
	register TWORD cnt = (opcode & 0xF0) >> 4;
	register TWORD value;

	addr = (opcode & 0xF);
	addr = ((addr+addr) + CPU_Registers.WP) & ~1;

	CYCLES(12, 5);

	if (cnt == 0)
	{
		CYCLES(8, 2);

		cnt = READREG(0) & 0xF;

		if (cnt == 0)
			cnt = 16;
	}

	CYCLES(cnt+cnt, cnt);

	switch ((opcode & 0x300) >> 8)
	{
	case 0:   /* SRA */
		/* SRA --- Shift Right Arithmetic */
		/* *W >>= C   (*W is filled on the left with a copy of the sign bit) */
		value = setst_sra_laec(readword(addr), cnt);
		writeword(addr, value);
		break;
	case 1:   /* SRL */
		/* SRL --- Shift Right Logical */
		/* *W >>= C   (*W is filled on the left with 0) */
		value = setst_srl_laec(readword(addr), cnt);
		writeword(addr, value);
		break;
	case 2:   /* SLA */
		/* SLA --- Shift Left Arithmetic */
		/* *W <<= C */
		value = setst_sla_laeco(readword(addr), cnt);
		writeword(addr, value);
		break;
	case 3:   /* SRC */
		/* SRC --- Shift Right Circular */
		/* *W = rightcircularshift(*W, C) */
		value = setst_src_laec(readword(addr), cnt);
		writeword(addr, value);
		break;
	}
}

/*==========================================================================
   Jump, CRU bit instructions,                                 >1000->1FFF
 ---------------------------------------------------------------------------

     0 1 2 3-4 5 6 7+8 9 A B-C D E F
    ---------------------------------
    |  o p c o d e  | signed offset |
    ---------------------------------

  JMP, JLT, JLE, JEQ, JHE, JGT, JNE, JNC, JOC, JNO, JL, JH, JOP
  SBO, SBZ, TB
============================================================================*/

// "I'm sending your ass back to Extreme." -- joke I refuse to explain
#define JUMP() { CPU_Registers.PC += ((offset + offset) - 0); }

void h1000(TWORD opcode)
{
	/* we convert 8 bit signed word offset to a 16 bit effective word offset. */
	register int16_t offset = ((int8_t) opcode);


	switch ((opcode & 0xF00) >> 8)
	{
	case 0:   /* JMP */
		/* JMP --- unconditional JuMP */
		/* PC += offset */
		JUMP();
		CYCLES(10, 3);
		break;
	case 1:   /* JLT */
		/* JLT --- Jump if Less Than (arithmetic) */
		/* if (A==0 && EQ==0), PC += offset */
		if (! (CPU_Registers.ST & (ST_AGT | ST_EQ)))
		{
			JUMP();
			CYCLES(10, 3);
		}
		else
			CYCLES(8, 3);
		break;
	case 2:   /* JLE */
		/* JLE --- Jump if Lower or Equal (logical) */
		/* if (L==0 || EQ==1), PC += offset */
		if ((! (CPU_Registers.ST & ST_LGT)) || (CPU_Registers.ST & ST_EQ))
		{
			JUMP();
			CYCLES(10, 3);
		}
		else
			CYCLES(8, 3);
		break;
	case 3:   /* JEQ */
		/* JEQ --- Jump if EQual */
		/* if (EQ==1), PC += offset */
		if (CPU_Registers.ST & ST_EQ)
		{
			JUMP();
			CYCLES(10, 3);
		}
		else
			CYCLES(8, 3);
		break;
	case 4:   /* JHE */
		/* JHE --- Jump if Higher or Equal (logical) */
		/* if (L==1 || EQ==1), PC += offset */
		if (CPU_Registers.ST & (ST_LGT | ST_EQ))
		{
			JUMP();
			CYCLES(10, 3);
		}
		else
			CYCLES(8, 3);
		break;
	case 5:   /* JGT */
		/* JGT --- Jump if Greater Than (arithmetic) */
		/* if (A==1), PC += offset */
		if (CPU_Registers.ST & ST_AGT)
		{
			JUMP();
			CYCLES(10, 3);
		}
		else
			CYCLES(8, 3);
		break;
	case 6:   /* JNE */
		/* JNE --- Jump if Not Equal */
		/* if (EQ==0), PC += offset */
		if (! (CPU_Registers.ST & ST_EQ))
		{
			JUMP();
			CYCLES(10, 3);
		}
		else
			CYCLES(8, 3);
		break;
	case 7:   /* JNC */
		/* JNC --- Jump if No Carry */
		/* if (C==0), PC += offset */
		if (! (CPU_Registers.ST & ST_C))
		{
			JUMP();
			CYCLES(10, 3);
		}
		else
			CYCLES(8, 3);
		break;
	case 8:   /* JOC */
		/* JOC --- Jump On Carry */
		/* if (C==1), PC += offset */
		if (CPU_Registers.ST & ST_C)
		{
			JUMP();
			CYCLES(10, 3);
		}
		else
			CYCLES(8, 3);
		break;
	case 9:   /* JNO */
		/* JNO --- Jump if No Overflow */
		/* if (OV==0), PC += offset */
		if (! (CPU_Registers.ST & ST_OV))
		{
			JUMP();
			CYCLES(10, 3);
		}
		else
			CYCLES(8, 3);
		break;
	case 10:  /* JL */
		/* JL ---- Jump if Lower (logical) */
		/* if (L==0 && EQ==0), PC += offset */
		if (! (CPU_Registers.ST & (ST_LGT | ST_EQ)))
		{
			JUMP();
			CYCLES(10, 3);
		}
		else
			CYCLES(8, 3);
		break;
	case 11:  /* JH */
		/* JH ---- Jump if Higher (logical) */
		/* if (L==1 && EQ==0), PC += offset */
		if ((CPU_Registers.ST & ST_LGT) && ! (CPU_Registers.ST & ST_EQ))
		{
			JUMP();
			CYCLES(10, 3);
		}
		else
			CYCLES(8, 3);
		break;
	case 12:  /* JOP */
		/* JOP --- Jump On (odd) Parity */
		/* if (P==1), PC += offset */
		{
			/* Let's set ST_OP. */
			int i;
			TBYTE a;
				a = lastparity;
			i = 0;

			while (a != 0)
			{
				if (a & 1)  /* If current bit is set, */
					i++;      /* increment bit count. */
				a >>= 1U;   /* Next bit. */
			}

			/* Set ST_OP bit. */
			/*if (i & 1)
				CPU_Registers.ST |= ST_OP;
			else
				CPU_Registers.ST &= ~ ST_OP;*/

			/* Jump accordingly. */
			if (i & 1)  /*(CPU_Registers.ST & ST_OP)*/
			{
				JUMP();
				CYCLES(10, 3);
			}
			else
				CYCLES(8, 3);
		}

		break;
	case 13:  /* SBO */
		/* SBO --- Set Bit to One */
		/* CRU Bit = 1 */
		//writeCRU((READREG(R12) >> 1) + offset, 1, 1);
		CLA_SetCRUBit(1, (int8_t)offset);
		CYCLES(12, 8);
		break;
	case 14:  /* SBZ */
		/* SBZ --- Set Bit to Zero */
		/* CRU Bit = 0 */
		//writeCRU((READREG(R12) >> 1) + offset, 1, 0);
		CLA_SetCRUBit(0, (int8_t)offset);
		CYCLES(12, 8);
		break;
	case 15:  /* TB */
		/* TB ---- Test Bit */
		/* EQ = (CRU Bit == 1) */
		setst_e(CLA_GetCRUBit((int8_t)offset), 1);
		CYCLES(12, 8);
		break;
	}
}


/*==========================================================================
   General and One-Register instructions                       >2000->3FFF
 ---------------------------------------------------------------------------

     0 1 2 3-4 5 6 7+8 9 A B-C D E F
    ---------------------------------
    |  opcode   |   D   |TS |   S   |
    ---------------------------------

  COC, CZC, XOR, LDCR, STCR, XOP, MPY, DIV
tms9940 : DCA, DCS, LIIM
==========================================================================*/

/* xop, ldcr and stcr are handled elsewhere */
void h2000(TWORD opcode)
{
	register TWORD dest = (opcode & 0x3C0) >> 6;
	register TWORD src;
	register TWORD value;

	src = decipheraddr(opcode);

	switch ((opcode & 0x1C00) >> 10)
	{
	case 0:   /* COC */
		/* COC --- Compare Ones Corresponding */
		/* status E bit = (S&D == S) */
		dest = ((dest+dest) + CPU_Registers.WP) & ~1;
		src &= ~1;
		value = readword(src);
		setst_e(value & readword(dest), value);
		CYCLES(14, 4);
		break;
	case 1:   /* CZC */
		/* CZC --- Compare Zeroes Corresponding */
		/* status E bit = (S&~D == S) */
		dest = ((dest+dest) + CPU_Registers.WP) & ~1;
		src &= ~1;
		value = readword(src);
		setst_e(value & (~ readword(dest)), value);
		CYCLES(14, 4);
		break;
	case 2:   /* XOR */
		/* XOR --- eXclusive OR */
		/* D ^= S */
		dest = ((dest+dest) + CPU_Registers.WP) & ~1;
		src &= ~1;
		value = readword(dest) ^ readword(src);
		setst_lae(value);
		writeword(dest,value);
		CYCLES(14, 4);
		break;
	/*case 3:*/   /* XOP is implemented elsewhere */
	/*case 4:*/   /* LDCR is implemented elsewhere */
	/*case 5:*/   /* STCR is implemented elsewhere */
	case 6:   /* MPY */
		/* MPY --- MultiPlY  (unsigned) */
		/* Results:  D:D+1 = D*S */
		/* Note that early TMS9995 reportedly perform an extra dummy read in PC space */
		dest = ((dest+dest) + CPU_Registers.WP) & ~1;
		src &= ~1;
		{
			unsigned long prod = ((unsigned long) readword(dest)) * ((unsigned long) readword(src));
			writeword(dest, prod >> 16);
			writeword(dest+2, prod);
		}
		CYCLES(52, 23);
		break;
	case 7:   /* DIV */
		/* DIV --- DIVide    (unsigned) */
		/* D = D/S    D+1 = D%S */
		dest = ((dest+dest) + CPU_Registers.WP) & ~1;
		src &= ~1;
		{
			TWORD d = readword(src);
			TWORD hi = readword(dest);
			unsigned long divq = (((unsigned long) hi) << 16) | readword(dest+2);

			if (d <= hi)
			{
				CPU_Registers.ST |= ST_OV;
				CYCLES(16, 6);
			}
			else
			{
				CPU_Registers.ST &= ~ST_OV;
				writeword(dest, divq/d);
				writeword(dest+2, divq%d);
				/* tms9900 : from 92 to 124, possibly 92 + 2*(number of bits to 1 (or 0?) in quotient) */
				/* tms9995 : 28 is the worst case */
				CYCLES(92, 28);
			}
		}
		break;
	}
}

void xop(TWORD opcode)
{	/* XOP */
	/* XOP --- eXtended OPeration */
	/* WP = *(40h+D), PC = *(42h+D) */
	/* New R13=old WP, New R14=Old IP, New R15=Old ST */
	/* New R11=S */
	/* Xop bit set */

#if(0)
	register TWORD immediate = (opcode & 0x3C0) >> 6;
	register TWORD operand;

	operand = decipheraddr(opcode);

		contextswitch(0x40 + (immediate << 2));
		CPU_Registers.ST |= ST_X;
		WRITEREG(R11, operand);
		CYCLES(36, 15);
		disable_interrupt_recognition = 1;
#else
	// The Tutor does not seem to use this instruction.
	fprintf(stderr, "XOP??\n");
	CYCLES(36,15);
#endif
}

/* LDCR and STCR */
void ldcr_stcr(TWORD opcode)
{
	register TWORD cnt = (opcode & 0x3C0) >> 6;
	register TWORD addr;
	register TWORD value;

	if (cnt == 0)
		cnt = 16;

	if (cnt <= 8)
		addr = decipheraddrbyte(opcode);
	else
		addr = decipheraddr(opcode) & ~1;

	if (opcode < 0x3400)
	{	/* LDCR */
#if(0)
		/* LDCR -- LoaD into CRu */
		/* CRU R12--CRU R12+D-1 set to S */
		if (cnt <= 8)
		{
			/* just for once, tms9995 behaves like earlier 8-bit tms99xx chips */
			/* this must be because instruction decoding is too complex */
			value = readword(addr);
			if (addr & 1)
				value &= 0xFF;
			else
				value = (value >> 8) & 0xFF;
			(void)READREG(cnt+cnt); /*dummy read (reasonnable guess, cf TMS9995)*/
			setst_byte_laep(value);
			writeCRU((READREG(R12) >> 1), cnt, value);
		}
		else
		{
			value = readword(addr);
			(void)READREG(cnt+cnt); /*dummy read (reasonnable guess, cf TMS9995)*/
			setst_lae(value);
			writeCRU((READREG(R12) >> 1), cnt, value);
		}
#else
		// The Tutor does not seem to use this instruction.
		fprintf(stderr, "LDCR??\n");
#endif
		CYCLES(20 + cnt+cnt, 9 + cnt+cnt);
	}
	else
	{	/* STCR */
		/* STCR -- STore from CRu */
		/* S = CRU R12--CRU R12+D-1 */
		
		// In practice, the Tutor seems to mostly pull eight bits at a pop.
		if (cnt <= 8)
		{
			/* just for once, tms9995 behaves like earlier 8-bit tms99xx chips */
			/* this must be because instruction decoding is too complex */
			int value2 = readword(addr);

			READREG(cnt+cnt); /*dummy read (guessed from timing table)*/
			//value = readCRU((READREG(R12) >> 1), cnt);
			value = CLA_GetCRUWord(cnt);
			setst_byte_laep(value);

			if (addr & 1)
				writeword(addr, (value & 0x00FF) | (value2 & 0xFF00));
			else
				writeword(addr, (value2 & 0x00FF) | ((value << 8) & 0xFF00));

			CYCLES(Mooof!, 19 + cnt);
		}
		else
		{
			// This actually doesn't work, but it's here for future use.
			// CLA_GetCRUWord doesn't support anything larger than byte right now.
			(void)readword(addr); /*dummy read*/
			(void)READREG(cnt+cnt); /*dummy read (reasonnable guess, cf TMS9995)*/
			value = CLA_GetCRUWord(cnt);
			setst_lae(value);
			writeword(addr, value);
			CYCLES((cnt != 16) ? 58 : 60, 27 + cnt);
		}
	}
}


/*==========================================================================
   Two-Operand instructions                                    >4000->FFFF
 ---------------------------------------------------------------------------

      0 1 2 3-4 5 6 7+8 9 A B-C D E F
    ----------------------------------
    |opcode|B|TD |   D   |TS |   S   |
    ----------------------------------

  SZC, SZCB, S, SB, C, CB, A, AB, MOV, MOVB, SOC, SOCB
============================================================================*/

/* word instructions */
void h4000w(TWORD opcode)
{
	register TWORD src;
	register TWORD dest;
	register TWORD value;

	src = decipheraddr(opcode) & ~1;
	dest = decipheraddr(opcode >> 6) & ~1;

	switch ((opcode >> 13) & 0x0007)    /* ((opcode & 0xE000) >> 13) */
	{
	case 2:   /* SZC */
		/* SZC --- Set Zeros Corresponding */
		/* D &= ~S */
		value = readword(dest) & (~ readword(src));
		setst_lae(value);
		writeword(dest, value);
		CYCLES(14, 4);
		break;
	case 3:   /* S */
		/* S ----- Subtract */
		/* D -= S */
		value = setst_sub_laeco(readword(dest), readword(src));
		writeword(dest, value);
		CYCLES(14, 4);
		break;
	case 4:   /* C */
		/* C ----- Compare */
		/* ST = (D - S) */
		setst_c_lae(readword(dest), readword(src));
		CYCLES(14, 4);
		break;
	case 5:   /* A */
		/* A ----- Add */
		/* D += S */
		value = setst_add_laeco(readword(dest), readword(src));
		writeword(dest, value);
		CYCLES(14, 4);
		break;
	case 6:   /* MOV */
		/* MOV --- MOVe */
		/* D = S */
		value = readword(src);
		setst_lae(value);
		writeword(dest, value);
		CYCLES(14, 3);
		break;
	case 7:   /* SOC */
		/* SOC --- Set Ones Corresponding */
		/* D |= S */
		value = readword(dest) | readword(src);
		setst_lae(value);
		writeword(dest, value);
		CYCLES(14, 4);
		break;
	}
}

/* byte instruction */
void h4000b(TWORD opcode)
{
	register TWORD src;
	register TWORD dest;
	register TWORD value;

	src = decipheraddrbyte(opcode);
	dest = decipheraddrbyte(opcode >> 6);

	switch ((opcode >> 13) & 0x0007)    /* ((opcode & 0xE000) >> 13) */
	{
	case 2:   /* SZCB */
		/* SZCB -- Set Zeros Corresponding, Byte */
		/* D &= ~S */
		value = readbyte(dest) & (~ readbyte(src));
		setst_byte_laep(value);
		writebyte(dest, value);
		CYCLES(14, 4);
		break;
	case 3:   /* SB */
		/* SB ---- Subtract, Byte */
		/* D -= S */
		value = setst_subbyte_laecop(readbyte(dest), readbyte(src));
		writebyte(dest, value);
		CYCLES(14, 4);
		break;
	case 4:   /* CB */
		/* CB ---- Compare Bytes */
		/* ST = (D - S) */
		value = readbyte(src);
		setst_c_lae(readbyte(dest)<<8, value<<8);
		lastparity = value;
		CYCLES(14, 4);
		break;
	case 5:   /* AB */
		/* AB ---- Add, Byte */
		/* D += S */
		value = setst_addbyte_laecop(readbyte(dest), readbyte(src));
		writebyte(dest, value);
		break;
	case 6:   /* MOVB */
		/* MOVB -- MOVe Bytes */
		/* D = S */
		value = readbyte(src);
		setst_byte_laep(value);
		writebyte(dest, value);
		CYCLES(14, 3);
		break;
	case 7:   /* SOCB */
		/* SOCB -- Set Ones Corresponding, Byte */
		/* D |= S */
		value = readbyte(dest) | readbyte(src);
		setst_byte_laep(value);
		writebyte(dest, value);
		CYCLES(14, 4);
		break;
	}
}

	/* tms9989 and tms9995 include 4 extra instructions, and one additional instruction type */

	void (* jumptable[256])(TWORD) =
	{
		&h0040,&h0100,&h0200,&h0200,&h0400,&h0400,&h0400,&h0400,
		&h0800,&h0800,&h0800,&h0800,&illegal,&illegal,&illegal,&illegal,
		&h1000,&h1000,&h1000,&h1000,&h1000,&h1000,&h1000,&h1000,
		&h1000,&h1000,&h1000,&h1000,&h1000,&h1000,&h1000,&h1000,
		&h2000,&h2000,&h2000,&h2000,&h2000,&h2000,&h2000,&h2000,
		&h2000,&h2000,&h2000,&h2000,&xop,&xop,&xop,&xop,
		&ldcr_stcr,&ldcr_stcr,&ldcr_stcr,&ldcr_stcr,&ldcr_stcr,&ldcr_stcr,&ldcr_stcr,&ldcr_stcr,
		&h2000,&h2000,&h2000,&h2000,&h2000,&h2000,&h2000,&h2000,
		&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,
		&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,
		&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,
		&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,
		&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,
		&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,
		&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,
		&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,
		&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,
		&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,
		&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,
		&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,
		&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,
		&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,
		&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,
		&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,
		&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,
		&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,
		&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,
		&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,
		&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,
		&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,&h4000w,
		&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,
		&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b,&h4000b
	};

inline void execute(TWORD opcode)
{
	(* jumptable[opcode >> 8])(opcode);
}

/*** Public interface. ***/

// Called to initialize the Core, usually at CPU reset.
// TMS9995_Init calls this directly.
void CoreInit()
{
	CPU_Registers.ST = 0; /* TMS9980 and TMS9995 Data Book say so */
	setstat();
	field_interrupt();
}

// Runs the next instruction, as pointed to by PC.
// Upon completion, PC will be at the next instruction to execute.
// Returns number of CPU cycles used executing the instruction.
// TMS9995_ExecuteInstruction calls this directly.
int CoreOp()
{
	TMS99XX_ICOUNT = 0;

// clang has to have this spelled out for it.
#if(1)
	TWORD opcode = fetch();
	//fprintf(stdout, "%04x %04x|", (CPU_Registers.PC-2), opcode);
	execute(opcode);
#else
	execute(fetch());
#endif
	return TMS99XX_ICOUNT;
}
	
