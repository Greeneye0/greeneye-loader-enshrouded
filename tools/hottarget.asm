; Hot reload test targets in the exe's .text (tools\hottest.cpp): Target(x) = x + 7, with 13
; bytes of nops in front so a 13-byte hook fits; the bytes after ret mark them (GL1 / GL2).
; HotCaller(x) calls HotTarget3 (x + 7) through one "call rel32" (GL4). HotSpin(flag) spins while
; *flag is set, inside the 13 bytes a hook overwrites, then returns 9 (GL3). HotTarget4 = x + 7 for
; the second test mod only (GL5).
_TEXT SEGMENT
PUBLIC HotTarget1
PUBLIC HotTarget2
PUBLIC HotCaller
PUBLIC HotSpin
PUBLIC HotTarget4
ALIGN 16
HotTarget1:
    db 066h, 00Fh, 01Fh, 084h, 000h, 000h, 000h, 000h, 000h, 00Fh, 01Fh, 040h, 000h
    lea eax, [rcx + 7]
    ret
    db 0CCh, 'G', 'L', '1'
ALIGN 16
HotTarget2:
    db 066h, 00Fh, 01Fh, 084h, 000h, 000h, 000h, 000h, 000h, 00Fh, 01Fh, 040h, 000h
    lea eax, [rcx + 7]
    ret
    db 0CCh, 'G', 'L', '2'
ALIGN 16
HotTarget3:
    lea eax, [rcx + 7]
    ret
ALIGN 16
HotCaller:
    sub rsp, 28h
    call HotTarget3
    add rsp, 28h
    ret
    db 0CCh, 'G', 'L', '4'
ALIGN 16
HotSpin:
    db 08Bh, 001h, 085h, 0C0h, 075h, 0FAh                    ; spin: mov eax,[rcx]; test eax,eax; jnz spin
    db 00Fh, 01Fh, 080h, 000h, 000h, 000h, 000h              ; 7-byte nop
    mov eax, 9
    ret
    db 0CCh, 'G', 'L', '3'
ALIGN 16
HotTarget4:                                               ; hooked only by the second test mod
    db 066h, 00Fh, 01Fh, 084h, 000h, 000h, 000h, 000h, 000h, 00Fh, 01Fh, 040h, 000h
    lea eax, [rcx + 7]
    ret
    db 0CCh, 'G', 'L', '5'
_TEXT ENDS
END
