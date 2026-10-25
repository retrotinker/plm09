; Stand-in for the 6809 Microprocessor Kit's monitor, so runtime/kswichit
; hooks can be tested in sim6809 (with --acia 0xA000 for the kit's
; serial port).  It does what the real ROM's reset code does that the
; hooks depend on (stack at $7FF0; RAM interrupt vectors: a JMP to the
; tick counter at $7FF0 for IRQ, RTI at $7FF3 for NMI and $7FF6 for FIRQ;
; SWI vectored to the monitor), then runs the program at $0200, as GO
; does.  Its SWI handler checks that the program put the RAM vectors
; back and ends the simulation with B, the program's exit status (98 if
; a vector wasn't restored).  Assemble to S-records and load AFTER the
; program, so this END wins.
	PRAGMA	6809
SIM_IO	EQU	$FF00
TICK	EQU	$700E

	ORG	$C000
RESET	LDS	#$7FF0
	LDA	#$7E		; JMP
	STA	$7FF0
	LDX	#IRQSERV
	STX	$7FF1
	LDA	#$3B		; RTI
	STA	$7FF3
	STA	$7FF6
	LDX	#0
	STX	$7FF4
	STX	$7FF7
	JMP	$0200		; GO, with PC = $0200

IRQSERV	INC	TICK
	RTI

; the monitor's SWI service: saves the user registers; here, B is the
; exit status, once the vectors are checked
SWISERV	LDA	$7FF0
	CMPA	#$7E
	BNE	BAD
	LDX	$7FF1
	CMPX	#IRQSERV
	BNE	BAD
	LDA	$7FF3
	CMPA	#$3B
	BNE	BAD
	LDA	$7FF6
	CMPA	#$3B
	BNE	BAD
	LDB	2,S		; stacked B
	STB	SIM_IO+2
BAD	LDB	#98
	STB	SIM_IO+2

	ORG	$FFF6
	FDB	$7FF6		; FIRQ
	FDB	$7FF0		; IRQ
	FDB	SWISERV		; SWI
	FDB	$7FF3		; NMI
	FDB	RESET
	END	RESET
