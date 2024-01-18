global nop_3x1_all_bytes
global nop_1x3_all_bytes
global nop_1x9_all_bytes

section .text

nop_3x1_all_bytes:
	xor rax, rax
.loop:
	db 0x0f, 0x1f, 0x00 ; the byte sequence for a 3-byte NOP
	inc rax
	cmp rax, rcx
	jb .loop
	ret

nop_1x3_all_bytes:
	xor rax, rax
.loop:
	nop
	nop
	nop
	inc rax
	cmp rax, rcx
	jb .loop
	ret

nop_1x9_all_bytes:
	xor rax, rax
.loop:
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	nop
	inc rax
	cmp rax, rcx
	jb .loop
	ret

