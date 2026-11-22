/* 256 COM guest-ABI thunks. The C helper returns the guest continuation VA. */
	.text
	.globl arm_com_bridge_common
	.type arm_com_bridge_common,@function
arm_com_bridge_common:
	pushl %ebp
	movl %esp, %ebp
	pushl %ebx
	pushl %ecx
	pushl %eax
	movl %ecx, %ebx
	andl $-16, %esp
	subl $8, %esp
	pushl -12(%ebp)                 /* arg2: index */
	pushl -8(%ebp)                  /* arg1: cpu */
	call arm_com_bridge_common_c
	addl $16, %esp
	andl $-16, %esp
	subl $12, %esp
	pushl %eax
	call reasm_indirect_resolve
	addl $16, %esp
	testl %eax, %eax
	jz .Lcom_unresolved
	movl %eax, %edx
	movl -8(%ebp), %ecx
	movl -4(%ebp), %ebx
	movl %ebp, %esp
	popl %ebp
	jmp *%edx
.Lcom_unresolved:
	andl $-16, %esp
	subl $8, %esp
	pushl 136(%ebx)
	pushl %ebx
	call reasm_indirect_unresolved
	ud2
	.size arm_com_bridge_common,.-arm_com_bridge_common

	.altmacro
	.macro COM_THUNK n
	.globl arm_com_thunk_\n
	.type arm_com_thunk_\n,@function
arm_com_thunk_\n:
	movl $\n, %eax
	jmp arm_com_bridge_common
	.size arm_com_thunk_\n,.-arm_com_thunk_\n
	.endm
	.macro COM_TABLE n
	.long arm_com_thunk_\n
	.endm
	.set i,0
	.rept 256
	COM_THUNK %i
	.set i,i+1
	.endr
	.data
	.align 4
	.globl arm_com_thunk_table
	.type arm_com_thunk_table,@object
arm_com_thunk_table:
	.set i,0
	.rept 256
	COM_TABLE %i
	.set i,i+1
	.endr
	.size arm_com_thunk_table,.-arm_com_thunk_table
	.extern arm_com_bridge_common_c
	.extern reasm_indirect_resolve
	.extern reasm_indirect_unresolved
