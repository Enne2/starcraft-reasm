%_cpu = type { [36 x i32] }

define fastcc void @fnA(%_cpu* %cpu) {
entry:
  musttail call fastcc void @fnB(%_cpu* %cpu)
  ret void
}

define fastcc void @fnB(%_cpu* %cpu) {
entry:
  musttail call fastcc void @asm_trampoline(%_cpu* %cpu)
  ret void
}

declare fastcc void @asm_trampoline(%_cpu*)

define void @fnA_cdecl(%_cpu* %cpu) {
entry:
  call fastcc void @fnA(%_cpu* %cpu)
  ret void
}

define fastcc void @fastcc_destination(%_cpu* %cpu) {
entry:
  %word = getelementptr %_cpu, %_cpu* %cpu, i32 0, i32 0, i32 0
  store i32 305419896, i32* %word, align 4
  ret void
}
