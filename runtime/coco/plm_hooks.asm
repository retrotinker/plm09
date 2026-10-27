; plm09 hooks for the Tandy Color Computer 1/2/3 under Color BASIC.
;
; Build and run (the program must not load below $0E00: $0100-$0111 holds
; BASIC's RAM interrupt vectors):
;     plmc --org 3000H -o prog.asm prog.plm
;     lwasm -9 -f decb -I runtime -I runtime/coco -o PROG.BIN prog.asm
;     CLEAR 200,&H2FFF : LOADM"PROG" : EXEC
;
; The program runs on BASIC's stack (RT_STACKTOP = 0) with DP = 0, as the
; ROM expects, and returns to BASIC when it ends, on HALT, or on EXIT.
; Output goes through the ROM's CHROUT, so it follows DEVNUM (the screen
; unless redirected); input comes from the keyboard through POLCAT.
;
; Hook ABI: see README.  Arguments arrive as 16-bit words, the last one
; also in D; BYTE results in B, ADDRESS results in D; any register but S
; may be changed.

RT_STACKTOP	EQU	0		; keep BASIC's stack
RT_CYCLES100	EQU	89		; TIME: 0.895 MHz (179 after POKE &HFFD7,0)

ROM_POLCAT	EQU	$A000		; vector: poll keyboard; A = key, Z set if none
ROM_CHROUT	EQU	$A002		; vector: output A to device DEVNUM
ROM_CLS	EQU	$A928		; Color BASIC: clear text screen, home cursor

; end of program; back to BASIC (the exit status in B is discarded by EXEC).
; Puts back any interrupt vectors PLM_SETINT changed, and leaves IRQ
; enabled and FIRQ disabled, as BASIC runs.
PLM_EXIT	ORCC	#$50
	LDX	#COCO_SAVE
	LDY	#COCO_VECS
PLM_EXIT1	TST	3,X		; changed?
	BEQ	PLM_EXIT2
	LDU	,Y
	LDA	,X		; the old JMP instruction
	STA	,U
	LDA	1,X
	STA	1,U
	LDA	2,X
	STA	2,U
	CLR	3,X
PLM_EXIT2	LEAX	4,X
	LEAY	2,Y
	CMPY	#COCO_VECS+6
	BNE	PLM_EXIT1
	ANDCC	#$EF
	LDS	RT_ENTRYS
	RTS

; HALT statement: also back to BASIC
PLM_HALT	CLRB
	BRA	PLM_EXIT

; PUTCH: PROCEDURE (C) EXTERNAL; DECLARE C BYTE;
; LF (10) is sent as CR (13), the CoCo's newline, so programs written for
; the simulator print the same way.
PUTCH	CMPB	#10
	BNE	PUTCH1
	LDB	#13
PUTCH1	TFR	B,A
	JMP	[ROM_CHROUT]

; GETCH: PROCEDURE BYTE EXTERNAL;   waits for a key (ENTER gives 13)
GETCH	JSR	[ROM_POLCAT]
	BEQ	GETCH
	TFR	A,B
	CLRA
	RTS

; PUTDEC: PROCEDURE (N) EXTERNAL; DECLARE N ADDRESS;
; prints N in decimal followed by a newline
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
	PSHS	Y		; don't assume the ROM preserves Y
	BSR	PUTCH
	PULS	Y
	LEAY	-1,Y
	BNE	PUTDEC2
	LDB	#13
	BRA	PUTCH

; EXIT: PROCEDURE (CODE) EXTERNAL; DECLARE CODE BYTE;
EXIT	JMP	PLM_EXIT

; CLS: PROCEDURE EXTERNAL;   clear the text screen
CLS	JMP	ROM_CLS

; PLM_SETINT: install handler X for interrupt B (0 IRQ, 1 FIRQ, 2 NMI);
; called at start-up for each INTERRUPT procedure.  The ROM's vectors
; jump through RAM: a JMP at $010C (IRQ), $010F (FIRQ) or $0109 (NMI),
; which is pointed at the handler.  The old JMP is saved for PLM_EXIT.
; The IRQ handler must acknowledge the PIA (read $FF02 for the 60 Hz
; field sync, $FF00 for line sync), or the interrupt repeats at once;
; BASIC's own IRQ work (TIMER, PLAY, ...) stops while it is replaced.
PLM_SETINT	PSHS	CC
	ORCC	#$50		; not while the vector is half written
	ASLB
	LDY	#COCO_VECS
	LDU	B,Y
	ASLB
	LDY	#COCO_SAVE
	LEAY	B,Y
	TST	3,Y		; saved already?
	BNE	PLM_SETINT1
	LDA	,U
	STA	,Y
	LDA	1,U
	STA	1,Y
	LDA	2,U
	STA	2,Y
	INC	3,Y
PLM_SETINT1	LDA	#$7E		; JMP extended
	STA	,U
	STX	1,U
	PULS	CC,PC

COCO_VECS	FDB	$010C,$010F,$0109
COCO_SAVE	FCB	0,0,0,0,0,0,0,0,0,0,0,0	; per vector: old JMP, saved flag
