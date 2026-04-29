#include "trap.h"
#include "types.h"
#include "minilib.h"
#include "platform.h"
#include "syscall.h"
#include "stimer.h" // for timer_handler

extern void trap_vector(void);

/* -----------------------------------------------------------------------
 * trap_init() — 初始化陷阱向量
 *
 * 将 trap_vector 的地址写入 mtvec CSR（Machine Trap-Vector Base-Address），
 * 使 CPU 在发生任何 M-mode 中断或异常时自动跳转到 trap_vector。
 *
 * mtvec 工作在"直接模式"（低 2 位 = 00），即所有中断/异常共享同一入口。
 * ----------------------------------------------------------------------- */
void trap_init() {
  write_mtvec((reg_t)trap_vector);
}

/* -----------------------------------------------------------------------
 * trap_handler() — M-mode 统一中断/异常分发器（由 trap.S 调用）
 *
 * 参数：
 *   epc   — 被打断时的程序计数器（来自 mepc CSR）
 *   cause — 原因码（来自 mcause CSR）
 *             bit31 = 1：中断（Interrupt）
 *             bit31 = 0：同步异常（Exception / Trap）
 *   cxt   — 指向当前任务 struct context 的指针（来自 mscratch CSR）
 *
 * 返回值：
 *   处理完成后 CPU 应跳回的 PC 地址。
 *   - 普通中断/异常：返回原 epc，CPU 重新执行被打断的指令。
 *   - ecall 系统调用：返回 epc + 4，跳过 ecall 指令本身。
 *
 * mcause 编码（本芯片 CH32FV2x/V3x 扩展）：
 *   中断（bit31=1），低 12 位为中断号：
 *     12 — 机器定时器中断（RISC-V 标准，对应 MTI 位）
 *     14 — 机器软件中断（RISC-V 标准，对应 MSI 位；此处用于触发调度）
 *     53 — USART1 外设中断（厂商扩展）
 *     54 — USART2 外设中断（厂商扩展）
 *   异常（bit31=0），低位为异常编号：
 *     11 — M-mode 环境调用（ecall 指令，即系统调用入口）
 *
 * cause id
 * ref CH32FV2x_V3xRM.PDF v2.3 p78 
 * 
 * 53: USART1
 * 54: USART2
 * ----------------------------------------------------------------------- */
reg_t trap_handler(reg_t epc, reg_t cause, struct context *cxt) {
  reg_t return_pc = epc;
  /* 屏蔽 mcause 高 4 位（符号位及保留位），仅保留有效原因码 */
  reg_t cause_code = cause & 0xfff; /* clear the top 4 bits */

  if (cause & 0x80000000) {
    /* ----------------------------------------------------------------
     * 中断分支（mcause bit31 = 1）
     * 硬件在进入 trap_vector 时已自动清除 mstatus.MIE（关中断），
     * 处理完成后 mret 会通过 mstatus.MPIE 自动恢复中断使能状态。
     * ---------------------------------------------------------------- */
    switch (cause_code) {
      case 12:
        /* 机器定时器中断（Machine Timer Interrupt, MTI）
         * 由 RISC-V 标准定义，通常用于操作系统时间片计数和抢占调度。
         * 此处调用 timer_handler() 重新装载定时器并可触发软件中断。 */
        // uart_puts("Timer interruption!\n");
        timer_handler();
        break;
      case 14:
        /* 机器软件中断（Machine Software Interrupt, MSI）
         * 由定时器处理器通过写 STK_REG->CTLR bit31 触发，用于实现
         * 协作式调度（task_yield）或抢占式调度的上下文切换。
         * 处理前必须先清除该位，否则 mret 后会立刻再次触发本中断。 */
        // printf("Software interruption!\n");
        /* Clear the software interrupt bit */
        STK_REG->CTLR &= ~((uint32_t)(1 << 31));
        /* 调用轮转调度器，切换到下一个就绪任务 */
        schedule();
        break;
      case 53: 
        /* USART1 外设中断（CH32 厂商扩展中断号 53）
         * 由 USART1 接收/发送完成等事件触发，交由驱动层处理。 */
        // printf("USART1 interruption!\n");
        uart1_irq_handler();
        break;
      case 54:
        /* USART2 外设中断（CH32 厂商扩展中断号 54）
         * 处理逻辑同 USART1。 */
        // printf("USART2 interruption!\n");
        uart2_irq_handler();
        break;
      default:
        printf("unkown interruption! code = %d\n", cause_code);
        break;
      }
  } else {
    /* ----------------------------------------------------------------
     * 异常分支（mcause bit31 = 0）
     * 同步异常由指令执行本身触发，return_pc 的处理因异常类型而异。
     * ---------------------------------------------------------------- */
    switch (cause) {
      case 11:
        /* M-mode 环境调用异常（ecall 指令）
         * 用户任务通过 ecall 陷入内核执行系统调用。
         * do_syscall() 根据 cxt->a7（系统调用号）分发到具体服务函数。
         * ecall 指令本身占 4 字节，返回时需跳过它（epc + 4），
         * 否则 mret 后会再次执行 ecall，造成无限递归。 */
        // printf("System call from U-mode!\n");
        do_syscall(cxt);
        return_pc += 4;  /* 跳过 ecall 指令，返回到 ecall 的下一条指令 */
        break;
      default:
        /* 其他未处理异常（非法指令、访存对齐错误、断点等）
         * 当前不做恢复处理，直接 panic 挂起系统，避免进入未定义行为。 */
        printf("exception! code = %d\n", cause_code);
        panic("System stall!");
        /* we don't handle this exception 
         * so the trap test will loop here
         */
    }
  }

  return return_pc;
}
