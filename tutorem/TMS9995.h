/* TMS 9995 emulator...
   This is mostly now the miscellaneous I/O and CLA stuff.
	Ian Gledhill 31/03/03 and Cameron Kaiser */

#include "TMS9995_arch.h"

void TMS9995_Init(char *ROM1, char *ROM2);
inline TWORD TMS9995_GetNextInstruction();
void TMS9995_ExecuteInstruction();

TWORD TMS9995_FetchWord(TWORD address);
void TMS9995_WriteWord(TWORD address, TWORD value);
void TMS9995_WriteByte(TWORD address, TBYTE value);

inline TWORD TMS9995_GetRegister(int registerNumber);
inline void TMS9995_SetRegister(int registerNumber, TWORD value);
inline void TMS9995_SetFlags(int flag, int value);
inline void TMS9995_GetFlags(int flag);

void TMS9995_TriggerInterrupt(int interruptLevel);
void TMS9995_HandledInterrupt();
inline int TMS9995_GetInterruptMask();

void TMS9995_TriggerDecrementer();
void TMS9995_TriggerDebugger();

void CLA_SetCRUBit(int bit, signed char displacement);
int CLA_GetCRUBit(signed char displacement);
TWORD CLA_GetCRUWord(int bits);

void TMS9995_StopTapeInterface();

#if __ppc__ && __APPLE__
/* MacPorts gcc doesn't handle SDL endian macros right for some reason. */
#undef SDL_BYTEORDER
#define SDL_BYTEORDER SDL_BIG_ENDIAN
#endif
