/* Disassemble.h
	Used for disassembling the opcode!
	Ian Gledhill 02/04/03 */

void DisassembleOpcode(TWORD *thisOpcode, char *tokenBuffer, int *currentByteNumber, TWORD **currentOpcode);
void DisassembleOpcodeMaybePrint(int doPrint, TWORD *thisOpcode, char *tokenBuffer, int *currentByteNumber, TWORD **currentOpcode);
