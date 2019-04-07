/* The Tomy Tutor emulator! Started 31/03/03 by Ian Gledhill! */

#include <windows.h>

#include <stdio.h>
#include <fcntl.h>
#include <io.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "SDL/SDL.h"

#include "TMS9995.h"
#include "TMS9918ANL.h"
#include "Debugger.h"

char TT_ROM1[32768], TT_ROM2[16384];

int LoadROM(char *ROM1, char *ROM2);

void PollKeyboard();

int gQuitWhenAble=0;

#define CLOCKSPEED		2675000
#define FPS				60
#define TICKSPERFRAME	(CLOCKSPEED / FPS)

int gCycle;
extern int gDecrementerEnabled;

char gKeyboard[SDLK_LAST];

int main(int argc, char *argv[])
{
	TWORD instruction;

	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		printf("Couldn`t load SDL.\n");
		exit(-1);
	}

	atexit(SDL_Quit);

	LoadROM("ROM\\tutor1.bin", "ROM\\tutor2.bin");
	
	memset(gKeyboard, 0, 256);

	TMS9995_Init(TT_ROM1, TT_ROM2);
	TMS9918_Init();
	Debugger_Init();

	while (!gQuitWhenAble)
	{
		gCycle = 0;

		while (gCycle < TICKSPERFRAME && !gQuitWhenAble)
		{
			if (gDebugger.breakpointHit && !gDebugger.enabled)
				Debugger_Enable();
			if (gDebugger.breakpointHit == 0 || gDebugger.inInterrupt)
			{
				instruction = TMS9995_GetNextInstruction();
				TMS9995_ExecuteInstruction(instruction);
			}
			if (gDebugger.enabled && !gDebugger.inInterrupt)
			{
				Debugger_Enable();
				Debugger_Update();
				TMS9918_Update();
				PollKeyboard();
			}
		}
		gCycle -= TICKSPERFRAME;

		VDP_Registers.ST &= ~VDP_ST_FLAG_F;
		if (gDecrementerEnabled)
			TMS9995_DecDecrementer();
		TMS9918_Redraw();
		if (TMS9995_GetInterruptMask() >= 3)
		{
			TMS9995_TriggerInterrupt(0x03);
			CPU_Registers.ST &= 0xFFF8;
			CPU_Registers.ST += 0x03;
		}
		PollKeyboard();
	}

	SDL_Quit();	

	return 0;
}

int LoadROM(char *ROM1, char *ROM2)
{
	int fd, r;

	if ((fd = open(ROM1, O_RDONLY | O_BINARY)) < 0)
	{
		printf("Error opening ROM %s.\n", ROM1);
		return -1;
	}
	if ((r=read(fd, (void *)TT_ROM1, 32768)) < 32768)
	{
		printf("Error reading ROM %s.\n", ROM1);
		close(fd);
		return -1;
	}
	close(fd);
	if ((fd = open(ROM2, O_RDONLY | O_BINARY)) < 0)
	{
		printf("Error opening ROM %s.\n", ROM2);
		return -1;
	}
	if (read(fd, (void *)TT_ROM2, 16384) < 0)
	{
		printf("Error reading ROM %s.\n", ROM2);
		close(fd);
		return -1;
	}
	close(fd);

	return 0;
}

void SaveSnapshot()
{
	int fd;

	fd = open("tutor.tut", O_WRONLY | O_CREAT | O_BINARY, S_IREAD | S_IWRITE);
	if (fd < 0)
		return;

	write(fd, (void *)memoryMap, 0x10000);
	write(fd, (void *)VDP_MemoryMap, 0x4000);
	write(fd, (void *)(&CPU_Registers), sizeof(CPU_Registers_Type));
	write(fd, (void *)(&VDP_Registers), sizeof(VDP_Registers));

	close(fd);
}

void LoadSnapshot()
{
	int fd;

	fd = open("tutor.tut", O_RDONLY | O_BINARY);
	if (fd < 0)
		return;

	read(fd, (void *)memoryMap, 0x10000);
	read(fd, (void *)VDP_MemoryMap, 0x4000);
	read(fd, (void *)(&CPU_Registers), sizeof(CPU_Registers_Type));
	read(fd, (void *)(&VDP_Registers), sizeof(VDP_Registers));

	close(fd);
}

void PollKeyboard()
{
	TWORD instruction;

	SDL_Event event;
	SDLKey sym;
	TWORD maximumMemory;
	static int Shift, Ctrl;

	if (gDebugger.memoryMode == CPU_TMS9995)
		maximumMemory = 0xFFFF;
	else
		maximumMemory = 0x3FFF;

	while (SDL_PollEvent(&event) == 1) {
		switch(event.type) {
			case SDL_KEYDOWN:
				sym = event.key.keysym.sym;
				if (sym == SDLK_F5)
					SaveSnapshot();
				if (sym == SDLK_F6)
					LoadSnapshot();
				if (sym == SDLK_LSHIFT)
					Shift = 1;
				if (sym == SDLK_ESCAPE)
					gQuitWhenAble = 1;
				if (sym == SDLK_F1)
					if (gDebugger.breakpointHit == 1)
					{
						instruction = TMS9995_GetNextInstruction();
						TMS9995_ExecuteInstruction(instruction);
						TMS9918_Update();
					}
					else
						gDebugger.breakpointHit = 1;
				if (gDebugger.enabled)
				{
					if (sym == SDLK_F2)
						gDebugger.breakpointHit = 0;
					if (sym == SDLK_DOWN)
					{
						if (gDebugger.memoryTop < maximumMemory-0x7F)
							gDebugger.memoryTop += 0x08;
					}
					if (sym == SDLK_UP)
						if (gDebugger.memoryTop > 0x0000)
							gDebugger.memoryTop -= 0x08;
					if (sym == SDLK_HOME)
					{
						if (gDebugger.memoryMode == CPU_TMS9995)
							gDebugger.memoryTop = 0xF000;
						else
							gDebugger.memoryTop = 0x0000;
					}
					if (sym == SDLK_END)
					{
						gDebugger.memoryTop = maximumMemory-0x7F;
					}
					if (sym == SDLK_PAGEUP)
					{
						gDebugger.memoryTop -= (Shift ? 0x1000 : 0x40);
						if (gDebugger.memoryTop < 0x00)
							gDebugger.memoryTop = 0x0000;
					}
					if (sym == SDLK_PAGEDOWN)
					{	
						gDebugger.memoryTop += (Shift ? 0x1000 : 0x40);
						if (gDebugger.memoryTop > maximumMemory-0x7F)
							gDebugger.memoryTop = maximumMemory-0x7F;
					}
					if (sym == SDLK_p)
						gDebugger.memoryTop = 0xE000;
					if (sym == SDLK_v)
						Debugger_SwitchToVGP();
					if (sym == SDLK_c)
						Debugger_SwitchToCPU();
					if (sym == SDLK_e)
						Debugger_Disable();
					if (sym == SDLK_F10)
						Debugger_Enable();
				}
				else if (sym >= SDLK_BACKSPACE && sym <= SDLK_z || sym == SDLK_RETURN || sym == SDLK_SPACE || sym >= SDLK_UP && sym <= SDLK_LEFT)
				{
					gKeyboard[sym] = 1;
				}
				else if (sym >= SDLK_F8 && sym <= SDLK_F11)
				{
					gKeyboard[sym] = 1;
				}
				else if (sym >= SDLK_LSHIFT && sym <= SDLK_LCTRL )
				{
					gKeyboard[sym] = 1;
				}
				break;
			case SDL_KEYUP:
				sym = event.key.keysym.sym;
				if (sym == SDLK_LSHIFT)
				{
					Shift = 0;
				}
				if (sym >= SDLK_BACKSPACE && sym <= SDLK_z || sym == SDLK_RETURN || sym == SDLK_SPACE || sym >= SDLK_UP && sym <= SDLK_LEFT)
					gKeyboard[sym] = 0;
				else if (sym >= SDLK_F8 && sym <= SDLK_F11)
				{
					gKeyboard[sym] = 0;
				}
				else if (sym >= SDLK_LSHIFT && sym <= SDLK_LCTRL )
				{
					gKeyboard[sym] = 0;
				}
				break;
			default:
				break;
		}
	}

}

/* Tape interfaces which TMS9995.c expects to call. */
FILE *SetupTapeLoad()
{
	char *x = loadTape(); // Get filename from SDLMain.
	if (x) {
		fprintf(stderr, "load from %s\n", x);
	} else {
		fprintf(stderr, "no load filename");
	}
	return NULL;
}

FILE *SetupTapeSave()
{
	char *x = saveTape(); // Get filename from SDLMain.
	if (x) {
		fprintf(stderr, "save to %s\n", x);
	} else {
		fprintf(stderr, "no save filename");
	}
	return NULL;
}

