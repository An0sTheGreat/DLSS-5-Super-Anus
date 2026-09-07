option casemap:none

EXTERN embedded_draw_inline_settings:PROC
EXTERN embedded_on_init_device:PROC
EXTERN embedded_on_destroy_device:PROC
EXTERN embedded_on_init_command_list:PROC
EXTERN embedded_on_destroy_command_list:PROC
EXTERN embedded_on_destroy_resource:PROC
EXTERN DllMain:PROC
EXTERN auto_native_source:PROC

PUBLIC combined_entry
PUBLIC settings_bridge
PUBLIC init_device_bridge
PUBLIC destroy_device_bridge
PUBLIC init_command_list_bridge
PUBLIC destroy_command_list_bridge
PUBLIC destroy_resource_bridge
PUBLIC auto_native_source_bridge

.code

; Native SR descriptor store at 9C776. Preserve registers except R14B, the
; selected source. The original CMP/JE and frame gate run after this returns.
auto_native_source_bridge PROC FRAME
    sub rsp, 0C8h
    .allocstack 0C8h
    .endprolog
    mov [rsp+20h], rax
    mov [rsp+28h], rcx
    mov [rsp+30h], rdx
    mov [rsp+38h], r8
    mov [rsp+40h], r9
    mov [rsp+48h], r10
    mov [rsp+50h], r11
    movdqu [rsp+60h], xmm0
    movdqu [rsp+70h], xmm1
    movdqu [rsp+80h], xmm2
    movdqu [rsp+90h], xmm3
    movdqu [rsp+0A0h], xmm4
    movdqu [rsp+0B0h], xmm5
    movzx ecx, r14b
    mov rdx, r15
    call auto_native_source
    test al, al
    jz auto_source_original
    mov r14b, 2
auto_source_original:
    movdqu xmm0, [rsp+60h]
    movdqu xmm1, [rsp+70h]
    movdqu xmm2, [rsp+80h]
    movdqu xmm3, [rsp+90h]
    movdqu xmm4, [rsp+0A0h]
    movdqu xmm5, [rsp+0B0h]
    mov r11, [rsp+50h]
    mov r10, [rsp+48h]
    mov r9, [rsp+40h]
    mov r8, [rsp+38h]
    mov rdx, [rsp+30h]
    mov rcx, [rsp+28h]
    mov rax, [rsp+20h]
    movups [rbp+168h], xmm0
    add rsp, 0C8h
    ret
auto_native_source_bridge ENDP

; Runs the official RenoDX entry first on attach so its ReShade and ImGui state
; exists before the embedded component initializes. Detach is reversed.
combined_entry PROC
    push rbx
    push rsi
    push rdi
    sub rsp, 20h
    mov rbx, rcx
    mov esi, edx
    mov rdi, r8
    mov qword ptr [module_base], rbx
    test esi, esi
    jz detach_path

    mov rcx, rbx
    mov edx, esi
    mov r8, rdi
    lea rax, [rbx+1771B4h]
    call rax
    test eax, eax
    jz entry_done
    mov rcx, rbx
    mov edx, esi
    mov r8, rdi
    call DllMain
    jmp entry_done

detach_path:
    mov rcx, rbx
    mov edx, esi
    mov r8, rdi
    call DllMain
    mov rcx, rbx
    mov edx, esi
    mov r8, rdi
    lea rax, [rbx+1771B4h]
    call rax

entry_done:
    add rsp, 20h
    pop rdi
    pop rsi
    pop rbx
    ret
combined_entry ENDP

; After the previous section's Indent/TreePop, before the new section header.
; Displaced instruction: cmp qword ptr [r12+0B8h],10h. Its flags must survive.
settings_bridge PROC
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
    mov [rsp+0B0h], rcx
    mov rcx, r12
    call embedded_draw_inline_settings
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
    mov rcx, [rsp+0B0h]
    add rsp, 0B8h
    cmp qword ptr [r12+0B8h], 10h
    ret
settings_bridge ENDP

init_device_bridge PROC
    sub rsp, 28h
    mov [rsp+20h], rcx
    call embedded_on_init_device
    mov rcx, [rsp+20h]
    add rsp, 28h
    push rbp
    push r15
    push r14
    mov rax, 0AC0D5h
    add rax, qword ptr [module_base]
    jmp rax
init_device_bridge ENDP

destroy_device_bridge PROC
    sub rsp, 28h
    mov [rsp+20h], rcx
    call embedded_on_destroy_device
    mov rcx, [rsp+20h]
    add rsp, 28h
    push rbp
    push r15
    push r14
    mov rax, 0AC6B5h
    add rax, qword ptr [module_base]
    jmp rax
destroy_device_bridge ENDP

init_command_list_bridge PROC
    sub rsp, 28h
    mov [rsp+20h], rcx
    call embedded_on_init_command_list
    mov rcx, [rsp+20h]
    add rsp, 28h
    mov rax, 0CB750h
    add rax, qword ptr [module_base]
    jmp rax
init_command_list_bridge ENDP

destroy_command_list_bridge PROC
    sub rsp, 28h
    mov [rsp+20h], rcx
    call embedded_on_destroy_command_list
    mov rcx, [rsp+20h]
    add rsp, 28h
    push rsi
    push rdi
    push rbx
    sub rsp, 50h
    mov rax, 0AB897h
    add rax, qword ptr [module_base]
    jmp rax
destroy_command_list_bridge ENDP

destroy_resource_bridge PROC
    sub rsp, 38h
    mov [rsp+20h], rcx
    mov [rsp+28h], rdx
    call embedded_on_destroy_resource
    mov rdx, [rsp+28h]
    mov rcx, [rsp+20h]
    add rsp, 38h
    push rbp
    push r15
    push r14
    mov rax, 0AB9B5h
    add rax, qword ptr [module_base]
    jmp rax
destroy_resource_bridge ENDP

.data
ALIGN 8
module_base dq 0180000000h

END
