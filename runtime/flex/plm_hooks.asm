; plm09 hooks for FLEX 9 (TSC's disk operating system for 6809 machines).
;
; Build and run (srec2flex makes a FLEX binary, with the S9 entry address
; as its transfer address):
;     plmc -o prog.asm prog.plm
;     lwasm -9 -f srec -I runtime -I runtime/flex -o prog.s19 prog.asm
;     srec2flex prog.s19 PROG.CMD
;     (copy PROG.CMD to a FLEX disk and type PROG, or run it with
;     sim6809 --flex PROG.CMD)
;
; Only FLEX's documented DOS entry points ($CD00 jump table) are used, so
; the hooks work on any FLEX 9 system.  User RAM is $0000-$BFFF: the
; default load address $0100 is fine, and the stack is put just below
; FLEX's own, at the top of user RAM.  The program returns to FLEX through
; WARMS when it ends, on HALT, or on EXIT; FLEX has no exit status (B is
; passed on, which sim6809 --flex uses as its exit status).
;
; Hook ABI: see the plmc manual.  Arguments arrive as 16-bit words, the last
; one also in D; BYTE results in B, ADDRESS results in D; any register but S
; may be changed.  FLEX's routines keep Y and U.

	IFNDEF	RT_STACKTOP
RT_STACKTOP	EQU	$C000		; below FLEX's stack ($C000-$C07F)
	ENDC
	IFNDEF	RT_PORTS
RT_PORTS	EQU	$E000		; INPUT/OUTPUT: the I/O area of SWTPC and
	ENDC				; Gimix systems; set yours with -D
	IFNDEF	RT_CYCLES100
RT_CYCLES100	EQU	100		; TIME: a 1 MHz CPU; set yours with -D
	ENDC

FLEX_WARMS	EQU	$CD03		; return to FLEX
FLEX_GETCHR	EQU	$CD15		; A = a character from the terminal (or file)
FLEX_PUTCHR	EQU	$CD18		; output A, honoring TTYSET
FLEX_PCRLF	EQU	$CD24		; new line, honoring TTYSET (pause, nulls)
FLEX_OUTDEC	EQU	$CD39		; print the 16-bit number at X in decimal

; end of program; back to FLEX
PLM_EXIT	JMP	FLEX_WARMS

; HALT statement: also back to FLEX
PLM_HALT	CLRB
	BRA	PLM_EXIT

; PUTCH: PROCEDURE (C) EXTERNAL; DECLARE C BYTE;
; LF (10) starts a new line with PCRLF (CR, LF and TTYSET's nulls), so
; programs written for the simulator print the same way.
PUTCH	CMPB	#10
	BNE	PUTCH1
	JMP	FLEX_PCRLF
PUTCH1	TFR	B,A
	JMP	FLEX_PUTCHR

; GETCH: PROCEDURE BYTE EXTERNAL;   waits for a key (RETURN gives 13).
; Most FLEX terminals echo what is typed.
GETCH	JSR	FLEX_GETCHR
	TFR	A,B
	CLRA
	RTS

; PUTDEC: PROCEDURE (N) EXTERNAL; DECLARE N ADDRESS;
; prints N in decimal (OUTDEC, no leading spaces) and a new line
PUTDEC	PSHS	D
	TFR	S,X		; X -> the number
	CLRB			; no leading spaces
	JSR	FLEX_OUTDEC
	LEAS	2,S
	JMP	FLEX_PCRLF

; EXIT: PROCEDURE (CODE) EXTERNAL; DECLARE CODE BYTE;
EXIT	JMP	PLM_EXIT

; INTERRUPT procedures need PLM_SETINT, which isn't defined here: where a
; FLEX machine's interrupt vectors are, and what FLEX's drivers do with
; them, depends on the machine.  Add one for yours if you need it.
