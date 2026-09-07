option casemap:none

EXTERN draw_settings:PROC
EXTERN draw_osd:PROC
PUBLIC settings_hook
PUBLIC overlay_hook
PUBLIC apply_current_preset
PUBLIC module_base_slot

.code

; Replaces the seven-byte instruction `mov rcx,[r15+378h]` with a call.
; Preserve the live volatile state, draw the custom controls, then reproduce it.
settings_hook PROC
    sub rsp, 0B8h
    mov [rsp+20h], rax
    mov [rsp+28h], rdx
    mov [rsp+30h], r8
    mov [rsp+38h], r9
    mov [rsp+40h], r10
    mov [rsp+48h], r11
    movdqu [rsp+50h], xmm0
    movdqu [rsp+60h], xmm1
    movdqu [rsp+70h], xmm2
    movdqu [rsp+80h], xmm3
    movdqu [rsp+90h], xmm4
    movdqu [rsp+0A0h], xmm5
    mov rcx, r15
    call draw_settings
    movdqu xmm0, [rsp+50h]
    movdqu xmm1, [rsp+60h]
    movdqu xmm2, [rsp+70h]
    movdqu xmm3, [rsp+80h]
    movdqu xmm4, [rsp+90h]
    movdqu xmm5, [rsp+0A0h]
    mov r11, [rsp+48h]
    mov r10, [rsp+40h]
    mov r9, [rsp+38h]
    mov r8, [rsp+30h]
    mov rdx, [rsp+28h]
    mov rax, [rsp+20h]
    add rsp, 0B8h
    mov rcx, [r15+378h]
    ret
settings_hook ENDP

; The original registered callback address stays intact for CFG. Its first five
; bytes jump here; after drawing the OSD, reproduce the stolen pushes and resume.
overlay_hook PROC
    sub rsp, 68h
    mov [rsp+20h], rcx
    mov [rsp+28h], rdx
    mov [rsp+30h], r8
    mov [rsp+38h], r9
    movdqu [rsp+40h], xmm0
    movdqu [rsp+50h], xmm1
    call draw_osd
    movdqu xmm0, [rsp+40h]
    movdqu xmm1, [rsp+50h]
    mov r9, [rsp+38h]
    mov r8, [rsp+30h]
    mov rdx, [rsp+28h]
    mov rcx, [rsp+20h]
    add rsp, 68h
    push rbp
    push r15
    push r14
    mov rax, 0AEC65h
    add rax, qword ptr [module_base_slot]
    jmp rax
overlay_hook ENDP

; Reproduce the updated preset function prologue and enter immediately after
; its combo widget reported a change. This is the proven V4 apply mechanism.
apply_current_preset PROC
    mov rax, rcx
    mov qword ptr [module_base_slot], rcx
    push rbp
    push r15
    push r14
    push r13
    push r12
    push rsi
    push rdi
    push rbx
    sub rsp, 58h
    lea rbp, [rsp+50h]
    mov qword ptr [rbp], -2
    lea rsi, [rsp+20h]
    mov byte ptr [rsi], 0
    add rax, 0CC59Ah
    jmp rax
apply_current_preset ENDP

.data
ALIGN 8
module_base_slot dq 0

END
