; IRQ, FIRQ and NMI from the MMIO interrupt registers, the timer, CWAI
; and SYNC.  Expected values worked out from the MC6809 datasheet:
; IRQ and NMI stack the entire state with E=1 (12 bytes), FIRQ only PC
; and CC with E=0 (3 bytes); IRQ sets I, FIRQ and NMI set I and F.
	INCLUDE	harness.inc

INTSET	EQU	$FF06
INTCLR	EQU	$FF07
TIMHI	EQU	$FF08
TIMLO	EQU	$FF09
TIMLINE	EQU	$FF0A

C	MACRO
	JSR	CHKA
	FCB	\1
	ENDM
CD	MACRO
	JSR	CHKD
	FDB	\1
	ENDM

MAIN	LDX	#N_IRQ
	JSR	TBEGIN
	CLR	NIRQ
	ORCC	#$50
	LDA	#1
	STA	INTSET		; IRQ asserted, but masked
	NOP
	LDA	NIRQ
	C	0
	LDA	INTSET		; reads the asserted lines
	C	1
	LDA	#$5A
	LDX	#$1234
	STS	SMAIN
	ANDCC	#$AF		; unmask: taken before the next instruction
	NOP
	C	$5A		; A and X restored by RTI
	TFR	X,D
	CD	$1234
	LDA	NIRQ
	C	1
	LDA	ICC		; stacked CC: E set, I clear
	ANDA	#$90
	C	$80
	LDA	HCC		; CC inside the handler: I set, F clear
	ANDA	#$50
	C	$10
	LDD	SMAIN
	SUBD	HS		; 12 bytes stacked
	CD	12
	LDA	INTSET		; the handler released the line
	C	0
	JSR	TEND

	LDX	#N_FIRQ
	JSR	TBEGIN
	CLR	NFIRQ
	ORCC	#$50
	LDA	#2
	STA	INTSET
	LDA	#$77
	STS	SMAIN
	ANDCC	#$BF		; unmask F only
	NOP
	C	$99		; FIRQ doesn't stack A: the handler's value
	LDA	NFIRQ
	C	1
	LDA	ICC		; stacked CC: E clear
	ANDA	#$80
	C	0
	LDA	HCC		; I and F set inside
	ANDA	#$50
	C	$50
	LDD	SMAIN
	SUBD	HS		; 3 bytes stacked
	CD	3
	JSR	TEND

	LDX	#N_NMI
	JSR	TBEGIN
	CLR	NNMI
	ORCC	#$50		; NMI ignores the masks
	LDA	#4
	STA	INTSET
	NOP
	NOP
	LDA	NNMI		; taken once: it is an edge
	C	1
	LDA	ICC
	ANDA	#$80
	C	$80
	LDA	HCC
	ANDA	#$50
	C	$50
	JSR	TEND

	LDX	#N_PRIO
	JSR	TBEGIN
	LDX	#ORDER
	STX	ORDERP
	ORCC	#$50
	LDA	#3		; IRQ and FIRQ together
	STA	INTSET
	ANDCC	#$AF
	NOP
	LDA	ORDER		; FIRQ first, then IRQ
	C	'F
	LDA	ORDER+1
	C	'I
	JSR	TEND

	LDX	#N_TIMER
	JSR	TBEGIN
	CLR	NIRQ
	ORCC	#$50
	LDD	#100
	STA	TIMHI
	STB	TIMLO		; an IRQ every 100 instructions
	ANDCC	#$EF
	LDX	#0
TLOOP	LEAX	1,X		; 4 instructions per pass
	LDA	NIRQ
	CMPA	#3
	BNE	TLOOP
	CLR	TIMLO		; stop it (period 0)
	LDA	NIRQ
	C	3
	; The timer counts every instruction, the handler's 15 too, and an
	; interrupt is taken between instructions.  First period: ANDCC, LDX,
	; 24 passes, LEAX, LDA (X=25).  Second: handler, CMPA, BNE, 20
	; passes, LEAX, LDA, CMPA (X=46).  Third: handler, BNE, 21 passes
	; (X=67).  Then the handler, and one more LEAX before the loop ends.
	TFR	X,D
	CD	68
	JSR	TEND

	LDX	#N_CWAI
	JSR	TBEGIN
	CLR	NIRQ
	ORCC	#$50
	LDD	#50
	STA	TIMHI
	STB	TIMLO
	STS	SMAIN
	CWAI	#$EF		; unmask I, stack everything, wait
	CLR	TIMLO
	LDA	NIRQ
	C	1
	LDA	ICC		; stacked by CWAI: E set, I clear
	ANDA	#$90
	C	$80
	LDD	SMAIN
	SUBD	HS		; 12 bytes, stacked once
	CD	12
	JSR	TEND

	LDX	#N_SYNC
	JSR	TBEGIN
	CLR	NIRQ
	ORCC	#$50		; masked: SYNC just ends when the line rises
	LDD	#20
	STA	TIMHI
	STB	TIMLO
	SYNC
	CLR	TIMLO
	LDA	NIRQ
	C	0
	LDA	INTSET
	C	1
	ANDCC	#$EF		; now it is taken
	NOP
	LDA	NIRQ
	C	1
	JSR	TEND

	LDX	#N_TFIRQ
	JSR	TBEGIN
	CLR	NFIRQ
	CLR	NNMI
	LDA	#2
	STA	TIMLINE		; the timer drives FIRQ now
	LDD	#10
	STA	TIMHI
	STB	TIMLO
	ANDCC	#$AF
TF1	LDA	NFIRQ
	CMPA	#2
	BLO	TF1
	LDA	#4
	STA	TIMLINE		; and NMI
TF2	LDA	NNMI
	BEQ	TF2
	CLR	TIMLO
	LDA	#1
	STA	TIMLINE
	LDA	NIRQ		; no IRQ meanwhile (the loops above
	C	1		; only end if FIRQ and NMI happen)
	JSR	TEND
	JMP	DONE

; IRQ: count, note CC and S, release the line (as a device would be
; acknowledged), change A and X (RTI restores them)
IRQH	INC	NIRQ
	LDA	,S
	STA	ICC
	TFR	CC,A
	STA	HCC
	STS	HS
	LDA	#1
	STA	INTCLR
	LDX	ORDERP
	LDA	#'I
	STA	,X+
	STX	ORDERP
	LDA	#$EE
	LDX	#$EEEE
	RTI

FIRQH	INC	NFIRQ
	STA	,-S		; S as it was on entry is 1,S here
	LDA	1,S
	STA	ICC
	TFR	CC,A
	STA	HCC
	LEAX	1,S
	STX	HS
	LDA	#2
	STA	INTCLR
	LDX	ORDERP
	LDA	#'F
	STA	,X+
	STX	ORDERP
	LEAS	1,S
	LDA	#$99
	RTI

NMIH	INC	NNMI
	LDA	,S
	STA	ICC
	TFR	CC,A
	STA	HCC
	RTI

NIRQ	FCB	0
NFIRQ	FCB	0
NNMI	FCB	0
ICC	FCB	0
HCC	FCB	0
HS	FDB	0
SMAIN	FDB	0
ORDERP	FDB	SCRATCH
ORDER	RMB	4
SCRATCH	RMB	16
N_IRQ	FCN	"IRQ"
N_FIRQ	FCN	"FIRQ"
N_NMI	FCN	"NMI"
N_PRIO	FCN	"PRIORITY"
N_TIMER	FCN	"TIMER"
N_CWAI	FCN	"CWAI"
N_SYNC	FCN	"SYNC"
N_TFIRQ	FCN	"TIMERLINES"

	ORG	$FFF6
	FDB	FIRQH
	FDB	IRQH
	ORG	$FFFC
	FDB	NMIH
	END	H_START
