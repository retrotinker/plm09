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

ROM_POLCAT	EQU	$A000		; vector: poll keyboard; A = key, Z set if none
ROM_CHROUT	EQU	$A002		; vector: output A to device DEVNUM
ROM_CLS	EQU	$A928		; Color BASIC: clear text screen, home cursor

; end of program; back to BASIC (the exit status in B is discarded by EXEC)
PLM_EXIT	LDS	RT_ENTRYS
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
