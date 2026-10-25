; sim6809 --flex: a DOS entry point that isn't emulated stops the program
	PRAGMA	6809
	ORG	$0100
START	JSR	$CD1B		; INBUFF
	END	START
