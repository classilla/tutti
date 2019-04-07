/* TMS9918ANL.h
	Ian Gledhill 02/04/03 */

typedef struct TMS9918_Struct
{
	TWORD MP;
	TBYTE ST;
	TWORD Registers[8];
} TMS9918_Type;

typedef struct Sprite_Struct
{
	TBYTE y;
	TBYTE x;
	TBYTE id;
	TBYTE colour;
} TMS9918_SpriteData;

#define VDP_ST_FLAG_F	0x80
#define VDP_ST_FLAG_S5	0x40
#define VDP_ST_FLAG_C	0x20

extern TMS9918_Type VDP_Registers;
extern unsigned char VDP_MemoryMap[16384];

int TMS9918_Init();

void TMS9918_PrintDebugFont(int x, int y, char letter);
void TMS9918_Update();
void TMS9918_WriteToVDPRegister(TBYTE byte);
void TMS9918_WriteToVDPData(TBYTE byte);
TBYTE TMS9918_ReadFromVDPData();
TBYTE TMS9918_ReadStatusRegister();

inline void TMS9918_DrawPixel(int x, int y, int PaletteEntry);
void TMS9918_DrawSprites();

inline void TMS9918_Slock();
inline void TMS9918_Sulock();
void TMS9918_Force_Redraw();
void TMS9918_Redraw();
