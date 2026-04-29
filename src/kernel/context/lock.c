#include "platform.h"
#include "context.h"

/* -----------------------------------------------------------------------
 * miniOS 自旋锁实现
 *
 * 在单核（单 hart）RISC-V 系统中，"并发"来源只有中断（定时器、外设等）。
 * 因此只需关闭全局中断即可保证临界区的原子性，无需硬件原子指令（AMO）。
 *
 * 实现方式：操作 mstatus CSR 中的 MIE（Machine Interrupt Enable）位：
 *   MIE = 0 → 屏蔽所有 M-mode 中断 → 进入临界区（加锁）
 *   MIE = 1 → 恢复中断响应        → 离开临界区（解锁）
 *
 * 注意：此实现不支持多核（多 hart）场景；若移植到多核平台，
 *       需改用 LR/SC 原子指令实现真正的自旋锁。
 * ----------------------------------------------------------------------- */

/*
 * spin_lock() — 加锁（关闭全局中断）
 *
 * 清除 mstatus.MIE 位，使 CPU 停止响应 M-mode 中断。
 * 调用后，当前执行流在调用 spin_unlock() 之前不会被中断抢占。
 */
int spin_lock() {
  write_mstatus(read_mstatus() & ~MSTATUS_MIE);
  return 0;
}

/*
 * spin_unlock() — 解锁（重新开启全局中断）
 *
 * 置位 mstatus.MIE，恢复 M-mode 中断响应。
 * 此后定时器中断等可以再次触发，调度器可以抢占当前任务。
 */
int spin_unlock() {
  write_mstatus(read_mstatus() | MSTATUS_MIE);
  return 0;
}
