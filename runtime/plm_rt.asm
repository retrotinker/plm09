; plm09 runtime support routines.
; All preserve U, and all but RT_COPY preserve Y; they change at most D,
; X and CC.

; RT_MUL16: D = low 16 bits of X * D (unsigned).
;   Lh:Ll * Rh:Rl = Ll*Rl + ((Ll*Rh + Lh*Rl) << 8)  (mod 2^16)
RT_MUL16	PSHS	D,X		; 0,S=Rh 1,S=Rl 2,S=Lh 3,S=Ll
	LDA	3,S
	LDB	1,S
	MUL			; D = Ll*Rl
	PSHS	D		; 0,S=Ph 1,S=Pl 2,S=Rh 3,S=Rl 4,S=Lh 5,S=Ll
	LDA	5,S
	LDB	2,S
	MUL			; low byte of Ll*Rh goes into the high byte
	ADDB	,S
	STB	,S
	LDA	4,S
	LDB	3,S
	MUL			; low byte of Lh*Rl goes into the high byte
	ADDB	,S
	TFR	B,A
	LDB	1,S
	LEAS	6,S
	RTS

; RT_DIV16: unsigned X / D.  Returns quotient in D, remainder in X.
;   Division by zero gives quotient $FFFF and remainder = dividend.
RT_DIV16	PSHS	D,X		; 0,S=divisor 2,S=dividend, shifted into quotient
	LDB	#16
	PSHS	B		; 0,S=count 1,S=divisor 3,S=quotient
	CLRA
	CLRB			; D = partial remainder
RT_DIV1	LSL	4,S		; next dividend bit into C, 0 into quotient bit
	ROL	3,S
	ROLB
	ROLA			; no carry out: remainder <= dividend prefix < $8000
	CMPD	1,S
	BLO	RT_DIV3
	SUBD	1,S
	INC	4,S		; quotient bit = 1
RT_DIV3	DEC	,S
	BNE	RT_DIV1
	TFR	D,X
	LDD	3,S
	LEAS	5,S
	RTS

; RT_COPY: copy D bytes (D > 0) from X to Y, upwards (X and Y end past
; the bytes).  Used by --os9 programs to load INITIAL values into the data
; area, and by MOVE; assembled only when needed.
	IFDEF	RT_NEED_COPY
RT_COPY	PSHS	D
RT_COPY1	LDA	,X+
	STA	,Y+
	LDD	,S
	SUBD	#1
	STD	,S
	BNE	RT_COPY1
	PULS	D,PC
	ENDC

; RT_TIME: wait B * 100 microseconds (PL/M-80 TIME; B = 0 returns at once).
; The hooks give RT_CYCLES100, the CPU (E) cycles in 100 microseconds.
; Each pass of the outer loop takes 8 * RT_TIMEK + 8 cycles (LDX 3,
; LEAX 5, BNE 3, DECB 2, BNE 3), RT_TIMEK chosen to come nearest.
; Interrupts, if enabled, make the wait longer.  Changes B, X and CC.
	IFDEF	RT_NEED_TIME
RT_TIMEK	EQU	(RT_CYCLES100-4)/8
RT_TIME	TSTB
	BEQ	RT_TIME3
RT_TIME1	LDX	#RT_TIMEK
RT_TIME2	LEAX	-1,X
	BNE	RT_TIME2
	DECB
	BNE	RT_TIME1
RT_TIME3	RTS
	ENDC

; RT_PARITY: B = 0FFH if B has an even number of 1 bits, else 0 (PL/M-80
; PARITY, as the 8080's P flag).  Changes A and CC.
	IFDEF	RT_NEED_PARITY
RT_PARITY	CLRA
RT_PAR1	LSRB			; count the bits in A
	ADCA	#0
	TSTB
	BNE	RT_PAR1
	ANDA	#1		; 1 if odd
	DECA			; even: $FF, odd: 0
	TFR	A,B
	RTS
	ENDC
