/* Debugger.c
	Ian Gledhill 02/04/03 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "SDL/SDL.h"

#include "TMS9995.h"
#include "TMS9918ANL.h"
#include "Debugger.h"
#include "Disassemble.h"

TMS9918Screen gDebugScreen, gTIScreen;
DebuggerType gDebugger;

void Debugger_printf(int x, int y, const char *text, ... )
{
	va_list thisList;
	char Buffer[40], *pointer;
	unsigned char *bitmapPointer;

	va_start(thisList, text);

	vsprintf(Buffer, text, thisList);

	pointer = (char *)Buffer;
	bitmapPointer = &(gDebugScreen.data[y][x]);
	while (*pointer)
	{
		*bitmapPointer++ = *pointer++;
	}
}

void Debugger_ClearScreen()
{
	int x, y;
	for (x=0 ; x<40 ; x++)
		for (y=0 ; y<24 ; y++)
		{
			gDebugScreen.data[y][x] = 0;
			gTIScreen.data[y][x] = 0;
		}
}

void Debugger_Init()
{
	gDebugger.breakpointHit =
	gDebugger.inInterrupt =
	gDebugger.dumpToStdout = 0;

	gDebugger.lastPC = -1;

	gDebugger.TMS9995_memoryTop = 0xF000;
	gDebugger.TMS9918_memoryTop = 0x0000;
	gDebugger.memoryTop = gDebugger.TMS9918_memoryTop;
	gDebugger.memoryMode = CPU_TMS9918;

	Debugger_SwitchToCPU();

	Debugger_UpdateScreen();

	Debugger_Disable();
//	Debugger_Enable();
}

void Debugger_SwitchToCPU()
{
	int y;

	if (gDebugger.memoryMode == CPU_TMS9995)
		return;

	Debugger_ClearScreen();
	gDebugger.memoryMode = CPU_TMS9995;
	gDebugger.TMS9918_memoryTop = gDebugger.memoryTop;
	gDebugger.memoryTop = gDebugger.TMS9995_memoryTop;
	Debugger_printf(0, 0, "CPU REGISTERS");
	Debugger_printf(0, 1, "PC");
	Debugger_printf(0, 2, "ST");
	Debugger_printf(0, 3, "WP");

	Debugger_printf(24,0, "MEMORY:");

	Debugger_printf(0, 5, "WORKSPACE");
	for (y=0 ; y<16 ; y++)
		Debugger_printf((y/8)*9,(y%8)+6, "R%d", y);

	Debugger_UpdateScreen();
}

// Misnomer kept because I'm too lazy.
void Debugger_SwitchToVGP()
{
	if (gDebugger.memoryMode == CPU_TMS9918)
		return;
	Debugger_ClearScreen();
	gDebugger.memoryMode = CPU_TMS9918;
	gDebugger.TMS9995_memoryTop = gDebugger.memoryTop;
	gDebugger.memoryTop = gDebugger.TMS9918_memoryTop;
	Debugger_printf(0, 0, "VDP REGISTERS");
	Debugger_printf(0, 1, "MP");
	Debugger_printf(0, 2, "SR");
	Debugger_printf(0, 4, "R0");
	Debugger_printf(0, 5, "R1");
	Debugger_printf(0, 6, "R2");
	Debugger_printf(0, 7, "R3");
	Debugger_printf(0, 8, "R4");
	Debugger_printf(0, 9, "R5");
	Debugger_printf(0, 10, "R6");
	Debugger_printf(0, 11, "R7");

	Debugger_printf(24,0, "MEMORY:");

	Debugger_UpdateScreen();
}

void Debugger_UpdateScreen()
{
	int x, y;

	for (x=0 ; x<40 ; x++)
		for (y=0 ; y<24 ; y++)
			TMS9918_PrintDebugFont(x, y, gDebugScreen.data[y][x]);
}

void Debugger_UpdateCharacters(int characterX1, int characterY, int characterX2)
{
	int x;
	for (x=characterX1 ; x<characterX2 ; x++)
		TMS9918_PrintDebugFont(x, characterY, gDebugScreen.data[characterY][x]);
}

void Debugger_UpdateTMS9995()
{
	int x, y;
	TWORD *opcode;
	char Buffer[40];
	int byteNumber = CPU_Registers.PC;
	int doPrint = gDebugger.dumpToStdout;

	Debugger_printf(4, 1, "%04X", CPU_Registers.PC);
	Debugger_printf(4, 2, "%04X", CPU_Registers.ST);
	Debugger_printf(4, 3, "%04X", CPU_Registers.WP);

	Debugger_UpdateCharacters(4, 1, 8);
	Debugger_UpdateCharacters(4, 2, 8);
	Debugger_UpdateCharacters(4, 3, 8);

	for (y=0 ; y<16 ; y++)
	{
		Debugger_printf(18,y, "%04X", gDebugger.memoryTop+y*8);
		for (x=0 ; x<8 ; x++)
			Debugger_printf(24+x*2,y, "%02X", (unsigned char )(memoryMap[gDebugger.memoryTop+y*8+x]));
		Debugger_UpdateCharacters(18,y,40);

		Debugger_printf((y/8)*9+4, (y%8)+6, "%04X", TMS9995_GetRegister(y));
		Debugger_UpdateCharacters((y/8)*9+4,(y%8)+6,(y/8)*9+8);
	}

	if (gDebugger.lastPC == byteNumber) 
		doPrint = 0;
	else
		gDebugger.lastPC = byteNumber;
	opcode = (TWORD *)(memoryMap+CPU_Registers.PC);
	//Debugger_printf(0, 17, "%04X %04X ", *opcode, *(opcode+1));
	if (doPrint) {
		fprintf(stdout, 
"ST: %04x  2--- 3--- 4--- 5--- 6--- 7--- 8--- 9--- 10-- 11-- 12-- 13-- 14-- 15--\n",
CPU_Registers.ST);
		fprintf(stdout, "%04x", TMS9995_GetRegister(0));
		for(y=1;y<16;y++) {
			fprintf(stdout, " %04x", TMS9995_GetRegister(y));
		}
		fprintf(stdout, "\n");
	}
	DisassembleOpcodeMaybePrint(doPrint,
		opcode, Buffer, &byteNumber, &opcode);

	//Debugger_printf(10, 17, "%s", Buffer);
	Debugger_printf(0, 17, "%s", Buffer);
	Debugger_printf(strlen(Buffer), 25, "                  ");
	Debugger_UpdateCharacters(0, 17, 30);
}

void Debugger_UpdateTMS9918()
{
	int x, y;
	
	Debugger_printf(4, 1, "%04X", VDP_Registers.MP);
	Debugger_printf(4, 2, "%04X", VDP_Registers.ST);
	Debugger_UpdateCharacters(4, 1, 8);
	Debugger_UpdateCharacters(4, 2, 8);

	for (y=4 ; y<12 ; y++)
	{
		Debugger_printf(4, y, "%04X", VDP_Registers.Registers[y-4]);
		Debugger_UpdateCharacters(4, y, 8);
	}

	for (y=0 ; y<16 ; y++)
	{
		Debugger_printf(18,y, "%04X", gDebugger.memoryTop+y*8);
		for (x=0 ; x<8 ; x++)
			Debugger_printf(24+x*2,y, "%02X", (unsigned char )(VDP_MemoryMap[gDebugger.memoryTop+y*8+x]));
		Debugger_UpdateCharacters(18,y,40);
	}


}

void Debugger_Update()
{
	if (gDebugger.memoryMode == CPU_TMS9995)
		Debugger_UpdateTMS9995();
	else
		Debugger_UpdateTMS9918();
}

void Debugger_Disable()
{
	gDebugger.enabled = 0;
	gDebugger.breakpointHit = 0;
	TMS9918_Force_Redraw();
}

void Debugger_Enable()
{
	gDebugger.enabled = 1;

	if (gDebugger.memoryMode == CPU_TMS9995)
	{
		Debugger_SwitchToVGP();
		Debugger_SwitchToCPU();
	}
	else
	{
		Debugger_SwitchToCPU();
		Debugger_SwitchToVGP();
	}
		
	Debugger_Update();
	TMS9918_Update();
}
