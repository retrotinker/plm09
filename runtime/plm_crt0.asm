; plm09 startup code.  Generated programs INCLUDE this right after their ORG.
; The hooks file must define RT_STACKTOP and PLM_EXIT.
; RT_STACKTOP = 0 means "keep running on the caller's stack" (for example
; BASIC's, so PLM_EXIT can return with LDS RT_ENTRYS / RTS).
RT_START	STS	RT_ENTRYS
	LDX	#RT_STACKTOP
	BEQ	RT_START1
	TFR	X,S
RT_START1	CLRA
	TFR	A,DP
	SETDP	0
	JSR	PLM_MAIN
	CLRB			; normal end of program: exit status 0
	JMP	PLM_EXIT
RT_ENTRYS	FDB	0		; S on entry to RT_START
