; plm09 startup code for OS-9 program modules (plmc --os9).
; Generated modules INCLUDE this right after their name and edition.
; OS-9 enters with U = the data area (all variables are U-relative),
; S = the top of the stack inside the data area, DP = the data page, and
; X/D describing the parameter string (not used).  Everything here is
; position-independent: the module may be loaded at any address.
RT_START	LBSR	PLM_MAIN
	CLRB			; normal end of program: exit status 0
	LBRA	PLM_EXIT
