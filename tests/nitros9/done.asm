; Last command of the nitros9 suite's startup script.  It reads $FF6A, an
; unused address in the CoCo 3 I/O page (which NitrOS-9 Level 2 maps into
; every process); XRoar traps on that read and quits, so the suite doesn't
; have to guess how long the programs take.
	PRAGMA	6809
	MOD	EOM,NAME,$11,$81,START,SIZE
	RMB	64
SIZE	EQU	.
NAME	FCS	/tdone/
	FCB	1
START	LDA	$FF6A
	CLRB
	OS9	$06		; F$Exit
	EMOD
EOM	EQU	*
	END
