; Indexed addressing: every postbyte form, indirect, PCR, direct page.
; DATA+i holds i for i = 0..255; PTRS holds pointers into DATA.
	INCLUDE	harness.inc

DATA	EQU	$3000
PTRS	EQU	$3200

C	MACRO
	JSR	CHKA
	FCB	\1
	ENDM
CD	MACRO
	JSR	CHKD
	FDB	\1
	ENDM

MAIN	LDX	#DATA		; DATA+i = i
	CLRB
FILL	STB	,X+
	INCB
	BNE	FILL
	LDX	#DATA+5		; PTRS = DATA+5, DATA+6, DATA+7
	STX	PTRS
	LDX	#DATA+6
	STX	PTRS+2
	LDX	#DATA+7
	STX	PTRS+4

	LDX	#N_OFF
	JSR	TBEGIN
	LDX	#DATA+128
	LDA	,X
	C	$80
	LDA	15,X		; 5-bit
	C	$8F
	LDA	-16,X
	C	$70
	LDA	100,X		; 8-bit
	C	$E4
	LDA	-128,X
	C	$00
	LDA	127,X
	C	$FF
	LDY	#DATA-172	; 16-bit
	LDA	300,Y
	C	$80
	LDU	#DATA+300
	LDA	-300,U
	C	$00
	LDA	#-5		; accumulator offsets are signed
	LDA	A,X
	C	$7B
	LDB	#$10
	LDA	B,X
	C	$90
	LDB	#$F0
	LDA	B,X
	C	$70
	LDD	#-100
	LDA	D,X
	C	$1C
	LDY	#DATA+$54	; Y, U, S as base
	LDA	1,Y
	C	$55
	LDU	#DATA+50
	LDA	1,U
	C	$33
	LDA	#$42
	PSHS	A
	CLRA
	LDA	,S+
	C	$42
	TFR	S,D
	CD	$8000
	JSR	TEND

	LDX	#N_AUTO
	JSR	TBEGIN
	LDX	#DATA+10
	LDA	,X+
	C	$0A
	LDA	,X
	C	$0B
	LDA	,X++
	C	$0B
	LDA	,X
	C	$0D
	LDA	,-X
	C	$0C
	LDA	,--X
	C	$0A
	TFR	X,D
	CD	DATA+10
	LDU	#DATA+20
	LDD	,U++		; 16-bit auto-increment
	CD	$1415
	TFR	U,D
	CD	DATA+22
	LDA	#$99		; store with auto-decrement
	LDY	#$6010
	STA	,-Y
	LDA	$600F
	C	$99
	JSR	TEND

	LDX	#N_PCR
	JSR	TBEGIN
	BRA	PCR1
NEAR	FCB	$5A		; backward reference -> 8-bit offset
PCR1	LDA	NEAR,PCR
	C	$5A
	LDA	FAR,PCR		; 16-bit PC offset
	C	$A5
	LEAX	NEAR,PCR
	TFR	X,D
	CD	NEAR
	JSR	TEND

	LDX	#N_IND
	JSR	TBEGIN
	LDX	#PTRS
	LDA	[,X]
	C	$05
	LDA	[2,X]
	C	$06
	LDA	[,X++]
	C	$05
	LDA	[,X]
	C	$06
	LDA	[,--X]
	C	$05
	LDA	[PTRS]
	C	$05
	LDB	#2
	LDA	[B,X]
	C	$06
	LDA	#4
	LDA	[A,X]
	C	$07
	LDD	#4
	LDA	[D,X]
	C	$07
	LDY	#PTRS-1000
	LDA	[1000,Y]
	C	$05
	LDA	[PPTR,PCR]
	C	$06
	LDA	#$77		; store through a pointer
	STA	[PTRS+4]
	LDA	DATA+7
	C	$77
	LDA	#7
	STA	DATA+7
	LEAX	[PTRS]		; LEA with indirection
	TFR	X,D
	CD	DATA+5
	JSR	TEND

	LDX	#N_LEA
	JSR	TBEGIN
	LDX	#$1000
	LEAX	5,X
	TFR	X,D
	CD	$1005
	LDY	#1		; LEAY sets Z
	LEAY	-1,Y
	TFR	CC,A
	ANDA	#4
	C	4
	LDU	#$1000
	LDD	#$0234
	LEAU	D,U
	TFR	U,D
	CD	$1234
	LEAS	-2,S
	TFR	S,D
	CD	$7FFE
	LEAS	2,S
	TFR	S,D
	CD	$8000
	JSR	TEND

	LDX	#N_DP		; direct page follows DP
	JSR	TBEGIN
	LDA	#DATA/256
	TFR	A,DP
	SETDP	DATA/256
	LDA	<$09
	C	$09
	CLRA
	TFR	A,DP
	SETDP	0
	JSR	TEND
	JMP	DONE

PPTR	FDB	DATA+6
N_OFF	FCN	"OFFSETS"
N_AUTO	FCN	"AUTOINC"
N_PCR	FCN	"PCR"
N_IND	FCN	"INDIRECT"
N_LEA	FCN	"LEA"
N_DP	FCN	"DP"
	RMB	300
FAR	FCB	$A5
	END	H_START
