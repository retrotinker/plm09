; smoke test: strings, decimal port, hex printer, exit status
	INCLUDE	harness.inc

MAIN	LDX	#MSG
	JSR	PUTS
	LDD	#0
	STD	IO_DEC
	LDD	#65535
	STD	IO_DEC
	LDD	#1234
	STD	IO_DEC
	LDA	#$5A
	JSR	PUTHEX
	JSR	PUTNL
	LDD	#$BEEF
	JSR	PUTHEX16
	JSR	PUTNL
	INC	H_FAILS		; two "failures" -> exit status 2
	INC	H_FAILS
	JMP	DONE

MSG	FCC	"HELLO, 6809"
	FCB	10,0
	END	H_START
