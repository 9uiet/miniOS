  .global _start

  .text

# -----------------------------------------------------------------------
# _start: 系统上电/复位后执行的第一段代码（启动入口）
#
# 执行顺序：
#   1. 初始化栈指针
#   2. 将 .data 段从 Flash（LMA）复制到 RAM（VMA）
#   3. 将 .bss  段清零
#   4. 配置 mstatus CSR（MPP + MPIE）
#   5. 跳转到 C 入口函数 start_kernel()
# -----------------------------------------------------------------------
_start:
  # ---- 1. 栈初始化 ----
  # 将栈指针 sp 设置到 SRAM 末尾（_stack_end 由链接脚本定义）。
  # RISC-V 调用约定要求 sp 16 字节对齐；链接脚本已保证对齐。
  la      sp, _stack_end  # set stack pointer to the end of SRAM

  # ---- 2. 将 .data 段从 Flash 加载到 RAM ----
  # 编译器将已初始化的全局/静态变量的初始值存放在 Flash（LMA 地址），
  # 但程序运行时从 RAM（VMA 地址）读写。因此上电后需手动完成这次搬运。
  #   a0 = _data_lma  (Flash 中的加载地址，数据源)
  #   a1 = _data_vma  (RAM  中的运行地址，目的地)
  #   a2 = _data_end  (RAM  中 .data 段结束地址)
  la      a0, _data_lma
  la      a1, _data_vma
  la      a2, _data_end
  bgeu    a1, a2, 2f  # if a1 >= a2, data section is empty
1:
  lw      t0, (a0)    # 从 Flash 读取 4 字节
  sw      t0, (a1)    # 写入 RAM
  addi    a0, a0, 4   # 源地址 +4
  addi    a1, a1, 4   # 目的地址 +4
  bltu    a1, a2, 1b  # loop until a1 >= a2
2:
  # ---- 3. 清零 .bss 段 ----
  # .bss 段存放未初始化的全局/静态变量，C 标准要求其初值为 0。
  # Flash 中不存储 .bss 内容，因此直接在 RAM 中将该区域写零。
  #   a0 = _bss_start
  #   a1 = _bss_end
  la      a0, _bss_start
  la      a1, _bss_end
  bgeu    a0, a1, 2f  # if a0 >= a1, bss section is empty
1:
  sw      zero, (a0)  # 将当前地址写零
  addi    a0, a0, 4   # 地址 +4
  bltu    a0, a1, 1b  # loop until a0 >= a1
2:
  # ---- 4. 配置 mstatus CSR ----
  # start_kernel() 末尾会调用 schedule()，schedule() 内部通过 switch_to()
  # 执行 MRET 指令切换到第一个任务。MRET 会从 mstatus 中读取以下字段：
  #   mstatus.MPP  [12:11] = 0b11 (3)
  #       MRET 后保持 Machine 模式运行（本系统所有任务均在 M-mode 执行）。
  #   mstatus.MPIE [7]     = 1
  #       MRET 后将 MIE 置为 MPIE 的值（即 1），从而自动开启全局中断，
  #       使定时器、外设等中断能够正常触发。
  # 注意：上电默认 mstatus = 0，需要显式设置。
  li      t0, 3 << 11 | 1 << 7   # 构造掩码：MPP=3, MPIE=1
  csrr    a1, mstatus             # 读取当前 mstatus
  or      t0, t0, a1             # 合并：保留原有位，追加 MPP 和 MPIE
  csrw    mstatus, t0             # 写回 mstatus

  # ---- 5. 跳转到 C 入口 ----
  # 跳转到 kernel.c 中的 start_kernel()，完成剩余的子系统初始化。
  # 注意：使用 j（无条件跳转）而非 call，因为 start_kernel 不会返回。
  j       start_kernel  # jump to start_kernel

.end
