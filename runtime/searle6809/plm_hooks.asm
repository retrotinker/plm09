; plm09 hooks for Grant Searle's 6809 single-board computer
; (searle.wales/6809/Simple6809.html) and boards built from it, such as
; Jeff Tranter's PCB, with either ROM:
;   - Jeff Tranter's "combined" ROM (ASSIST09 monitor + BASIC): load with
;     the monitor's L command, run with C 1000 (call as a subroutine)
;   - Grant Searle's BASIC ROM: run with EXEC &H1000 after CLEAR 200,&HFFF
;     (this BASIC can't load machine code itself; the program has to get
;     into RAM some other way)
;
; Build:
;     plmc --org 1000H -o prog.asm prog.plm
;     lwasm -9 -f srec -I runtime -I runtime/searle6809 -o prog.s19 prog.asm
;
; The board: a 68B09 with a 7.3728 MHz crystal (1.8432 MHz E clock), RAM
; $0000-$7FFF, an MC6850 ACIA at $A000 (status/control) and $A001 (data),
; minimally decoded over $A000-$BFFF, set up by the ROM for 115200 bit/s,
; free space at $8000-$9FFF, and the ROM at $C000-$FFFF.
;
; The program runs as a subroutine of the monitor or BASIC and returns to
; it with RTS (so start it with C or EXEC, not ASSIST09's G).  It has its
; own stack below $6FD0, where the combined ROM's disassembler, trace and
; ASSIST09 keep their RAM ($6FD0-$7051); under BASIC that is free memory
; between BASIC's variables and its stack and strings at the top of RAM.
;
; Hook ABI: see the plmc manual.  Arguments arrive as 16-bit words, the last
; one also in D; BYTE results in B, ADDRESS results in D; any register but S
; may be changed.

RT_STACKTOP	EQU	$6FD0		; below the monitor's RAM
RT_CYCLES100	EQU	184		; TIME: 1.8432 MHz

ACIA_STAT	EQU	$A000
ACIA_DATA	EQU	$A001

; end of program, HALT, EXIT: back to the monitor or BASIC with RTS, on
; the stack it called us with (ASSIST09 then shows the registers, B being
; the exit status).  Interrupt vectors that PLM_SETINT changed are put back
; first.
PLM_EXIT	ORCC	#$50
	PSHS	B		; the exit status
	LDX	#GS_SAVE
PLM_EXIT1	LDB	5,X		; changed? (the number of bytes)
	BEQ	PLM_EXIT2
	LDU	,X		; where
	LDA	2,X		; the bytes that were there
	STA	,U
	LDA	3,X
	STA	1,U
	CMPB	#3
	BNE	PLM_EXIT3
	LDA	4,X
	STA	2,U
PLM_EXIT3	CLR	5,X
PLM_EXIT2	LEAX	6,X
	CMPX	#GS_SAVE+18
	BNE	PLM_EXIT1
	PULS	B
	ANDCC	#$AF		; as the ROMs run
	LDS	RT_ENTRYS
	RTS

PLM_HALT	CLRB
	BRA	PLM_EXIT

; PUTCH: PROCEDURE (C) EXTERNAL; DECLARE C BYTE;
; LF (10) is sent as CR LF, for a terminal.
PUTCH	CMPB	#10
	BNE	PUTCH1
	LDB	#13
	BSR	PUTCH1
	LDB	#10
PUTCH1	LDA	ACIA_STAT	; wait for TDRE
	BITA	#$02
	BEQ	PUTCH1
	STB	ACIA_DATA
	RTS

; GETCH: PROCEDURE BYTE EXTERNAL;   waits for a character (RETURN gives 13)
GETCH	LDA	ACIA_STAT	; wait for RDRF
	BITA	#$01
	BEQ	GETCH
	LDB	ACIA_DATA
	CLRA
	RTS

; PUTDEC: PROCEDURE (N) EXTERNAL; DECLARE N ADDRESS;
; prints N in decimal followed by a new line
PUTDEC	LDY	#0		; digit count
PUTDEC1	TFR	D,X
	LDD	#10
	JSR	RT_DIV16	; D = quotient, X = remainder
	EXG	D,X
	ADDB	#'0
	PSHS	B		; digits come out least significant first
	LEAY	1,Y
	TFR	X,D
	CMPX	#0
	BNE	PUTDEC1
PUTDEC2	PULS	B
	BSR	PUTCH1
	LEAY	-1,Y
	BNE	PUTDEC2
	LDB	#10
	BRA	PUTCH

; EXIT: PROCEDURE (CODE) EXTERNAL; DECLARE CODE BYTE;
EXIT	JMP	PLM_EXIT

; PLM_SETINT: B = interrupt number (0 IRQ, 1 FIRQ, 2 NMI), X = handler.
; Both ROMs send interrupts through RAM, differently: ASSIST09's vectors
; lead to JMP [n,PCR] instructions that read the handler's address from
; its RAM vector table; Grant Searle's BASIC points them at JMPs in RAM
; ($00A7 IRQ, $00AA FIRQ, $00A4 NMI).  So the hooks follow the CPU vector
; and either store the handler's address in the word the JMP [n,PCR]
; reads, or put a JMP to the handler at the target.  The bytes changed are
; saved and put back when the program ends.
PLM_SETINT	PSHS	CC
	ORCC	#$50		; not while the vector is half written
	ASLB
	LDY	#GS_CPUVEC
	LDU	[B,Y]		; U = where the CPU vector leads
	PSHS	B
	LDA	,U		; ASSIST09's JMP [n,PCR]?  (6E 9D nnnn)
	CMPA	#$6E
	BNE	SETINT1
	LDA	1,U
	CMPA	#$9D
	BNE	SETINT1
	LDD	2,U
	LEAU	D,U
	LEAU	4,U		; U = the word it reads (relative to the next PC)
	LDA	#2		; (2 bytes: the next vector's word follows)
	BSR	SETSAVE
	STX	,U
	PULS	B
	PULS	CC,PC
SETINT1	LDA	#3
	BSR	SETSAVE		; a RAM vector: JMP to the handler
	LDA	#$7E		; JMP extended
	STA	,U
	STX	1,U
	PULS	B
	PULS	CC,PC

; save the A (2 or 3) bytes at U for interrupt 2,S/2 (once), for PLM_EXIT
SETSAVE	LDB	2,S		; 2 * the interrupt number
	LDY	#GS_SAVE
	LEAY	B,Y
	LEAY	B,Y
	LEAY	B,Y		; Y = GS_SAVE + 6 * n
	TST	5,Y
	BNE	SETSAVE1
	STU	,Y
	STA	5,Y		; the number of bytes
	LDA	,U
	STA	2,Y
	LDA	1,U
	STA	3,Y
	LDA	2,U
	STA	4,Y
SETSAVE1	RTS

GS_CPUVEC	FDB	$FFF8,$FFF6,$FFFC	; IRQ, FIRQ, NMI
GS_SAVE	FCB	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0	; per interrupt: address,
					; 3 bytes, how many were changed
