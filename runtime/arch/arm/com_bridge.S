/* 256 tiny, .text-resident guest-ABI entries, plus shared tail-dispatch gate. */
	.arm
	.syntax unified
	.text
	.global arm_com_bridge_common
	.type arm_com_bridge_common, %function
arm_com_bridge_common:
	push {r4, r5, r6, lr}
	mov r4, r0
	bl arm_com_bridge_common_c
	mov r1, r0                    @ guest return EIP
	bl reasm_indirect_resolve
	cmp r0, #0
	beq 1f
	mov r2, r0
	mov r0, r4
	pop {r4, r5, r6, lr}
	bx r2
1:
	mov r0, r4
	ldr r1, [r4, #136]
	bl reasm_indirect_unresolved
	b .
	.size arm_com_bridge_common, .-arm_com_bridge_common

	.altmacro
	.macro COM_THUNK n
	.global arm_com_thunk_\n
	.type arm_com_thunk_\n, %function
arm_com_thunk_\n:
	ldr r1, =\n
	b arm_com_bridge_common
	.size arm_com_thunk_\n, .-arm_com_thunk_\n
	.endm
	.macro COM_TABLE n
	.word arm_com_thunk_\n
	.endm
	.set i, 0
	.rept 256
	COM_THUNK %i
	.set i, i+1
	.endr
	.data
	.align 2
	.global arm_com_thunk_table
	.type arm_com_thunk_table, %object
arm_com_thunk_table:
	.set i, 0
	.rept 256
	COM_TABLE %i
	.set i, i+1
	.endr
	.size arm_com_thunk_table, .-arm_com_thunk_table
