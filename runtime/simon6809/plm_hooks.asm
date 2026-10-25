; plm09 hooks for the Simon6809 board (Erturk Kocalar's 6809 SBC, 2007)
; with its BUGGY monitor ROM (Lennart Benschop's sbc09 monitor, ported).
;
; Build and run:
;     plmc --org 0400H -o prog.asm prog.plm
;     lwasm -9 -f srec -I runtime -I runtime/simon6809 -o prog.s19 prog.asm
;     (send prog.s19 to the board's USB port at the monitor's "." prompt,
;     then type G400; when the program ends, the monitor shows the
;     registers, B being the exit status, and prompts again)
;
; The board: a 6809 at 1 MHz (4 MHz crystal), RAM $0000-$7FFF (the
; monitor uses $0000-$03FF, user programs start at $0400), an FT245RL USB
; FIFO at $D000 as the console (its "data available" line drives FIRQ),
; I/O selects at $D000-$D7FF, and the monitor in ROM at $E000-$FFFF.
;
; The console goes through the monitor's I/O vectors in RAM, which it
; sets up at reset: getchar ($001B, B = the next character, waiting for
; one), putchar ($001E, prints B) and putcr ($0027, CR LF).  The monitor
; runs with DP = 0, as plmc's code does.
;
; Hook ABI: see the plmc manual.  Arguments arrive as 16-bit words, the last
; one also in D; BYTE results in B, ADDRESS results in D; any register but S
; may be changed.

RT_STACKTOP	EQU	$8000		; the top of RAM
RT_PORTS	EQU	$D000		; INPUT/OUTPUT: the I/O selects ($D000 = FT245)
RT_CYCLES100	EQU	100		; TIME: 1 MHz

MON_GETCHAR	EQU	$001B		; monitor I/O vectors (JMPs in RAM)
MON_PUTCHAR	EQU	$001E
MON_PUTCR	EQU	$0027

; end of program, HALT, EXIT: back to the monitor with SWI, which it
; treats as a breakpoint: it shows the registers (B = the exit status)
; and prompts for a command.  This works however the program was started
; (G or J).  Interrupt vectors that PLM_SETINT changed are put back first.
PLM_EXIT	ORCC	#$50
	LDX	#SIMON_SAVE
	LDY	#SIMON_VECS
PLM_EXIT1	TST	3,X		; changed?
	BEQ	PLM_EXIT2
	LDU	,Y
	LDA	,X		; the monitor's JMP instruction
	STA	,U
	LDA	1,X
	STA	1,U
	LDA	2,X
	STA	2,U
	CLR	3,X
PLM_EXIT2	LEAX	4,X
	LEAY	2,Y
	CMPY	#SIMON_VECS+6
	BNE	PLM_EXIT1
	ANDCC	#$AF		; as the monitor runs (its console needs FIRQ)
	SWI
	BRA	PLM_EXIT	; (if the program is continued)

PLM_HALT	CLRB
	BRA	PLM_EXIT

; PUTCH: PROCEDURE (C) EXTERNAL; DECLARE C BYTE;
; LF (10) is sent as CR LF (the monitor's putcr), so programs written for
; the simulator print the same way.
PUTCH	CMPB	#10
	BNE	PUTCH1
	JMP	MON_PUTCR
PUTCH1	JMP	MON_PUTCHAR

; GETCH: PROCEDURE BYTE EXTERNAL;   waits for a character (RETURN gives 13)
GETCH	JSR	MON_GETCHAR
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
	JSR	MON_PUTCHAR
	LEAY	-1,Y
	BNE	PUTDEC2
	JMP	MON_PUTCR

; EXIT: PROCEDURE (CODE) EXTERNAL; DECLARE CODE BYTE;
EXIT	JMP	PLM_EXIT

; PLM_SETINT: B = interrupt number (0 IRQ, 1 FIRQ, 2 NMI), X = handler.
; The monitor's interrupt vectors are JMPs in RAM ($0009 IRQ, $0006 FIRQ,
; $000F NMI); the JMP is pointed at the handler, and the monitor's is put
; back when the program ends.  FIRQ is the console's "data available"
; interrupt: a program that takes it over must not use GETCH (the
; monitor's getchar waits for that line itself, with FIRQ masked, and
; still works), and the handler is entered while the FIFO has data.
PLM_SETINT	PSHS	CC
	ORCC	#$50		; not while the vector is half written
	ASLB
	LDY	#SIMON_VECS
	LDU	B,Y
	ASLB
	LDY	#SIMON_SAVE
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

SIMON_VECS	FDB	$0009,$0006,$000F
SIMON_SAVE	FCB	0,0,0,0,0,0,0,0,0,0,0,0	; per vector: old JMP, saved flag
