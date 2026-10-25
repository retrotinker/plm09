; Run before each test program, with its output also sent to the printer
; (P,MARK): prints a line that separates the programs' outputs.
	PRAGMA	6809
PUTCHR	EQU	$CD18
PCRLF	EQU	$CD24
WARMS	EQU	$CD03
	ORG	$C100		; the utility command area, as FLEX's commands
START	LDX	#TEXT
LOOP	LDA	,X+
	BEQ	END1
	JSR	PUTCHR
	BRA	LOOP
END1	JSR	PCRLF
	JMP	WARMS
TEXT	FCC	"=== plm09 ==="
	FCB	0
	END	START
