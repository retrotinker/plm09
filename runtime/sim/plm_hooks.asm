; plm09 hooks for sim6809 (MMIO at $FF00).
;
; Hook ABI (see README): EXTERNAL procedures receive each argument as a
; 16-bit word pushed left to right, so the last argument is at 2,S above
; the return address; the last argument is also in D on entry.  BYTE
; results are returned in B, ADDRESS results in D.  Hooks may change any
; register except S.
SIM_IO	EQU	$FF00
RT_STACKTOP	EQU	$FF00		; stack grows down from $FEFF

; end of program; B = exit status
PLM_EXIT	STB	SIM_IO+2
	BRA	PLM_EXIT

; HALT statement
PLM_HALT	CLRB
	BRA	PLM_EXIT

; PUTCH: PROCEDURE (C) EXTERNAL; DECLARE C BYTE;
PUTCH	STB	SIM_IO
	RTS

; PUTDEC: PROCEDURE (N) EXTERNAL; DECLARE N ADDRESS;   prints N and a newline
PUTDEC	STD	SIM_IO+4
	RTS

; GETCH: PROCEDURE BYTE EXTERNAL;   returns 0 at end of input
GETCH	LDB	SIM_IO+1
	CLRA
	RTS

; EXIT: PROCEDURE (CODE) EXTERNAL; DECLARE CODE BYTE;
EXIT	JMP	PLM_EXIT
