/* Tutti II sound emulation
   (C)2015-7 Cameron Kaiser. All rights reserved. */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "SN76489AN.h"

#if defined(__clang__) || defined(__GNUC__)
#  define LIKELY(x)   (__builtin_expect(!!(x), 1))
#  define UNLIKELY(x) (__builtin_expect(!!(x), 0))
#else
#  define LIKELY(x)   (!!(x))
#  define UNLIKELY(x) (!!(x))
#endif

/*

The SN76489 has four 4-bit volume registers, 3 10-bit tone registers and 1
3-bit noise register, making one two-bit sound chip (compared to the
C64's SID :-) ). The lower the tone register value, the higher the frequency
(they are treated like counter resets). 0x001 is 111.861kHz, but the highest
generated frequency in practice is 18.643MHz (0x006), well-within the
Nyquist frequency at our sampling rate of 44.1kHz. 0x3ff generates 0.109kHz.
This is a range of approximately 10 octaves. Hz is computed by
3579545 / ((32)(register value)), so 440Hz is roughly 0x0fe.

0x0f is silent, 0x00 is maximum volume (!). We reverse this internally.

The output waveform is approximately a simple "perfect" square wave, although
there is some analogue distortion in the Tutor's output stage. Here we use a
simplified approximation, but most Tutor applications did not exercise the DCSG
very thoroughly, so that will suffice.

*/

/* internal oscillator settings */
uint8_t vol[4];		// Volume for internal mixer, also a register
double freqstep[4];	// Step for frequency generator
double freqstat[4];	// Current position on wave
uint16_t noisenet;	// Pseudorandom noise emulation shift register

/* register status */
uint8_t latch;
uint16_t freq[4];
// We don't track the internal status of the noise register; we
// don't need to because of the way it is written to by the port.

/* biases we use for mixing */
#define FACTOR 32768.0
#define AMPMAX 65535.0

/* precomputed */
uint8_t allquiet;	// Fast all-clear marker
uint8_t on[4];		// Which channels are on?
double volf[4];		// Volume factor (computed from volume)

// Internal utility functions.

// Caches what's playing, if anything.
inline void isallquiet() {
#define ON_ANON(a) on[a] = !(freq[a] == 0 || vol[a] == 0); if(!on[a]) freqstat[a] = 0.0;
	ON_ANON(0)
	ON_ANON(1)
	ON_ANON(2)
	on[3] = !(vol[3] == 0); if (!on[3]) freqstat[3] = 0.0;
	allquiet = !(on[0] || on[1] || on[2] || on[3]);
}

// Convert volume bits to amplitude: each bit increment
// drops the volume by 2dB.
inline double dBtoamp(uint8_t db) {
	// Magic constants determined manually.
	double boost = pow(10.0, ((db*2.02468)/20.0));
	return (boost - 1) * (A_AMPLITUDE / 32.0);
}

// Waveform generators.

inline uint8_t noisebit() {
	// Noise generator. This needs separate processing to generate amplitude
	// (see SN76489AN_GenerateSamples).
	//
	// The SN76489A uses a 15-bit shift register. Taps are on bits 13 and 14
	// and the output is bit 15.
	uint8_t newbit;
	
	// Compute parity of tapped bits.
	newbit = ((noisenet & 0x02) && (noisenet & 0x04)) ? 0 :
			  (noisenet & 0x02) ? 1 :
			  (noisenet & 0x04) ? 1 : 0;
	// Feed it back.
	noisenet |= newbit << 15;
	
	// Shift out next pseudorandom bit.
	newbit = noisenet & 1;
	noisenet >>= 1;
	return newbit;
}

inline double envelope(double freqstat, double vol) {
	// Tone synthesis generator, emitting a ready-to-use amplitude.
	// Square it off for that harsh Tomy sound!
	return vol * (sin(freqstat) + sin(3*freqstat)/3 + sin(5*freqstat)/5 + sin(7*freqstat)/7 + sin(9*freqstat)/9 + sin(11*freqstat)/11);
}

inline double mixer(double a, double b) {
	// Takes the output of two generators and returns a mixed version.
	double m;

	// Pre-bias the amplitudes.
	a += FACTOR;
	b += FACTOR;

	// Use a high quality mixing algorithm to reduce distortion.
	if (a < FACTOR && b < FACTOR) {
		m = (a * b) / FACTOR;
	} else {
		m = (2.0 * (a+b)) - ((a*b) / FACTOR) - AMPMAX;
	}
	if (m > AMPMAX) m=AMPMAX;

	// Remove the bias and return.
	return (m - FACTOR);
}

inline double noisedivider(uint8_t noise) {
	// The Tomy OS supports clock/512, clock/1024 and clock/2048 for the
	// noise channel (BASIC -1, -2 and -3 respectively). clock/512 is also
	// the value used in GBASIC for TONE NO4.
	if (noise == 4) return (3579545.0/512.0);
	if (noise == 5) return (3579545.0/1024.0);
	if (noise == 6) return (3579545.0/2048.0);
	
	// It wouldn't be hard to support using channel 2 (i.e., a value of 7),
	// but the Tomy OS doesn't seem to implement that, so we don't either.
#if DEBUG
	fprintf(stderr, "unexpected noise divider: %i\n", noise);
#endif
	return (3579545.0/512.0);
}

// Public API.

void SN76489AN_Init() {
	size_t i;
	
	for(i=0; i<4; i++) {
		vol[i] = 0x0;
		freq[i] = 0x0;
		volf[i] = 0.0;
		freqstep[i] = 0.0;
		freqstat[i] = 0.0;
		on[i] = 0;
	}
	latch = 0;
	allquiet = 1;
	noisenet = 0x4000; // a guess
}

void SN76489AN_WritePort(uint8_t value) {
	int chan;

	// Tone registers do not wait for all 10 bits. This means that
	// multiple writes back to back could temporarily "eek" if we aren't
	// fast enough.
	if (!latch && !(value & 0x80)) return; // Spurious initial write

	if (value & 0x80) {
		int type = (value & 0x10);
		int data = (value & 0x0f);
		chan = (value & 0x60) >> 5;
		latch = value;

		if (type) { // volume. Precompute factor.		
			vol[chan] = 15 - data;
			volf[chan] = dBtoamp(vol[chan]);
			isallquiet();
			return;
		} else { // tone or noise data
			if (chan == 3) {
				freqstep[3] = noisedivider(data & 0x07);
				return;
			}
			// Place into "low four bits" (that means
			// upper, in this case).
			// ---- --00 00XX XXXX
			freq[chan] &= 0x03f0;
			freq[chan] |= data;
			// fall through to end
		}
	} else { // use existing latch
		int type = (latch & 0x10);
		int data = (value & 0x3f);
		chan = (latch & 0x60) >> 5;

		if (type) { // volume
			vol[chan] = 15 - (data & 0x0f);
			volf[chan] = dBtoamp(vol[chan]);
			isallquiet();
			return;
		} else {
			if (chan == 3) {
				freqstep[3] = noisedivider(data & 0x07);
				return;
			}
			// Place into "upper six bits"
			freq[chan] &= 0x000f;
			freq[chan] |= data << 4;
		}
	}

	// Update oscillators.
	// Compute a new step (2 * pi * frequency / sample rate).
	if (freq[chan] == 0) {
		freqstep[chan] = 0.0;
	} else {
		double nufreq;

		nufreq = 3579545.0/(32.0*freq[chan]);
		freqstep[chan] = 2 * M_PI * nufreq / M_FREQUENCY;
	}
	isallquiet();
}

void SN76489AN_GenerateSamples(int16_t *buffer, size_t count) {
	size_t i;
	uint8_t noisy;
	double a, b, m;

	// The Tutor uses channel 2 (of 0-2) for the system tones, plus noise,
	// and since the same routines service TONE NOa in GBASIC the same
	// applies to GRAPHIC mode. However, BASIC mode can theoretically
	// play all three channels with noise on any combination of channels.
	// Because the general mixer case can be expensive, we have a number of
	// optimized loops we will choose between to generate sound as cheaply
	// as possible.
	//	
	// In the best case, no channels are playing at all. If so, just blank
	// the sound buffer and return.

	if (LIKELY(allquiet)) {
#if __APPLE__ && __ppc__
		// This seems to be a bit faster on 10.4 and 10.5.
		bzero((void *)buffer, count);
#else
		(void)memset((void *)buffer, 0, count);
#endif
		return;
	}

	count >>= 1;
	
	// Some sound is playing, somewhere. Try some optimizations first.
	// Handle solo tone synthesis voice cases with a tight non-mixed loop.
#define SOLO(x, y, z, a) \
	if (!on[x] && !on[y] && !on[z]) { \
		for(i=0; i<count; i++) { \
			buffer[i] = (int16_t)envelope(freqstat[a], volf[a]); \
			freqstat[a] += freqstep[a]; \
		} \
		return; \
	}

	// First handle channel 2 solo. This is the most common situation
	// (GRAPHIC, GBASIC and the menu).
	SOLO(0,1,3, 2)
	
	// Now handle channel 0 solo. This is the second most common situation
	// (BASIC SOUND() with the minimum number of arguments).
	SOLO(1,2,3, 0)
	
	// Now handle noise with or without channel 2, but no other channels.
	// We've handled solo 0 and 2, so if channel 1 isn't on, this must be
	// the situation.
	if (!on[0] && !on[1]) {
#if DEBUG
fprintf(stderr, "sound: noise/2\n");
#endif
		noisy = noisebit();
		if (on[2]) {
			// Channel 2 plus noise (TONE NO4, etc.).
			for(i=0; i<count; i++) {
				a = envelope(freqstat[2], volf[2]);
				b = (noisy) ? volf[3] : -volf[3];
				buffer[i] = (int16_t)mixer(a, b);
				freqstat[2] += freqstep[2];
				freqstat[3] += freqstep[3];
				if (freqstat[3] > M_FREQUENCY) {
					freqstat[3] -= M_FREQUENCY;
					noisy = noisebit();
				}
			}
			return;
		}
		// Noise solo. This can only be triggered in BASIC with
		// something like SOUND(240,-1,0), but this is documented,
		// so it's not rare.
		for(i=0; i<count; i++) {
			b = (noisy) ? volf[3] : -volf[3];
			buffer[i] = (int16_t)b;
			freqstat[3] += freqstep[3];
			if (freqstat[3] > M_FREQUENCY) {
				freqstat[3] -= M_FREQUENCY;
				noisy = noisebit();
			}
		}
		return;
	}
				
	// Now handle channel 1 solo. This can only happen if the channel 0
	// argument for SOUND() had an amplitude of 30, which will mute it;
	// it does not occur in GBASIC or the menus. Nevertheless it's still
	// worth doing even though the situation is rare because it's very
	// inexpensive.
	SOLO(0,2,3, 1)
	
	// For other combinations of voices, we use this multistage mixer.
	// It does a lot of work inside the loop, so we try to avoid it.
#if DEBUG
fprintf(stderr, "sound: can't solo, using mixer\n");
#endif
#define VOICE(n,v) \
	if (!on[n]) { \
		v = 0.0; \
	} else { \
		v = envelope(freqstat[n], volf[n]); \
		freqstat[n] += freqstep[n]; \
	}

	if (on[3]) noisy = noisebit(); // Prepare to generate noise.
	for(i=0; i<count; i++) {
		// Mix channels 0 and 1.
		VOICE(0, a)
		VOICE(1, b)
		m = mixer(a, b);
		
		// Mix channel 2, if enabled.
		if (on[2]) {
			a = envelope(freqstat[2], volf[2]);
			m = mixer(m, a);
			freqstat[2] += freqstep[2];
		}
		
		// Mix noise, if enabled, and emit to buffer.
		if (!on[3]) {
			buffer[i] = (int16_t)m;
		} else {
			b = (noisy) ? volf[3] : -volf[3];
			buffer[i] = (int16_t)mixer(m, b);
			freqstat[3] += freqstep[3];
			if (freqstat[3] > M_FREQUENCY) {
				freqstat[3] -= M_FREQUENCY;
				noisy = noisebit();
			}
		}
	}
}

