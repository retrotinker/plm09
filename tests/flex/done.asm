; Last command of the dragonflex suite's command file.  It runs at $C600,
; in the utility command area, where nothing else the suite runs has code
; (the test programs load at $0100, MARK, P and EXEC at $C100); XRoar traps
; on that PC and quits, so the suite doesn't have to guess how long the
; programs take.
	PRAGMA	6809
	ORG	$C600
START	NOP
	JMP	$CD03		; WARMS
	END	START
