#include "context.h"
#include "minilib.h"
#include "platform.h"

/* defined in entry.S */
extern void switch_to(struct context *next);

/* -----------------------------------------------------------------------
 * 任务栈与上下文控制块（TCB）数组
 *
 * task_stack[][]: 每个任务独占一段固定大小的栈空间。
 *   - 按 16 字节对齐，满足 RISC-V 调用约定对 sp 的对齐要求。
 *   - STACK_SIZE / MAX_TASKS 由 context.h 中的宏定义。
 *
 * ctx_tasks[]:   与 task_stack 一一对应的上下文描述符（struct context）。
 *   - struct context 至少保存 ra（返回地址）和 sp（栈指针），
 *     其余通用寄存器在 entry.S 的 switch_to() 中被保存/恢复。
 * ----------------------------------------------------------------------- */
/*
 * In the standard RISC-V calling convention, the stack pointer sp
 * is always 16-byte aligned.
 */
uint8_t __attribute__((aligned(16))) task_stack[MAX_TASKS][STACK_SIZE];
struct context ctx_tasks[MAX_TASKS];

/*
 * _top     — 当前已注册的任务数量，同时也是下一个空槽的索引。
 *            取值范围 [0, MAX_TASKS]，初始为 0（无任务）。
 * _current — 当前正在执行的任务在 ctx_tasks[] 中的索引。
 *            初始为 -1，表示系统尚未切换到任何任务。
 *
 * _top is used to mark the max available position of ctx_tasks
 * _current is used to point to the context of current task
 */
static int _top = 0;
static int _current = -1;

/*
 * sched_init() — 调度器初始化
 *
 * 将 mscratch CSR 清零。mscratch 被 entry.S 用于保存"当前任务上下文指针"；
 * 置零后 switch_to() 第一次被调用时会检测到 mscratch == 0，
 * 从而跳过"保存旧任务"步骤，直接加载第一个任务的上下文。
 */
void sched_init() {
  write_mscratch(0);
}

/*
 * schedule() — 循环 FIFO 调度器
 *
 * 每次调用将 _current 向后推进一个槽位（对 _top 取模），
 * 形成环形轮转（Round-Robin / FIFO）。所有任务优先级相同，
 * 按注册顺序依次获得 CPU 时间片。
 *
 * implement a simple cycle FIFO schedular
 */
void schedule() {
  if (_top <= 0) {
    panic("Num of task should be greater than zero!");
    return;
  }

  /* 轮转到下一个任务（循环取模，使索引在 [0, _top) 范围内环绕） */
  _current = (_current + 1) % _top;
  struct context *next = &ctx_tasks[_current];
  /* 调用 entry.S 中的 switch_to()，保存当前上下文并加载 next 的上下文 */
  switch_to(next);
}

/*
 * task_create() — 注册一个新任务
 *
 * 在 ctx_tasks[_top] 中初始化任务上下文：
 *   - sp: 指向该任务独立栈的顶端（高地址端，栈向低地址方向增长）。
 *   - ra: 设为任务入口函数地址，使 switch_to() 通过 ret 指令跳入该函数。
 *
 * DESCRIPTION
 *  Create a task.
 *  - start_routin: task routine entry
 * RETURN VALUE
 *  0: success
 *  -1: if error occurred
 */
int task_create(void (*start_routin)(void)) {
  if (_top < MAX_TASKS) {
    /* 栈从高地址向低地址增长，因此 sp 初始化为栈数组末尾（最高地址） */
    ctx_tasks[_top].sp = (reg_t)&task_stack[_top][STACK_SIZE];
    /* ra 设为任务函数入口，switch_to() 末尾的 ret 将跳转至此处 */
    ctx_tasks[_top].ra = (reg_t)start_routin;
    _top++;
    return 0;
  } else {
    printf("Maximum number of tasks reached. Task creation failed.");
    return -1;
  }
}

/*
 * task_yield() — 主动让出 CPU
 *
 * 当前任务调用此函数后，调度器会立即切换到下一个就绪任务。
 * 本函数返回时，说明轮转一圈后当前任务重新获得 CPU。
 *
 * DESCRIPTION
 *  task_yield() allows the currently executing task to relinquish the CPU
 *  and let system scheduler to select a new task for execution.
 */
void task_yield() {
  schedule();
}

/*
 * task_delay() — 忙等延时
 *
 * 通过空循环消耗 CPU 时钟周期实现粗粒度延时，不依赖定时器硬件。
 * count 乘以 10000 是经验性倍数，实际延时长度与 CPU 主频有关。
 * 注意：此实现会独占 CPU，不适合对实时性要求高的场景。
 *
 * a very rough implementaion, just to consume the cpu
 */
void task_delay(volatile int count) {
  count *= 10000;
  while (count--);
}
