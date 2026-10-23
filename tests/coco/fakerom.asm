; Stand-in for the Color BASIC ROM so runtime/coco hooks can be tested in
; sim6809.  Only what the hooks use: the POLCAT/CHROUT vectors at $A000,
; CLS at $A928, plus a fake EXEC caller that runs the program at $3000
; on a BASIC-like stack and checks that it comes back with S intact.
; Assemble to S-records and load AFTER the program, so this END wins.
	PRAGMA	6809
SIM_IO	EQU	$FF00
BASIC_S	EQU	$2F00		; stack pointer at EXEC time

	ORG	$A000
	FDB	FPOLCAT		; [$A000] POLCAT
	FDB	FCHROUT		; [$A002] CHROUT

	ORG	$A100
; POLCAT: A = key or 0 with Z set.  End of input looks like "no key".
FPOLCAT	LDA	SIM_IO+1
	RTS
; CHROUT: print A unchanged (so CR stays CR).  Clobbers Y to catch hooks
; that rely on the ROM preserving it.
FCHROUT	STA	SIM_IO
	LDY	#$A5A5
	RTS

; EXEC: B (the hooks' exit status) becomes the simulator's exit status;
; 99 means the program came back with the wrong stack pointer.
FBASIC	LDS	#BASIC_S
	CLRA
	TFR	A,DP
	JSR	$3000
	CMPS	#BASIC_S
	BEQ	FBASIC1
	LDB	#99
FBASIC1	STB	SIM_IO+2
	BRA	*

	ORG	$A928
FCLS	LDA	#'[
	STA	SIM_IO
	LDA	#'C
	STA	SIM_IO
	LDA	#']
	STA	SIM_IO
	RTS
	END	FBASIC
