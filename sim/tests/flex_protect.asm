; sim6809 --flex: a write to FLEX's memory ($C080-$DFFF) stops the program
	PRAGMA	6809
	ORG	$0100
START	LDA	#1
	STA	$C100		; the utility command area
	JMP	$CD03
	END	START
