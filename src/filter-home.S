.intel_syntax noprefix
.text
.global home_blob, home_blob_end, home_l_threshold, home_l_n1x, home_l_n1y, home_l_n2x, home_l_n2y, home_r_threshold, home_r_n1x, home_r_n1y, home_r_n2x, home_r_n2y, home_config
.global home_entry0, home_entry1
.macro stick offset, p
    movzx edx, byte ptr [rbx+\offset]
    movzx edi, byte ptr [rbx+\offset+1]
    sub edx, 128
    sub edi, 128
    mov r8d, edx
    mov r9d, edi
    imul r8d, r8d
    imul r9d, r9d
    add r8d, r9d
    .byte 0x41,0x81,0xf8
\p\()_threshold:
    .long 16641
    jg 1f
    .byte 0x44,0x69,0xc2
\p\()_n1x:
    .long -512
    .byte 0x44,0x69,0xcf
\p\()_n1y:
    .long -887
    add r8d, r9d
    js 1f
    .byte 0x44,0x69,0xc2
\p\()_n2x:
    .long -512
    .byte 0x44,0x69,0xcf
\p\()_n2y:
    .long 887
    add r8d, r9d
    js 1f
    mov word ptr [rbx+\offset], 0x8080
1:
.endm
.macro entry name, mode
.p2align 5
\name:
    movabs rax, 0x1122334455667788
    mov r10d, \mode
    jmp common
.endm
 home_blob:
 entry home_entry0, 240
 entry home_entry1, 336
common:
    push rbx
    push r12
    push r13
    mov rbx, rsi
    mov r12d, edx
    mov r13d, r10d
    call rax
    test rbx, rbx
    jz done
    test r13d, 1
    jnz batch
    test eax, eax
    jnz done
    mov ecx, 1
    jmp filter
batch:
    test eax, eax
    jle done
    cmp eax, r12d
    jg done
    cmp eax, 64
    jg done
    mov ecx, eax
filter:
    shr r13d, 1
next:
    cmp byte ptr [rbx+76], 1
    jne advance
    stick 4, home_l
    stick 6, home_r
advance:
    add rbx, r13
    dec ecx
    jnz next
done:
    pop r13
    pop r12
    pop rbx
    ret
 home_config:
    .long 182,180,60,0,180,60
 home_blob_end:
