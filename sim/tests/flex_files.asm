; sim6809 --flex: FLEX's File Management System on host files: a text file
; written with a run of spaces (compressed: $09 and the count) and read
; back (expanded), then read as binary (raw: 41 09 04 42..., and 252
; bytes, a whole sector, zero-padded); errors; rename; delete; and a file
; closed with nothing written is removed.  The FCB's function code is
; byte 0, its error byte 1, and FMS clears Z for an error.
	PRAGMA	6809
PUTCHR	EQU	$CD18
PCRLF	EQU	$CD24
OUTDEC	EQU	$CD39
OUTHEX	EQU	$CD3C
WARMS	EQU	$CD03
FMS	EQU	$D406
FMSCLS	EQU	$D403
	ORG	$0100
START	LDX	#FCB		; (a file a failed run left goes)
	LBSR	NAME1
	LDA	#12
	LBSR	CALL
	LDX	#FCB
	LBSR	NAME2
	LDA	#12
	LBSR	CALL
	LBSR	NAME1		; open for write, "A    B" CR, close
	LDA	#2
	LBSR	CALL
	LBNE	FAIL
	LEAY	TEXT,PCR
W1	LDA	,Y+
	BEQ	W2
	CLR	,X		; (function 0: the next byte)
	JSR	FMS
	LBNE	FAIL
	BRA	W1
W2	LDA	#4
	LBSR	CALL
	LBNE	FAIL
	LBSR	NAME1		; open for write again: error 3 (exists)
	LDA	#2
	LBSR	CALL
	LBEQ	FAIL
	LDA	1,X
	CMPA	#3
	LBNE	FAIL
	LBSR	NAME1		; open for read: the text, expanded
	LDA	#1
	LBSR	CALL
	LBNE	FAIL
R1	JSR	FMS
	BNE	R2
	JSR	PUTCHR
	BRA	R1
R2	LDA	1,X		; error 8 at the end
	CMPA	#8
	LBNE	FAIL
	LDA	#5		; rewind, then binary: raw bytes
	LBSR	CALL
	LBNE	FAIL
	LDA	#$FF
	STA	59,X
	LDB	#4
R3	JSR	FMS
	LBNE	FAIL
	PSHS	A,X
	TFR	S,X
	JSR	OUTHEX
	PULS	A,X
	DECB
	BNE	R3
	JSR	PCRLF
	LDD	#4		; count the rest: 252 in all
	STD	COUNT
R4	JSR	FMS
	BNE	R5
	LDD	COUNT
	ADDD	#1
	STD	COUNT
	BRA	R4
R5	PSHS	X
	LDX	#COUNT
	CLRB
	JSR	OUTDEC
	JSR	PCRLF
	PULS	X
	LDA	#4
	LBSR	CALL
	LBSR	NAME1		; rename to the second name
	LEAY	53,X
	LEAU	N2,PCR
	LDB	#11
RN	LDA	,U+
	STA	,Y+
	DECB
	BNE	RN
	LDA	#13
	LBSR	CALL
	LBNE	FAIL
	LBSR	NAME1		; the old name: error 4
	LDA	#1
	LBSR	CALL
	LBEQ	FAIL
	LDA	1,X
	CMPA	#4
	LBNE	FAIL
	LBSR	NAME2		; delete; again: error 4
	LDA	#12
	LBSR	CALL
	LBNE	FAIL
	LDA	#12
	LBSR	CALL
	LBEQ	FAIL
	LBSR	NAME1		; opened for write, closed empty: gone
	LDA	#2
	LBSR	CALL
	LBNE	FAIL
	LDA	#4
	LBSR	CALL
	LBSR	NAME1
	LDA	#1
	LBSR	CALL
	LBEQ	FAIL
	JSR	FMSCLS
	LDB	#42
	JMP	WARMS
FAIL	LDB	#99
	JMP	WARMS
CALL	STA	,X		; FMS function A on the FCB at X
	JMP	FMS
NAME1	LEAU	N1,PCR		; the FCB's name: FLXTST1.TXT, drive 0
	BRA	NAME
NAME2	LEAU	N2,PCR		; or FLXTST2.TXT
NAME	LDX	#FCB
	CLR	2,X		; (not open)
	CLR	3,X
	LEAY	4,X
	LDB	#11
NM	LDA	,U+
	STA	,Y+
	DECB
	BNE	NM
	RTS
N1	FCC	"FLXTST1"
	FCB	0
	FCC	"TXT"
N2	FCC	"FLXTST2"
	FCB	0
	FCC	"TXT"
TEXT	FCC	"A    B"
	FCB	13,0
COUNT	RMB	2
FCB	RMB	320
	END	START
