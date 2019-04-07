/* TMS9918ANL.c Ian Gledhill 02/04/03 */
/* Optimized, improved and bug fixed by Cameron Kaiser (C)2015-6 */

/* Again, this implements only what the Tutor needs:
   - Incomplete sprite emulation (although it does have Early Clock)
   - Updates tied to known attributes of Tomy OS VDP RAM usage
   - No multicolour mode
   
   Border would be nice in a future version. */

#include <stdlib.h>
#include <string.h>

#include "SDL/SDL.h"

#include "TMS9995.h"
#include "TMS9918ANL.h"
#include "Debugger.h"

/* Allow the Makefile to specify screen size. */
#ifndef SCREEN_X
#define SCREEN_X 2
#endif

#if (SCREEN_X > 2 || SCREEN_X < 1)
#error Only 1x or 2x screens are currently supported.
#endif

#if defined(__clang__) || defined(__GNUC__)
#  define LIKELY(x)   (__builtin_expect(!!(x), 1))
#  define UNLIKELY(x) (__builtin_expect(!!(x), 0))
#  define ALIGN16     __attribute__((aligned(16)))
#else
#  define LIKELY(x)   (!!(x))
#  define UNLIKELY(x) (!!(x))
// This could be problematic on an AltiVec-enabled platform with a weird
// C compiler, but you'll just have to deal (or use the pure C version).
#  define ALIGN16
#endif

#if __ALTIVEC__
#include <assert.h>
#include <altivec.h>

// Vectors for blitting.
vector unsigned char permvec1 ALIGN16 =
	{ 0, 1, 0, 1, 2, 3, 2, 3, 4, 5, 4, 5, 6, 7, 6, 7 };
vector unsigned char permvec2 ALIGN16 =
	{ 8, 9, 8, 9,10,11,10,11,12,13,12,13,14,15,14,15 };
#endif

SDL_Surface *screen;
typedef struct RGBValueStruct
{
	unsigned char r;
	unsigned char g;
	unsigned char b;
	unsigned char a;
} RGBValue;

typedef struct 
{
	unsigned char rows[8];
} FontCharacter;

TMS9918_Type VDP_Registers;
unsigned char VDP_MemoryMap[16384];
// This has to be global for gcc < 4.4 or it isn't aligned.
Uint16 pixels[49152] ALIGN16;
TBYTE lastByte=0x00;
TWORD VDPUsingAddress=0x0000;
int skipupdate = 0;

#define WM_NOTSTARTED		0
#define WM_BYTE1READY		1
#define WM_WAITINGFORDATA	2
#define WM_WAITINGFORREG	3

int writeMode = WM_NOTSTARTED;

#include "DebugFont.h"

#define TI_TRANSPARENT		0x00
#define TI_BLACK			0x01
#define TI_MEDIUM_GREEN		0x02
#define TI_LIGHT_GREEN		0x03
#define TI_DARK_BLUE		0x04
#define TI_LIGHT_BLUE		0x05
#define TI_DARK_RED			0x06
#define TI_CYAN				0x07
#define TI_MEDIUM_RED		0x08
#define	TI_LIGHT_RED		0x09
#define TI_DARK_YELLOW		0x0A
#define TI_LIGHT_YELLOW		0x0B
#define TI_DARK_GREEN		0x0C
#define TI_MAGENTA			0x0D
#define TI_GRAY				0x0E
#define TI_WHITE			0x0F
#define TI_FOREGROUND		0x10

static RGBValue ColourTable [17] = {
#if(0)
// Original TutorEm colours
        { 0x00, 0x00, 0x00, 0x00 },		// 0x00 TI_TRANSPARENT - Background
        { 0x00, 0x00, 0x00, 0x00 },		// 0x01 TI_BLACK
        { 0x48, 0x9C, 0x08, 0x00 },		// 0x02 TI_MEDIUM_GREEN *
        { 0x70, 0xBF, 0x88, 0x00 },		// 0x03 TI_LIGHT_GREEN *
        { 0x28, 0x3C, 0x8A, 0x00 },		// 0x04 TI_DARK_BLUE *
        { 0x50, 0x6C, 0xCF, 0x00 },		// 0x05 TI_LIGHT_BLUE *
        { 0xD0, 0x48, 0x00, 0x00 },		// 0x06 TI_DARK_RED *
        { 0x00, 0xCC, 0xFF, 0x00 },		// 0x07 TI_CYAN *
        { 0xD0, 0x58, 0x28, 0x00 },		// 0x08 TI_MEDIUM_RED *
        { 0xFF, 0xA0, 0x40, 0x00 },		// 0x09 TI_LIGHT_RED *
        { 0xFC, 0xF0, 0x50, 0x00 },		// 0x0A TI_DARK_YELLOW *
        { 0xFF, 0xFF, 0x80, 0x00 },		// 0x0B TI_LIGHT_YELLOW *
        { 0x00, 0x80, 0x00, 0x00 },		// 0x0C TI_DARK_GREEN
        { 0xCD, 0x58, 0xCD, 0x00 },		// 0x0D TI_MAGENTA *
        { 0xE0, 0xE0, 0xE0, 0x00 },		// 0x0E TI_GRAY *
        { 0xFF, 0xFF, 0xFF, 0x00 },		// 0x0F TI_WHITE
        { 0xFF, 0xFF, 0xFF, 0x00 }		// 0x10 - Text Mode Foreground
#else
// Tutti II 2.0 colours
{	0,		0,		0,		0	},
{	0,		0,		0,		0	},
{	33,		200,	66,		0	},
{	94,		220,	120,	0	},
{	84,		85,		237,	0	},
{	125,	118,	252,	0	},
{	212,	82,		77,		0	},
{	66,		235,	245,	0	},
{	252,	85,		84,		0	},
{	255,	121,	120,	0	},
{	212,	193,	84,		0	},
{	230,	206,	128,	0	},
{	33,		176,	59,		0	},
{	201,	91,		186,	0	},
{	204,	204,	204,	0	},
{	255,	255,	255,	0	},
{	255,	255,	255,	0	}
#endif
};

void TMS9918_Blit() {
	// Not for external callers. This does the scaling and blitting.
	// If we ever need this to be reentrant, we really need a mutex.
	int i, j, k, l;
	Uint16 *dst = (Uint16 *)screen->pixels;
#if __ALTIVEC__
	vector unsigned short input, output1, output2;

	vec_dstt(pixels, 32, 0);
	vec_dststt(dst, ((64 << 24) | 64), 1);
	vec_dststt((Uint16 *)(dst + 512), ((64 << 24) | 64), 2);
#endif

	if ( SDL_MUSTLOCK(screen) ) SDL_LockSurface(screen);
#if (SCREEN_X == 1)
	// Blit the screen. Favour memcpy() because it is often SIMD-powered.
	memcpy((void *)screen->pixels, (void *)pixels, 256 * 192 * 2);
#elif (SCREEN_X == 2)
	// AltiVec and pure C versions follow.
	for (i=0; i<49152; i+= 256) {
#if __ALTIVEC__
		for (k=i; k<i+256; k+=16) {
			// AltiVec version. Load eight 16-bit source pixels,
			// then vperm them into two output vectors. Unroll
			// it a bit and store into the next row while we're
			// at it.
			input = vec_ld(0, &(pixels[k]));
			// Seems to work best interleaving perms and LSU ops
			// since G4e can put the perms simultaneously with
			// the load/stores and the G5 only has one perm unit
			// anyway.
			output1 = vec_perm(input, input, permvec1);
			vec_st(output1,    0, dst);
			output2 = vec_perm(input, input, permvec2);
			vec_st(output1, 1024, dst);
			input = vec_ld(16, &(pixels[k]));
			vec_st(output2,   16, dst);
			output1 = vec_perm(input, input, permvec1);
			vec_st(output2, 1040, dst);
			output2 = vec_perm(input, input, permvec2);
			vec_st(output1,   32, dst);
			vec_st(output1, 1056, dst);
			vec_st(output2,   48, dst);
			vec_st(output2, 1072, dst);
			dst += 32;
		}
		dst += 512;
#else
		for (k=i; k<i+256; k+=8) {
			// Unrolled C version. Fast enough on icky Intel Macs.
			*dst++ = pixels[k];
			*dst++ = pixels[k];
			*dst++ = pixels[k+1];
			*dst++ = pixels[k+1];
			*dst++ = pixels[k+2];
			*dst++ = pixels[k+2];
			*dst++ = pixels[k+3];
			*dst++ = pixels[k+3];
			*dst++ = pixels[k+4];
			*dst++ = pixels[k+4];
			*dst++ = pixels[k+5];
			*dst++ = pixels[k+5];
			*dst++ = pixels[k+6];
			*dst++ = pixels[k+6];
			*dst++ = pixels[k+7];
			*dst++ = pixels[k+7];
		}
		// Dup the row with a memcpy().
		memcpy((void *)dst, (void *)(dst - 512), 1024);
		dst += 512;
#endif
	}
#else
#error NYI
#endif
#if __ALTIVEC__
	vec_dssall();
#endif
	if ( SDL_MUSTLOCK(screen) ) SDL_UnlockSurface(screen);
	TMS9918_Update();
}

int TMS9918_Init()
{
	int x, y;
#if __APPLE__
// SDL_HWSURFACE is noticeably faster even though it shouldn't be.
#define VMFLAGS SDL_HWSURFACE
#else
#define VMFLAGS SDL_SWSURFACE
#endif
	screen = SDL_SetVideoMode ( 256 * SCREEN_X, 192 * SCREEN_X,
		 16, VMFLAGS );
#if __ALTIVEC__
	fprintf(stderr, "Altivec enabled; display=%08x; backing=%08x\n",
		screen->pixels, pixels);
	// Enforce AltiVec alignment.
	assert(!((uintptr_t)(screen->pixels) & 0x0f) && 
		!((uintptr_t)(pixels) & 0x0f));
#endif
	skipupdate = 0;

	for (x=0 ; x<256 ; x++)
		for (y=0 ; y<192 ; y++)
			TMS9918_DrawPixel(x, y, TI_LIGHT_BLUE);
	TMS9918_Blit();

	VDP_Registers.MP = 0x0000;
	VDP_Registers.ST = 0x0000;	
	for (y=0 ; y<8 ; y++)
		VDP_Registers.Registers[y] = 0x0000;	

	memset(VDP_MemoryMap, 0xF0, 16384);

	return 0;
}

void TMS9918_WriteToVDPRegister(TBYTE byte)
{	static TBYTE lastcommand;

	skipupdate = 0;
	lastcommand = byte;
	if (writeMode == WM_NOTSTARTED || writeMode == WM_WAITINGFORDATA)
	{
		lastByte = byte; 
		writeMode = WM_BYTE1READY;
		return;
	}

	switch (byte & 0xC0)
	{
	case 0x00:
		writeMode = WM_NOTSTARTED;
		VDP_Registers.MP = lastByte + ((byte & 0x3F) << 8);
		return;
	case 0x40:
		writeMode = WM_WAITINGFORDATA;
		VDP_Registers.MP = lastByte + ((byte & 0x3F) << 8);
		return;
	case 0x80:
		writeMode = WM_NOTSTARTED;
		VDP_Registers.Registers[byte & 0x07] = lastByte;
		return;
	}

	printf("Eek! Invalid mode %02x writing to VDP.\n", byte);
#if DEBUG
	gDebugger.breakpointHit = 1;
#endif
}

inline void TMS9918_WriteToVDPData(TBYTE byte)
{
	// Only some kinds of writes require forcing an update.
	if (VDP_Registers.Registers[0] & 0x02) {
		// GRAPHIC mode. Writes above 0x3800 do not need an update.
		if (LIKELY(VDP_Registers.MP < 0x3800)) skipupdate = 0;
	} else {
		// BASIC mode. Writes above 0x0820 do not need an update.
		if (VDP_Registers.MP < 0x0820) skipupdate = 0;
	}
/*
if (VDP_Registers.MP == 0x30a1 && byte == 0x80) {
fprintf(stderr, "HIT! %04x\n", CPU_Registers.PC);
gDebugger.breakpointHit = 1;
}
*/
	
	VDP_MemoryMap[VDP_Registers.MP++] = byte; //memoryMap[0xE000];
}

inline TBYTE TMS9918_ReadFromVDPData()
{
	return VDP_MemoryMap[VDP_Registers.MP++];
}

inline TBYTE TMS9918_ReadStatusRegister()
{
	TBYTE OldST = VDP_Registers.ST;
	VDP_Registers.ST |= VDP_ST_FLAG_F;
	return VDP_Registers.ST;
}

inline void TMS9918_Update() 
{
	SDL_Flip(screen);
}

void TMS9918_PrintDebugFont(int x, int y, char letter)
{
	/* This doesn't have to be very fast. */
	FontCharacter *source;
	int pixelX, pixelY;

	if (letter > '(')
		source = &DebugFont[letter-'('];
	else
		source = &Debug_Space;

	for (pixelX = 0 ; pixelX < 6 ; pixelX++)
		for (pixelY = 0; pixelY < 8 ; pixelY++)
			if (source->rows[pixelY] & (1 << (5-pixelX+2)))
				TMS9918_DrawPixel(x*6+pixelX, y*8+pixelY, TI_BLACK);
			else
				TMS9918_DrawPixel(x*6+pixelX, y*8+pixelY, TI_LIGHT_BLUE);
}

inline void TMS9918_DrawPixel(int x, int y, int PaletteEntry)
{
	Uint32 color = SDL_MapRGB(screen->format, ColourTable[PaletteEntry].r, ColourTable[PaletteEntry].g, ColourTable[PaletteEntry].b);
	/* assume pitch is 512 */
	Uint16 *bufp = (Uint16 *)pixels + (y << 8) + x;
	*bufp = color;
}
inline void TMS9918_Draw8Pixels(SDL_Surface *screen, int x, int y,
	int PaletteEntry)
{
	Uint32 color = SDL_MapRGB(screen->format, ColourTable[PaletteEntry].r, ColourTable[PaletteEntry].g, ColourTable[PaletteEntry].b);
	/* assume pitch is 512, assume chunky video */
	Uint16 *bufp = (Uint16 *)pixels + (y << 8) + x;
	*bufp++=color;
	*bufp++=color;
	*bufp++=color;
	*bufp++=color;
	*bufp++=color;
	*bufp++=color;
	*bufp++=color;
	*bufp++=color;
}

/* Drawing the screen is in two separate routines.
   The Tutor is in mode 2 for the title screen, MENU and GRAPHIC.
   For BASIC, it's mode 0. */

#define DPX TMS9918_DrawPixel(cx8+x, cy8, (VDP_MemoryMap[ch8] & mask)?spalette1:spalette0);x++;mask>>=1;
void TMS9918_DrawCharacter_Mode0(int cx, int cy, TBYTE character)
{
	int x, y;
	TBYTE mask;
	TBYTE paletteIndex;
	TBYTE spalette0;
	TBYTE spalette1;
	int characterOffset = 0;
	int cx8 = cx << 3;
	int cy8 = cy << 3;
	int ch8 = character << 3;

	characterOffset = (VDP_Registers.Registers[4]) << 11;

	// Mode 0. Compute palette once.
	paletteIndex = VDP_MemoryMap[(VDP_Registers.Registers[3]<<6)+(character>>3)];
	if ((paletteIndex & 0x0F) == 0x00)
		paletteIndex = (paletteIndex & 0xF0) + (VDP_Registers.Registers[7] & 0x0F);
	if ((paletteIndex & 0xF0) == 0x00)
		paletteIndex = (paletteIndex & 0x0F) + ((VDP_Registers.Registers[7] & 0x0F) << 4);

	ch8 += characterOffset;
	spalette1 = paletteIndex >> 4;
	spalette0 = paletteIndex & 0x0F;
	for (y=0 ; y<8 ; y++)
	{
		x = 0;
		/* Empirically hit most. */
		if (LIKELY(VDP_MemoryMap[ch8] == 0x00)) {
			TMS9918_Draw8Pixels(screen, cx8, cy8,
				spalette0);
		} else
		if (VDP_MemoryMap[ch8] == 0xFF) {
			TMS9918_Draw8Pixels(screen, cx8, cy8,
				spalette1);
		} else {
			mask = 0x80;
DPX
DPX
DPX
DPX
DPX
DPX
DPX
DPX
		}
		ch8++;
		cy8++;
	}
}

void TMS9918_DrawCharacter_Mode2(int cx, int cy, TBYTE character)
{
	int x, y;
	TBYTE mask;
	TBYTE paletteIndex;
	TBYTE spalette0;
	TBYTE spalette1;
	int characterOffset = 0;
	int cx8 = cx << 3;
	int cy8 = cy << 3;
	int ch8 = character << 3;

	characterOffset = (cy & 0xfff8) << 8;

	// Mode 2
	// Palette index can change on every line.
	ch8 += characterOffset;
	characterOffset += (int)character << 3;
	for (y=0 ; y<8 ; y++)
	{
		paletteIndex = VDP_MemoryMap[0x2000 + characterOffset + y];
		if ((paletteIndex & 0x0F) == 0x00)
			paletteIndex = (paletteIndex & 0xF0) + (VDP_Registers.Registers[7] & 0x0F);
		if ((paletteIndex & 0xF0) == 0x00)
			paletteIndex = (paletteIndex & 0x0F) + ((VDP_Registers.Registers[7] & 0x0F) << 4);

		spalette1 = paletteIndex >> 4;
		spalette0 = paletteIndex & 0x0F;
		x = 0;
		/* Empirically hit most. */
		if (LIKELY(VDP_MemoryMap[ch8] == 0x00 ||
			spalette1 == spalette0)) {
			TMS9918_Draw8Pixels(screen, cx8, cy8,
				spalette0);
		} else
		if (VDP_MemoryMap[ch8] == 0xFF) {
			TMS9918_Draw8Pixels(screen, cx8, cy8,
				spalette1);
		} else {
			mask = 0x80;
DPX
DPX
DPX
DPX
DPX
DPX
DPX
DPX
		}
		ch8++;
		cy8++;
	}
}
#undef DPX

/*
 * The 9918 sprite system can best be considered a series of planes, with
 * the backdrop behind the image plane and then it behind the sprite planes,
 * with 31 being rearmost and 0 being frontmost. (Strictly speaking external
 * video is behind the backdrop and black behind that, but we don't model that
 * for the Tutor.)
 *
 * However, the VDP cannot display more than four sprites per scan line. The
 * algorithm is to go one by one through each sprite on each scan line and
 * see if it is visible there. If it isn't, move on. If it is, draw that line
 * (so it would be the scan line relative to its Y coordinate), and if four
 * scan lines were drawn, stop. Since this involves overdraw, we just collect
 * the scan lines to be drawn first and then draw them in reverse order, the
 * rearmost scanline first. Alternatively, if y=208, we stop collecting
 * sprites for display even if we haven't drawn four on that line.
 *
 * The Tomy OS does not appear to utilize or implement the early clock bit,
 * but we do have support for it "for the future." It does use scaling and
 * sizing attributes.
 */

void TMS9918_DrawSprites()
{
	TMS9918_SpriteData *currentSprite;
	TBYTE *spriteGraphic, *spriteLine, paletteIndex, mask;
	int spriteNumber=31, x, y, s, leftBorder=0;
	TBYTE cx, cy;
	TWORD ccx; // Has to handle > 256
	int scale, scalebit, size, extent, current;
	TMS9918_SpriteData *displayList[4];
	int displayLine[4];
	uintptr_t baseAddress = (uintptr_t)VDP_MemoryMap+(VDP_Registers.Registers[5] << 7);

	// Optimization: if the first sprite's Y position is 209, we can
	// just abort, as we will never render sprites on any line.
	currentSprite = (TMS9918_SpriteData *)(baseAddress);
	if (((currentSprite->y+1) & 0xff) == 209) return;

	scalebit = (VDP_Registers.Registers[1] & 0x01);
	scale = scalebit + 1;
	size = (((VDP_Registers.Registers[1] & 0x02) >> 1) << 3) + 8;

	// Display list sprite algorithm.

	extent = size * scale;
	// TODO: When we convert to a true scanline system, this loop
	// needs to be transplanted into that integrated loop.
	for(y=0; y<192; y++) {
		// Determine which sprite lines we will draw. Mark all
		// unused.
		current = 0;
		displayList[0] = NULL;
		displayList[1] = NULL;
		displayList[2] = NULL;
		displayList[3] = NULL;

		for(s=0; s<128; s+=4) {
			currentSprite = (TMS9918_SpriteData *)(baseAddress + s);
			cy = (currentSprite->y+1) & 0xff; // hack

			// Halt if a break is intercepted.
			if (cy == 209) break;

			// The test for which sprite planes are in use on
			// a scanline is not necessarily visibility, just
			// its Y-position. GBASIC will let sprites sit on
			// the top scan line and scroll back into the top
			// border, so we have to support that also (i.e.,
			// a Y position of >FF covers scanlines 0 through
			// |extent|, and (>0100 - |extent|) is completely
			// obscured in the top border).
			if (LIKELY(cy < 0xC1)) {
				// Sprite is between scanline 1 and the bottom.
				if (cy > y)
					continue; // not yet to that scan line
				if (y >= (cy + extent))
					continue; // sprite shown already
				// This line should be displayed. Set it.
				displayLine[current] = (y - cy) >> scalebit;
			} else {
				// Sprite is under the top border somewhere.
				// The number of lines visible is |extent|
				// minus (>FF - sprite Y). So, 255 is all
				// lines, 254 loses the top line, etc.
				int vlines = extent - (0xFF - cy);
				if (vlines < 1)
					continue; // no lines visible
				if (y >= vlines)
					continue; // sprite shown already
				// This line should be displayed. Set it.
				displayLine[current] = (y + (extent - vlines))
					>> scalebit;
			}

			// Add the sprite struct to the display list.
			displayList[current] = currentSprite;
			current++;

			// If we have all our slots filled, stop, else loop.
			if (current == 4) break;

			// TODO: The VDP sets its status register to
			// itself & >E0 | the first "dropped" sprite. This
			// means we need to continue the search and is
			// somewhat unnecessary as the Tomy OS does not
			// expose this register to programs, so let's not
			// do it yet.
		}

		// Draw the lines back to front, unless no sprites found.
		if (current) {
			current--;
			for(s=current; s>=0; s--) {
				currentSprite = displayList[s];
				if (!currentSprite) continue; // paranoia

				paletteIndex = (currentSprite->colour & 0x0F);
				if (!paletteIndex) continue;

				spriteGraphic =
(TBYTE *)(VDP_MemoryMap+(VDP_Registers.Registers[6] << 11)+((currentSprite->id)<<3));
				spriteLine = (TBYTE *)(spriteGraphic + displayLine[s]);
				cx = currentSprite->x;
				cy = currentSprite->y+1;
				if (currentSprite->colour & 0x80) { // Check early clock.
					cx -= 32;
					leftBorder = 1;
				}
				ccx = cx;
				mask = 0x80;
				if (UNLIKELY(scale == 1)) { // The Tomy OS only uses this in the menu.
					for (x=0 ; x<size ; x++) {
						if (ccx >= 256)
							break;
						if (UNLIKELY(leftBorder))
							if (ccx >= 0xD0) break;
						if ((*(spriteLine+(x>>3)*0x10)) & mask) {
							TMS9918_DrawPixel(ccx, y, paletteIndex);
						}
						mask >>= 1;
						if (!mask) mask = 0x80;
						ccx++;
					}
				} else {
					for (x=0 ; x<size ; x++) {
						if (ccx >= 256)
							break;
						if (UNLIKELY(leftBorder))
						 	if (ccx >= 0xD0) break;
						if ((*(spriteLine+(x>>3)*0x10)) & mask) {
							TMS9918_DrawPixel(ccx, y, paletteIndex);
							TMS9918_DrawPixel(ccx+1, y, paletteIndex);
						}
						mask >>= 1;
						if (!mask) mask = 0x80;
						ccx += 2;
					}
				}
			}
		}
	}
}

/* Put the redraw routines into separate ones so that we only have to test once. */

void TMS9918_Redraw_Row_Mode0(TBYTE *characterPointer, int currentCharacter)
{
	int cc32 = currentCharacter >> 5; /* / 32 */
	int mod = 0;
	/* Draw 32 characters */
#define D32	TMS9918_DrawCharacter_Mode0(mod++, cc32, *characterPointer++);
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
#undef D32
}

void TMS9918_Redraw_Row_Mode2(TBYTE *characterPointer, int currentCharacter)
{
	int cc32 = currentCharacter >> 5; /* / 32 */
	int mod = 0;
	/* Draw 32 characters */
#define D32	TMS9918_DrawCharacter_Mode2(mod++, cc32, *characterPointer++);
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
D32
#undef D32
}

void TMS9918_Force_Redraw()
{
	skipupdate = 0;
	TMS9918_Redraw();
}

void TMS9918_Redraw()
{
	int currentCharacter=0;
	TBYTE *characterPointer;
	
	if (skipupdate)
		return;
	
	if (UNLIKELY(!(VDP_Registers.Registers[1] & 0x40)))
	{
		// Blank screen, backdrop colour, no sprites.
		int PaletteEntry = VDP_Registers.Registers[7] & 0x0F;
		Uint16 color = SDL_MapRGB(screen->format, ColourTable[PaletteEntry].r, ColourTable[PaletteEntry].g, ColourTable[PaletteEntry].b);
		Uint16 *bufp = pixels;
		int i;

#if __ALTIVEC__
		// Splat a vector and blast it on. (More efficient would
		// be to do it right to the display, but this isn't heavily
		// hit.)
		// Can't use vec_splat_u16 because the colour is usually
		// out of range.
		vector unsigned short colourv = {
			color, color, color, color,
			color, color, color, color };
		for(i=0; i<49152*2; i+=16) { vec_st(colourv, i, bufp); }
#else
		// A simple memset() won't do here.
		for(i=0; i<49152; i++) { *bufp++ = color; }
#endif
		TMS9918_Blit();
		skipupdate = 1;
		return;
	}

	characterPointer = VDP_MemoryMap+(VDP_Registers.Registers[2] << 10);
	if (VDP_Registers.Registers[0] & 0x02) {
#define ROW TMS9918_Redraw_Row_Mode2(characterPointer, currentCharacter);\
		characterPointer += 32;\
		currentCharacter += 32;
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
#undef ROW
	} else {
		//characterPointer = VDP_MemoryMap+0x1100;
#define ROW TMS9918_Redraw_Row_Mode0(characterPointer, currentCharacter);\
		characterPointer += 32;\
		currentCharacter += 32;
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
ROW
#undef ROW
	}

	TMS9918_DrawSprites();
	TMS9918_Blit();
	skipupdate = 1;
}

