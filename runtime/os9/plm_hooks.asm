; plm09 hooks for OS-9 / NitrOS-9 (Level 1 and Level 2) program modules.
;
; Build and run:
;     plmc --os9 -o prog.asm prog.plm
;     lwasm -9 -f os9 -I runtime -I runtime/os9 -o prog prog.asm
;     (copy prog to CMDS on an OS-9 disk, or run it with sim6809 --os9 prog)
;
; Module code must be position-independent and reentrant: only relative
; branches and calls (BRA/LBRA/BSR/LBSR), and data only through U, which
; OS-9 points at the process's data area and which must be preserved.  RMB
; here reserves space in that data area, not in the module.
;
; Hook ABI: see the plmc manual.  Arguments arrive as 16-bit words, the last
; one also in D; BYTE results in B, ADDRESS results in D.  In OS-9 mode a
; hook may change any register except S and U.
;
; Standard input is path 0 and standard output path 1.  OS-9 ends lines
; with CR, so PUTCH sends LF (10) as CR; SCF terminals add the line feed.
; An I/O error other than end of file ends the program with the error
; code as its exit status, as most OS-9 utilities do.

; INPUT(n)/OUTPUT(n) address the hardware directly, port n at RT_PORTS + n
; (the CoCo 3's I/O page; normally best left to OS-9's drivers).  TIME
; waits in a loop timed for NitrOS-9's 1.79 MHz CoCo 3 and doesn't give
; up the CPU.
RT_PORTS	EQU	$FF00
RT_CYCLES100	EQU	179

F$Exit	EQU	$06
I$Read	EQU	$89
I$Write	EQU	$8A
E$EOF	EQU	$D3

RT_IOBUF	RMB	1		; one-character buffer (data area)
RT_DECBUF	RMB	6		; up to 5 digits and a CR

; end of program; exit status in B
PLM_EXIT	OS9	F$Exit

; HALT statement
PLM_HALT	CLRB
	OS9	F$Exit

; I/O error: exit with the OS-9 error code in B
RT_IOERR	OS9	F$Exit

; PUTCH: PROCEDURE (C) EXTERNAL; DECLARE C BYTE;
PUTCH	CMPB	#10
	BNE	PUTCH1
	LDB	#13
PUTCH1	STB	RT_IOBUF,U
	LEAX	RT_IOBUF,U
	LDY	#1
	LDA	#1		; standard output
	OS9	I$Write
	BCS	RT_IOERR
	RTS

; GETCH: PROCEDURE BYTE EXTERNAL;   0 at end of input
GETCH	LEAX	RT_IOBUF,U
	LDY	#1
	CLRA			; standard input
	OS9	I$Read
	BCC	GETCH1
	CMPB	#E$EOF
	BNE	RT_IOERR
	CLRB
	CLRA
	RTS
GETCH1	LDB	RT_IOBUF,U
	CLRA
	RTS

; PUTDEC: PROCEDURE (N) EXTERNAL; DECLARE N ADDRESS;
; prints N in decimal and a CR, with a single I$Write
PUTDEC	LEAY	RT_DECBUF+6,U	; Y = end of buffer; digits are stored backwards
	PSHS	D
	LDA	#13
	STA	,-Y
	PULS	D
PUTDEC1	TFR	D,X
	LDD	#10
	LBSR	RT_DIV16	; D = quotient, X = remainder (Y, U preserved)
	EXG	D,X
	ADDB	#'0
	STB	,-Y
	TFR	X,D
	CMPX	#0
	BNE	PUTDEC1
	TFR	Y,X		; X = first character
	LEAY	RT_DECBUF+6,U
	PSHS	X
	TFR	Y,D
	SUBD	,S++
	TFR	D,Y		; Y = length
	LDA	#1
	OS9	I$Write
	BCS	RT_IOERR
	RTS

; EXIT: PROCEDURE (CODE) EXTERNAL; DECLARE CODE BYTE;
EXIT	OS9	F$Exit
