
#include "platform.h"
#include "minilib.h"

#include "include/kernel.h"
#include "include/test.h"

extern void os_main(void);

/*
 * start_kernel() —— 内核 C 层启动入口
 *
 * 由 start.S 中的 _start 在完成汇编级初始化（栈/data/bss/mstatus）后跳入。
 * 按固定顺序依次初始化各子系统，最终通过 schedule() 执行第一次任务切换，
 * 从此系统进入多任务运行状态，本函数不会返回。
 */
void start_kernel(void) {
  /* ---- 硬件层初始化 ---- */

  /* 初始化时钟控制器（RCC），配置系统主频（本板 48 MHz），
   * 后续所有外设时钟均依赖此步骤完成。 */
  rcc_init();

  /* 初始化 UART2，作为调试串口（shell 输入/输出通道）。
   * 初始化完成后即可使用 printf 向串口输出调试信息。 */
  uart2_init();
  printf("Hello, miniOS!\n");

  /* ---- 内核子系统初始化 ---- */

  /* 初始化物理页分配器：扫描堆区范围，建立页描述符表，
   * 使 page_alloc() / page_free() 可用。 */
  page_init();

  /* 初始化陷阱向量：将 trap_vector 地址写入 mtvec CSR，
   * 此后所有中断和异常均由 trap.S 统一接管。 */
  trap_init();

  /* 初始化可编程快速中断控制器（PFIC），使能所需的外部中断源
   *（如 USART1/2、SysTick 等）。 */
  pfic_init();

  /* 初始化 SysTick 定时器：设置比较值为 1 秒，使能计数和中断，
   * 之后每秒触发一次 timer_handler()，驱动软件定时器列表和 _tick 计数。 */
  timer_init();

  /* 初始化调度器：将 mscratch 清零（标记"首次切换"），
   * 后续通过 task_create() 向就绪队列中注册任务。 */
  sched_init();

  /* ---- 可选测试用例（调试期间按需取消注释） ---- */
  // page_test();
  // task_test();
  // exception_test();
  // external_interrupt_test();
  // preemptive_task_test();
  // syscall_test();
  // timer_test();
  // lock_test();

  /* ---- 应用层入口 ---- */

  /* 调用应用层主函数，通常在此注册所有用户任务（task_create）。
   * os_main() 只做任务注册，不会进入任务循环。 */
  os_main();

  /* 执行第一次任务调度：
   * schedule() 内部调用 switch_to()，switch_to() 以 MRET 完成上下文切换。
   * MRET 会利用 start.S 中预先设置好的 mstatus（MPP=3, MPIE=1）：
   *   - 保持 Machine 模式运行；
   *   - 自动开启全局中断（MIE ← MPIE）。
   * 从此控制流转入第一个任务，本函数不再返回。 */
  schedule();

  /* 正常情况下永远不会到达这里；若到达则说明任务列表为空或调度器异常。 */
  while (1) { 
    printf("should not be here\n"); 
    task_delay(1000);
  }
}