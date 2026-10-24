; SIMARGS: --load 0x3123 --data 0x9E00 --param HELLO
; sim6809 --os9: F$Fork register setup and the emulated system calls.
; Stdin is empty in the test runner, so I$ReadLn must report E$EOF.
	PRAGMA	6809
	MOD	EOM,NAME,$11,$81,START,SIZE
BUF	RMB	16
	RMB	200
SIZE	EQU	.
NAME	FCS	/OS9SYS/
	FCB	1
START	CMPU	#$9E00		; U = data area
	BNE	FAIL
	CMPD	#6		; D = parameter size: "HELLO" + CR
	BNE	FAIL
	TFR	DP,A		; DP = data page
	CMPA	#$9E
	BNE	FAIL
	CMPS	#$9F00-6	; S = X = parameters at the top of the data area
	BNE	FAIL
	LDY	#6		; echo the parameters: I$WritLn stops at the CR
	LDA	#1
	OS9	$8C
	BCS	FAIL
	CMPY	#6
	BNE	FAIL
	LEAX	BUF,U		; I$ReadLn at end of input: carry set, B = E$EOF
	LDY	#16
	CLRA
	OS9	$8B
	BCC	FAIL
	CMPB	#$D3
	BNE	FAIL
	LEAX	MSG,PCR		; I$Write to stderr is accepted, path 5 is not
	LDY	#3
	LDA	#5
	OS9	$8A
	BCC	FAIL
	CMPB	#$C9		; E$BPNum
	BNE	FAIL
	LEAX	MSG,PCR
	LDY	#3
	LDA	#1
	OS9	$8A
	LDB	#42		; F$Exit with status 42
	OS9	$06
FAIL	LDB	#99
	OS9	$06
MSG	FCC	"OK"
	FCB	13
	EMOD
EOM	EQU	*
	END
