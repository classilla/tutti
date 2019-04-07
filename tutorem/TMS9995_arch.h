/* TMS 9995 emulator...
	Ian Gledhill 31/03/03, Cameron Kaiser, Raphael Nabet */

/* Registers:
	
	  Bit	Name		Function
		0	LGT			Logical Greater Than
		1	AGT			Arithmetic Greater Than
		2	Equal		When two operands are equal
		3	Carry		When a bit is carried
		4	Overflow	When overflowed
		5	Odd Parity	When result has odd number of 1's
		6	XOP			When eXtended OPeration is executed
		7	Unused
		8	Unused
		9	Unused
		10	AOE			Arithmetic Overflow Enable
		11	Unused
		12	IM0			Interrupt Mask
		13	IM1			Interrupt Mask
		14	IM2			Interrupt Mask
		15	IM3			Interrupt Mask
*/

/* These bits are set by every compare, move and ALU operation.
 * (Well, COC, CZC and TB only set the E bit, but they're weird. */
#define ST_LGT 0x8000 /* Logical Greater Than (strictly) */
#define ST_AGT 0x4000 /* Arithmetical Greater Than (strictly) */
#define ST_EQ  0x2000 /* Equal */
/* These bits are set by arithmetic operations, when it makes sense. */
#define ST_C   0x1000 /* Carry */
#define ST_OV  0x0800 /* OVerflow (overflow with operations on signed integers, */
                      /* and when the result of a 32bits:16bits division cannot fit in a 16-bit word.) */
/* This bit is set by move and arithmetic operations WHEN THEY USE BYTE OPERANDS. */
#define ST_OP  0x0400 /* Odd Parity */
/* This bit is set by the XOP instruction. */
#define ST_X   0x0200 /* Xop */

typedef unsigned short TWORD;
typedef unsigned char TBYTE;

typedef struct CPU_Register_Struct
{
	TWORD WP, PC, ST, IR;
} CPU_Registers_Type;

typedef struct CPU_WP_Register_Struct
{
	TWORD R0, R1, R2, R3, R4, R5, R6, R7, R8, R9, R10, R11, R12, R13, R14, R15;
} CPU_WP_Registers_Type;

extern CPU_Registers_Type CPU_Registers;
extern CPU_WP_Registers_Type *CPU_WP_Registers;
extern unsigned char memoryMap[65536];

