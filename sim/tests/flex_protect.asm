; sim6809 --flex: a write to FLEX's memory ($C700-$DFFF) stops the program
; (the utility command area below it belongs to the running command)
	PRAGMA	6809
	ORG	$0100
START	LDA	#1
	STA	$C6FF		; the utility command area: allowed
	STA	$C840		; the system FCB: stopped
	JMP	$CD03
	END	START
