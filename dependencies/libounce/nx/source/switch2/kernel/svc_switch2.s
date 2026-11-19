// SPDX-License-Identifier: ISC
// Switch 2 system calls. See include/switch2/kernel/svc.h.
// @author kurumuz

.macro SVC_BEGIN name
	.section .text.\name, "ax", %progbits
	.global \name
	.type \name, %function
	.align 2
	.cfi_startproc
\name:
.endm

.macro SVC_END
	.cfi_endproc
.endm

SVC_BEGIN svcUnknown80
	svc 0x80
	ret
SVC_END
