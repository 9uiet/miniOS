# MiniOS

## 简介

MiniOS 是一个基于 RISC-V 架构的简易实时操作系统。

## Quick Start

1. 安装依赖

请确保安装好 riscv-roolchain (编译需要) 和 openocd (烧写和Debug需要)，并添加到系统路径

2. 编译 miniOS

执行以下命令后可以见到 `build` 目录下生成 `miniOS.bin` 可执行文件。
```bash
mkdir build && cd build
cmake ..
make
```

3. 烧写到CH32V307

```bash
cd build
make flash
```

*Now, Enjoy it!*

![demo](./image/demo.png)

## Features List
- 支持动态内存管理
- 支持上下文切换和多任务
- 支持异常与中断处理
- 支持简易系统调用

### Application (TBC)

- minishell
- flybird
- miniplayer
- minislide

## 代码模块介绍

### 1. 启动与主流程：`start.S`, `kernel.c`

`start.S` 是系统上电后执行的第一段代码，负责设置栈指针、将 `.data` 段从 Flash 拷贝到 RAM、清零 `.bss` 段，并配置 `mstatus` 寄存器（设置 MPP=3 保持 Machine 模式、MPIE=1 使 `MRET` 后自动开中断），最终跳转至 C 入口函数 `start_kernel`。`kernel.c` 实现 `start_kernel()`，按顺序完成时钟、串口、物理页、陷阱、中断控制器、定时器和调度器的初始化，随后调用应用层入口 `os_main()` 并通过 `schedule()` 切换到第一个任务，构成整个系统的顶层启动流程。

### 2. 任务与调度：`context/sched.c`, `context/entry.S`, `context/lock.c`

`sched.c` 实现了一个简单的循环 FIFO 调度器，维护任务控制块数组 `ctx_tasks[]` 和任务栈 `task_stack[][]`，提供 `task_create`、`schedule`、`task_yield` 等接口，通过轮转方式依次切换各任务。`entry.S` 用汇编实现 `switch_to(struct context *next)`，利用 `mscratch` CSR 保存当前任务的全部通用寄存器上下文，再从目标任务的上下文中恢复寄存器，通过 `ret` 完成实际的控制流跳转。`lock.c` 通过读写 `mstatus` 寄存器中的 MIE 位来实现最简单的自旋锁（关中断/开中断），保护多任务环境下对内核共享数据结构的并发访问。

### 3. 中断与陷阱：`trap/trap.c`, `trap/trap.S`

`trap.S` 定义了 4 字节对齐的 `trap_vector` 入口，在任意异常或中断发生时由硬件跳入此处；它使用与 `entry.S` 相同的宏将全部通用寄存器保存到当前任务上下文，并将 `mepc`、`mcause`、上下文指针依次放入参数寄存器后调用 C 函数 `trap_handler`，最后从返回的 PC 值恢复执行。`trap.c` 根据 `mcause` 的最高位区分中断与异常：中断侧分派到定时器处理（SysTick，cause=12）、软件中断调度（cause=14）及 USART1/2 的 IRQ 处理；异常侧（cause=11）转发给系统调用处理函数 `do_syscall`，实现了中断/异常的统一分发框架。

### 4. 系统调用：`syscall/syscall.c`, `syscall/usys.S`

`usys.S` 提供用户侧的系统调用存根（stub）：将系统调用号写入寄存器 `a7`，执行 `ecall` 指令触发 Machine 模式陷阱，以此进入内核，是用户程序访问内核服务的唯一合法入口。`syscall.c` 在内核侧实现 `do_syscall()`，从任务上下文中读取 `a7` 中的系统调用号，通过 `switch` 分发到对应的处理函数（目前实现了 `sys_getarchid`，用于读取 RISC-V `marchid` CSR），并将返回值写回上下文的 `a0` 寄存器，完成内核态与用户态之间受控的功能调用机制。

### 5. 内存管理：`mem/page.c`, `mem/mem.S`

`page.c` 实现了一个基于页描述符（`struct page`）的物理页分配器：`page_init()` 根据链接脚本确定堆范围并初始化页描述符表，`page_alloc(npages)` 采用 first-fit 策略线性扫描描述符数组，找到足够连续的空闲页后标记 `PAGE_TAKEN` 和 `PAGE_LAST` 并返回首页地址，`page_free()` 则根据 `PAGE_LAST` 标记逐页清除描述符。`mem.S` 以只读数据节（`.rodata`）的形式将链接脚本中各段（text、data、bss、heap、stack）的起始/结束地址导出为全局符号，供 `page.c` 及其他模块在运行时获取精确的内存布局信息。

### 6. 定时器：`timer/stimer.c`

`stimer.c` 在 SysTick 硬件定时器中断的基础上实现了一个软件定时器管理器：每次 `timer_handler()` 被中断调用时全局计数器 `_tick` 加一，随后遍历 `timer_list[]` 数组，将到期的定时器回调触发一次后自动删除，同时清除 SysTick 状态寄存器标志位。该模块提供 `timer_create`、`timer_delete`、`timer_get_tick` 等接口，使内核及应用层可以方便地注册一次性超时事件，是任务超时、uptime 统计等时间相关功能的基础支撑。

## 参考文档

- [CH32FV2x_V3xRM.PDF](doc/CH32FV2x_V3xRM.PDF)
- [CH32V307DS0.PDF](doc/CH32V307DS0.PDF)

## 已知问题

- 当进入while后使用中断会直接halt
- 理论上shell的输入应该由keyboard的中断来触发，而不是uart2的中断，但目前没有keyboard进行实验，所以使用uart2的中断来触发shell的输入。
