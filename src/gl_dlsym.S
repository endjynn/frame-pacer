/* libc derives RTLD_NEXT's search origin from dlsym's return address.
 * Restore the original call frame and tail-jump, rather than calling libc
 * from the shim. This must also work without compiler tail-call optimization.
 */
.text
.globl dlsym
.type dlsym, @function
.hidden frame_pacer_dlsym_target
dlsym:
    .cfi_startproc
#if defined(__x86_64__)
    endbr64
    subq $24, %rsp
    .cfi_adjust_cfa_offset 24
    movq %rdi, 0(%rsp)
    movq %rsi, 8(%rsp)
    call frame_pacer_dlsym_target
    movq 0(%rsp), %rdi
    movq 8(%rsp), %rsi
    addq $24, %rsp
    .cfi_adjust_cfa_offset -24
    jmp *%rax
#elif defined(__i386__)
    endbr32
    subl $12, %esp
    .cfi_adjust_cfa_offset 12
    movl 16(%esp), %eax
    movl %eax, 0(%esp)
    movl 20(%esp), %eax
    movl %eax, 4(%esp)
    call frame_pacer_dlsym_target
    addl $12, %esp
    .cfi_adjust_cfa_offset -12
    jmp *%eax
#else
#error Unsupported dlsym trampoline architecture
#endif
    .cfi_endproc
.size dlsym, .-dlsym
.section .note.GNU-stack,"",@progbits
