; crt0 + sim hooks used the way compiled code uses them (EXTERNAL ABI:
; arguments pushed as words, last one also in D, caller pops).
	PRAGMA	6809
	ORG	$0100
	INCLUDE	"plm_crt0.asm"
PLM_MAIN	LDD	#'H
	PSHS	D
	JSR	PUTCH
	LEAS	2,S
	LDD	#'I
	PSHS	D
	JSR	PUTCH
	LEAS	2,S
	LDD	#1234
	PSHS	D
	JSR	PUTDEC
	LEAS	2,S
	JSR	GETCH		; stdin is empty: 0
	PSHS	D
	JSR	PUTDEC
	LEAS	2,S
	TFR	S,D		; stack is balanced at the top
	PSHS	D
	JSR	PUTDEC
	LEAS	2,S
	LDD	#7
	PSHS	D
	JSR	EXIT
	LEAS	2,S
	RTS
	INCLUDE	"plm_rt.asm"
	INCLUDE	"plm_hooks.asm"
	END	RT_START
