	.file	"main.c"
	.text
	.section .rdata,"dr"
.LC0:
	.ascii "rb\0"
.LC1:
	.ascii "cannot open '%s'\0"
	.text
	.def	read_file_or_die;	.scl	3;	.type	32;	.endef
	.seh_proc	read_file_or_die
read_file_or_die:
	pushq	%rbp
	.seh_pushreg	%rbp
	movq	%rsp, %rbp
	.seh_setframe	%rbp, 0
	subq	$64, %rsp
	.seh_stackalloc	64
	.seh_endprologue
	movq	%rcx, 16(%rbp)
	leaq	.LC0(%rip), %rdx
	movq	16(%rbp), %rax
	movq	%rax, %rcx
	call	fopen
	movq	%rax, -8(%rbp)
	cmpq	$0, -8(%rbp)
	jne	.L2
	movq	16(%rbp), %rax
	leaq	.LC1(%rip), %rcx
	movq	%rax, %rdx
	call	fatal
.L2:
	movq	-8(%rbp), %rax
	movl	$2, %r8d
	movl	$0, %edx
	movq	%rax, %rcx
	call	fseek
	movq	-8(%rbp), %rax
	movq	%rax, %rcx
	call	ftell
	movl	%eax, -12(%rbp)
	movq	-8(%rbp), %rax
	movl	$0, %r8d
	movl	$0, %edx
	movq	%rax, %rcx
	call	fseek
	movl	-12(%rbp), %eax
	cltq
	addq	$1, %rax
	movq	%rax, %rcx
	call	xmalloc
	movq	%rax, -24(%rbp)
	movl	-12(%rbp), %eax
	movslq	%eax, %rdx
	movq	-8(%rbp), %rcx
	movq	-24(%rbp), %rax
	movq	%rcx, %r9
	movq	%rdx, %r8
	movl	$1, %edx
	movq	%rax, %rcx
	call	fread
	movq	%rax, -32(%rbp)
	movq	-24(%rbp), %rdx
	movq	-32(%rbp), %rax
	addq	%rdx, %rax
	movb	$0, (%rax)
	movq	-8(%rbp), %rax
	movq	%rax, %rcx
	call	fclose
	movq	-24(%rbp), %rax
	addq	$64, %rsp
	popq	%rbp
	ret
	.seh_endproc
	.section .rdata,"dr"
.LC2:
	.ascii "src/hphpc/runtime\0"
.LC3:
	.ascii "../src/hphpc/runtime\0"
.LC4:
	.ascii "src\\hphpc\\runtime\0"
.LC5:
	.ascii "%s/hphp_rt.c\0"
.LC6:
	.ascii ".\0"
	.text
	.def	runtime_dir;	.scl	3;	.type	32;	.endef
	.seh_proc	runtime_dir
runtime_dir:
	pushq	%rbp
	.seh_pushreg	%rbp
	movq	%rsp, %rbp
	.seh_setframe	%rbp, 0
	subq	$96, %rsp
	.seh_stackalloc	96
	.seh_endprologue
	movq	dir.0(%rip), %rax
	testq	%rax, %rax
	je	.L5
	movq	dir.0(%rip), %rax
	jmp	.L10
.L5:
	leaq	.LC2(%rip), %rax
	movq	%rax, -64(%rbp)
	leaq	.LC3(%rip), %rax
	movq	%rax, -56(%rbp)
	leaq	.LC4(%rip), %rax
	movq	%rax, -48(%rbp)
	movq	$0, -40(%rbp)
	movl	$0, -4(%rbp)
	jmp	.L7
.L9:
	movl	-4(%rbp), %eax
	cltq
	movq	-64(%rbp,%rax,8), %rax
	leaq	.LC5(%rip), %rcx
	movq	%rax, %rdx
	call	fmt
	movq	%rax, -16(%rbp)
	leaq	.LC0(%rip), %rdx
	movq	-16(%rbp), %rax
	movq	%rax, %rcx
	call	fopen
	movq	%rax, -24(%rbp)
	movq	-16(%rbp), %rax
	movq	%rax, %rcx
	call	free
	cmpq	$0, -24(%rbp)
	je	.L8
	movq	-24(%rbp), %rax
	movq	%rax, %rcx
	call	fclose
	movl	-4(%rbp), %eax
	cltq
	movq	-64(%rbp,%rax,8), %rax
	movq	%rax, %rcx
	call	xstrdup
	movq	%rax, dir.0(%rip)
	movq	dir.0(%rip), %rax
	jmp	.L10
.L8:
	addl	$1, -4(%rbp)
.L7:
	movl	-4(%rbp), %eax
	cltq
	movq	-64(%rbp,%rax,8), %rax
	testq	%rax, %rax
	jne	.L9
	leaq	.LC6(%rip), %rax
	movq	%rax, %rcx
	call	xstrdup
	movq	%rax, dir.0(%rip)
	movq	dir.0(%rip), %rax
.L10:
	addq	$96, %rsp
	popq	%rbp
	ret
	.seh_endproc
	.def	load_program;	.scl	3;	.type	32;	.endef
	.seh_proc	load_program
load_program:
	pushq	%rbp
	.seh_pushreg	%rbp
	movq	%rsp, %rbp
	.seh_setframe	%rbp, 0
	subq	$48, %rsp
	.seh_stackalloc	48
	.seh_endprologue
	movq	%rcx, 16(%rbp)
	movq	%rdx, 24(%rbp)
	movq	24(%rbp), %rdx
	movq	16(%rbp), %rax
	movq	%rax, %rcx
	call	parse_program
	movq	%rax, -8(%rbp)
	movq	-8(%rbp), %rax
	addq	$48, %rsp
	popq	%rbp
	ret
	.seh_endproc
	.section .rdata,"dr"
.LC7:
	.ascii "src/hphpc/std\0"
.LC8:
	.ascii "%s/%s\0"
	.text
	.def	load_file_recursive;	.scl	3;	.type	32;	.endef
	.seh_proc	load_file_recursive
load_file_recursive:
	pushq	%rbp
	.seh_pushreg	%rbp
	movq	%rsp, %rbp
	.seh_setframe	%rbp, 0
	subq	$144, %rsp
	.seh_stackalloc	144
	.seh_endprologue
	movq	%rcx, 16(%rbp)
	movq	%rdx, 24(%rbp)
	movq	24(%rbp), %rax
	movq	%rax, %rcx
	call	intern
	movq	%rax, -24(%rbp)
	movq	$0, -8(%rbp)
	jmp	.L14
.L17:
	movq	16(%rbp), %rax
	movq	24(%rax), %rax
	movq	-8(%rbp), %rdx
	salq	$3, %rdx
	addq	%rdx, %rax
	movq	(%rax), %rax
	cmpq	%rax, -24(%rbp)
	jne	.L15
	movl	$0, %eax
	jmp	.L16
.L15:
	addq	$1, -8(%rbp)
.L14:
	movq	16(%rbp), %rax
	movq	32(%rax), %rax
	cmpq	%rax, -8(%rbp)
	jb	.L17
	movq	24(%rbp), %rax
	movq	%rax, %rcx
	call	read_file_or_die
	movq	%rax, -32(%rbp)
	movq	24(%rbp), %rdx
	movq	-32(%rbp), %rax
	movq	%rax, %rcx
	call	parse_program
	movq	%rax, -40(%rbp)
	movq	16(%rbp), %rax
	leaq	24(%rax), %rcx
	movq	-24(%rbp), %rax
	movq	%rax, %rdx
	call	ptrvec_push
	movq	$0, -16(%rbp)
	jmp	.L18
.L20:
	movq	-40(%rbp), %rax
	movq	(%rax), %rax
	movq	-16(%rbp), %rdx
	salq	$3, %rdx
	addq	%rdx, %rax
	movq	(%rax), %rax
	movq	%rax, -48(%rbp)
	movq	-48(%rbp), %rax
	movq	(%rax), %rcx
	leaq	.LC7(%rip), %rdx
	leaq	.LC8(%rip), %rax
	movq	%rcx, %r8
	movq	%rax, %rcx
	call	fmt
	movq	%rax, -56(%rbp)
	movq	-56(%rbp), %rdx
	movq	16(%rbp), %rax
	movq	%rax, %rcx
	call	load_file_recursive
	movq	%rax, -64(%rbp)
	cmpq	$0, -64(%rbp)
	jne	.L19
	movq	24(%rbp), %rax
	movl	$47, %edx
	movq	%rax, %rcx
	call	strrchr
	movq	%rax, -72(%rbp)
	movq	24(%rbp), %rax
	movl	$92, %edx
	movq	%rax, %rcx
	call	strrchr
	movq	%rax, -80(%rbp)
	movq	-80(%rbp), %rdx
	movq	-72(%rbp), %rax
	cmpq	%rax, %rdx
	cmovnb	%rdx, %rax
	movq	%rax, -88(%rbp)
	cmpq	$0, -88(%rbp)
	je	.L19
	movq	-88(%rbp), %rax
	subq	24(%rbp), %rax
	movq	%rax, %rdx
	movq	24(%rbp), %rax
	movq	%rax, %rcx
	call	xstrndup
	movq	%rax, -96(%rbp)
	movq	-48(%rbp), %rax
	movq	(%rax), %rdx
	movq	-96(%rbp), %rax
	leaq	.LC8(%rip), %rcx
	movq	%rdx, %r8
	movq	%rax, %rdx
	call	fmt
	movq	%rax, -104(%rbp)
	movq	-104(%rbp), %rdx
	movq	16(%rbp), %rax
	movq	%rax, %rcx
	call	load_file_recursive
	movq	%rax, -64(%rbp)
.L19:
	addq	$1, -16(%rbp)
.L18:
	movq	-40(%rbp), %rax
	movq	8(%rax), %rax
	cmpq	%rax, -16(%rbp)
	jb	.L20
	movq	16(%rbp), %rax
	movq	-40(%rbp), %rdx
	movq	%rax, %rcx
	call	ptrvec_push
	movq	-40(%rbp), %rax
.L16:
	addq	$144, %rsp
	popq	%rbp
	ret
	.seh_endproc
	.def	merge_programs;	.scl	3;	.type	32;	.endef
	.seh_proc	merge_programs
merge_programs:
	pushq	%rbp
	.seh_pushreg	%rbp
	pushq	%rbx
	.seh_pushreg	%rbx
	subq	$72, %rsp
	.seh_stackalloc	72
	leaq	64(%rsp), %rbp
	.seh_setframe	%rbp, 64
	.seh_endprologue
	movq	%rcx, 32(%rbp)
	movq	%rdx, %rbx
	movq	$0, -8(%rbp)
	jmp	.L22
.L25:
	movq	(%rbx), %rax
	movq	-8(%rbp), %rdx
	salq	$3, %rdx
	addq	%rdx, %rax
	movq	(%rax), %rax
	movq	%rax, -24(%rbp)
	movq	$0, -16(%rbp)
	jmp	.L23
.L24:
	movq	-24(%rbp), %rax
	movq	24(%rax), %rax
	movq	-16(%rbp), %rdx
	salq	$3, %rdx
	addq	%rdx, %rax
	movq	(%rax), %rax
	movq	32(%rbp), %rdx
	leaq	24(%rdx), %rcx
	movq	%rax, %rdx
	call	ptrvec_push
	addq	$1, -16(%rbp)
.L23:
	movq	-24(%rbp), %rax
	movq	32(%rax), %rax
	cmpq	%rax, -16(%rbp)
	jb	.L24
	addq	$1, -8(%rbp)
.L22:
	movq	8(%rbx), %rax
	cmpq	%rax, -8(%rbp)
	jb	.L25
	nop
	nop
	addq	$72, %rsp
	popq	%rbx
	popq	%rbp
	ret
	.seh_endproc
.lcomm needs_std_builtins,4,4
	.section .rdata,"dr"
	.align 8
.LC9:
	.ascii "HolyPHP compiler (hphpc) \342\200\224 compile .hphp to native code via C\12usage: hphpc <command> <file.hphp> [args...]\12  run     compile and execute (default)\12  build   compile to native executable (-o to set output)\12  emit    print generated C code\12  check   type- and borrow-check only\12\0"
.LC10:
	.ascii "-o\0"
.LC11:
	.ascii "hphpc: no input file\12\0"
.LC12:
	.ascii "emit\0"
.LC13:
	.ascii "check\0"
.LC14:
	.ascii "no programs loaded\0"
.LC15:
	.ascii "hphpc: compilation failed\12\0"
	.align 8
.LC16:
	.ascii "%s: OK (types and borrows check)\12\0"
.LC17:
	.ascii "hphp_out\0"
.LC18:
	.ascii ".exe\0"
.LC19:
	.ascii ".out\0"
.LC20:
	.ascii "%s.gen.c\0"
.LC21:
	.ascii "%s.c\0"
.LC22:
	.ascii "%s.exe\0"
.LC23:
	.ascii "wb\0"
.LC24:
	.ascii "cannot write %s\0"
.LC25:
	.ascii "%s/hphp_rt.h\0"
	.align 8
.LC26:
	.ascii "cc -O2 -std=c11 -I \"%s\" \"%s\" \"%s/hphp_rt.c\" \"%s/hphp_std.c\" -o \"%s\" -lm\0"
.LC27:
	.ascii "hphpc: C compiler failed\12\0"
.LC28:
	.ascii "build\0"
.LC29:
	.ascii "hphpc: built %s\12\0"
.LC30:
	.ascii "\"%s\"\0"
	.text
	.globl	main
	.def	main;	.scl	2;	.type	32;	.endef
	.seh_proc	main
main:
	pushq	%rbp
	.seh_pushreg	%rbp
	subq	$304, %rsp
	.seh_stackalloc	304
	leaq	128(%rsp), %rbp
	.seh_setframe	%rbp, 128
	.seh_endprologue
	movl	%ecx, 192(%rbp)
	movq	%rdx, 200(%rbp)
	call	__main
	cmpl	$1, 192(%rbp)
	jg	.L27
	movl	$2, %ecx
	movq	__imp___acrt_iob_func(%rip), %rax
	call	*%rax
	movq	%rax, %rdx
	leaq	.LC9(%rip), %rax
	movq	%rdx, %r9
	movl	$278, %r8d
	movl	$1, %edx
	movq	%rax, %rcx
	call	fwrite
	movl	$1, %eax
	jmp	.L46
.L27:
	movq	200(%rbp), %rax
	movq	8(%rax), %rax
	movq	%rax, 120(%rbp)
	movq	$0, 168(%rbp)
	movq	$0, 160(%rbp)
	movl	$2, 156(%rbp)
	jmp	.L29
.L32:
	movl	156(%rbp), %eax
	cltq
	leaq	0(,%rax,8), %rdx
	movq	200(%rbp), %rax
	addq	%rdx, %rax
	movq	(%rax), %rax
	leaq	.LC10(%rip), %rdx
	movq	%rax, %rcx
	call	strcmp
	testl	%eax, %eax
	jne	.L30
	movl	156(%rbp), %eax
	addl	$1, %eax
	cmpl	%eax, 192(%rbp)
	jle	.L30
	addl	$1, 156(%rbp)
	movl	156(%rbp), %eax
	cltq
	leaq	0(,%rax,8), %rdx
	movq	200(%rbp), %rax
	addq	%rdx, %rax
	movq	(%rax), %rax
	movq	%rax, 160(%rbp)
	jmp	.L31
.L30:
	cmpq	$0, 168(%rbp)
	jne	.L31
	movl	156(%rbp), %eax
	cltq
	leaq	0(,%rax,8), %rdx
	movq	200(%rbp), %rax
	addq	%rdx, %rax
	movq	(%rax), %rax
	movq	%rax, 168(%rbp)
.L31:
	addl	$1, 156(%rbp)
.L29:
	movl	156(%rbp), %eax
	cmpl	192(%rbp), %eax
	jl	.L32
	cmpq	$0, 168(%rbp)
	jne	.L33
	movl	$2, %ecx
	movq	__imp___acrt_iob_func(%rip), %rax
	call	*%rax
	movq	%rax, %rdx
	leaq	.LC11(%rip), %rax
	movq	%rdx, %r9
	movl	$21, %r8d
	movl	$1, %edx
	movq	%rax, %rcx
	call	fwrite
	movl	$1, %eax
	jmp	.L46
.L33:
	leaq	.LC12(%rip), %rdx
	movq	120(%rbp), %rax
	movq	%rax, %rcx
	call	strcmp
	testl	%eax, %eax
	sete	%al
	movb	%al, 119(%rbp)
	leaq	.LC13(%rip), %rdx
	movq	120(%rbp), %rax
	movq	%rax, %rcx
	call	strcmp
	testl	%eax, %eax
	sete	%al
	movb	%al, 118(%rbp)
	pxor	%xmm0, %xmm0
	movups	%xmm0, -16(%rbp)
	movups	%xmm0, 0(%rbp)
	movups	%xmm0, 16(%rbp)
	movq	168(%rbp), %rdx
	leaq	-16(%rbp), %rax
	movq	%rax, %rcx
	call	load_file_recursive
	movq	-8(%rbp), %rax
	testq	%rax, %rax
	jne	.L34
	leaq	.LC14(%rip), %rax
	movq	%rax, %rcx
	call	fatal
.L34:
	movl	$80, %edx
	movl	$1, %ecx
	call	xcalloc
	movq	%rax, 104(%rbp)
	movq	168(%rbp), %rax
	movq	%rax, %rcx
	call	intern
	movq	104(%rbp), %rdx
	movq	%rax, 72(%rdx)
	movq	-16(%rbp), %rax
	movq	-8(%rbp), %rdx
	movq	%rax, -80(%rbp)
	movq	%rdx, -72(%rbp)
	movq	0(%rbp), %rax
	movq	%rax, -64(%rbp)
	leaq	-80(%rbp), %rdx
	movq	104(%rbp), %rax
	movq	%rax, %rcx
	call	merge_programs
	movq	-16(%rbp), %rax
	movq	-8(%rbp), %rdx
	salq	$3, %rdx
	subq	$8, %rdx
	addq	%rdx, %rax
	movq	(%rax), %rax
	movq	%rax, 96(%rbp)
	movq	104(%rbp), %rcx
	movq	96(%rbp), %r8
	movq	48(%r8), %rax
	movq	56(%r8), %rdx
	movq	%rax, 48(%rcx)
	movq	%rdx, 56(%rcx)
	movq	64(%r8), %rax
	movq	%rax, 64(%rcx)
	call	builtins_init
	movq	104(%rbp), %rax
	movq	%rax, %rcx
	call	sema_run
	call	sema_result_ok
	xorl	$1, %eax
	testb	%al, %al
	je	.L35
	movl	$2, %ecx
	movq	__imp___acrt_iob_func(%rip), %rax
	call	*%rax
	movq	%rax, %rdx
	leaq	.LC15(%rip), %rax
	movq	%rdx, %r9
	movl	$26, %r8d
	movl	$1, %edx
	movq	%rax, %rcx
	call	fwrite
	movl	$1, %eax
	jmp	.L46
.L35:
	cmpb	$0, 118(%rbp)
	je	.L36
	movl	$2, %ecx
	movq	__imp___acrt_iob_func(%rip), %rax
	call	*%rax
	movq	%rax, %rcx
	movq	168(%rbp), %rdx
	leaq	.LC16(%rip), %rax
	movq	%rdx, %r8
	movq	%rax, %rdx
	call	fprintf
	movl	$0, %eax
	jmp	.L46
.L36:
	leaq	-48(%rbp), %rax
	movq	%rax, %rcx
	call	buf_init
	leaq	-48(%rbp), %rdx
	movq	104(%rbp), %rax
	movq	%rax, %rcx
	call	codegen_emit
	cmpb	$0, 119(%rbp)
	je	.L37
	movl	$1, %ecx
	movq	__imp___acrt_iob_func(%rip), %rax
	call	*%rax
	movq	%rax, %rdx
	movq	-40(%rbp), %rcx
	movq	-48(%rbp), %rax
	movq	%rdx, %r9
	movq	%rcx, %r8
	movl	$1, %edx
	movq	%rax, %rcx
	call	fwrite
	movl	$0, %eax
	jmp	.L46
.L37:
	cmpq	$0, 160(%rbp)
	setne	%al
	movzbl	%al, %eax
	andl	$1, %eax
	testb	%al, %al
	je	.L38
	movq	160(%rbp), %rax
	movq	%rax, 144(%rbp)
	jmp	.L39
.L38:
	leaq	.LC17(%rip), %rax
	movq	%rax, 144(%rbp)
.L39:
	movq	144(%rbp), %rax
	movl	$46, %edx
	movq	%rax, %rcx
	call	strrchr
	movq	%rax, 88(%rbp)
	cmpq	$0, 88(%rbp)
	je	.L40
	leaq	.LC18(%rip), %rdx
	movq	88(%rbp), %rax
	movq	%rax, %rcx
	call	strcmp
	testl	%eax, %eax
	je	.L41
	leaq	.LC19(%rip), %rdx
	movq	88(%rbp), %rax
	movq	%rax, %rcx
	call	strcmp
	testl	%eax, %eax
	jne	.L40
.L41:
	movq	144(%rbp), %rax
	leaq	.LC20(%rip), %rcx
	movq	%rax, %rdx
	call	fmt
	movq	%rax, 136(%rbp)
	movq	144(%rbp), %rax
	movq	%rax, %rcx
	call	xstrdup
	movq	%rax, 128(%rbp)
	jmp	.L42
.L40:
	movq	144(%rbp), %rax
	leaq	.LC21(%rip), %rcx
	movq	%rax, %rdx
	call	fmt
	movq	%rax, 136(%rbp)
	movq	144(%rbp), %rax
	leaq	.LC22(%rip), %rcx
	movq	%rax, %rdx
	call	fmt
	movq	%rax, 128(%rbp)
.L42:
	leaq	.LC23(%rip), %rdx
	movq	136(%rbp), %rax
	movq	%rax, %rcx
	call	fopen
	movq	%rax, 80(%rbp)
	cmpq	$0, 80(%rbp)
	jne	.L43
	movq	136(%rbp), %rax
	leaq	.LC24(%rip), %rcx
	movq	%rax, %rdx
	call	fatal
.L43:
	movq	-40(%rbp), %rdx
	movq	-48(%rbp), %rax
	movq	80(%rbp), %rcx
	movq	%rcx, %r9
	movq	%rdx, %r8
	movl	$1, %edx
	movq	%rax, %rcx
	call	fwrite
	movq	80(%rbp), %rax
	movq	%rax, %rcx
	call	fclose
	call	runtime_dir
	movq	%rax, 72(%rbp)
	movq	72(%rbp), %rax
	leaq	.LC25(%rip), %rcx
	movq	%rax, %rdx
	call	fmt
	movq	%rax, 64(%rbp)
	movq	72(%rbp), %r9
	movq	136(%rbp), %r8
	movq	72(%rbp), %rax
	leaq	.LC26(%rip), %rcx
	movq	128(%rbp), %rdx
	movq	%rdx, 40(%rsp)
	movq	72(%rbp), %rdx
	movq	%rdx, 32(%rsp)
	movq	%rax, %rdx
	call	fmt
	movq	%rax, 56(%rbp)
	movq	56(%rbp), %rax
	movq	%rax, %rcx
	call	system
	movl	%eax, 52(%rbp)
	cmpl	$0, 52(%rbp)
	je	.L44
	movl	$2, %ecx
	movq	__imp___acrt_iob_func(%rip), %rax
	call	*%rax
	movq	%rax, %rdx
	leaq	.LC27(%rip), %rax
	movq	%rdx, %r9
	movl	$25, %r8d
	movl	$1, %edx
	movq	%rax, %rcx
	call	fwrite
	movl	$1, %eax
	jmp	.L46
.L44:
	leaq	.LC28(%rip), %rdx
	movq	120(%rbp), %rax
	movq	%rax, %rcx
	call	strcmp
	testl	%eax, %eax
	jne	.L45
	movl	$2, %ecx
	movq	__imp___acrt_iob_func(%rip), %rax
	call	*%rax
	movq	%rax, %rcx
	movq	128(%rbp), %rdx
	leaq	.LC29(%rip), %rax
	movq	%rdx, %r8
	movq	%rax, %rdx
	call	fprintf
	movl	$0, %eax
	jmp	.L46
.L45:
	movq	128(%rbp), %rax
	leaq	.LC30(%rip), %rcx
	movq	%rax, %rdx
	call	fmt
	movq	%rax, 40(%rbp)
	movq	40(%rbp), %rax
	movq	%rax, %rcx
	call	system
.L46:
	addq	$304, %rsp
	popq	%rbp
	ret
	.seh_endproc
.lcomm dir.0,8,8
	.def	__main;	.scl	2;	.type	32;	.endef
	.ident	"GCC: (Rev5, Built by MSYS2 project) 16.1.0"
	.def	fopen;	.scl	2;	.type	32;	.endef
	.def	fatal;	.scl	2;	.type	32;	.endef
	.def	fseek;	.scl	2;	.type	32;	.endef
	.def	ftell;	.scl	2;	.type	32;	.endef
	.def	xmalloc;	.scl	2;	.type	32;	.endef
	.def	fread;	.scl	2;	.type	32;	.endef
	.def	fclose;	.scl	2;	.type	32;	.endef
	.def	fmt;	.scl	2;	.type	32;	.endef
	.def	free;	.scl	2;	.type	32;	.endef
	.def	xstrdup;	.scl	2;	.type	32;	.endef
	.def	parse_program;	.scl	2;	.type	32;	.endef
	.def	intern;	.scl	2;	.type	32;	.endef
	.def	ptrvec_push;	.scl	2;	.type	32;	.endef
	.def	strrchr;	.scl	2;	.type	32;	.endef
	.def	xstrndup;	.scl	2;	.type	32;	.endef
	.def	fwrite;	.scl	2;	.type	32;	.endef
	.def	strcmp;	.scl	2;	.type	32;	.endef
	.def	xcalloc;	.scl	2;	.type	32;	.endef
	.def	builtins_init;	.scl	2;	.type	32;	.endef
	.def	sema_run;	.scl	2;	.type	32;	.endef
	.def	sema_result_ok;	.scl	2;	.type	32;	.endef
	.def	fprintf;	.scl	2;	.type	32;	.endef
	.def	buf_init;	.scl	2;	.type	32;	.endef
	.def	codegen_emit;	.scl	2;	.type	32;	.endef
	.def	fwrite;	.scl	2;	.type	32;	.endef
	.def	system;	.scl	2;	.type	32;	.endef
