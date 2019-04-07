/* TMS 9995 emulator...
	Ian Gledhill 31/03/03 */
/* Expanded, debugged and tape support by Cameron Kaiser (C)2015-2019 */

/* This used to contain the CPU emulation, but that has largely
   moved to Core except for system wide things like resets and IRQs
   as well as the endian-dependent portions of the CPU emulation.
   CLA memory mapping and miscellaneous I/O mostly still live here, however. */

int extra = 0; // leftover cycles from Decrementer batching

#include "sys.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <SDL/SDL.h>

#include "TMS9995.h"
#include "SN76489AN.h"
#include "TMS9918ANL.h"
#include "Debugger.h"

#if defined(__clang__) || defined(__GNUC__)
#  define LIKELY(x)   (__builtin_expect(!!(x), 1))
#  define UNLIKELY(x) (__builtin_expect(!!(x), 0))
#else
#  define LIKELY(x)   (!!(x))
#  define UNLIKELY(x) (!!(x))
#endif

#ifdef DEBUG
void OutputDebugString_ (char *x) { fprintf(stderr, "%s", x); }
void AssertProperRegister(int r) {
	if (r > 16 || r < 0) {
		fprintf(stderr, "Assertion: %i out of range\n", r);
		exit(1);
	}
}
#else
#define OutputDebugString_(x) ;
#define AssertProperRegister(x) ;
#endif

/* Tape support. */
int gGotFilename=0; // Filename received
int gTapeMode = 0; // 1 = load, 2 = save
int gGotFirstWrite=0; // Skipped spurious first write
int gBitWaiting = 0; // Toggle flag for interrupts.
int gCRUTapeLine = 0; // CRU logical address 0xED00; see tape comments

/* Callbacks in the main loop. */
extern void SetupTapeLoad();
extern void FinishTapeLoad();
extern void SetupTapeSave();
extern void TapeOutput(int bit);
extern int  TapeInput();
extern void TapeInputSkipSync();
extern int  TapeInputReadByte();
extern void FinishTapeSave();

inline void WriteTWord(TWORD *address, TWORD value);
inline void WriteTByte(TBYTE *address, TBYTE value);
TWORD ReadTWord(TWORD *address);
TBYTE ReadTByte(TBYTE *address);
void TMS9995_DecDecrementer(int clocks);

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

	Memory:

	0x0000	0x7FFF		BIOS
						|-WP = F0A0
						\-PC = 028A
	0x8000	0x87FF		Unused?
	0x8800	0x8FFF		I/O ports (0x88xx and 0x8Cxx)
	0x9000	0xEFFF		BASIC?
	0xE000	0xE001		Mode = 0 VDP Data R/W
	0xE002	0xE003		Mode = 1 VDP Register R/W
	0xF000	0xF0FF		CPU RAM
	0xFFFA	0xFFFB		Decrementer
	0xFFFC	0xFFFF		NMI Vector

*/

CPU_Registers_Type CPU_Registers;
CPU_WP_Registers_Type *CPU_WP_Registers;

TWORD scratchpadTword;

TWORD decrementerBase;

unsigned char memoryMap[65536];
extern int gCycle;
extern int gPastewait;
extern int runDebugEnabled;
extern char gKeyboard[SDLK_LAST];
int gDecrementerEnabled=0, gDecrementerMode=0, gBasicBreak=0, gHandlerBreak=0;

inline TWORD SwitchEndianAlways(TWORD *thisWord)
{
	char LSB = *thisWord & 0xFF;
	*thisWord >>= 8;
	*thisWord &= 0xFF;
	*thisWord |= (LSB << 8);
	return *thisWord;
}

#if SDL_BYTEORDER == SDL_LIL_ENDIAN
inline TWORD SwitchEndian(TWORD *thisWord)
{
	return SwitchEndianAlways(thisWord);
}
#else
/* for external consumers */
inline TWORD SwitchEndian(TWORD *thisWord) { return *thisWord; }
/* speed up this file */
#define SwitchEndian(x) ;
#endif

void TMS9995_StopTapeInterface()
{
        gGotFilename = 0;
	gTapeMode = 0;
}

inline void TMS9995_SetWP(TWORD address)
{
	CPU_WP_Registers = (CPU_WP_Registers_Type *)((TWORD *)(memoryMap+CPU_Registers.WP));
#ifdef DEBUG
	if (address & 0x01)
{
fprintf(stderr, "breakpoint: odd address for WP: >%04x\n", address);
		gDebugger.breakpointHit = 1;
}
#endif
}

void TMS9995_Init(char *ROM1, char *ROM2)
{
	TMS9995_StopTapeInterface();
	gGotFirstWrite = 0;

	memset(memoryMap, 0xf0, 65536);
	memcpy(memoryMap, ROM1, 0x8000);
	memcpy(memoryMap+0x8000, ROM2, 0x4000);

	CPU_Registers.WP = *((TWORD *)memoryMap);
	CPU_Registers.PC = *((TWORD *)(memoryMap+2));
	CPU_Registers.ST = 0x0000;
	decrementerBase = 0x0000;
	extra = 0;

	SwitchEndian(&CPU_Registers.WP);
	SwitchEndian(&CPU_Registers.PC);

	TMS9995_SetWP(CPU_Registers.WP);

	CoreInit();
	fprintf(stderr,
		"CPU created....\n WP is 0x%x, PC is 0x%x.\n",
			CPU_Registers.WP, CPU_Registers.PC);
}

void TMS9995_TriggerInterrupt(int interruptLevel)
{
	TWORD oldPC, oldWP, oldST;
	TWORD operandValue;
	TWORD *operand = (TWORD *)(memoryMap + (interruptLevel << 2));
	
	if (TMS9995_GetInterruptMask() < interruptLevel) return;
	
	// PC is pointing to the next instruction. It shouldn't be.
	oldPC = CPU_Registers.PC;
	oldWP = CPU_Registers.WP;
	oldST = CPU_Registers.ST;

	operandValue = *operand;
	SwitchEndian(&operandValue);
	CPU_Registers.PC = *(operand+1);
	SwitchEndian(&CPU_Registers.PC);
	CPU_Registers.WP = *operand;
	SwitchEndian(&(CPU_Registers.WP));
	TMS9995_SetWP(CPU_Registers.WP);
	TMS9995_SetRegister(13, oldWP);
	TMS9995_SetRegister(14, oldPC);
	TMS9995_SetRegister(15, oldST);

	gDebugger.inInterrupt = 1;
if (interruptLevel == 4) {
/*
fprintf(stderr, "f071: %02x f0e6: %04x",
	(uint8_t)(*((uint8_t *)(memoryMap+0xf071))),
	(uint16_t)(*((uint16_t *)(memoryMap+0xf0e6))));
*/
fprintf(stderr, "=");
}
	//gDebugger.breakpointHit = 1;

	gCycle += 14;
	CPU_Registers.ST &= 0xfff8;
	CPU_Registers.ST |= interruptLevel;
}

void TMS9995_HandledInterrupt()
{
	gDebugger.inInterrupt = 0;
}

void TMS9995_TriggerDebugger()
{
	gDebugger.breakpointHit = 1;
}

/*

Decrementer behaviour: setting to zero (default) disables the decrementer.
On power-on, the decrementer is indeed zero.

Otherwise, when the decrementer reaches zero, the decrementer is reloaded
with the same initial value, and IRQ 3 is triggered.

The decrementer drops every fourth clock cycle. We usually just batch those.

This doesn't support the event counter mode yet and we probably need that to
finally get rid of the tape traps.

*/

void TMS9995_DecDecrementer(int clocks)
{
	TWORD decrementer = *((TWORD *)(memoryMap+0xFFFA));
	TWORD reload;
	int decval, effclocks;
	
	if (!decrementerBase) return; // no decrementer until activated
	
	SwitchEndian(&decrementer);
	decval = (int)decrementer;

	// The clocks are raw clocks, but we should only be counting every 4.
	// Don't drop clock pulses!
	effclocks = clocks + extra;
	decval -= (effclocks >> 2);
	extra = effclocks & 3; // save extra clocks for next batch

	if (decval <= 0) {
		// Don't use WriteTWord.
		// Remember to account for the extra interrupt cycles.

		//reload = (TWORD)(decval-14+decrementerBase);
		reload = (TWORD)(decval+decrementerBase);
		SwitchEndian(&reload);
		*(TWORD *)(memoryMap+0xFFFA) = reload;
		TMS9995_TriggerInterrupt(0x03);
	} else {
		reload = (TWORD)decval;
		SwitchEndian(&reload);
		*(TWORD *)(memoryMap+0xFFFA) = reload;
	}
}

void TMS9995_TriggerDecrementer()
{
	if (!decrementerBase) return;
	*(TWORD *)(memoryMap+0xFFFA) = 0x0000;
}

inline int TMS9995_GetInterruptMask()
{
	return CPU_Registers.ST & 0x0F;
}

inline TWORD TMS9995_GetNextInstruction()
{
	TWORD nextInstruction;

	nextInstruction = *((TWORD *)(memoryMap+CPU_Registers.PC));
	SwitchEndian(&nextInstruction);
	return nextInstruction;
}

inline TWORD TMS9995_FetchWord(TWORD address)
{
	TWORD value = (TWORD)ReadTWord((TWORD *)(memoryMap+address));
	SwitchEndian(&value);
	return value;
}

inline void TMS9995_WriteWord(TWORD address, TWORD rvalue)
{
	TWORD value = rvalue;
	SwitchEndian(&value);
	WriteTWord((TWORD *)(memoryMap+address), value);
}

inline void TMS9995_WriteByte(TWORD address, TBYTE value)
{
	WriteTByte((TBYTE *)(memoryMap+address), value);
}

inline TWORD TMS9995_GetRegister(int registerNumber)
{
	AssertProperRegister(registerNumber);
	// assume short
	//TWORD value = *((TWORD *)(memoryMap+CPU_Registers.WP+registerNumber * sizeof(TWORD)));
	TWORD value = *((TWORD *)(memoryMap+CPU_Registers.WP+registerNumber+registerNumber));
	SwitchEndian(&value);
	return value;
}

inline void TMS9995_SetRegister(int registerNumber, TWORD value)
{
	AssertProperRegister(registerNumber);
	SwitchEndian(&value);
	// assume short
	//*((TWORD *)(memoryMap+CPU_Registers.WP+registerNumber * sizeof(TWORD))) = value;
	*((TWORD *)(memoryMap+CPU_Registers.WP+registerNumber+registerNumber)) = value;
}

inline void TMS9995_SetFlag(int flag, int value)
{
	CPU_Registers.ST &= (0xffff ^ flag);
	CPU_Registers.ST |= (value) ? flag : 0;
}

inline int TMS9995_GetFlag(int flag)
{
	return ((CPU_Registers.ST & flag));
}

int SetAddress=0x0000;

// External consumers call this routine. Only TMS9995.c calls Core.
void TMS9995_ExecuteInstruction()
{
	static TWORD oldPC;
	int cycles;
	// Peek at the next word in the instruction stream.
	TWORD instruction = TMS9995_GetNextInstruction();

	SetAddress = 0x0000;

//fprintf(stdout, "%04x %04x\n", CPU_Registers.PC, instruction);

	// Shortcuts to speed up certain critical areas.
	if (instruction == 0x8360) {
		// The instruction is already endian correct, but we're
		// looking ahead in the stream, which is always big endian.
		// Convert it for CPUs that suck (x86, etc.) and don't for
		// CPUs that are awesome (PowerPC, 9995).
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
#define i3 0xD8F0
#define i4 0xFD16
#else
#define i3 0xF0D8
#define i4 0x16FD
#endif
		TWORD w1 = *((TWORD *)(memoryMap+CPU_Registers.PC+2));
		TWORD w2 = *((TWORD *)(memoryMap+CPU_Registers.PC+4));
		if (w1 == i3 && w2 == i4) {
			// 8360 F0D8 16FD = C @>F0D8, R13 : JNE -3
			// This is a ready busy-wait for the tape. Skip it
			// because the tape is always ready.
			CPU_Registers.PC+=6;
			return;
		}
	}
#undef i3
#undef i4

	// Traps. These are for areas that incomplete emulation does not
	// fully cover. We would like to completely eliminate these because
	// they use hard-coded entry points and retard use of custom ROMs.

	//if (CPU_Registers.PC == 0x004E) exit(1);
		//gDebugger.breakpointHit = 1;

	// Paste support. Wait until the Tutor has finished probing the
	// keyboard lines before continuing.
	if (CPU_Registers.PC == 0x18B2) {
		if (gPastewait)
			gPastewait--;
	}

	// GRAPHIC LOAD support
	if (CPU_Registers.PC == 0x2788) {
		// This is the entry point for LOAD from <MON>.
		// The IRQ mask, R7 and R9 are already set at >2778.
		if (!gGotFilename) {
			SetupTapeLoad();
			gGotFilename = 1;
		}
		// Continue with the sync mark check.
		TapeInputSkipSync();
		// Side effects.
		TMS9995_SetRegister(12, 0xED00);
		// Advance to read a byte from tape (which is the
		// next trap, as it happens, but do it next cycle).
		CPU_Registers.PC = 0x27BE;
		return;
	}
	if (CPU_Registers.PC == 0x27BE) {
		// Read a byte from tape into upper byte of R8.
		// On exit, R1 is zero.
		TWORD tapew;
		int tape = TapeInputReadByte();
		if (tape < 0) {
			// Throw FORM ERR and abort.
			CPU_Registers.PC = 0x284C;
			return;
		}
		tapew = tape << 8;
		TMS9995_SetRegister(8, tapew);
		// Side effects.
		TMS9995_SetRegister(5, tapew);
		TMS9995_SetRegister(1, 0x0000);
		TMS9995_SetRegister(6, 0x0000);
		TMS9995_SetRegister(12, 0x1EE0);
		// Advance to exit from basic block.
		CPU_Registers.PC = 0x27E8;
		return;
	}
	if (CPU_Registers.PC == 0x2848) {
		// Short-circuit this test to allow loads to continue.
		CPU_Registers.PC = 0x27BE;
		return;
	}


	// BASIC LOAD support
	if (CPU_Registers.PC == 0x8E40) {
		// This is the primary sync mark detector routine.
		// Treat this as a load.
		if (!gGotFilename) {
			SetupTapeLoad();
			gGotFilename = 1;
		}
		// Skip primary sync.
		TapeInputSkipSync();
		// Side effects from 8FCA.
		TMS9995_SetRegister(12, 0xED00);
		TMS9995_SetRegister(1, 0x0065);
		// Advance to exit from basic block and filename check.
		CPU_Registers.PC = 0x8E7C;
		return;
	}
	if (CPU_Registers.PC == 0x8FCA) {
		// This is the secondary sync mark detector routine.
		TapeInputSkipSync();
		TMS9995_SetRegister(12, 0xED00);
		TMS9995_SetRegister(1, 0x0065);
		// Return to *R11.
		CPU_Registers.PC = TMS9995_GetRegister(11);
		return;
	}
	if (CPU_Registers.PC == 0x8FE4) {
		// This fetches a string of eight bits from tape into R5's
		// upper byte.
		TWORD tapew;
		int tape = TapeInputReadByte();
		if (tape == -1) {
			// Out of tape, out of time.
			// Force an error (in this case, the infamous
			// ERR 19, since it's handy).
			CPU_Registers.PC = 0x8F30;
			return;
		}
		tapew = tape << 8;
		TMS9995_SetRegister(8, tapew);
		TMS9995_SetRegister(5, tapew);
		TMS9995_SetRegister(1, 0x0000);
		// Return to *R11.
		CPU_Registers.PC = TMS9995_GetRegister(11);
		return;
	}

	oldPC = CPU_Registers.PC;
	cycles = CoreOp();
	gCycle += cycles;
	TMS9995_DecDecrementer(cycles);
}

inline static void CommonTapeOutput(int bit)
{
	if (!gGotFilename) {
		SetupTapeSave();
		gTapeMode = 2;
		gGotFilename = 1;
	}
	TapeOutput(bit);
}

// Arrange these with most likely hit first.
inline void WriteTWord(TWORD *address, TWORD value)
{
#if DEBUG
	char Buffer[50];
#endif
	int shortAddress = (int)address - (int)memoryMap;

	if (UNLIKELY((int)address & 0x01))
	{
#if DEBUG
		sprintf(Buffer, "Write to odd address 0x%04x at PC=0x%04x.\n", (int)((int)address - (int)memoryMap), CPU_Registers.PC);
		OutputDebugString_(Buffer);
#endif
		(address)-=1;
		(shortAddress)-=1;
	}

#if DEBUG
	if (shortAddress < 0xC000 && shortAddress != 0x8000)
	{
		sprintf(Buffer, "Write to ROM address 0x%x at PC=0x%04x.\n", shortAddress, CPU_Registers.PC);
		OutputDebugString_(Buffer);
		gDebugger.breakpointHit = 1;
	}
	else
#endif
		*address = value;

#if DEBUG
	if (shortAddress < 0xF000 && shortAddress != 0xE002 && shortAddress != 0xE000 && shortAddress != 0x8000)
	{
		sprintf(Buffer, "PC:0x%04x 0x%04X = 0x%04x.\n", CPU_Registers.PC, shortAddress, value);
		OutputDebugString_(Buffer);
	}
#endif
	if (shortAddress == 0xE002)
{
		TMS9918_WriteToVDPRegister(value);//memoryMap[0xE002]);
		return;
}
	if (shortAddress == 0xE000)
{
		TMS9918_WriteToVDPData(value);//memoryMap[0xE000]);
		return;
}
	if (shortAddress == 0xEE00) {
		CommonTapeOutput(0);
		return;
	}
	if (shortAddress == 0xEE20) {
		/* On startup, a single write is written. */
		if (!gGotFirstWrite) {
			gGotFirstWrite = 1;
			return;
		}
		CommonTapeOutput(1);
		return;
	}
	if (shortAddress == 0xE200) {
		/* Here for completeness, but the actual guts is WriteTByte. */
		SN76489AN_WritePort(value);//memoryMap[0xE200]);
		return;
	}
	if (shortAddress == 0xEE40 || shortAddress == 0xEE60) {
//fprintf(stderr, "\ntape interrupt %s\n", (shortAddress==0xEE40)?"on":"off");

		// Interrupt disabled
		// This gets fired when EE40 is triggered, or on reset.
		if (shortAddress==0xEE60) {
			// If there is a bit ready (we're in a load state),
			// keep the tape file open. This occurs after the
			// tape line goes high.
			if (gBitWaiting)
				return;

			// Otherwise, the load has concluded, or we reset.
			if (gTapeMode == 2)
				FinishTapeSave();
			else 
				FinishTapeLoad();
			return;
		}

		// Interrupt requested
fprintf(stderr, ">EE40\n");
		gDebugger.breakpointHit = 1;

#if(0)
/* This section of code does not work correctly with interrupts. */
		if (!gGotFilename) {
			SetupTapeLoad();
			gGotFilename = 1;
		}

fprintf(stderr, "^");
		// Based on analysis of the Tomy tape routines, EE40 is
		// a one-shot trigger. On each pulse of the line, clock in
		// a bit. If the bit is zero, return. If one, fire IRQ 4.
		// (Does this tickle the decrementer? Is that counting pulses?)
		gCRUTapeLine = TapeInput();
		gBitWaiting = gCRUTapeLine;
		if (!gBitWaiting)
			return;

		//WriteTByte((TBYTE *)(memoryMap+0xF071), 0xFF);
                TMS9995_TriggerInterrupt(0x04);
                CPU_Registers.ST &= 0xFFF8;
                CPU_Registers.ST += 0x04;
		return;
#endif
	}
	
	if (shortAddress == 0xFFFA) {
		// Decrementer. There is no byte access for this value.
		decrementerBase = value;
		extra = 0; // reset overflow
		SwitchEndian(&decrementerBase);
#if DEBUG
fprintf(stderr, "Decrementer: %i\n", decrementerBase);
#endif
		return;
	}
}

void WriteTByte(TBYTE *address, TBYTE value)
{
#if DEBUG
	char Buffer[50];
#endif
	int shortAddress = (int)address - (int)memoryMap;

#if DEBUG
	if (shortAddress < 0xC000)
	{
		sprintf(Buffer, "Write to ROM address 0x%x at PC=0x%04x.\n", shortAddress, CPU_Registers.PC);
		OutputDebugString_(Buffer);
		exit(1);
		gDebugger.breakpointHit = 1;
	}
	else
#endif
		*address = value;

#if DEBUG
	if (shortAddress < 0xF000 && shortAddress != 0xE002 && shortAddress != 0xE000 && shortAddress != 0x8000)
	{
		sprintf(Buffer, "PC:0x%04x 0x%04X = 0x%02x.\n", CPU_Registers.PC, shortAddress, value);
		OutputDebugString_(Buffer);
	}
#endif
	
	if (shortAddress == 0xE002) {
		TMS9918_WriteToVDPRegister(value); //memoryMap[0xE002]);
		return;
	}
	if (shortAddress == 0xE000) {
		TMS9918_WriteToVDPData(value); //memoryMap[0xE000]);
		return;
	}
	if (shortAddress == 0xE200) {
		SN76489AN_WritePort(value); //memoryMap[0xE200]);
		return;
	}
}

TWORD ReadTWord(TWORD *address)
{
#if DEBUG
	char Buffer[50];
#endif
	TWORD shortAddress = (int)address - (int)memoryMap;

	if (UNLIKELY((int)address & 0x01))
	{
		//gDebugger.breakpointHit = 1;
		//sprintf(Buffer, "Read from odd address 0x%04x at PC=0x%04x.\n", (int)((int)address - (int)memoryMap), CPU_Registers.PC);
		//OutputDebugString_(Buffer);
		(address)-=1;
		(shortAddress)-=1;
	}

	if (shortAddress == 0xE000) {
		*((TBYTE *)address) = TMS9918_ReadFromVDPData();
		return *address;
	}
	if (shortAddress == 0xE002) {
		*((TBYTE *)address) = TMS9918_ReadStatusRegister();
		return *address;
	}

	if ((shortAddress < 0xE000 && shortAddress > 0xBFFF) || (shortAddress > 0xE002 && shortAddress < 0xF000))
	{
		*((TWORD *)address) = 0x0000;
	}

	return *address;
}

TBYTE ReadTByte(TBYTE *address)
{
#if DEBUG
	char Buffer[50];
#endif
	TWORD shortAddress = (int)address - (int)memoryMap;

	if (shortAddress == 0xE000) {
		*((TBYTE *)address) = TMS9918_ReadFromVDPData();
		return *address;
	}
	if (shortAddress == 0xE002) {
		*((TBYTE *)address) = TMS9918_ReadStatusRegister();
		return *address;
	}

	if ((shortAddress < 0xE000 && shortAddress > 0xBFFF) || (shortAddress > 0xE002 && shortAddress < 0xF000))
	{
#if DEBUG
		sprintf(Buffer, "Read from other port 0x%04x at PC=0x%04x.\n", shortAddress, CPU_Registers.PC);
		OutputDebugString_(Buffer);
#endif
		*address = 0xFF;
	}

	return *address;
}

void CLA_SetCRUBit(int bit, signed char displacement)
{
	int R12;

	R12 = CPU_WP_Registers->R12;
	SwitchEndian((TWORD *)&R12);
	R12 += displacement;
	switch (R12)
	{

/*

 From MAME tms9995.cpp:

    The TMS9995 CRU address space is larger than the CRU space of the TMS9900:
    0000-fffe (even addresses) instead of 0000-1ffe. Unlike the TMS9900, the
    9995 uses the data bus lines D0-D2 to indicate external operations.

    Internal CRU locations (read/write)
    -----------------------------------
    1EE0 Flag 0     Decrementer as event counter (1=yes)
    1EE2 Flag 1     Decrementer enable (1=yes)
    1EE4 Flag 2     Level 1 interrupt present (read only, also set when interrupt mask disallows interrupts)
    1EE6 Flag 3     Level 3 interrupt present (see above)
    1EE8 Flag 4     Level 4 interrupt present (see above)
    ...
    1EFE Flag 15
    1FDA MID flag (only indication, does not trigger when set)

  It seems like the Tutor only uses the first two flags.
  
*/
	case 0x1EE0:
	{
		gDecrementerMode = bit;
		break;
	}
	case 0x1EE2:
	{
		gDecrementerEnabled = bit;
		break;
	}
	default:
		break;
	}
	return;
}

int CLA_GetCRUBit(signed char displacement)
{
#if(0)
	// The Tutor uses this in the SAVE routine.
	int R12 = TMS9995_GetRegister(12);
	if ((R12+displacement) == 0xED00) {
		// Set the EQ bit to gCRUTapeLine.
		TMS9995_SetFlag(FLAG_E, gCRUTapeLine);
		// Advance the tape bit, simulating a moving cassette head.
		gCRUTapeLine = TapeInput();
		gBitWaiting = gCRUTapeLine;
	} else {
		fprintf(stderr, "op_TB: %i %i\n", R12, displacement);
	}
#endif
    fprintf(stderr, "TB not yet supported\n");
	gDebugger.breakpointHit = 1;
	return 0;
}

TWORD CLA_GetCRUWord(int bits)
{
	TBYTE testvalue=0x00;
	static TBYTE rotating=0x01;
	static TWORD address = 0xEC60;
	static TBYTE thisBit=0x10;

	// It appears the keyboard is mapped into 8 sections.
	// EC60 holds the enter key...

	if (bits != 8)
	{
		int a= 1;
	}

	if (bits <= 8)
	{
		int R12 = TMS9995_GetRegister(12);
		TBYTE value=0x00;

		switch (R12)	
		{
		case 0xEC00:
			if (gKeyboard[SDLK_1])
				testvalue |= 0x01;
			if (gKeyboard[SDLK_2])
				testvalue |= 0x02;
			if (gKeyboard[SDLK_q])
				testvalue |= 0x04;
			if (gKeyboard[SDLK_w])
				testvalue |= 0x08;
			if (gKeyboard[SDLK_a])
				testvalue |= 0x10;
			if (gKeyboard[SDLK_s])
				testvalue |= 0x20;
			if (gKeyboard[SDLK_z])
				testvalue |= 0x40;
			if (gKeyboard[SDLK_x])
				testvalue |= 0x80;
			break;
		case 0xEC10:
			if (gKeyboard[SDLK_3])
				testvalue |= 0x01;
			if (gKeyboard[SDLK_4])
				testvalue |= 0x02;
			if (gKeyboard[SDLK_e])
				testvalue |= 0x04;
			if (gKeyboard[SDLK_r])
				testvalue |= 0x08;
			if (gKeyboard[SDLK_d])
				testvalue |= 0x10;
			if (gKeyboard[SDLK_f])
				testvalue |= 0x20;
			if (gKeyboard[SDLK_c])
				testvalue |= 0x40;
			if (gKeyboard[SDLK_v])
				testvalue |= 0x80;
			break;
		case 0xEC20:
			if (gKeyboard[SDLK_5])
				testvalue |= 0x01;
			if (gKeyboard[SDLK_6])
				testvalue |= 0x02;
			if (gKeyboard[SDLK_t])
				testvalue |= 0x04;
			if (gKeyboard[SDLK_y])
				testvalue |= 0x08;
			if (gKeyboard[SDLK_g])
				testvalue |= 0x10;
			if (gKeyboard[SDLK_h])
				testvalue |= 0x20;
			if (gKeyboard[SDLK_b])
				testvalue |= 0x40;
			if (gKeyboard[SDLK_n])
				testvalue |= 0x80;
			break;
		case 0xEC30:
			if (gKeyboard[SDLK_7])
				testvalue |= 0x01;
			if (gKeyboard[SDLK_8])
				testvalue |= 0x02;
			if (gKeyboard[SDLK_9])
				testvalue |= 0x04;
			if (gKeyboard[SDLK_u])
				testvalue |= 0x08;
			if (gKeyboard[SDLK_i])
				testvalue |= 0x10;
			if (gKeyboard[SDLK_j])
				testvalue |= 0x20;
			if (gKeyboard[SDLK_k])
				testvalue |= 0x40;
			if (gKeyboard[SDLK_m])
				testvalue |= 0x80;
			break;
		case 0xEC40:
			// Joystick/controller 1 also detected here.
			if (gKeyboard[SDLK_0])
				testvalue |= 0x01;
			if (gKeyboard[SDLK_MINUS])
				testvalue |= 0x02;
			if (gKeyboard[SDLK_o] || gKeyboard[SDLK_KP0]) // SL
				testvalue |= 0x04;
			if (gKeyboard[SDLK_p] || gKeyboard[SDLK_KP_PERIOD]) // SR
				testvalue |= 0x08;
			if (gKeyboard[SDLK_l] || gKeyboard[SDLK_KP5]) // down
				testvalue |= 0x10;
			if (gKeyboard[SDLK_SEMICOLON] || gKeyboard[SDLK_KP4]) // left
				testvalue |= 0x20;
			if (gKeyboard[SDLK_COMMA] || gKeyboard[SDLK_KP8]) // up
				testvalue |= 0x40;
			if (gKeyboard[SDLK_PERIOD] || gKeyboard[SDLK_KP6]) // right
				testvalue |= 0x80;
			break;
		case 0xEC50:
			// Controller 2 also detected here.
			// 0x01, 0x02 apparently unused
			if (gKeyboard[SDLK_EQUALS] || gKeyboard[SDLK_KP0]) // handaku // SL
				testvalue |= 0x04;
			if (gKeyboard[SDLK_BACKQUOTE] || gKeyboard[SDLK_KP_PERIOD]) // _ // SR
				testvalue |= 0x08;
			if (gKeyboard[SDLK_QUOTE] || gKeyboard[SDLK_KP5]) // : // down
				testvalue |= 0x10;
			if (gKeyboard[SDLK_LEFTBRACKET] || gKeyboard[SDLK_KP4]) // left
				testvalue |= 0x20;
			if (gKeyboard[SDLK_SLASH] || gKeyboard[SDLK_KP8]) // up
				testvalue |= 0x40;
			if (gKeyboard[SDLK_RIGHTBRACKET] || gKeyboard[SDLK_KP6]) // right
				testvalue |= 0x80;
			break;
		case 0xEC60:
			// 0x01 apparently unused
			if (gKeyboard[CAPS_LOCK_KEY])	// Alpha Lock (sysdep)
				testvalue |= 0x02;
			if (gKeyboard[SDLK_LSHIFT] || gKeyboard[SDLK_RSHIFT])
				testvalue |= 0x04;
			if (gKeyboard[SDLK_BACKSLASH]) // MON
				testvalue |= 0x08;
			if (gKeyboard[SDLK_RETURN])
				testvalue |= 0x10;
			// 0x20 apparently unused
			// MOD
			// right CTRL may not exist on some laptops ...
			// for that matter, the iBook doesn't even have RALT
			if (gKeyboard[SDLK_RCTRL] || gKeyboard[SDLK_RALT]
					|| gKeyboard[SDLK_KP_ENTER])
				testvalue |= 0x40;
			if (gKeyboard[SDLK_SPACE])
				testvalue |= 0x80;
			break;
		case 0xEC70:
			// 0xF0 apparently unused
			if (gKeyboard[SDLK_LEFT] || gKeyboard[SDLK_BACKSPACE])
				testvalue |= 0x01;
			if (gKeyboard[SDLK_UP])
				testvalue |= 0x02;
			if (gKeyboard[SDLK_DOWN])
				testvalue |= 0x04;
			if (gKeyboard[SDLK_RIGHT])
				testvalue |= 0x08;
			break;
		default:
			{
			int a=1;
			testvalue = (2 << (bits-1))-1;
			}
			break;
		}
	}
	else
	{
		// Uh...
	}

	return (TBYTE)testvalue;
}

