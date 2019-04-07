/* The Tomy Tutor emulator! Started 31/03/03 by Ian Gledhill! */
/* Mac version (C)2015-7 Cameron Kaiser */

#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <SDL/SDL.h>

#include "tutorem/TMS9995.h"
#include "tutorem/SN76489AN.h"
#include "tutorem/TMS9918ANL.h"
#include "tutorem/Debugger.h"

char TT_ROM1[32768], TT_ROM2[16384];

/* callbacks to SDLMain */
extern void toggleWarpSpeed();
extern void toggleFrameLock();
extern void setWarpSpeed(int value);
extern void setFrameLock(int value);
extern void enableStopTape();
extern void disableStopTape();
extern char *loadFilename();
extern char *saveFilename();
extern char *getPastedText(int *cblen);
extern char *pathToTutor1();
extern char *pathToTutor2();
extern void minimizeApp();

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

/* Me love you, long long_time!() */
static inline long long_time() {
	gettimeofday(&tv, NULL);
	return (tv.tv_sec * 1000000) + tv.tv_usec;
}

static inline void SaveSpeed() {
	gOldWarpSpeed = gWarpSpeed;
	gOldForceSync = gForceSync;
	setFrameLock(gForceSync = 1);
	setWarpSpeed(gWarpSpeed = WARPSPEED); 
}
static inline void RestoreSpeed() {
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
	char *x = loadFilename("tutor.out"); // Get filename from SDLMain.
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
	char *x = saveFilename("tutor.out"); // Get filename from SDLMain.
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
	bzero(gKeyboard, SDLK_LAST);
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

	LoadROM(pathToTutor1(), pathToTutor2());
	resetTutor();

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
			ticks = factor - (long_time() - gShowFrame);
			if (ticks > 0)
				usleep((useconds_t)ticks);
		}
	}

	SDL_Quit();	

	return 0;
}

int LoadROM(char *ROM1, char *ROM2)
{
	int fd, r;

	if ((fd = open(ROM1, O_RDONLY)) < 0)
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
	if ((fd = open(ROM2, O_RDONLY)) < 0)
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

	fd = open(filename, O_WRONLY | O_CREAT , S_IREAD | S_IWRITE);
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
	char *filename = loadFilename();
	if (!filename) return;

	fd = open(filename, O_RDONLY);
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
			
/*
			case SDL_USEREVENT:
				gShowFrame = 1;
				return;
*/

			case SDL_KEYDOWN:
#define COMMANDKEY ((KMOD_LMETA & event.key.keysym.mod) || (KMOD_RMETA & event.key.keysym.mod))
				sym = event.key.keysym.sym;
/* Tutti keys -- handled here with events from SDLMain */
				if (COMMANDKEY) {
				if (sym == SDLK_q) {
					gQuitWhenAble = 1;
					return;
				}
				if (sym == SDLK_m) {
					minimizeApp();
					return;
				}
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
				if (sym == SDLK_F1) {
					if (gDebugger.breakpointHit == 1)
					{
						TMS9995_ExecuteInstruction();
						TMS9918_Update();
					}
					else
					{
						gDebugger.breakpointHit = 1;
					}
				}

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
				gKeyboard[SDLK_LCTRL] = 1;
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
		// Wait an extra beat for ENTER.
		gPastewait = (pastechar == 10 || pastechar == 13) ? LONGPASTEWAIT : SHORTPASTEWAIT;
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
	KEYSET((pastechar == 10 || pastechar == 13), SDLK_RETURN);
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

