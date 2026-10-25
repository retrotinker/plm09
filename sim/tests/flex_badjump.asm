; sim6809 --flex: running code in FLEX's memory other than at a DOS entry
; point (here, the middle of FMS) stops the program
	PRAGMA	6809
	ORG	$0100
START	JMP	$D400
	END	START
