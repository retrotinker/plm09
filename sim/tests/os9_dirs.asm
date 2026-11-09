; SIMARGS: --load 0x2000 --data 0x8000
; sim6809 --os9: F$Mem (ask, grow, use the new memory, too much, shrink),
; and directories: I$MakDir, a file in it, I$ChgDir into it, the
; directory read (I$Open with DIR.) as RBF's 32-byte entries, whose names
; are printed, I$ChgDir back, and I$Delete of the file and the directory.
	PRAGMA	6809
	MOD	EOM,NAME,$11,$81,START,SIZE
PATH	RMB	1
SAVEU	RMB	2
ENT	RMB	32
	RMB	200
SIZE	EQU	.
NAME	FCS	/OS9DIRS/
	FCB	1
START	STU	SAVEU,U
	LEAX	FILE,PCR	; (what a failed run left goes)
	OS9	$87
	LEAX	DNAME,PCR
	OS9	$87
	LDD	#0		; F$Mem 0: the size, 1 page (D), its end (Y)
	OS9	$07
	LBCS	FAIL
	CMPD	#$100
	LBNE	FAIL
	CMPY	#$8100
	LBNE	FAIL
	LDD	#$1001		; grow to $1001: whole pages, $1100
	OS9	$07
	LBCS	FAIL
	CMPD	#$1100
	LBNE	FAIL
	CMPY	#$9100
	LBNE	FAIL
	LDA	#$5A		; the new memory can be written
	STA	$90FF
	LDD	#$7000		; up to $F000 is free; $FF00 is the I/O
	OS9	$07
	LBCS	FAIL
	LDD	#$7F01		; past it: E$MemFul
	OS9	$07
	LBCC	FAIL
	CMPB	#$CF
	LBNE	FAIL
	LDD	#$200		; shrink
	OS9	$07
	LBCS	FAIL
	CMPY	#$8200
	LBNE	FAIL
	LEAX	DNAME,PCR	; I$MakDir; again is E$CEF
	LDB	#$3F
	OS9	$85
	LBCS	FAIL
	LEAX	DNAME,PCR
	LDB	#$3F
	OS9	$85
	LBCC	FAIL
	CMPB	#$DA
	LBNE	FAIL
	LEAX	FILE,PCR	; a file in it
	LDA	#2
	OS9	$83
	LBCS	FAIL
	OS9	$8F
	LEAX	DNAME,PCR	; I$Open of a directory without DIR.: E$FNA
	LDA	#1
	OS9	$84
	LBCC	FAIL
	CMPB	#$D6
	LBNE	FAIL
	LEAX	DNAME,PCR	; I$ChgDir into it
	LDA	#3
	OS9	$86
	LBCS	FAIL
	LEAX	DOT,PCR		; ".", read as a directory
	LDA	#$81
	OS9	$84
	LBCS	FAIL
	STA	PATH,U
ENTRY	LEAX	ENT,U		; each entry: its name, printed
	LDY	#32
	LDA	PATH,U
	OS9	$89
	BCS	EOF
	CMPY	#32
	LBNE	FAIL
	LEAX	ENT,U
NAMEC	LDA	,X
	ANDA	#$7F
	PSHS	X
	STA	ENT+31,U	; (its LSN is no longer needed)
	LEAX	ENT+31,U
	LDY	#1
	LDA	#1
	OS9	$8A
	PULS	X
	TST	,X+
	BPL	NAMEC
	LEAX	CRMSG,PCR
	LDY	#1
	LDA	#1
	OS9	$8A
	BRA	ENTRY
EOF	CMPB	#$D3
	LBNE	FAIL
	LDA	PATH,U
	OS9	$8F
	LEAX	UP,PCR		; I$ChgDir back to where it started
	LDA	#3
	OS9	$86
	LBCS	FAIL
	LEAX	FILE,PCR	; I$Delete the file, and the directory
	OS9	$87
	LBCS	FAIL
	LEAX	DNAME,PCR
	OS9	$87
	LBCS	FAIL
	LEAX	DNAME,PCR	; gone: I$ChgDir fails
	LDA	#3
	OS9	$86
	LBCC	FAIL
	LEAX	MSG,PCR
	LDY	#3
	LDA	#1
	OS9	$8C
	LDB	#42
	OS9	$06
FAIL	LDB	#99
	OS9	$06
DNAME	FCC	"build/tests/os9dirs.tmp"
	FCB	13
FILE	FCC	"build/tests/os9dirs.tmp/afile"
	FCB	13
DOT	FCC	"."
	FCB	13
UP	FCC	"../../.."
	FCB	13
CRMSG	FCB	13
MSG	FCC	"OK"
	FCB	13
	EMOD
EOM	EQU	*
	END
