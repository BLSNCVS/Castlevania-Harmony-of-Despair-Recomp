# Synthetic PPC code exercising the setjmp/longjmp scanner.
# Regenerate jmp_scan_fixture.bin with tools/hodtool/tests/assemble_fixture.sh.
# Layout is loaded at guest address 0x82000000 by the test; the expected
# entry offsets are asserted in jmp_scan_test.cpp.
    .text
    .globl _start
_start:
# 0x000: ordinary leaf function
filler:
    addi r3, r3, 1
    addi r4, r4, 2
    blr
# __savegprlr_14 style helper: r1-based, must NOT be reported
savegprlr_14:
    std r14, -152(r1)
    std r15, -144(r1)
    std r16, -136(r1)
    std r17, -128(r1)
    std r18, -120(r1)
    std r19, -112(r1)
    std r20, -104(r1)
    std r21, -96(r1)
    std r22, -88(r1)
    std r23, -80(r1)
    std r24, -72(r1)
    std r25, -64(r1)
    std r26, -56(r1)
    std r27, -48(r1)
    std r28, -40(r1)
    std r29, -32(r1)
    std r30, -24(r1)
    std r31, -16(r1)
    stw r12, -8(r1)
    blr
# real setjmp: r3 = jmp_buf
setjmp:
    mflr r0
    std r13, 0x00(r3)
    std r1, 0x08(r3)
    std r0, 0x10(r3)
    std r14, 0x18(r3)
    std r15, 0x20(r3)
    std r16, 0x28(r3)
    std r17, 0x30(r3)
    std r18, 0x38(r3)
    std r19, 0x40(r3)
    std r20, 0x48(r3)
    std r21, 0x50(r3)
    std r22, 0x58(r3)
    std r23, 0x60(r3)
    std r24, 0x68(r3)
    std r25, 0x70(r3)
    std r26, 0x78(r3)
    std r27, 0x80(r3)
    std r28, 0x88(r3)
    std r29, 0x90(r3)
    std r30, 0x98(r3)
    std r31, 0xa0(r3)
    stfd f14, 0xa8(r3)
    stfd f15, 0xb0(r3)
    stfd f16, 0xb8(r3)
    stfd f17, 0xc0(r3)
    stfd f18, 0xc8(r3)
    stfd f19, 0xd0(r3)
    stfd f20, 0xd8(r3)
    stfd f21, 0xe0(r3)
    stfd f22, 0xe8(r3)
    stfd f23, 0xf0(r3)
    stfd f24, 0xf8(r3)
    stfd f25, 0x100(r3)
    stfd f26, 0x108(r3)
    stfd f27, 0x110(r3)
    stfd f28, 0x118(r3)
    stfd f29, 0x120(r3)
    stfd f30, 0x128(r3)
    stfd f31, 0x130(r3)
    li r3, 0
    blr
# decoy: a context-save routine with a different buffer layout (base r5)
ctx_save:
    std r31, 0x200(r5)
    std r30, 0x208(r5)
    std r29, 0x210(r5)
    std r28, 0x218(r5)
    std r27, 0x220(r5)
    std r26, 0x228(r5)
    std r25, 0x230(r5)
    std r24, 0x238(r5)
    std r23, 0x240(r5)
    std r22, 0x248(r5)
    std r21, 0x250(r5)
    std r20, 0x258(r5)
    std r19, 0x260(r5)
    std r18, 0x268(r5)
    std r17, 0x270(r5)
    std r16, 0x278(r5)
    std r15, 0x280(r5)
    std r14, 0x288(r5)
    blr
# real longjmp: r3 = jmp_buf, r4 = value
longjmp:
    ld r13, 0x00(r3)
    ld r14, 0x18(r3)
    ld r15, 0x20(r3)
    ld r16, 0x28(r3)
    ld r17, 0x30(r3)
    ld r18, 0x38(r3)
    ld r19, 0x40(r3)
    ld r20, 0x48(r3)
    ld r21, 0x50(r3)
    ld r22, 0x58(r3)
    ld r23, 0x60(r3)
    ld r24, 0x68(r3)
    ld r25, 0x70(r3)
    ld r26, 0x78(r3)
    ld r27, 0x80(r3)
    ld r28, 0x88(r3)
    ld r29, 0x90(r3)
    ld r30, 0x98(r3)
    ld r31, 0xa0(r3)
    lfd f14, 0xa8(r3)
    lfd f15, 0xb0(r3)
    lfd f16, 0xb8(r3)
    lfd f17, 0xc0(r3)
    lfd f18, 0xc8(r3)
    lfd f19, 0xd0(r3)
    lfd f20, 0xd8(r3)
    lfd f21, 0xe0(r3)
    lfd f22, 0xe8(r3)
    lfd f23, 0xf0(r3)
    lfd f24, 0xf8(r3)
    lfd f25, 0x100(r3)
    lfd f26, 0x108(r3)
    lfd f27, 0x110(r3)
    lfd f28, 0x118(r3)
    lfd f29, 0x120(r3)
    lfd f30, 0x128(r3)
    lfd f31, 0x130(r3)
    ld r1, 0x08(r3)
    ld r0, 0x10(r3)
    mtlr r0
    cmpwi r4, 0
    bne 1f
    li r4, 1
1:  mr r3, r4
    blr
# decoy: context-restore matching ctx_save, but no FPRs or LR
ctx_restore:
    ld r31, 0x200(r5)
    ld r30, 0x208(r5)
    ld r29, 0x210(r5)
    ld r28, 0x218(r5)
    ld r27, 0x220(r5)
    ld r26, 0x228(r5)
    ld r25, 0x230(r5)
    ld r24, 0x238(r5)
    ld r23, 0x240(r5)
    ld r22, 0x248(r5)
    ld r21, 0x250(r5)
    ld r20, 0x258(r5)
    ld r19, 0x260(r5)
    ld r18, 0x268(r5)
    ld r17, 0x270(r5)
    ld r16, 0x278(r5)
    ld r15, 0x280(r5)
    ld r14, 0x288(r5)
    blr
    .long 0
    .long 0
