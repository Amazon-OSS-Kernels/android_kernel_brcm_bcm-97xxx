#include <linux/linkage.h>
#include <asm/assembler.h>

.arm

/*
 * Register usage
 * r8  - temporary
 * r9  - the driver buffer
 * r10 - temporary
 * r11 - interrupts mask
 * r12 - base pointers
 * r13 - interrupts status
 */

	.text

	.global nmi_fiq_end

ENTRY(nmi_fiq_start)
	cpsid   if
	ldr r1, =nmi_tdrv_log
	mov pc, r1
	//Should never reach here
	b .
exit:
	subs	pc, lr, #4			@ return from FIQ
ENDPROC(nmi_fiq_start)
	.ltorg

nmi_fiq_end:
	b .

ENTRY(nmi_reg_save)
	push {r14}
	mrs r2, cpsr
	mrs r1, spsr
	msr cpsr_c, r1
	stmia r0!, {r1}
	stmia	r0!, {r4 - sl, fp, sp, lr}
	msr cpsr_c, r2
	pop {pc}
ENDPROC(nmi_reg_save)

ENTRY(nmi_lock_acquire)
lock_1:
	push {r14}
	ldrex r3, [r0]
	tst r3, #0x1
	bne lock_1
	movs r1, #0x1
	strex r2, r1, [r0]
	pop {pc}
ENDPROC(nmi_lock_acquire)

ENTRY(nmi_lock_release)
	push {r14}
	ldrex r3, [r0]
	tst r3, #0x1
	beq release_2
	movs r1, #0x0
	strex r2, r1, [r0]
release_2:
	pop {pc}
ENDPROC(nmi_lock_release)

