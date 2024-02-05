; assemble with: nasm -f win64 simple_branch.asm -o simple_branch.o

global simple_branch

section .text

; rcx is count
; rdx is data pointer
simple_branch:
	xor rax, rax
.loop:
	mov r10, [rdx + rax]
	inc rax
	test r10, 1
	jnz .skip
	nop
.skip:
	cmp rax, rcx
	jb .loop
	ret

