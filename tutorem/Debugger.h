/* Debugger.h
	Ian Gledhill 02/04/03 */

typedef struct TextScreenStruct 
{
	unsigned char data[24][40];
} TMS9918Screen;

#define CPU_TMS9995		0
#define CPU_TMS9918		1

typedef struct DebuggerStruct
{
	int breakpointHit;
	int inInterrupt;
	int TMS9995_memoryTop;
	int TMS9918_memoryTop;
	int memoryTop;
	int	memoryMode;
	int enabled;
	int dumpToStdout;
	int lastPC;
} DebuggerType;

extern DebuggerType gDebugger;

void Debugger_Init();
void Debugger_Update();
void Debugger_UpdateCharacters(int characterX1, int characterY, int characterX2);
void Debugger_UpdateScreen();
void Debugger_SwitchToCPU();
void Debugger_SwitchToVGP();
void Debugger_UpdateTMS9995();
void Debugger_UpdateTMS9918();
void Debugger_ClearScreen();
void Debugger_Enable();
void Debugger_Disable();
