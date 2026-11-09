; sim6809 --os9: host files through I$Create, I$Open, I$Write, I$Read,
; I$Seek, I$GetStt, I$SetStt, I$Close and I$Delete.  Files are read and
; written as they are (CR stays CR); the terminal's paths are SCF devices.
	PRAGMA	6809
	MOD	EOM,NAME,$11,$81,START,SIZE
PATH	RMB	1
BUF	RMB	32
	RMB	200
SIZE	EQU	.
NAME	FCS	/OS9FILES/
	FCB	1
START	LEAX	FNAME,PCR	; (a file left by a failed run goes)
	OS9	$87
	LEAX	BUF,U		; path 1 is a terminal: SS.Opt type 0
	LDD	#$0100
	OS9	$8D
	LBCS	FAIL
	LDA	BUF,U
	LBNE	FAIL
	LEAX	FNAME,PCR	; I$Create, write: the new path is 3
	LDA	#2
	OS9	$83
	LBCS	FAIL
	CMPA	#3
	LBNE	FAIL
	STA	PATH,U
	LDA	,X		; X goes past the name, to its CR
	CMPA	#13
	LBNE	FAIL
	LEAX	DATA,PCR	; I$Write 6 bytes, with a CR among them
	LDY	#6
	LDA	PATH,U
	OS9	$8A
	LBCS	FAIL
	CMPY	#6
	LBNE	FAIL
	LEAX	BUF,U		; a file: SS.Opt type 1
	LDA	PATH,U
	LDB	#0
	OS9	$8D
	LBCS	FAIL
	LDA	BUF,U
	CMPA	#1
	LBNE	FAIL
	LDA	PATH,U		; I$Close; closing it again is E$BPNum
	OS9	$8F
	LBCS	FAIL
	LDA	PATH,U
	OS9	$8F
	LBCC	FAIL
	CMPB	#$C9
	LBNE	FAIL
	LEAX	FNAME,PCR	; I$Create of an existing file: E$CEF
	LDA	#2
	OS9	$83
	LBCC	FAIL
	CMPB	#$DA
	LBNE	FAIL
	LEAX	FNAME,PCR	; I$Open, read
	LDA	#1
	OS9	$84
	LBCS	FAIL
	STA	PATH,U
	LDB	#2		; SS.Size: 6, in X:U
	PSHS	U
	OS9	$8D
	TFR	U,D
	PULS	U
	LBCS	FAIL
	CMPX	#0
	LBNE	FAIL
	CMPD	#6
	LBNE	FAIL
	LEAX	BUF,U		; I$Read 4: "AB",CR,"C"
	LDY	#4
	LDA	PATH,U
	OS9	$89
	LBCS	FAIL
	CMPY	#4
	LBNE	FAIL
	LDA	BUF+2,U
	CMPA	#13
	LBNE	FAIL
	LDA	PATH,U		; SS.Pos: 4
	LDB	#5
	PSHS	U
	OS9	$8D
	TFR	U,D
	PULS	U
	LBCS	FAIL
	CMPD	#4
	LBNE	FAIL
	LDA	PATH,U		; SS.EOF: not yet
	LDB	#6
	OS9	$8D
	LBCS	FAIL
	LDA	PATH,U		; I$Seek to 1, I$ReadLn: "B",CR
	LDX	#0
	PSHS	U
	LDU	#1
	OS9	$88
	PULS	U
	LBCS	FAIL
	LEAX	BUF,U
	LDY	#32
	LDA	PATH,U
	OS9	$8B
	LBCS	FAIL
	CMPY	#2
	LBNE	FAIL
	LEAX	BUF,U		; (written to the terminal: "B" and a newline)
	LDA	#1
	OS9	$8A
	LDA	PATH,U		; I$Seek to 6: SS.EOF and I$Read give E$EOF
	LDX	#0
	PSHS	U
	LDU	#6
	OS9	$88
	PULS	U
	LBCS	FAIL
	LDA	PATH,U
	LDB	#6
	OS9	$8D
	LBCC	FAIL
	CMPB	#$D3
	LBNE	FAIL
	LEAX	BUF,U
	LDY	#32
	LDA	PATH,U
	OS9	$89
	LBCC	FAIL
	CMPB	#$D3
	LBNE	FAIL
	LDA	PATH,U		; an unknown status code: E$UnkSvc
	LDB	#$7F
	OS9	$8D
	LBCC	FAIL
	CMPB	#$D0
	LBNE	FAIL
	LDA	PATH,U
	OS9	$8F
	LEAX	FNAME,PCR	; I$Open, update; SS.Size 2 with I$SetStt
	LDA	#3
	OS9	$84
	LBCS	FAIL
	STA	PATH,U
	LDB	#2
	LDX	#0
	PSHS	U
	LDU	#2
	OS9	$8E
	PULS	U
	LBCS	FAIL
	LEAX	BUF,U		; and reading it all gives 2
	LDY	#32
	LDA	PATH,U
	OS9	$89
	LBCS	FAIL
	CMPY	#2
	LBNE	FAIL
	LDA	PATH,U
	OS9	$8F
	LEAX	FNAME,PCR	; I$Delete; then I$Open fails with E$PNNF
	OS9	$87
	LBCS	FAIL
	LEAX	FNAME,PCR
	LDA	#1
	OS9	$84
	LBCC	FAIL
	CMPB	#$D8
	LBNE	FAIL
	LEAX	MSG,PCR
	LDY	#3
	LDA	#1
	OS9	$8C
	LDB	#42		; F$Exit with status 42
	OS9	$06
FAIL	LDB	#99
	OS9	$06
FNAME	FCC	"build/tests/os9files.tmp"
	FCB	13
DATA	FCC	"AB"
	FCB	13
	FCC	"CDE"
MSG	FCC	"OK"
	FCB	13
	EMOD
EOM	EQU	*
	END
