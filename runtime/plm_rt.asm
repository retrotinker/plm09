; plm09 runtime support routines.
; All preserve Y and U; they change D, X and CC.

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
