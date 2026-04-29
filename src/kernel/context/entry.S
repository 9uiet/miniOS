# -----------------------------------------------------------------------
# reg_save / reg_restore — 通用寄存器保存/恢复宏
#
# struct context 的内存布局（偏移量以字节计，每个寄存器占 4 字节）：
#   offset  0: ra      offset 36: a0      offset  72: s3
#   offset  4: sp      offset 40: a1      offset  76: s4
#   offset  8: (gp — 不保存，见下方说明)
#   offset 12: (tp — 不保存，见下方说明)
#   offset 16: t0      offset 44: a2      offset  80: s5
#   offset 20: t1      offset 48: a3      offset  84: s6
#   offset 24: t2      offset 52: a4      offset  88: s7
#   offset 28: s0      offset 56: a5      offset  92: s8
#   offset 32: s1      offset 60: a6      offset  96: s9
#                      offset 64: a7      offset 100: s10
#                                         offset 104: s11
#                                         offset 108: t3
#                                         offset 112: t4
#                                         offset 116: t5
#                                         offset 120: t6  (单独保存，见 switch_to)
#
# 为什么不保存 gp（x3）和 tp（x4）？
#   - gp（全局指针）：编译器用它访问 .sdata/.sbss 等全局数据，
#     在整个程序生命周期内保持不变，无需随任务切换保存。
#   - tp（线程指针）：在 miniOS 中用于存放 hartid（硬件线程 ID），
#     这是全局不变值，同样无需保存。
#
# Save all General-Purpose(GP) registers to context.
# struct context *base = &ctx_task;
# base->ra = ra;
# ......
# These GP registers to be saved don't include gp
# and tp, because they are not caller-saved or
# callee-saved. These two registers are often used
# for special purpose. For example, in RVOS, 'tp'
# (aka "thread pointer") is used to store hartid,
# which is a global value and would not be changed
# during context-switch.
# -----------------------------------------------------------------------
.macro reg_save base
    sw ra, 0(\base)
    sw sp, 4(\base)
    sw t0, 16(\base)
    sw t1, 20(\base)
    sw t2, 24(\base)
    sw s0, 28(\base)
    sw s1, 32(\base)
    sw a0, 36(\base)
    sw a1, 40(\base)
    sw a2, 44(\base)
    sw a3, 48(\base)
    sw a4, 52(\base)
    sw a5, 56(\base)
    sw a6, 60(\base)
    sw a7, 64(\base)
    sw s2, 68(\base)
    sw s3, 72(\base)
    sw s4, 76(\base)
    sw s5, 80(\base)
    sw s6, 84(\base)
    sw s7, 88(\base)
    sw s8, 92(\base)
    sw s9, 96(\base)
    sw s10, 100(\base)
    sw s11, 104(\base)
    sw t3, 108(\base)
    sw t4, 112(\base)
    sw t5, 116(\base)
    # t6 不在此处保存：因为我们用 t6 本身作为 base 指针，
    # 若在宏内保存 t6，则会覆盖掉 t6 中存放的上下文地址。
    # t6 的真实值在 switch_to() 中通过 mscratch 单独取回并保存。
    # we don't save t6 here, due to we have used
    # it as base, we have to save t6 in an extra step
    # outside of reg_save
.endm

# restore all General-Purpose(GP) registers from the context
# except gp & tp.
# struct context *base = &ctx_task;
# ra = base->ra;
# ......
.macro reg_restore base
    lw ra, 0(\base)
    lw sp, 4(\base)
    lw t0, 16(\base)
    lw t1, 20(\base)
    lw t2, 24(\base)
    lw s0, 28(\base)
    lw s1, 32(\base)
    lw a0, 36(\base)
    lw a1, 40(\base)
    lw a2, 44(\base)
    lw a3, 48(\base)
    lw a4, 52(\base)
    lw a5, 56(\base)
    lw a6, 60(\base)
    lw a7, 64(\base)
    lw s2, 68(\base)
    lw s3, 72(\base)
    lw s4, 76(\base)
    lw s5, 80(\base)
    lw s6, 84(\base)
    lw s7, 88(\base)
    lw s8, 92(\base)
    lw s9, 96(\base)
    lw s10, 100(\base)
    lw s11, 104(\base)
    lw t3, 108(\base)
    lw t4, 112(\base)
    lw t5, 116(\base)
    lw t6, 120(\base)
.endm

# -----------------------------------------------------------------------
# 关于 mscratch 与 t6 的使用约定：
#   - mscratch CSR 始终保存"当前正在执行任务"的 struct context 指针。
#   - 我们选择 t6（x31，编号最大的通用寄存器）作为 reg_save/reg_restore
#     的 base 寄存器，原因是在执行 lw/sw 指令时，base 寄存器不会被
#     宏内的 load/store 操作覆盖（t6 是最后一个被处理的寄存器）。
#   - CSR（包括 mscratch）不能直接用作 load/store 的地址寄存器，
#     必须先将其值读入通用寄存器后才能使用。
#
# Something to note about save/restore:
# - We use mscratch to hold a pointer to context of current task
# - We use t6 as the 'base' for reg_save/reg_restore, because it is the
#   very bottom register (x31) and would not be overwritten during loading.
#   Note: CSRs(mscratch) can not be used as 'base' due to load/restore
#   instruction only accept general purpose registers.
# -----------------------------------------------------------------------

.text

# -----------------------------------------------------------------------
# switch_to(struct context *next)
#
# 参数：
#   a0 — 指向"下一个任务"的 struct context
#
# 执行流程：
#   1. csrrw t6, mscratch, t6
#        原子地交换 t6 与 mscratch：
#          - t6 ← mscratch（旧值：当前任务的上下文指针，首次为 0）
#          - mscratch ← t6（暂存 t6 的原始值，稍后取回）
#   2. beqz t6, 1f
#        首次调用时 mscratch == 0（由 sched_init() 保证），
#        交换后 t6 == 0，说明没有"旧任务"需要保存，直接跳到恢复阶段。
#   3. reg_save t6
#        将所有通用寄存器（除 t6 外）保存到当前任务的 struct context。
#   4. 单独保存 t6 的真实值：
#        mv t5, t6       — t5 暂存当前上下文指针
#        csrr t6, mscratch — 从 mscratch 中取回 t6 的原始值（步骤 1 中存入）
#        sw t6, 120(t5)  — 将真实 t6 值写入上下文的 offset 120
#   5. csrw mscratch, a0
#        更新 mscratch，使其指向下一个任务的上下文，
#        为下次 switch_to() 提供正确的"当前任务"指针。
#   6. mv t6, a0 + reg_restore t6
#        以 t6 为 base，从下一个任务的上下文恢复所有通用寄存器
#        （包括 ra、sp、t6 等），此时 ra 已被更新为下一个任务的入口地址。
#   7. ret
#        等价于 jalr x0, ra, 0，跳转到 ra 所指的地址，
#        即下一个任务的入口函数（或上次被切走时的返回地址），
#        完成上下文切换。
# -----------------------------------------------------------------------
# void switch_to(struct context *next);
# a0: pointer to the context of the next task
.global switch_to
.align 4
switch_to:
    # 步骤 1：原子交换 t6 与 mscratch。
    # 交换后：t6 = 旧 mscratch（当前任务上下文指针），mscratch = 旧 t6
    csrrw   t6, mscratch, t6    # swap t6 and mscratch
    # 步骤 2：首次切换特判。
    # 系统启动后 sched_init() 将 mscratch 清零，第一次调用时 t6 == 0，
    # 没有"当前任务"需要保存，跳过保存阶段。
    beqz    t6, 1f              # Note: the first time switch_to() is
                                # called, mscratch is initialized as zero
                                # (in sched_init()), which makes t6 zero,
                                # and that's the special case we have to
                                # handle with t6
    # 步骤 3：保存当前任务的通用寄存器（ra/sp/t0-t5/s0-s11/a0-a7）
    reg_save    t6              # save context of prev task

    # 步骤 4：单独保存 t6 的真实值（t6 在步骤 1 中已被 csrrw 替换为上下文指针）
    #save the actual t6 register, which we swapped into mscratch
    mv      t5, t6              # t5 points to the context of current task
    csrr    t6, mscratch        # read t6 back from mscratch
    sw      t6, 120(t5)         # save t6 with t5 as base
1:
    # 步骤 5：将 mscratch 更新为下一个任务的上下文指针，
    # 使下次 switch_to() 能够正确找到"当前任务"并保存其上下文。
    # switch mscratch to point to the context of the next task
    csrw    mscratch, a0

    # 步骤 6：从下一个任务的上下文恢复所有通用寄存器。
    # 以 t6 为 base（t6 = a0 = 下一个任务的上下文指针），
    # 恢复后 ra 指向下一个任务的入口（或被切走时的返回地址）。
    # Restore all GP registers
    # use t6 to point to the context of the new task
    mv      t6, a0
    reg_restore t6

    # 步骤 7：执行实际的跳转，完成上下文切换。
    # ret = jalr x0, ra, 0，跳转到 ra 所指地址（下一个任务的入口）。
    # Do actual context switching. (ra has been changed)
    ret

.end