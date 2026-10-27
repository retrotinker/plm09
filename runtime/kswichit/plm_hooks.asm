; plm09 hooks for Wichit Sirichote's 6809 Microprocessor Kit (2017,
; kswichit.net/6809/6809.htm) with its monitor (version 3 or later).
;
; Build and run:
;     plmc --org 0200H -o prog.asm prog.plm
;     lwasm -9 -f srec -I runtime -I runtime/kswichit -o prog.s19 prog.asm
;     (press LOAD on the kit and send prog.s19 over the serial port, 19200
;     bit/s 8N1 with a 1 ms delay after each character; then GO runs it
;     from $0200.  When it ends, the kit shows the user registers again,
;     B being the exit status)
;
; The kit: a 68B09 with a 4.9152 MHz crystal (1.2288 MHz E clock), RAM
; $0000-$7FFF (the monitor keeps its variables and stack at $7000-$7FFF,
; with RAM interrupt vectors at $7FF0 IRQ, $7FF3 NMI, $7FF6 FIRQ), I/O
; ports at $8000-$8003 (GPIO1 LEDs, keypad, 7-segment display), a text
; LCD at $9000, an MC6850 ACIA at $A000 (the serial port, which the
; monitor has set up for 19200 bit/s), and the monitor ROM from $C000.
;
; The console is the serial port: the hooks use the ACIA directly, not
; the monitor's routines, whose addresses change from one monitor version
; to the next.
;
; Hook ABI: see the plmc manual.  Arguments arrive as 16-bit words, the last
; one also in D; BYTE results in B, ADDRESS results in D; any register but S
; may be changed.

RT_STACKTOP	EQU	$7000		; below the monitor's variables and stack
RT_CYCLES100	EQU	123		; TIME: 1.2288 MHz

ACIA_STAT	EQU	$A000		; status (read), control (write)
ACIA_DATA	EQU	$A001

; end of program, HALT, EXIT: back to the monitor with SWI, which saves
; the registers as the user registers (B = the exit status) and shows the
; program counter.  Interrupt vectors that PLM_SETINT changed are put back
; first.
PLM_EXIT	ORCC	#$50
	LDX	#KIT_SAVE
	LDY	#KIT_VECS
PLM_EXIT1	TST	3,X		; changed?
	BEQ	PLM_EXIT2
	LDU	,Y
	LDA	,X		; the monitor's instruction (JMP, or RTI)
	STA	,U
	LDA	1,X
	STA	1,U
	LDA	2,X
	STA	2,U
	CLR	3,X
PLM_EXIT2	LEAX	4,X
	LEAY	2,Y
	CMPY	#KIT_VECS+6
	BNE	PLM_EXIT1
	SWI
	BRA	PLM_EXIT	; (if GO continues the program)

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
; The ROM's vectors point into RAM ($7FF0 IRQ, $7FF6 FIRQ, $7FF3 NMI),
; where the monitor puts a JMP to its 10 ms tick counter for IRQ and an
; RTI for the others.  A JMP to the handler goes there instead, and the
; monitor's instruction is put back when the program ends.  (The IRQ
; switch selects the 89C2051's 10 ms tick or the IRQ button; the handler
; needs no acknowledgement for either.)
PLM_SETINT	PSHS	CC
	ORCC	#$50		; not while the vector is half written
	ASLB
	LDY	#KIT_VECS
	LDU	B,Y
	ASLB
	LDY	#KIT_SAVE
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

KIT_VECS	FDB	$7FF0,$7FF6,$7FF3
KIT_SAVE	FCB	0,0,0,0,0,0,0,0,0,0,0,0	; per vector: old instruction, saved flag
