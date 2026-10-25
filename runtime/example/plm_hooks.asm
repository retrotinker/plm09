; plm09 hooks template for real 6809 hardware.
;
; Copy this directory, adjust the EQUs and routines for your board, and
; assemble with  -I runtime -I <your copy>  instead of -I runtime/sim.
; This example drives an MC6850 ACIA, as found on many 6809 single-board
; computers.  It is checked to assemble but cannot run in sim6809.
;
; Hook ABI: EXTERNAL procedures receive each argument as a 16-bit word,
; pushed left to right (last argument at 2,S, above the return address);
; the last argument is also in D on entry.  The caller removes the
; arguments.  Return BYTE results in B, ADDRESS results in D.  Hooks may
; change any register except S.  The routines here are included after
; plm_rt.asm, so RT_DIV16 / RT_MUL16 are available.

	IFNDEF	ACIA_BASE
ACIA_BASE	EQU	$A000		; control/status register; data at +1
	ENDC
	IFNDEF	RT_STACKTOP
RT_STACKTOP	EQU	$8000		; stack grows down from here (top of RAM)
	ENDC
	IFNDEF	RT_PORTS
RT_PORTS	EQU	ACIA_BASE	; INPUT(n)/OUTPUT(n): I/O registers from here
	ENDC
	IFNDEF	RT_CYCLES100
RT_CYCLES100	EQU	100		; TIME: E clock cycles in 100 us (1 MHz)
	ENDC
ACIA_STAT	EQU	ACIA_BASE
ACIA_DATA	EQU	ACIA_BASE+1

; end of program; exit status in B.  With no OS to return to, stop here.
; (Under a ROM monitor such as ASSIST09 you could SWI instead.)
PLM_EXIT	BRA	PLM_EXIT

; HALT statement
PLM_HALT	CLRB
	BRA	PLM_EXIT

; PUTCH: PROCEDURE (C) EXTERNAL; DECLARE C BYTE;   waits for TDRE
PUTCH	LDA	ACIA_STAT
	BITA	#$02
	BEQ	PUTCH
	STB	ACIA_DATA
	RTS

; GETCH: PROCEDURE BYTE EXTERNAL;   waits for RDRF
GETCH	LDA	ACIA_STAT
	BITA	#$01
	BEQ	GETCH
	LDB	ACIA_DATA
	CLRA
	RTS

; PUTDEC: PROCEDURE (N) EXTERNAL; DECLARE N ADDRESS;
; prints N in decimal followed by CR LF
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
	BSR	PUTCH
	LEAY	-1,Y
	BNE	PUTDEC2
	LDB	#13
	BSR	PUTCH
	LDB	#10
	BRA	PUTCH

; EXIT: PROCEDURE (CODE) EXTERNAL; DECLARE CODE BYTE;
EXIT	JMP	PLM_EXIT

; PLM_SETINT: install handler X for interrupt B (0 IRQ, 1 FIRQ, 2 NMI);
; called at start-up for each INTERRUPT procedure.  On a board whose
; vectors are in ROM there is nothing to do: build the ROM image with
; plmc --vectors, which puts RT_INT0/1/2 in the vector table.  If your
; vectors go through RAM (as on the CoCo), store X there instead.
PLM_SETINT	RTS
