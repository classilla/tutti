/* The Tutti disassembler/command line debugger.
   Interfaces with the TutorEm disassembler module.
   (C)2015 Cameron Kaiser */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

/* Determine endianness without SDL. This may need work. */
#if __APPLE__
  #include <machine/endian.h>
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
  #include <sys/types.h>
  #include <sys/endian.h>
#elif defined(WIN32)
  #define LITTLE_ENDIAN 3412
  #define BYTE_ORDER LITTLE_ENDIAN
#else
  #include <endian.h>
#endif

#include "tutorem/TMS9995_arch.h"

unsigned char memoryMap[65536];
char TT_ROM1[32768], TT_ROM2[16384];

/* Disassembler wrapper */

inline TWORD SwitchEndianAlways(TWORD *thisWord)
{
        char LSB = *thisWord & 0xFF;
        *thisWord >>= 8;
        *thisWord &= 0xFF;
        *thisWord += (LSB << 8);
        return *thisWord;
}

#if BYTE_ORDER == LITTLE_ENDIAN
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
        if (read(fd, (void *)TT_ROM2, 16384) < 0)
        {
                printf("Error reading ROM %s.\n", ROM2);
                close(fd);
                return -1;
        }
        close(fd);

        return 0;
}

int main(int argc, char **argv) {
	int sa, ea, posn, oposn, spa;
	char *bad;
	char buf[256];
	TWORD *opcode;
	TWORD sopcode;

	if (argc < 2 || argc > 3) {
		fprintf(stderr, "usage: %s sa [ea]\n", argv[0]);
		return 1;
	}
	sa = strtol(argv[1], &bad, 0);
	if (*bad != '\0' || sa < 0 || sa > 65535 || (sa & 1)) {
		fprintf(stderr, "%s: bad starting address %s\n",
			argv[0], argv[1]);
		return 1;
	}
	if (argc == 3) {
		ea = strtol(argv[2], &bad, 0);
		if (*bad != '\0' || ea < 0 || ea > 65535 || (ea & 1)) {
			fprintf(stderr, "%s: bad ending address %s\n",
				argv[0], argv[2]);
			return 1;
		}
	} else
		ea = sa + 2;
	
	if (LoadROM("tutor1.bin", "tutor2.bin"))
		return 1;

        memset(memoryMap, 0xf0, 65536);
        memcpy(memoryMap, TT_ROM1, 0x8000);
        memcpy(memoryMap+0x8000, TT_ROM2, 0x4000);
	posn = sa;

	while (posn < ea) {
		opcode = (TWORD *)(memoryMap+posn);
#if(0)
		spa = 15;
		oposn = posn;
		sopcode = *opcode;
		SwitchEndian(&sopcode);

		fprintf(stdout, "%04X: %04X ", posn, sopcode);
		DisassembleOpcode(opcode, buf, &posn, &opcode);
		while (posn > oposn) {
			oposn += 2;
			opcode = (TWORD *)(memoryMap+oposn);
			sopcode = *opcode;
			SwitchEndian(&sopcode);
			fprintf(stdout, "%04X ", sopcode);
			spa -= 5;
		}
		while(--spa) { fprintf(stdout, " "); }
		fprintf(stdout, "%s\n", buf);
#else
		DisassembleOpcodeMaybePrint(1, opcode, buf, &posn, &opcode);
#endif
		posn += 2;
	}

	return 0;
}

