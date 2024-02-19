/* Disassemble.c
	Used for disassembling the opcode!
	Ian Gledhill 02/04/03 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "TMS9995_arch.h"

typedef TWORD Opcode;

void DisassembleOpcode(Opcode *thisOpcode, char *tokenBuffer, int *currentByteNumber, Opcode **currentOpcode);
void SwitchEndian(Opcode *thisOpcode);
void Diss_DualOpWithMultiAddress(Opcode thisOpcode, char *tokenBuffer, int *currentByteNumber, Opcode **currentOpcode);
void Diss_DualOpWithMultiAddressDestIsWR(Opcode thisOpcode, char *tokenBuffer, int *currentByteNumber, Opcode **currentOpcode);
void Diss_SingleOp(Opcode thisOpcode, char *tokenBuffer, int *currentByteNumber, Opcode **currentOpcode);
void Diss_CRUMultiBit(Opcode thisOpcode, char *tokenBuffer, int *currentByteNumber, Opcode **currentOpcode);
void Diss_CRUSingleBit(Opcode thisOpcode, char *tokenBuffer, int *currentByteNumber, Opcode **currentOpcode);
void Diss_JumpInstruction(Opcode thisOpcode, char *tokenBuffer, int *currentByteNumber, Opcode **currentOpcode);
void Diss_ShiftInstruction(Opcode thisOpcode, char *tokenBuffer, int *currentByteNumber, Opcode **currentOpcode);
void Diss_ImmediateRegister(Opcode thisOpcode, char *tokenBuffer, int *currentByteNumber, Opcode **currentOpcode);
void Diss_RegisterLoadImmediate(Opcode thisOpcode, char *tokenBuffer, int *currentByteNumber, Opcode **currentOpcode);
void Diss_RegisterStore(Opcode thisOpcode, char *tokenBuffer, int *currentByteNumber, Opcode **currentOpcode);

void SwitchEndian(Opcode *thisOpcode);

void DisassembleOpcode(Opcode *thisOpcode, char *tokenBuffer, int *currentByteNumber, Opcode **currentOpcode)
{
	Opcode code = *thisOpcode;
	char opType;

	SwitchEndian(&code);
	opType = (code & 0xE000) >> 13;

	tokenBuffer[0] = 0;

	if (opType > 0x01)
	{
		Diss_DualOpWithMultiAddress(code, tokenBuffer, currentByteNumber, currentOpcode);
	}
	else if (opType == 0x01)
		Diss_DualOpWithMultiAddressDestIsWR(code, tokenBuffer, currentByteNumber, currentOpcode);
	else if (opType == 0x00)
		Diss_SingleOp(code, tokenBuffer, currentByteNumber, currentOpcode);
}

void DisassembleOpcodeMaybePrint(int doPrint, Opcode *thisOpcode, char *tokenBuffer, int *currentByteNumber, Opcode **currentOpcode)
{
	int spa=15;
	int oposn=*currentByteNumber;
	TWORD *byteref = (TWORD *)(thisOpcode + 1);
	TWORD sopcode = (TWORD)(*thisOpcode);

	if (!doPrint) {
		DisassembleOpcode(thisOpcode, tokenBuffer,
			currentByteNumber, currentOpcode);
		return;
	}
	SwitchEndian(&sopcode);
	fprintf(stdout, "%04X: %04X ", *currentByteNumber, sopcode);
	DisassembleOpcode(thisOpcode, tokenBuffer,
		currentByteNumber, currentOpcode);
	while(*currentByteNumber > oposn) {
		oposn += 2;
		sopcode = *byteref;
		SwitchEndian(&sopcode);
		fprintf(stdout, "%04X ", sopcode);
		byteref++;
		spa -= 5;
	}
	while(--spa) { fprintf(stdout, " "); }
	fprintf(stdout, "%s\n", tokenBuffer);
}

#define ADDSTR(x) sprintf(tokenBuffer+strlen(tokenBuffer), x);
#define ADDSTR2(x,y) sprintf(tokenBuffer+strlen(tokenBuffer), x, y);
#define ADDSTR3(x,y,z) sprintf(tokenBuffer+strlen(tokenBuffer), x, y, z);
#define ADDFOLLOWINGWORD		{Opcode nextValue = *(*currentOpcode+1); \
								SwitchEndian(&nextValue); \
								ADDSTR2("@>%04X", nextValue); \
								*currentByteNumber += 2; \
								*currentOpcode += 1;}
#define ADDFOLLOWINGWORDNOAT	{Opcode nextValue = *(*currentOpcode+1); \
								SwitchEndian(&nextValue); \
								ADDSTR2(">%04X", nextValue); \
								*currentByteNumber += 2; \
								*currentOpcode += 1;}
#define ADDFOLLOWINGINDEX(x)	{Opcode nextValue = *(*currentOpcode+1); \
								SwitchEndian(&nextValue); \
								ADDSTR3("@>%04X(R%d)", nextValue, x); \
								*currentByteNumber += 2; \
								*currentOpcode += 1;}
#define ADDTOKEN(x,y) sprintf(tokenBuffer, "%s ", x[y]);
#define ADDBTOKEN(x,y) sprintf(tokenBuffer, "%sB ", x[y]);
#define ADDREG(x) ADDSTR2("R%d", x)
#define ADDREGIND(x) ADDSTR2("*R%d", x)
#define ADDREGINDINC(x) ADDSTR3("*R%d%s", x, "+")
#define ADDINT(x) ADDSTR2("%d", x)
#define ADDBYTE(x) ADDSTR2(">%02x", x)
#define ADDWORD(x) ADDSTR2("@>%04X", x)
#define ADDWORDNOAT(x) ADDSTR2(">%04X", x)
#define ADDCOMMA ADDSTR(", ")
#define ADDACCORDINGTOT(x)		if (t ## x == 0x00)				\
									ADDREG(x)					\
								else if (t ## x == 0x01)		\
									ADDREGIND(x)				\
								else if (t ## x == 0x02)		\
								{								\
									if (x)						\
										ADDFOLLOWINGINDEX(x)	\
									else						\
										ADDFOLLOWINGWORD		\
								}								\
								else if (t ## x == 0x03)		\
									ADDREGINDINC(x)


char MultiOpTable[6][20] = {"SZC","S","C","A","MOV","SOC"};

void Diss_DualOpWithMultiAddress(Opcode code, char *tokenBuffer, int *currentByteNumber, Opcode **currentOpcode)
{
	int thisCode = ((code >> 13) & 0x07) - 2;
	int bBit = (code >> 12) & 0x01;
	int tD, D, tS, S;

	if (bBit)
	{
		ADDBTOKEN(MultiOpTable, thisCode)
	}
	else
		ADDTOKEN(MultiOpTable, thisCode)
	
	tD = (code & 0x0C00) >> 10;
	D = (code & 0x03D0) >> 6;
	tS = (code & 0x0030) >> 4;
	S = (code & 0x000F) >> 0;

	ADDACCORDINGTOT(S)
	ADDSTR(", ");
	ADDACCORDINGTOT(D)

}

char DOPWMADIWRTable[8][20] = {"COC", "CZC", "XOR", "WRONG", "WRONG", "WRONG", "MPY", "DIV"};

void Diss_DualOpWithMultiAddressDestIsWR(Opcode thisOpcode, char *tokenBuffer, int *currentByteNumber, Opcode **currentOpcode)
{
	char opType = (thisOpcode & 0x1C00) >> 10;
	if (opType == 0x03)
		sprintf(tokenBuffer, "XOP");
	else if (opType == 0x04 || opType == 0x05)
		Diss_CRUMultiBit(thisOpcode, tokenBuffer, currentByteNumber, currentOpcode);	
	else
	{
		int tS = (thisOpcode & 0x0030) >> 4;
		int S = (thisOpcode & 0x000F);
		int D = (thisOpcode & 0x03C0) >> 6;

		ADDTOKEN(DOPWMADIWRTable, (thisOpcode & 0x1C00) >> 10)
		ADDACCORDINGTOT(S)
		ADDCOMMA
		ADDREG(D)
	}
}

char SingleOpTable[16][20] = {	"BLWP", "B",   "X",    "CLR", "NEG",  "INV",  "INC",   "INCT",
								"DEC",  "DECT", "BL",  "SWPB", "SETO", "ABS", "WRONG", "WRONG"};

char DataTable[1][20]={"DATA"};
char SingleOpDivTable[2][20] = {"MPYS", "DIVS"};

void Diss_SingleOp(Opcode thisOpcode, char *tokenBuffer, int *currentByteNumber, Opcode **currentOpcode)
{
	char opType = (thisOpcode & 0x1C00) >> 10;
	char opType2 = (thisOpcode & 0x0300) >> 8;
	char opType3 = (thisOpcode & 0x0080) >> 7;

	if (opType == 0x00 && opType2 == 0x00 && !opType3)
	{
		ADDTOKEN(DataTable, 0)
		ADDWORDNOAT(thisOpcode)
		return;
	}
	if (opType == 0x01)
	{
		int tS = (thisOpcode & 0x0030) >> 4;
		int S = (thisOpcode & 0x000F);

		ADDTOKEN(SingleOpTable, (thisOpcode & 0x03C0) >> 6)
		ADDACCORDINGTOT(S)
		return;
	}
	if (opType == 0x02)
	{
		Diss_ShiftInstruction(thisOpcode, tokenBuffer, currentByteNumber, currentOpcode);
		return;
	}
	if (opType == 0x07 && opType2)
	{
		Diss_CRUSingleBit(thisOpcode, tokenBuffer, currentByteNumber, currentOpcode);
		return;
	}
	if (opType && ((thisOpcode & 0xF000) == 0x1000))
	{
		Diss_JumpInstruction(thisOpcode, tokenBuffer, currentByteNumber, currentOpcode);
		return;
	}
	if (!opType && opType2 == 0x01 && opType3)
	{	
		int tS = (thisOpcode & 0x0030) >> 4;
		int S = (thisOpcode & 0x000F);

		ADDTOKEN(SingleOpDivTable, (thisOpcode & 0x0040) >> 6);
		ADDACCORDINGTOT(S);
		return;
	}
	if (thisOpcode & 0x0E00 || opType3)
	{
		Diss_ImmediateRegister(thisOpcode, tokenBuffer, currentByteNumber, currentOpcode);
		return;
	}
	ADDTOKEN(DataTable, 0)
	ADDWORDNOAT(thisOpcode)
}

char CRUMTable[2][20] = {"LDCR", "STCR"};

void Diss_CRUMultiBit(Opcode thisOpcode, char *tokenBuffer, int *currentByteNumber, Opcode **currentOpcode)
{
	int tS = (thisOpcode & 0x0030) >> 4;
	int S = (thisOpcode & 0x000F);
	int C = (thisOpcode & 0x03C0) >> 6;

	ADDTOKEN(CRUMTable, (thisOpcode & 0x0400) >> 10)
	ADDACCORDINGTOT(S)
	ADDCOMMA
	ADDINT(C)
}

char CRUSTable[4][20] = {"WRONG", "SBO", "SBZ", "TB"};

void Diss_CRUSingleBit(Opcode thisOpcode, char *tokenBuffer, int *currentByteNumber, Opcode **currentOpcode)
{
	ADDTOKEN(CRUSTable, (thisOpcode & 0x0300) >> 8)
	ADDBYTE(thisOpcode & 0x00FF)	
}

char JumpTable[16][20]={"JMP", "JLT", "JLE", "JEQ", "JHE", "JGT",   "JNE",   "JNC",
						"JOC", "JNO", "JL",  "JH",  "JOP", "WRONG", "WRONG", "WRONG"};
char NOPTable[1][20]={"NOP"};

void Diss_JumpInstruction(Opcode thisOpcode, char *tokenBuffer, int *currentByteNumber, Opcode **currentOpcode)
{
	signed char offset;
	unsigned short address;
	offset = thisOpcode & 0x00FF;

	if (((thisOpcode & 0xFF00) != 0x1000) || offset)
	{
		address = *currentByteNumber + 2 + offset + offset; 
		ADDTOKEN(JumpTable, (thisOpcode & 0x0F00) >> 8)
		ADDWORDNOAT(address)
	}
	else
	{
		ADDTOKEN(NOPTable, 0)
	}
}

char ShiftTable[4][20]={"SRA","SRL","SLA","SRC"};

void Diss_ShiftInstruction(Opcode thisOpcode, char *tokenBuffer, int *currentByteNumber, Opcode **currentOpcode)
{
	ADDTOKEN(ShiftTable, (thisOpcode & 0x0300) >> 8)
	ADDREG(thisOpcode & 0x000F)
	ADDCOMMA
	ADDINT((thisOpcode & 0x00F0) >> 4)
}

char ImmRegTable[8][20]={"LI","AI","ANDI","ORI","CI","WRONG","WRONG","WRONG"};
char RTWPTable[1][20]={"RTWP"};

void Diss_ImmediateRegister(Opcode thisOpcode, char *tokenBuffer, int *currentByteNumber, Opcode **currentOpcode)
{
	char opType = (thisOpcode & 0x0380) >> 7;		// D6-D8
	char opType2 = (thisOpcode & 0x0060) >> 5;		// D9-D10
	if ((opType == 0x04 && opType2 < 4) || (opType == 0x05 && !opType2))
	{
		ADDTOKEN(ImmRegTable, (thisOpcode & 0x00E0) >> 5)
		ADDREG(thisOpcode & 0x000F)
		ADDCOMMA
		ADDFOLLOWINGWORDNOAT
	}
	else if ((opType == 0x06 && (opType2 == 0x03 || opType2 == 0x00)) ||
			(opType == 0x05 && opType2 == 0x03))
		Diss_RegisterLoadImmediate(thisOpcode, tokenBuffer, currentByteNumber, currentOpcode);
	else if ((opType == 0x05 || opType == 0x01) && opType2 == 0x00)
		Diss_RegisterStore(thisOpcode, tokenBuffer, currentByteNumber, currentOpcode);
	else if (opType == 0x07 && opType2 == 0x00)
		ADDTOKEN(RTWPTable, 0)	
	else if (opType >= 0x06)
		sprintf(tokenBuffer, "EXTERNAL");
	else
	{
		ADDTOKEN(DataTable, 0)
		ADDWORDNOAT(thisOpcode)
	}

}

char RLITable[2][20]={"LWPI","LIMI"};

void Diss_RegisterLoadImmediate(Opcode thisOpcode, char *tokenBuffer, int *currentByteNumber, Opcode **currentOpcode)
{
	ADDTOKEN(RLITable, (thisOpcode & 0x0100) >> 8)
	ADDFOLLOWINGWORD
}

char RSTable[8][20]={"LST", "LWP", "STWP", "WRONG", "STST", "WRONG", "WRONG", "WRONG"};

void Diss_RegisterStore(Opcode thisOpcode, char *tokenBuffer, int *currentByteNumber, Opcode **currentOpcode)
{
	ADDTOKEN(RSTable, (thisOpcode & 0x0070) >> 4)
	ADDREG(thisOpcode & 0x000F)
}
