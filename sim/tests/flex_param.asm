; SIMARGS: --param ONE,TWO
; sim6809 --flex: the command line in the line buffer ($C080), the file's
; name and the parameters, ending with CR, and the line buffer pointer
; ($CC14) past the name and the blank: prints the line, then the
; parameters
	PRAGMA	6809
	ORG	$0100
START	LDX	#$C080		; the whole line
	BSR	LINE
	LDX	$CC14		; from the line buffer pointer
	BSR	LINE
	CLRB
	JMP	$CD03		; WARMS
LINE	LDA	,X+		; print to the CR, then a new line
	CMPA	#13
	BEQ	EOL
	JSR	$CD18		; PUTCHR
	BRA	LINE
EOL	JMP	$CD24		; PCRLF
	END	START
