; assemble with: nasm -f win64 movs_per_iteration.asm -o movs_per_iteration.o

global load1
global load2
global load3
global load4

global store1
global store2
global store3
global store4

section .text

; rcx is iterations
; rdx is pointer to value
load1:
	align 64
.loop:
	mov rax, [rdx]
	sub rcx, 1
	jnle .loop
	ret

load2:
	align 64
.loop:
	mov rax, [rdx]
	mov rax, [rdx]
	sub rcx, 2
	jnle .loop
	ret

load3:
	align 64
.loop:
	mov rax, [rdx]
	mov rax, [rdx]
	mov rax, [rdx]
	sub rcx, 3
	jnle .loop
	ret

load4:
	align 64
.loop:
	mov rax, [rdx]
	mov rax, [rdx]
	mov rax, [rdx]
	mov rax, [rdx]
	sub rcx, 4
	jnle .loop
	ret

; rcx is iterations
; rdx is pointer to value
store1:
	align 64
.loop:
	mov [rdx], rax
	sub rcx, 1
	jnle .loop
	ret

store2:
	align 64
.loop:
	mov [rdx], rax
	mov [rdx], rax
	sub rcx, 2
	jnle .loop
	ret

store3:
	align 64
.loop:
	mov [rdx], rax
	mov [rdx], rax
	mov [rdx], rax
	sub rcx, 3
	jnle .loop
	ret

store4:
	align 64
.loop:
	mov [rdx], rax
	mov [rdx], rax
	mov [rdx], rax
	mov [rdx], rax
	sub rcx, 4
	jnle .loop
	ret

