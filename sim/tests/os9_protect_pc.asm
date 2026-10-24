; sim6809 --os9 stops when the PC leaves the module (exit status 125):
; JMP to an absolute address works only where the module was assembled
	PRAGMA	6809
	MOD	EOM,NAME,$11,$81,START,SIZE
	RMB	100
SIZE	EQU	.
NAME	FCS	/PP/
	FCB	1
START	JMP	THERE
THERE	CLRB
	OS9	$06
	EMOD
EOM	EQU	*
	END
