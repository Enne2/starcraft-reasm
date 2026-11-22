.text
.globl asm_trampoline
.type asm_trampoline,@function
asm_trampoline:
    pushl %ebp
    movl %esp, %ebp
    pushl %ecx                 # keep fastcc cpu pointer across cdecl callback
    pushl %ecx                 # cdecl arg: cpu
    call c_callback
    addl $4, %esp
    popl %ecx                  # restore fastcc arg register
    leave
    jmp *%eax                  # destination fastcc, caller's return address remains
.size asm_trampoline, .-asm_trampoline
.section .note.GNU-stack,"",@progbits
