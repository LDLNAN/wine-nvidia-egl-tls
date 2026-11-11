/* i386 ___tls_get_addr: index in EAX, result in EAX, other registers preserved.
 * No floating-point/vector instructions are used on the fast path.
 *
 * Only the NVIDIA EGL module is eligible. It is held with RTLD_NODELETE and an
 * open reference, so its module ID and initialized per-thread TLS block cannot
 * be recycled by dlclose. Never cache the DTV pointer itself: read the current
 * thread's DTV on every call. New/uninitialized slots always enter glibc.
 */
    .text
    .globl ___tls_get_addr
    .type ___tls_get_addr,@function
___tls_get_addr:
    .cfi_startproc
    endbr32
    pushl %ecx
    .cfi_adjust_cfa_offset 4
    .cfi_offset %ecx,-8
    pushl %edx
    .cfi_adjust_cfa_offset 4
    .cfi_offset %edx,-12
    call .Lget_pc_cx
    addl $_GLOBAL_OFFSET_TABLE_,%ecx
    movl (%eax),%edx
    testl %edx,%edx
    jz .Lfallback
    cmpl quake_tls_module@GOTOFF(%ecx),%edx
    jne .Lfallback
    movl %gs:4,%ecx
    testl %ecx,%ecx
    jz .Lfallback_reload
    /* glibc i386 dtv_t is 8 bytes; dtv[-1].counter is slot capacity. */
    cmpl %edx,-8(%ecx)
    jb .Lfallback_reload
    movl (%ecx,%edx,8),%ecx
    cmpl $-1,%ecx
    je .Lfallback_reload
    testl %ecx,%ecx
    jz .Lfallback_reload
    addl 4(%eax),%ecx
    movl %ecx,%eax
    call .Lget_pc_cx
    addl $_GLOBAL_OFFSET_TABLE_,%ecx
    cmpl $0, quake_tls_enable_count@GOTOFF(%ecx)
    jz .Lreturn
    lock addl $1, quake_tls_fastpath_count@GOTOFF(%ecx)
    jmp .Lreturn
.Lfallback_reload:
    call .Lget_pc_cx
    addl $_GLOBAL_OFFSET_TABLE_,%ecx
.Lfallback:
    /* The public cdecl wrapper in ld.so invokes its own IRELATIVE resolver.
     * It does not bind back to this interposed three-underscore symbol. */
    pushl %eax
    .cfi_adjust_cfa_offset 4
    call *__tls_get_addr@GOT(%ecx)
    addl $4,%esp
    .cfi_adjust_cfa_offset -4
    call .Lget_pc_cx
    addl $_GLOBAL_OFFSET_TABLE_,%ecx
    cmpl $0, quake_tls_enable_count@GOTOFF(%ecx)
    jz .Lreturn
    lock addl $1, quake_tls_fallback_count@GOTOFF(%ecx)
.Lreturn:
    popl %edx
    .cfi_adjust_cfa_offset -4
    .cfi_restore %edx
    popl %ecx
    .cfi_adjust_cfa_offset -4
    .cfi_restore %ecx
    ret
    .cfi_endproc
    .size ___tls_get_addr,.-___tls_get_addr

    .type .Lget_pc_cx,@function
.Lget_pc_cx:
    .cfi_startproc
    movl (%esp),%ecx
    ret
    .cfi_endproc

    .section .note.GNU-stack,"",@progbits
