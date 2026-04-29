# -----------------------------------------------------------------------
# reg_save / reg_restore — 通用寄存器保存/恢复宏（与 entry.S 共用相同约定）
#
# struct context 内存布局（每个寄存器占 4 字节）：
#   offset   0: ra    offset  36: a0    offset  72: s3
#   offset   4: sp    offset  40: a1    offset  76: s4
#   offset   8: (gp — 不保存)           offset  80: s5
#   offset  12: (tp — 不保存)           offset  84: s6
#   offset  16: t0    offset  44: a2    offset  88: s7
#   offset  20: t1    offset  48: a3    offset  92: s8
#   offset  24: t2    offset  52: a4    offset  96: s9
#   offset  28: s0    offset  56: a5    offset 100: s10
#   offset  32: s1    offset  60: a6    offset 104: s11
#                     offset  64: a7    offset 108: t3
#                                       offset 112: t4
#                                       offset 116: t5
#                                       offset 120: t6  (单独保存)
#                                       offset 124: mepc (trap 专用，保存被打断时的 PC)
#
# 不保存 gp（全局指针）和 tp（线程指针）的原因：
#   - gp：编译器用于访问全局/静态数据，程序运行期间不变。
#   - tp：miniOS 用其存放 hartid（硬件线程 ID），全局不变。
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
  # t6 不在宏内保存：t6 本身被用作 base 指针，
  # 其真实值在进入 trap_vector 时通过 csrrw 存入 mscratch，
  # 需要在宏外单独取回并写入 offset 120。
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
# 关于 mscratch / t6 的使用约定（与 entry.S 相同）：
#   - mscratch 始终保存"当前任务"的 struct context 指针。
#   - 选用 t6（x31）作为 base：它是编号最大的通用寄存器，
#     在 lw/sw 序列中最后被覆盖，不会影响之前的读写操作。
#   - CSR 不能直接用作 load/store 的地址，必须先读入通用寄存器。
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
# trap_vector — M-mode 统一陷阱入口
#
# 当 CPU 在 M-mode 下发生任何中断或异常时，硬件会：
#   1. 将被打断指令的地址保存到 mepc CSR。
#   2. 将原因编码保存到 mcause CSR（bit31=1 为中断，=0 为异常）。
#   3. 跳转到 mtvec 中存储的地址（即本函数）。
#
# 对齐要求：mtvec 要求向量基地址必须 4 字节对齐（直接模式下低 2 位为 0）。
#
# 执行流程（7 步）：
#   1. csrrw t6, mscratch, t6
#        原子交换 t6 与 mscratch：
#          t6      ← mscratch（当前任务上下文指针）
#          mscratch ← t6（暂存 t6 的原始值）
#   2. reg_save t6
#        将所有通用寄存器（除 t6 外）保存到当前任务的 struct context。
#   3. 单独保存 t6 的真实值（从 mscratch 取回）及 mepc（被打断的 PC）。
#   4. csrw mscratch, t5
#        将 mscratch 恢复为当前任务上下文指针（t5 在步骤 3 中临存该值）。
#   5. 调用 C 函数 trap_handler(epc, cause, cxt)：
#        a0 = mepc, a1 = mcause, a2 = mscratch（上下文指针）
#        trap_handler 返回值（a0）为处理后应跳回的 PC。
#   6. csrw mepc, a0
#        用返回值更新 mepc，使 mret 跳到正确地址。
#        （系统调用时 trap_handler 会将 epc+4，跳过 ecall 指令）
#   7. reg_restore + mret
#        恢复全部通用寄存器，mret 返回到 mepc 所指地址并恢复特权级。
# -----------------------------------------------------------------------
# interrupts and exceptions while in machine mode come here.
.global trap_vector
# the trap vector base address must always be aligned on a 4-byte boundary
.align 4
trap_vector:
  # 步骤 1：原子交换 t6 与 mscratch。
  # 交换后：t6 = 当前任务上下文指针，mscratch = t6 的原始值。
  # save context(registers).
  csrrw   t6, mscratch, t6  # swap t6 and mscratch
  # 步骤 2：保存所有通用寄存器（除 t6 以外）到当前任务上下文。
  reg_save  t6

  # 步骤 3a：单独保存 t6 的真实值。
  # t6 在步骤 1 中被替换为上下文指针，其原始值被存入 mscratch，
  # 此处将其取回并写到 struct context 的 offset 120。
  # save the actual t6 register, which we swapped into mscratch
  mv    t5, t6        # t5 points to the context of current task
  csrr  t6, mscratch    # read t6 back from mscratch
  sw    t6, 120(t5)     # save t6 with t5 as base

  # 步骤 3b：保存 mepc（被打断时的程序计数器）到上下文 offset 124。
  # trap_handler 可能修改返回 PC（例如 ecall 需要 +4），
  # 将其存入上下文便于调试和未来扩展。
  # save mepc to context of current task
  csrr    a0, mepc
  sw      a0, 124(t5)

  # 步骤 4：恢复 mscratch 为当前任务上下文指针（t5 暂存了该值）。
  # 这样 trap_handler 可以通过 mscratch 访问完整的 struct context。
  # Restore the context pointer info mscratch
  csrw  mscratch, t5

  # 步骤 5：调用 C 语言 trap 处理函数。
  # 参数约定：
  #   a0 = mepc    — 被打断时的程序计数器（中断/异常返回地址）
  #   a1 = mcause  — 中断/异常原因码（bit31=1 表示中断，=0 表示异常）
  #   a2 = mscratch — 当前任务的 struct context 指针
  # call the C trap handler in trap.c
  csrr  a0, mepc
  csrr  a1, mcause
  csrr  a2, mscratch
  call  trap_handler

  # 步骤 6：trap_handler 通过 a0 返回处理后的跳回 PC，写入 mepc。
  # 若是 ecall，trap_handler 已将 epc+4（跳过 ecall 指令本身）。
  # trap_handler will return the return address via a0.
  csrw  mepc, a0

  # 步骤 7：恢复所有通用寄存器，然后 mret 返回被打断的执行流。
  # mret 同时恢复 mstatus（含 MIE 位），重新使能中断。
  # restore context(registers).
  csrr  t6, mscratch
  reg_restore t6

  # return to whatever we were doing before trap.
  mret

.end  
