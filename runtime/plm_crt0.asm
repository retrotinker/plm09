; plm09 startup code.  Generated programs INCLUDE this right after their ORG.
; The hooks file must define RT_STACKTOP and PLM_EXIT.
RT_START	LDS	#RT_STACKTOP
	CLRA
	TFR	A,DP
	SETDP	0
	JSR	PLM_MAIN
	CLRB			; normal end of program: exit status 0
	JMP	PLM_EXIT
