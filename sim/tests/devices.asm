; SIMARGS: --swi --acia 0xA000 --ft245 0xD000
; --swi: SWI, SWI2 and SWI3 interrupt through $FFFA, $FFF4, $FFF2, stack
; everything with E set, and (SWI only) set I and F.  --acia: an MC6850 at
; $A000/$A001 and --ft245: an FT245RL at $D000, both on stdin/stdout;
; stdin is empty here, so the ACIA reads RDRF+TDRE and data 0, and the
; FT245 never asserts FIRQ.  The test ends in SYNC, waiting for FT245
; input that won't come, which ends the run with status 0 (after the
; harness has printed its results, if all passed).
	INCLUDE	harness.inc

ACIAST	EQU	$A000
ACIADAT	EQU	$A001
FIFO	EQU	$D000

C	MACRO
	JSR	CHKA
	FCB	\1
	ENDM
CD	MACRO
	JSR	CHKD
	FDB	\1
	ENDM

MAIN	LDX	#N_SWI
	JSR	TBEGIN
	ANDCC	#0
	LDA	#$11
	STS	SMAIN
	SWI
	C	$11		; A restored by RTI
	LDA	WHICH
	C	1
	LDA	HCC		; I and F set by SWI
	ANDA	#$50
	C	$50
	LDA	SCC		; stacked CC: E set
	ANDA	#$80
	C	$80
	LDD	SMAIN
	SUBD	HS
	CD	12		; all registers stacked
	ANDCC	#0
	SWI2
	LDA	WHICH
	C	2
	LDA	HCC		; SWI2 leaves the masks alone
	ANDA	#$50
	C	0
	SWI3
	LDA	WHICH
	C	3
	JSR	TEND

	LDX	#N_ACIA
	JSR	TBEGIN
	LDA	ACIAST		; RDRF and TDRE (end of input: data 0)
	C	3
	LDA	ACIADAT
	C	0
	LDA	#'A		; data writes go to stdout
	STA	ACIADAT
	LDA	#$15		; control writes are ignored
	STA	ACIAST
	LDA	#10
	STA	ACIADAT
	JSR	TEND

	LDX	#N_FIFO
	JSR	TBEGIN
	LDA	#'F		; writes go to stdout
	STA	FIFO
	LDA	#10
	STA	FIFO
	LDA	FIFO		; no input: 0
	C	0
	JSR	TEND
	LDA	H_FAILS		; any failure: exit with the count
	BEQ	WAIT
	JMP	DONE
WAIT	ORCC	#$50
	SYNC			; no input will come: the run ends, status 0
	JMP	DONE		; (not reached)

SWIH	LDA	#1
	BRA	SAVE
SWI2H	LDA	#2
	BRA	SAVE
SWI3H	LDA	#3
SAVE	STA	WHICH
	LDA	,S
	STA	SCC
	TFR	CC,A
	STA	HCC
	STS	HS
	LDA	#$EE
	RTI

WHICH	FCB	0
SCC	FCB	0
HCC	FCB	0
HS	FDB	0
SMAIN	FDB	0
N_SWI	FCN	"SWI"
N_ACIA	FCN	"ACIA"
N_FIFO	FCN	"FT245"

	ORG	$FFF2
	FDB	SWI3H
	FDB	SWI2H
	ORG	$FFFA
	FDB	SWIH
	END	H_START
