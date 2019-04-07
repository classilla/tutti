/* Tutti II sound driver for SDL. This is implemented as a callback which
   the main loop calls with its register block.

   (C) 2015-6 Cameron Kaiser */

#define ENABLE_AUDIO 1

#define A_AMPLITUDE 8192.0
#define A_FREQUENCY 44100
#define M_FREQUENCY 44100.0

void SN76489AN_Init();
void SN76489AN_WritePort(uint8_t input);
void SN76489AN_GenerateSamples(int16_t *buffer, size_t count);

