#include "minilib.h"
#include "syscall.h"
#include "context.h"
#include "platform.h"

/* -----------------------------------------------------------------------
 * sys_getarchid() — 系统调用处理函数：读取硬件架构 ID
 *
 * 参数：
 *   ptr_archid — 用户态传入的 unsigned int 指针（来自 cxt->a0）。
 *                内核通过读取 marchid CSR 获得架构 ID 并写入该地址。
 *
 * 返回：
 *    0  成功，*ptr_archid 已被填写。
 *   -1  失败（ptr_archid 为 NULL，无法写入）。
 *
 * marchid CSR 含义（RISC-V 规范）：
 *   标识 RISC-V 处理器的微架构版本，只读，由芯片厂商定义。
 *   CH32V 系列上该值为厂商特定编码（参见芯片手册）。
 * ----------------------------------------------------------------------- */
int sys_getarchid(unsigned int *ptr_archid) {
  // printf("--> sys_getarchid, arg0 = 0x%x\n", ptr_archid);
  if (ptr_archid == NULL) {
    return -1;
  }

  /* 读取 marchid CSR，将架构 ID 写入用户态提供的地址 */
  *ptr_archid = read_marchid();
  return 0;
}

/* -----------------------------------------------------------------------
 * do_syscall() — 内核系统调用分发器（由 trap_handler 调用）
 *
 * 调用时机：
 *   trap_handler 在 mcause == 11（M-mode ecall 异常）时调用本函数。
 *
 * 参数：
 *   cxt — 指向当前任务 struct context 的指针（来自 mscratch CSR）。
 *         上下文在 trap_vector 中已完整保存，此处直接读写寄存器字段。
 *
 * 系统调用号读取：
 *   按 RISC-V ABI，用户态在执行 ecall 前将调用号写入 a7，
 *   trap_vector 将所有通用寄存器保存到 struct context，
 *   因此 cxt->a7 即为本次系统调用号。
 *
 * 参数传递：
 *   用户态将调用参数放在 a0–a5，内核通过 cxt->a0–cxt->a5 读取。
 *
 * 返回值写回：
 *   处理函数的返回值写入 cxt->a0。
 *   trap_handler 返回后，trap_vector 执行 reg_restore 将 cxt->a0
 *   恢复到物理寄存器 a0，mret 返回用户态，调用方即可从 a0 获取结果。
 *
 * 当前支持的系统调用：
 *   SYS_getarchid (1) — 读取 marchid CSR，返回硬件架构 ID。
 * ----------------------------------------------------------------------- */
void do_syscall(struct context *cxt) {
  /* 从上下文中读取系统调用号（用户态写入 a7，已由 trap_vector 保存） */
  uint32_t syscall_num = cxt->a7;

  switch (syscall_num) {
    case SYS_getarchid:
      /* 参数：cxt->a0 = 用户态传入的 unsigned int* 指针
       * 返回：sys_getarchid 的返回值写回 cxt->a0，
       *       mret 后用户态从 a0 寄存器获取结果 */
      cxt->a0 = sys_getarchid((unsigned int *)cxt->a0);
      break;
    default:
      /* 未知系统调用号：打印警告并通过 a0 返回 -1 给用户态 */
      printf("Unknown syscall no: %d\n", syscall_num);
      cxt->a0 = -1;
  }

  return;
}