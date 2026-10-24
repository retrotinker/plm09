; sim6809 --os9 stops a write outside the data area (exit status 125)
	PRAGMA	6809
	MOD	EOM,NAME,$11,$81,START,SIZE
	RMB	100
SIZE	EQU	.
NAME	FCS	/PW/
	FCB	1
START	LDA	#1
	STA	$0400		; absolute address: not position-independent
	CLRB
	OS9	$06
	EMOD
EOM	EQU	*
	END
