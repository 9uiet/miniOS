// #include "syscall.h"

/* -----------------------------------------------------------------------
 * usys.S — 用户态系统调用存根（User-mode Syscall Stubs）
 *
 * RISC-V 系统调用约定（与 Linux RISC-V ABI 一致）：
 *   - 系统调用号写入 a7（x17）寄存器。
 *   - 最多 6 个参数依次放在 a0–a5（x10–x15）寄存器中。
 *   - 执行 ecall 指令触发 M-mode 异常（mcause = 11），陷入内核。
 *   - 内核处理完成后，返回值写回 a0（x10）寄存器。
 *   - ret 指令将 a0 中的返回值带回调用方。
 *
 * 每个系统调用存根的执行流程：
 *   1. 将系统调用号立即数加载到 a7。
 *   2. 执行 ecall：CPU 跳转到 mtvec（trap_vector），进入 trap 处理器。
 *   3. trap_handler 调用 do_syscall(cxt)，根据 a7 分发到内核服务函数。
 *   4. 服务函数的返回值通过 cxt->a0 写回，mret 恢复执行。
 *   5. ret 返回调用方，调用方从 a0 获取结果。
 * ----------------------------------------------------------------------- */

/* 系统调用号常量（必须与 syscall.h 中 #define 保持同步） */
.equ SYS_getarchid, 1

/* -----------------------------------------------------------------------
 * getarchid(unsigned int *archid) — 获取硬件架构 ID
 *
 * 参数：a0 = archid 指针（传递给内核，内核写入 marchid CSR 读取结果）
 * 返回：a0 = 0 成功，-1 失败（NULL 指针）
 *
 * 步骤：
 *   1. li a7, SYS_getarchid  — 将系统调用号 1 写入 a7，标识本次调用类型。
 *   2. ecall                 — 触发 ecall 异常，陷入 M-mode trap 处理器。
 *   3. ret                   — 内核处理完毕、mret 返回此处后，
 *                              a0 已被 do_syscall 写入返回值，ret 带回调用方。
 * ----------------------------------------------------------------------- */
.global getarchid
getarchid:
  li  a7, SYS_getarchid  /* 设置系统调用号 → a7，内核用此值分发处理函数 */
  ecall                  /* 陷入内核：触发 mcause=11 异常，跳转到 trap_vector */
  ret                    /* 返回调用方，a0 中已含内核写回的返回值 */