/* The Tomy Tutor emulator! Started 31/03/03 by Ian Gledhill! */
/* Windows MXE version (C)2019 Cameron Kaiser */

#include "sys.h"

#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <assert.h>
#include <time.h>
#include <windows.h>
#include <shlwapi.h>
#include <string.h>
#include <SDL.h>
#include <SDL_syswm.h>

#include "tutorem/TMS9995.h"
#include "tutorem/SN76489AN.h"
#include "tutorem/TMS9918ANL.h"
#include "tutorem/Debugger.h"

char TT_ROM1[32768], TT_ROM2[16384];

/* Windows system stuff */

HWND GetHwnd() {
	SDL_SysWMinfo windowinfo;

	SDL_VERSION(&windowinfo.version);
	if (!SDL_GetWMInfo(&windowinfo)) return(NULL);
	return(windowinfo.window);
}

// This needs to be global so that references to strings
// within it remain valid.
char fileName[MAX_PATH+1];

// Menu IDs.
#define ID_FILE_STOP_TAPE	9011
#define ID_FILE_SAVE_SNAPSHOT	9012
#define ID_FILE_LOAD_SNAPSHOT	9013
#define ID_FILE_EXIT		9014
#define ID_EDIT_PASTE_AS_K	9021
#define ID_EMULATION_RESET	9031
#define ID_EMULATION_STEP_IN_D	9032
#define ID_EMULATION_TURBO	9034
#define ID_EMULATION_SYNC_IRQ3	9035
#define ID_HELP_ABOUT		9041
#define ID_SEPARATOR		9999
HMENU hMenu;
HANDLE hTimer;
LARGE_INTEGER liTval;

// XXX: move to a separate file?
void toggleWarpSpeed() {
	(void)CheckMenuItem(hMenu, ID_EMULATION_TURBO,
		(CheckMenuItem(hMenu, ID_EMULATION_TURBO, MF_BYCOMMAND))
			? MF_UNCHECKED : MF_CHECKED);
}
void toggleFrameLock() {
	(void)CheckMenuItem(hMenu, ID_EMULATION_SYNC_IRQ3,
		(CheckMenuItem(hMenu, ID_EMULATION_SYNC_IRQ3, MF_BYCOMMAND))
			? MF_UNCHECKED : MF_CHECKED);
}
void setWarpSpeed(int value) {
	(void)CheckMenuItem(hMenu, ID_EMULATION_TURBO,
		(value) ? MF_CHECKED : MF_UNCHECKED);
}
void setFrameLock(int value) {
	(void)CheckMenuItem(hMenu, ID_EMULATION_SYNC_IRQ3,
		(value) ? MF_CHECKED : MF_UNCHECKED);
}
void enableStopTape() {
	(void)EnableMenuItem(hMenu, ID_FILE_STOP_TAPE, MF_ENABLED);
}
void disableStopTape() {
	(void)EnableMenuItem(hMenu, ID_FILE_STOP_TAPE, MF_GRAYED);
}
void setUpCommonDialogue(char *s, OPENFILENAME *ofn) {
	ZeroMemory(ofn, sizeof(OPENFILENAME));
	ofn->lStructSize = OPENFILENAME_SIZE_VERSION_400;
	ofn->hwndOwner = GetHwnd();
	ofn->lpstrFile = fileName;
	if (s) {
		strcpy(fileName, s);
		// Win32 doesn't have strcasestr, boooooooooo.
		if (StrStrIA(fileName, ".tut"))
			ofn->lpstrFilter = "Memory images (*.TUT)\0*.TUT\0All\0*.*\0";
		else
			ofn->lpstrFilter = "Bitstreams (*.BIT)\0*.BIT\0All\0*.*\0";
	} else {
		ofn->lpstrFile[0] = '\0';
		ofn->lpstrFile[1] = '\0';
		ofn->lpstrFilter = "Bitstreams (*.BIT)\0*.BIT\0All\0*.*\0";
	}
	ofn->nMaxFile = sizeof(fileName);
	ofn->nFilterIndex = 1;
	ofn->lpstrFileTitle = NULL;
	ofn->nMaxFileTitle = 0;
	ofn->lpstrInitialDir = NULL;
}
char *loadFilename(char *s) {
	OPENFILENAME ofn;

	setUpCommonDialogue(s, &ofn);
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
	return (GetOpenFileName(&ofn)) ? ofn.lpstrFile : NULL;
}
char *saveFilename(char *s) {
	OPENFILENAME ofn;

	setUpCommonDialogue(s, &ofn);
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;
	return (GetSaveFileName(&ofn)) ? ofn.lpstrFile : NULL;
}
char *getPastedText(int *cblen) {
	HANDLE hData = NULL;
	char *cData = NULL;
	char *buf = NULL;
	size_t len;

	if (!IsClipboardFormatAvailable(CF_TEXT))
		return NULL;
	if (!OpenClipboard(GetHwnd()))
		return NULL;
	hData = GetClipboardData(CF_TEXT);
	if (hData != NULL) {
		cData = (char *)GlobalLock(hData);
		if (cData) {
			len = strnlen(cData, 16384);
			buf = (char *)malloc(len);
			if (buf) {
				strncpy(buf, cData, len);
				*cblen = len;
			}
		}
		GlobalUnlock(hData);
		CloseClipboard();
		return buf;
	}

	CloseClipboard();
	return NULL;
}
char *pathToTutor1() { return "tutor1.bin"; }
char *pathToTutor2() { return "tutor2.bin"; }
// void minimizeApp() { }  /* not implemented for Windows toolkit */

int LoadROM(char *ROM1, char *ROM2);

void PollKeyboard();

char *pasteboard;
char pastechar=0;
int pastechars=0;
int pastepos=0;
int pastechord=0;
int pastephase=0;
FILE *tape;
int gPastewait=0;
int gQuitWhenAble=0;
int gResetWhenAble=0;
int gForceSync=1;
int gWarpSpeed=0;
int gOldForceSync=-1;
int gOldWarpSpeed=-1;
int gInSave=0;
int gDebuggerShowScreen=0;
long gShowFrame=0;

int gClockedBits=0;
int gCurrentBit=0;

/* Compute number of tutor CPU ticks per frame */
#define CLOCKSPEED		2700000
/* We have to refresh quite often or keypresses get dropped, so
   we define a high FPS rate and then divide that down for actual screen
   updates. */
#define FPS				600
/* Empirically selected to match the real machine refresh rate. */
#define FPSDIVISOR 30
#define TICKSPERFRAME	(CLOCKSPEED / FPS)
/* Additional frames to skip when in warp. More than
   this yields little additional CPU overhead and interferes with visual
   understanding. */
#define WARPSPEED 128

#define SHORTPASTEWAIT 40
#define LONGPASTEWAIT 300

int gCycle;
extern int gDecrementerEnabled;
int runDebugEnabled;
int frameCountDown;
struct timeval tv;

char gKeyboard[SDLK_LAST];

void AddMenus(HWND hwnd) {
	HMENU hSubMenu, hMenu2, hSubMenu2;

	hMenu = CreateMenu();
	
	// No accelerator keys: it interferes with emulation.

	// File
	// Stop Tape // Save Snapshot... / Load Snapshot... // Exit
	hSubMenu = CreatePopupMenu();
	AppendMenu(hSubMenu, MF_STRING | MF_GRAYED, ID_FILE_STOP_TAPE, "Stop Tape\tCtrl+S");
	AppendMenu(hSubMenu, MF_SEPARATOR, ID_SEPARATOR, "-"); 
	AppendMenu(hSubMenu, MF_STRING, ID_FILE_SAVE_SNAPSHOT, "Save Snapshot...\tF5");
	AppendMenu(hSubMenu, MF_STRING, ID_FILE_LOAD_SNAPSHOT, "Load Snapshot...\tF6");
	AppendMenu(hSubMenu, MF_SEPARATOR, ID_SEPARATOR, "-"); 
	AppendMenu(hSubMenu, MF_STRING, ID_FILE_EXIT, "Exit\tAlt+F4");
	AppendMenu(hMenu, MF_STRING | MF_POPUP, (UINT)hSubMenu, "File");

	// Edit
	// Paste as Keystrokes
	hSubMenu = CreatePopupMenu();
	AppendMenu(hSubMenu, MF_STRING, ID_EDIT_PASTE_AS_K, "Paste as Keystrokes\tCtrl+V");
	AppendMenu(hMenu, MF_STRING | MF_POPUP, (UINT)hSubMenu, "Edit");

	// Emulation
	// Reset / Step in Debugger // Turbo / Sync IRQ3 to System Clock
	hSubMenu = CreatePopupMenu();
	AppendMenu(hSubMenu, MF_STRING, ID_EMULATION_RESET, "Reset\tCtrl+R");
	AppendMenu(hSubMenu, MF_STRING, ID_EMULATION_STEP_IN_D, "Step in Debugger\tF1");
	AppendMenu(hSubMenu, MF_SEPARATOR, ID_SEPARATOR, "-"); 
	AppendMenu(hSubMenu, MF_STRING, ID_EMULATION_TURBO, "Turbo\tCtrl+B");
	AppendMenu(hSubMenu, MF_STRING, ID_EMULATION_SYNC_IRQ3, "Sync IRQ3 to System Clock\tCtrl+F");
	AppendMenu(hMenu, MF_STRING | MF_POPUP, (UINT)hSubMenu, "Emulation");

	// Help
	// About Tutti II...
	hSubMenu = CreatePopupMenu();
	AppendMenu(hSubMenu, MF_STRING, ID_HELP_ABOUT, "About Tutti...");
	AppendMenu(hMenu, MF_STRING | MF_POPUP, (UINT)hSubMenu, "Help");

	// The Mac OS X specific "Window" menu is not needed here.
        SetMenu(hwnd, hMenu);
}

/* Me love you, long long_time!() */
static inline long long_time() {
	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000000) + tv.tv_usec;
}

static inline SaveSpeed() {
	gOldWarpSpeed = gWarpSpeed;
	gOldForceSync = gForceSync;
	setFrameLock(gForceSync = 1);
	setWarpSpeed(gWarpSpeed = WARPSPEED); 
}
static inline RestoreSpeed() {
	if (gOldWarpSpeed >= 0)
		setWarpSpeed(gWarpSpeed = gOldWarpSpeed);
	if (gOldForceSync >= 0)
		setFrameLock(gForceSync = gOldForceSync);
}

/* Tape interfaces which TMS9995.c expects to call. */
void FinishTapeLoad()
{
	fprintf(stderr, "closed load\n");
	RestoreSpeed();
	TMS9995_StopTapeInterface();
	if (tape) fclose(tape);
	tape=NULL;
	SDL_WM_SetCaption("Tutti", "Tutti");
}

void FinishTapeSave()
{
	if (!gInSave) return;

	fprintf(stderr, "closed save\n");
	disableStopTape();
	RestoreSpeed();
	TMS9995_StopTapeInterface();
	gInSave=0;
	if (tape) fclose(tape);
	tape=NULL;
	SDL_WM_SetCaption("Tutti", "Tutti");
}

void SetupTapeLoad()
{
	if (gInSave)
		FinishTapeSave();
	char *x = loadFilename("tutor.out");
	if (x) {
		gClockedBits = 0;
		fprintf(stderr, "load from %s\n", x);
		SDL_WM_SetCaption("Tutti (load on)", "Tutti (load on)");
		SaveSpeed();
		if (tape) fclose(tape);
		tape = fopen(x, "rb");
		if (!tape) {
			perror("LOAD FAILED");
			FinishTapeLoad();
		}
	} else {
		fprintf(stderr, "no load filename\n");
	}
}

/* Convert logical bits from a tape file to the physical bit pattern. */
int TapeInput()
{
	/* bit patterns: (from SAVE routine)
	   not sure this is actually what the load sees.
	   01010    = 1
	   010      = 0 */
	int bit = 0;

	if (!tape) return -1;
	if (!gClockedBits) {
		/* Need a bit from the file */
		char tapebyte = 0x30; /* emulate low signal if EOF */
		if (!fread(&tapebyte, 1, 1, tape)) {
			return -1;
		}
		gCurrentBit = (tapebyte == '0') ? 0 : 1;
	}
	bit = (gClockedBits & 1);
	gClockedBits = (gClockedBits == 2 && gCurrentBit == 0) ? 0 :
			(gClockedBits == 4) ? 0 :
			 (gClockedBits+1);
	return bit;
}

/* These are called by the traps in TMS9995.c to do logical tape operations
   since we do not fully emulate the LOAD side in this version of Tutti. */

/* Read ones until we get a zero or EOF. */
void TapeInputSkipSync() {
	char tapebyte;

	if (!tape) return;
	while(1) {
		if (!fread(&tapebyte, 1, 1, tape))
			return;
		if (tapebyte == '0') return;
		// Do not ungetc; we expect the null to be consumed.
	}
}

int TapeInputReadByte() {
	char tapebyte;
	int result = 0;
	int i, j;

	if (!tape) return -1;
	for(i=0; i<8; i++) {
		tapebyte = 0x30; /* emulate low signal if EOF */
		if (!fread(&tapebyte, 1, 1, tape))
			return -1;
		j = (tapebyte == '0') ? 0 : 1;
		result <<= 1;
		result |= j;
	}
	return result;
}

void SetupTapeSave()
{
	if (gInSave)
		FinishTapeSave();
	char *x = saveFilename("tutor.out");
	if (x) {
		gClockedBits = 0;
		fprintf(stderr, "save to %s\n", x);
		gInSave = 1;
		SDL_WM_SetCaption("Tutti (save on)", "Tutti (save on)");
		SaveSpeed();
		enableStopTape();
		if (tape) fclose(tape);
		tape = fopen(x, "wb");
		if (!tape) {
			fprintf(stderr, "SAVE FAILED");
			FinishTapeSave();
		} else {
			fprintf(tape, "1");
		}
	} else {
		fprintf(stderr, "no save filename\n");
	}
}

/* Accept physical bits from the emulated Tutor and emit logical bits. */
void TapeOutput(int bit)
{
	/* bit patterns:
	   01010    = 1
	   010      = 0 */
	switch(gClockedBits) {
		case 0:
			if (bit != 0) {
				// Treat sync failure at the beginning of a
				// new physical bit group as the end of
				// transmission. This can occur for loads or
				// saves.
				fprintf(stderr, "end of data detected\n");
				// If we were saving, this was invariably a
				// one. Emit it.
				if (tape) fprintf(tape, "1");
				(gInSave) ? FinishTapeSave() :
					FinishTapeLoad();
				return;
			}
			gClockedBits++;
			return;
		case 1:
		case 2:
			if ((gClockedBits & 1) != bit) {
				fprintf(stderr, "sync lost, bit %i\n",
					gClockedBits);
				gClockedBits = 0;
				return;
			}
			gClockedBits++;
			return;
		case 3:
			gCurrentBit = bit;
			if (bit == 0) {
				if (tape) fprintf(tape, "0");
				gClockedBits=1;
			} else
				gClockedBits++;
			return;
		case 4:
			if (bit == 1) {
				fprintf(stderr, "sync lost, bit %i\n",
					gClockedBits);
				gClockedBits = 0;
				return;
			}
			if (tape) fprintf(tape, "1");
			gClockedBits = 0;
			return;
	}
	fprintf(stderr, "huh? bits clocked=%i\n", gClockedBits);
}

/* internal routines */

void resetTutor()
{
	TMS9995_Init(TT_ROM1, TT_ROM2);
	TMS9918_Init();
#if ENABLE_AUDIO
	SDL_PauseAudio(1);
	SN76489AN_Init();
	SDL_PauseAudio(0);
#endif
	SDL_WM_SetCaption("Tutti", "Tutti");
	Debugger_Init();
	// We only need to force saves to finish;
	// the Tutor's init routine turns IRQs off
	// and we automatically close loads as a consequence.
        FinishTapeSave();
        memset(gKeyboard, 0, SDLK_LAST);
	gPastewait = 0;
	if (pastechars) {
		free(pasteboard);
		pastechars = 0;
	}
	gQuitWhenAble = 0;
	gResetWhenAble = 0;
	gShowFrame = long_time();
	gCycle = 0;
	setFrameLock(gForceSync);
	setWarpSpeed(gWarpSpeed);
	frameCountDown = FPSDIVISOR;
}

static void audio_callback(void *userdata, uint8_t *stream, int length)
{
	SN76489AN_GenerateSamples((int16_t *)stream, (size_t)length);
}

int main(int argc, char *argv[])
{
	TWORD instruction;
	long ticks;
	SDL_TimerID frameclock;
	int factor = 1000000/FPS;
	int runticks = TICKSPERFRAME;
#if ENABLE_AUDIO
	SDL_AudioSpec *desired = malloc(sizeof(SDL_AudioSpec));

	desired->freq = A_FREQUENCY;
	desired->format = AUDIO_S16SYS;
	desired->channels = 1;
	// This needs to be very, VERY low latency to intercept
	// rapid changes to the DCSG registers.
	desired->samples = 512;
	desired->callback = audio_callback;
	desired->userdata = NULL;

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
	{
		printf("Couldn`t load SDL.\n");
		exit(-1);
	}
 	atexit(SDL_Quit);
	if (SDL_OpenAudio(desired, NULL) < 0)
	{
		printf("Couldn't open audio: %s\n", SDL_GetError());
		exit(-1);
	}
	free(desired);
#else
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
	{
		printf("Couldn`t load SDL.\n");
		exit(-1);
	}
	atexit(SDL_Quit);
#endif
	/* Actually check the return value in the Windows version because
	   we don't have a packaged app like for OS X. */
	if(LoadROM(pathToTutor1(), pathToTutor2())) {
		printf("ROMs are not in the same directory as Tutti.\n");
		exit(-1);
	}

	AddMenus(GetHwnd());
	SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
	resetTutor();
	hTimer = CreateWaitableTimer(NULL, TRUE, NULL);

	if (argc == 2 && !strncmp(argv[1], "-d", 2))
		gDebugger.breakpointHit = 1;

	gCycle = 0;
	while (!gQuitWhenAble)
	{
		gShowFrame = long_time();
		while (gCycle < runticks && !gQuitWhenAble && !gResetWhenAble)
		{
			if (gDebugger.breakpointHit && !gDebugger.enabled)
				Debugger_Enable();
			if (gDebugger.breakpointHit == 0 || gDebugger.inInterrupt)
			{
				TMS9995_ExecuteInstruction();
			}
			if (gDebugger.enabled && !gDebugger.inInterrupt)
			{
				Debugger_Enable();
				Debugger_Update();
				if (gDebuggerShowScreen)
					TMS9918_Force_Redraw();
				else
					TMS9918_Blit();
				PollKeyboard();
			}
		}

		gCycle -= TICKSPERFRAME;
		if (gResetWhenAble)
			resetTutor();

		PollKeyboard();
		if (frameCountDown++ >= FPSDIVISOR+gWarpSpeed) {
			TMS9918_Redraw();
			frameCountDown = 0;
			VDP_Registers.ST &= ~VDP_ST_FLAG_F;
		}
		if (!gForceSync)
		{
			TMS9995_TriggerDecrementer();
		}
		if (!gWarpSpeed) {
			// Usleep instead of using timers; it's more efficient.
			// Unfortunately Win32 doesn't really have usleep(),
			// so we use an API alternative.
			ticks = factor - (long_time() - gShowFrame);
			if (ticks > 0) {
				liTval.QuadPart = -(10*ticks);
				SetWaitableTimer(hTimer, &liTval, 0,
					NULL, NULL, 0);
				WaitForSingleObject(hTimer, INFINITE);
			}
		}
	}

	CloseHandle(hTimer);
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
	if (read(fd, (void *)TT_ROM2, 16384) < 16384)
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
	char *filename = saveFilename("tutor.tut");
	if (!filename) return;

	fd = open(filename, O_WRONLY | O_CREAT | O_BINARY , S_IREAD | S_IWRITE);
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
	char *filename = loadFilename("tutor.tut");
	if (!filename) return;

	fd = open(filename, O_RDONLY | O_BINARY);
	if (fd < 0)
		return;

	read(fd, (void *)memoryMap, 0x10000);
	read(fd, (void *)VDP_MemoryMap, 0x4000);
	read(fd, (void *)(&CPU_Registers), sizeof(CPU_Registers_Type));
	read(fd, (void *)(&VDP_Registers), sizeof(VDP_Registers));

	close(fd);
	TMS9918_Force_Redraw();
}

void PollKeyboard()
{
	TWORD instruction;

	SDL_Event event;
	SDLKey sym;
	TWORD maximumMemory;
	static int Shift, Ctrl;
	SDLKey lastsym = 0;

	if (gDebugger.memoryMode == CPU_TMS9995)
		maximumMemory = 0xFFFF;
	else
		maximumMemory = 0x3FFF;

	while (SDL_PollEvent(&event)) {
		switch(event.type) {
			case SDL_QUIT:
				gQuitWhenAble = 1;
				return;
				
/* Windows system events */
			case SDL_SYSWMEVENT: switch(event.syswm.msg->msg) {
			case WM_COMMAND: switch(LOWORD(event.syswm.msg->wParam)) {

				case ID_FILE_EXIT:
					gQuitWhenAble = 1;
					return;

				case ID_HELP_ABOUT:
MessageBox(GetHwnd(),
"Tutti II 2.2 for Windows\n"
"\n"
"Copyright 2003 Ian Gledhill\n"
"Copyright 2020 Cameron Kaiser"
, "About Tutti II", MB_OK | MB_ICONINFORMATION);
					return;

#define _MKEY(e,x,m) case e: { \
	SDL_Event event; \
	event.type = SDL_KEYDOWN; \
	event.key.keysym.sym = x; \
	event.key.keysym.mod = m; \
	SDL_PushEvent(&event); \
	return; \
}

				_MKEY(ID_EMULATION_RESET, SDLK_r, KMOD_LCTRL)
				_MKEY(ID_FILE_STOP_TAPE, SDLK_s, KMOD_LCTRL)
				_MKEY(ID_FILE_SAVE_SNAPSHOT, SDLK_F5, 0)
				_MKEY(ID_FILE_LOAD_SNAPSHOT, SDLK_F6, 0)
				_MKEY(ID_EDIT_PASTE_AS_K, SDLK_v, KMOD_LCTRL)
				_MKEY(ID_EMULATION_STEP_IN_D, SDLK_F1, 0)
				_MKEY(ID_EMULATION_TURBO, SDLK_b, KMOD_LCTRL)
				_MKEY(ID_EMULATION_SYNC_IRQ3,
					SDLK_f, KMOD_LCTRL)

				}
			}
			
/*
			case SDL_USEREVENT:
				gShowFrame = 1;
				return;
*/

			case SDL_KEYDOWN:
#define COMMANDKEY ((KMOD_LCTRL & event.key.keysym.mod) || (KMOD_RCTRL & event.key.keysym.mod))
				sym = event.key.keysym.sym;
/* Tutti keys -- handled here with events from menu selections */
				if (sym == SDLK_F4 &&
		((KMOD_LALT & event.key.keysym.mod) || (KMOD_RALT & event.key.keysym.mod))) {
					gQuitWhenAble = 1;
					return;
				}
				if (COMMANDKEY) {
				if (sym == SDLK_q) {
					gQuitWhenAble = 1;
					return;
				}
/*
				if (sym == SDLK_m) {
					minimizeApp();
					return;
				}
*/
				if (sym == SDLK_r) {
					gResetWhenAble = 1;
					return;
				}
				if (sym == SDLK_b) {
					// Increasing the slice more isn't much
					// use, since it becomes a small part
					// of runtime.
					gWarpSpeed ^= WARPSPEED;
					toggleWarpSpeed();
					return;
				}
				if (sym == SDLK_f) {
					gForceSync ^= 1;
					toggleFrameLock();
					return;
				}
				if (sym == SDLK_s) {
					FinishTapeSave();
					return;
				}
				if (sym == SDLK_v) {
					if (pastechars) {
						pastepos = pastechars; // halt
						return;
					}
					pasteboard =
						getPastedText(&pastechars);
					if (pastechars) {
						SDL_WM_SetCaption("Tutti (paste on)", "Tutti (paste on)");
						pastepos = 0;
						SaveSpeed();
					}
					return;
				}
				}
				if (sym == SDLK_F5)
					SaveSnapshot();
				if (sym == SDLK_F6)
					LoadSnapshot();
				if (sym == SDLK_F1)
					if (gDebugger.breakpointHit == 1)
					{
						TMS9995_ExecuteInstruction();
						TMS9918_Update();
					}
					else
						gDebugger.breakpointHit = 1;

/* Mode keys */
				// Handle specially: MOD
				if (sym == SDLK_RCTRL || sym == SDLK_RALT || sym == SDLK_KP_ENTER)
					gKeyboard[sym] = 1;
				if (sym == SDLK_LSHIFT || sym == SDLK_RSHIFT)
					Shift = 1;
/* Debugger keys */
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
					if (sym == SDLK_F7)
						gDebugger.dumpToStdout ^= 1;
					if (sym == SDLK_s)
						gDebuggerShowScreen ^= 1;
					if (sym == SDLK_v)
						Debugger_SwitchToVGP();
					if (sym == SDLK_c)
						Debugger_SwitchToCPU();
					if (sym == SDLK_e)
						Debugger_Disable();
					if (sym == SDLK_F10)
						Debugger_Enable();
				}
				else if (!pastechars) {
					if ((sym >= SDLK_BACKSPACE && sym <= SDLK_z) || sym == SDLK_RETURN || sym == SDLK_SPACE || (sym >= SDLK_UP && sym <= SDLK_LEFT))
					{
						gKeyboard[sym] = 1;
					}
/*
					else if (sym >= SDLK_F8 && sym <= SDLK_F11)
					{
						gKeyboard[sym] = 1;
					}
*/
					else if (sym == SDLK_RSHIFT) {
						gKeyboard[sym] = 1;
					}
					else if (sym >= SDLK_LSHIFT && sym <= SDLK_LCTRL )
					{
						gKeyboard[sym] = 1;
					}
					else if (sym >= SDLK_KP0 && sym <= SDLK_KP_PERIOD)
					{
						gKeyboard[sym] = 1;
					}
				}
				break;
			case SDL_KEYUP:
//fprintf(stderr, "keyup   ");
				sym = event.key.keysym.sym;
				if (sym == SDLK_LSHIFT || sym == SDLK_RSHIFT)
				{
					Shift = 0;
				}
				gKeyboard[sym] = 0;
/*
				if (sym >= SDLK_BACKSPACE && sym <= SDLK_z || sym == SDLK_RETURN || sym == SDLK_SPACE || sym >= SDLK_UP && sym <= SDLK_LEFT)
					gKeyboard[sym] = 0;
				else if (sym >= SDLK_F8 && sym <= SDLK_F11)
				{
					gKeyboard[sym] = 0;
				}
				else if (sym >= SDLK_LSHIFT && sym <= SDLK_LCTRL )
				{
				}
*/
				break;
			default:
				break;
		}
	}

	/* Handle pasting keystrokes. The CPU core ticks down the paste
		timeout by watching for the right PC location. */

	/* GBASIC cannot handle multiple key rollover, so key combinations
		require phased keyups. */

	if (!pastechars || gPastewait || gDebugger.enabled) return;
	if (pastechord > 0) {
		// Within a lowercase alpha lock series. 
		// slow, but always puts the alpha lock back to a known
		// state. As a side effect, if they start out in lower case,
		// this makes everything uppercase -- considered a feature.
		memset(gKeyboard, 0, SDLK_LAST);
		gPastewait = SHORTPASTEWAIT;
		switch(--pastechord) {
			case 4:
			case 0:
				// Hit alpha lock
				gKeyboard[CAPS_LOCK_KEY] = 1;
				return;
			case 2:
				gKeyboard[pastechar] = 1;
				return;	
			default:
				return;
		}
	}
	if (pastepos == pastechars) {
		SDL_WM_SetCaption("Tutti", "Tutti");
		memset(gKeyboard, 0, SDLK_LAST);
		free(pasteboard);
		pastechars = 0;
		RestoreSpeed();
		return;
	}

	// Emulate keyup.
	if (gKeyboard[SDLK_LSHIFT]) {
		if (pastechord != -1) {
			memset(gKeyboard, 0, SDLK_LAST);
			gKeyboard[SDLK_LSHIFT] = 1;
			gPastewait = SHORTPASTEWAIT;
			pastechord = -1;
			return;
		} else
			pastechord = 0;
	}
	memset(gKeyboard, 0, SDLK_LAST);
	if (pastechar) {
		// Wait an extra beat for ENTER (but only for 0d, not 0a,
		// since this is Windows).
		gPastewait = (pastechar == 13) ? LONGPASTEWAIT : SHORTPASTEWAIT;
		pastechar = 0;
		return;
	}

	// Emulate keydown.
	pastechar = pasteboard[pastepos++];
	if (!pastechar) return; // politely ignore
	fprintf(stderr, "%c", pastechar);
	gPastewait = SHORTPASTEWAIT;

// Convert pasted characters to Tomy virtual keystrokes AS THEY WOULD BE
// ENTERED (not the actual Tomy keys).
#define KEYSET(x,z) if ((x)) { gKeyboard[z] = 1; return; }
#define KEYSSET(x,z) if ((x)) { gKeyboard[SDLK_LSHIFT] = 1; gKeyboard[z] = 1; return; }
#define KEYCODE(x,z) KEYSET((pastechar == x), z)
#define KEYSCODE(x,z) KEYSSET((pastechar == x), z)

	// RETURN
	// Since this is Windows, just pass 0d, not 0a.
	KEYSET((pastechar == 13), SDLK_RETURN);
	// unshifted: SPACE and , (44) through ; (59). This doesn't
	// get 58: we have to do that specially since it's mapped to '
	KEYCODE(58, 39);
	KEYSET((pastechar == 32 || (pastechar > 43 && pastechar < 60)),
		pastechar);
	// Conveniently, ! (33) through ) (41) are exactly 1-9 shifted
	KEYSSET((pastechar > 32 && pastechar < 42), (pastechar + 16));
	// capital letters
	KEYSSET((pastechar > 64 && pastechar < 91), (pastechar + 32));

	// = is Shift-0
	KEYSCODE(61, 48);
	// + is Shift-;
	KEYSCODE(43, 59);
	// * is Shift-:, which is mapped to '
	KEYSCODE(42, 39);
	// ? is Shift-/
	KEYSCODE(63, 47);
	// < is Shift-,
	KEYSCODE(60, 44);
	// > is Shift-.
	KEYSCODE(62, 46);
	// { is Shift-[
	KEYCODE(91, 91);
	KEYSCODE(123, 91);
	// } is Shift-]
	KEYCODE(93, 93);
	KEYSCODE(125, 93);
	// Handaku is \, which is mapped to =
	// Caret is shift-Handaku
	KEYCODE(92, 61);
	KEYSCODE(94, 61);
	// Flat is shift-- (pipe)
	KEYSCODE(124, 45);
	// _ is mapped to `
	// @ is shift-_
	KEYCODE(95, 96);
	KEYSCODE(64, 96);

	// finally, lowercase
	if (pastechar > 96 && pastechar < 123) {
		pastechord = 5;
		return;
	}

	// could not translate this character to a keystroke.
	fprintf(stderr, "?");
}

