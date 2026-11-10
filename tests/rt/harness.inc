; Shared harness for sim6809 self-tests.
;
; A test file INCLUDEs this, defines MAIN, and ends with "END H_START".
; MAIN runs with S=$8000 and finishes with JMP DONE, which exits with
; status = total number of failed checks.
;
; Check groups:  LDX #name / JSR TBEGIN ... checks ... JSR TEND
;   prints "OK name" if every check in the group passed, otherwise one
;   "FAIL name #nn ..." line per failed check (nn = check number, hex,
;   counting from 00 within the group).
IO_PUTCH	EQU	$FF00
IO_GETCH	EQU	$FF01
IO_EXIT	EQU	$FF02
IO_DEC	EQU	$FF04

	PRAGMA	6809
	ORG	$1000
H_START	LDS	#$8000
	CLR	H_FAILS
	JMP	MAIN

H_FAILS	FCB	0		; failures in whole file
T_NAME	FDB	0		; current group name
T_NUM	FCB	0		; check number within group
T_BAD	FCB	0		; failures within group

; ---------------------------------------------------------------- output
; All output routines preserve every register except CC.

; print zero-terminated string at X
PUTS	PSHS	A,X
PUTS1	LDA	,X+
	BEQ	PUTS2
	STA	IO_PUTCH
	BRA	PUTS1
PUTS2	PULS	A,X,PC

PUTNL	PSHS	A
	LDA	#10
	STA	IO_PUTCH
	PULS	A,PC

PUTSP	PSHS	A
	LDA	#32
	STA	IO_PUTCH
	PULS	A,PC

; print A as two hex digits
PUTHEX	PSHS	A
	LSRA
	LSRA
	LSRA
	LSRA
	BSR	PUTNIB
	LDA	,S
	BSR	PUTNIB
	PULS	A,PC
PUTNIB	ANDA	#$0F
	ADDA	#'0
	CMPA	#'9
	BLS	PUTNIB1
	ADDA	#7
PUTNIB1	STA	IO_PUTCH
	RTS

; print D as four hex digits
PUTHEX16	PSHS	D
	BSR	PUTHEX
	TFR	B,A
	BSR	PUTHEX
	PULS	D,PC

; ---------------------------------------------------------------- groups

; begin a check group named by the string at X
TBEGIN	STX	T_NAME
	CLR	T_NUM
	CLR	T_BAD
	RTS

; end a check group: print OK if nothing failed
TEND	PSHS	X
	TST	T_BAD
	BNE	TEND1
	LDX	#S_OK
	JSR	PUTS
	LDX	T_NAME
	JSR	PUTS
	JSR	PUTNL
TEND1	PULS	X,PC

; count a failure of check T_NUM and print "FAIL name #nn" (no newline)
REPORT	PSHS	A,X
	INC	T_BAD
	INC	H_FAILS
	LDX	#S_FAIL
	JSR	PUTS
	LDX	T_NAME
	JSR	PUTS
	LDX	#S_NUM
	JSR	PUTS
	LDA	T_NUM
	JSR	PUTHEX
	PULS	A,X,PC

; compare A with the inline byte after the JSR; preserves all but CC
CHKA	PSHS	X
	LDX	2,S		; return address -> expected byte
	LEAX	1,X
	STX	2,S		; (STX sets flags, so compare after)
	CMPA	-1,X
	BEQ	CHKA1
	JSR	REPORT
	LDX	#S_GOT
	JSR	PUTS
	JSR	PUTHEX
	JSR	PUTNL
CHKA1	INC	T_NUM
	PULS	X,PC

; compare D with the inline word after the JSR; preserves all but CC
CHKD	PSHS	X
	LDX	2,S
	LEAX	2,X
	STX	2,S
	CMPD	-2,X
	BEQ	CHKD1
	JSR	REPORT
	LDX	#S_GOT
	JSR	PUTS
	JSR	PUTHEX16
	JSR	PUTNL
CHKD1	INC	T_NUM
	PULS	X,PC

; ---------------------------------------------------- table-driven ALU tests
; RUN8:  X = table, Y = op routine.  Table: count byte, then 6-byte vectors
;        A_in, M, CC_in, A_expected, CC_expected, CC_mask.
;        The routine is entered with A = A_in, CC = CC_in, X -> vector, so
;        it can reach M as 1,X; it must leave the result in A, flags in CC.
; RUN16: same with 9-byte vectors D_in(2), M(2), CC_in, D_expected(2),
;        CC_expected, CC_mask; M is at 2,X.
R_CNT	FCB	0
R_RES	FDB	0
R_CC	FCB	0

RUN8	PSHS	X,Y
	LDB	,X+
	STB	R_CNT
RUN8L	LDA	2,X
	PSHS	A
	LDA	,X
	PULS	CC
	JSR	,Y
	PSHS	CC
	STA	R_RES
	PULS	A
	STA	R_CC
	LDA	R_RES
	CMPA	3,X
	BNE	RUN8B
	LDA	R_CC
	ANDA	5,X
	CMPA	4,X
	BEQ	RUN8N
RUN8B	BSR	RBAD8
RUN8N	INC	T_NUM
	LEAX	6,X
	DEC	R_CNT
	BNE	RUN8L
	PULS	X,Y,PC

RBAD8	JSR	REPORT
	PSHS	X
	LDX	#S_GOT
	JSR	PUTS
	LDA	R_RES
	JSR	PUTHEX
	LDX	#S_CC
	JSR	PUTS
	LDA	R_CC
	JSR	PUTHEX
	JSR	PUTNL
	PULS	X,PC

RUN16	PSHS	X,Y
	LDB	,X+
	STB	R_CNT
RUN16L	LDA	4,X
	PSHS	A
	LDD	,X
	PULS	CC
	JSR	,Y
	PSHS	CC
	STD	R_RES
	PULS	A
	STA	R_CC
	LDD	R_RES
	CMPD	5,X
	BNE	RUN16B
	LDA	R_CC
	ANDA	8,X
	CMPA	7,X
	BEQ	RUN16N
RUN16B	BSR	RBAD16
RUN16N	INC	T_NUM
	LEAX	9,X
	DEC	R_CNT
	BNE	RUN16L
	PULS	X,Y,PC

RBAD16	JSR	REPORT
	PSHS	X
	LDX	#S_GOT
	JSR	PUTS
	LDD	R_RES
	JSR	PUTHEX16
	LDX	#S_CC
	JSR	PUTS
	LDA	R_CC
	JSR	PUTHEX
	JSR	PUTNL
	PULS	X,PC

DONE	LDA	H_FAILS
	STA	IO_EXIT
	BRA	*

S_OK	FCC	"OK "
	FCB	0
S_FAIL	FCC	"FAIL "
	FCB	0
S_NUM	FCC	" #"
	FCB	0
S_GOT	FCC	" got "
	FCB	0
S_CC	FCC	" cc "
	FCB	0
