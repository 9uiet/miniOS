/*
 * stimer.c means software timer, which is a software implementation
 * of hardware timer. timer used by miniOS are all software timers.
 *
 * 工作原理概述：
 *   平台层的 SysTick 硬件定时器以固定周期（此处为 1 秒）产生中断，
 *   每次中断调用 timer_handler()，全局计数 _tick 加一。
 *   timer_handler() 随后遍历 timer_list[]，将每个定时器的
 *   timeout_tick 与当前 _tick 比较，到期则调用回调并自动删除。
 *
 * 接口一览：
 *   timer_create()  — 注册一个一次性软件定时器
 *   timer_delete()  — 提前取消一个软件定时器
 *   timer_handler() — SysTick 中断服务例程，由平台 ISR 调用（内部）
 */

#include "minilib.h"
#include "stimer.h"
#include "context.h"
#include "platform.h"

/*
 * _tick — 全局系统节拍计数器
 *
 * 每次 SysTick 中断触发 timer_handler() 时加一。
 * 单位为"节拍"，硬件定时器的比较值决定每节拍实际时长
 * （platform/ch32v307/timer.c 中配置为 1 秒）。
 * 仅在中断上下文中被修改，无需额外同步。
 */
static uint32_t _tick = 0;

/* timer_list — 静态分配的软件定时器池，最多同时存在 MAX_TIMER 个 */
#define MAX_TIMER 10
static struct timer timer_list[MAX_TIMER];

/*
 * timer_create() — 注册一个一次性软件定时器
 *
 * 参数：
 *   handler  — 定时器到期时调用的回调函数，不能为 NULL。
 *   arg      — 传递给回调函数的用户数据指针，可为 NULL。
 *   timeout  — 到期的绝对节拍值（_tick >= timeout 时触发），不能为 0。
 *
 * 返回：
 *   成功返回指向 timer_list[] 中空闲槽的指针；
 *   参数非法或池已满时返回 NULL。
 *
 * 注意：
 *   函数通过 spin_lock/spin_unlock 保护 timer_list，
 *   确保多任务并发调用时的数据一致性。
 *   timeout 为绝对节拍，调用者需结合 get_tick() 计算相对延迟：
 *     timer_create(cb, arg, get_tick() + 5);  // 5 个节拍后触发
 */
struct timer *timer_create(void (*handler)(void *arg), 
                           void *arg, uint32_t timeout) {
  /* TBD: params should be checked more, but now we just simplify this */
  if (handler == NULL || timeout == 0) {
    return NULL;
  }

  /* use lock to protect the shared timer_list between multiple tasks */
  spin_lock();

  /* 遍历 timer_list，找到第一个空闲槽（func == NULL 表示未使用） */
  struct timer *t = timer_list;

  int i = 0;
  for (; i < MAX_TIMER; i++) {
    if (t->func == NULL) {
      break;  /* 找到空闲槽，退出循环 */
    }
    t++;
  }

  /* 若所有槽均被占用，解锁并返回失败 */
  if (i >= MAX_TIMER) {
    spin_unlock();
    return NULL;
  }

  /* 填写定时器字段，完成注册 */
  t->func = handler;          /* 到期回调函数 */
  t->arg = arg;               /* 回调参数 */
  t->timeout_tick = timeout;  /* 到期的绝对节拍值 */

  spin_unlock();

  return t;  /* 返回定时器句柄，供调用方后续传给 timer_delete() */
}

/*
 * timer_delete() — 提前取消一个软件定时器
 *
 * 参数：
 *   timer — 由 timer_create() 返回的定时器句柄。
 *
 * 实现：
 *   将对应槽的 func 和 arg 清零，timer_check() 将跳过 func==NULL 的槽，
 *   从而实现"逻辑删除"，无需搬移数组元素。
 *   同样通过 spin_lock/spin_unlock 保护并发访问。
 */
void timer_delete(struct timer *timer) {
  spin_lock();

  struct timer *t = timer_list;
  for (int i = 0; i < MAX_TIMER; i++) {
    if (t == timer) {
      /* 通过将 func 置 NULL 标记该槽为空闲 */
      t->func = NULL;
      t->arg = NULL;
      break;
    }
    t++;
  }

  spin_unlock();
}

/*
 * timer_check() — 遍历 timer_list，触发到期定时器的回调
 *
 * 调用时机：
 *   仅由 timer_handler() 在中断上下文中调用，此时中断已被硬件屏蔽，
 *   无需额外加锁即可安全访问 timer_list。
 *
 * 逻辑：
 *   对每个已注册的定时器（func != NULL），比较当前 _tick 与
 *   timeout_tick：若 _tick >= timeout_tick，则：
 *     1. 调用回调函数 t->func(t->arg)。
 *     2. 将 func/arg 清零，实现"一次性"语义（one-shot timer）。
 *   若需循环定时器，可在回调中重新调用 timer_create()。
 */
/* this routine should be called in interrupt context (interrupt is disabled) */
static inline void timer_check() {
  struct timer *t = timer_list;
  for (int i = 0; i < MAX_TIMER; i++) {
    if (t->func) {
      printf("t->timeout_tick: %d\n", t->timeout_tick);
      printf("_tick: %d\n", _tick);
      /* 当前节拍 >= 定时器到期节拍，触发回调 */
      if (_tick >= t->timeout_tick) {
        t->func(t->arg);  /* 执行用户注册的到期回调 */

        /* once time, just delete it after timeout */
        /* 一次性定时器：回调后立即清除，防止重复触发 */
        t->func = NULL;
        t->arg = NULL;

        // break;
      }
    }
    t++;
  }
}

/*
 * timer_handler() — SysTick 中断服务例程（ISR）
 *
 * 调用路径：
 *   SysTick 硬件定时器比较值匹配 → 产生中断 → 平台 ISR 调用本函数。
 *   （在 CH32V307 上，由 trap_handler 根据 mcause 路由到此处）
 *
 * 执行步骤：
 *   1. _tick++ — 全局节拍计数加一，记录已过去的时间节拍数。
 *   2. timer_check() — 遍历 timer_list，触发所有已到期定时器的回调。
 *   3. 清除 SysTick 硬件状态：
 *      - CTLR[5] 置 1：将计数器重置为 0，开始下一个计时周期。
 *      - SR[0]  清 0：清除中断挂起标志，防止中断被重复响应。
 */
void timer_handler() {
  _tick++;  /* 节拍计数加一，每次 SysTick 中断触发一次 */
  // printf("tick: %d\n", _tick);

  timer_check();  /* 检查并触发所有到期定时器的回调 */

  /* Clear the status register flag bit */
  /* 重置计数器寄存器（CTLR[5]=1），清除中断状态位（SR[0]=0） */
  STK_REG->CTLR |= (uint32_t)(1 << 5);
  STK_REG->SR &= ~(1 << 0);
}
