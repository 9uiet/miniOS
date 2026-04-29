/*
 * mem.S — 链接脚本段地址符号导出
 *
 * 问题背景：
 *   链接脚本（minios.ld）通过 PROVIDE() 定义了各内存段的起止地址符号
 *   （如 _text_start、_bss_end 等），但这些符号本身是"地址标号"，
 *   其值就是段边界地址，不能直接以 extern uint32_t 的方式在 C 中读取数值。
 *
 * 解决方案：
 *   将每个链接脚本符号的地址值用 .word 存入 .rodata 段，并对外声明为
 *   全局符号。C 代码（mem.h）以 extern uint32_t TEXT_START 等方式引用，
 *   解引用后即得到对应段的起始/结束地址数值。
 *
 * 内存布局（由 minios.ld 定义，CH32V307 配置）：
 *   FLASH (rx)  0x00000000 ~ 0x00040000  (256 KB) — 代码和只读数据
 *   RAM   (xrw) 0x20000000 ~ 0x20010000  ( 64 KB) — 数据、BSS、堆、栈
 *
 *   [ .text ][ .rodata ] → FLASH
 *   [ .data ][ .bss ][ heap ][ stack ] → RAM
 *
 * 用法：
 *   C 文件 #include "mem.h" 后直接使用 TEXT_START、HEAP_SIZE 等常量，
 *   它们均为 uint32_t 类型的地址/大小值。
 */

/* 将本文件所有符号放入 .rodata，保证只读且随 .text 一同烧写到 FLASH */
.section .rodata

/* ── .text 段：可执行代码区（位于 FLASH）─────────────────────────── */
.global TEXT_START
TEXT_START: .word _text_start   /* .text 段起始地址 */

.global TEXT_END
TEXT_END: .word _text_end       /* .text 段结束地址 */

/* ── .data 段：已初始化全局/静态变量（运行时位于 RAM）──────────────── */
.global DATA_START
DATA_START: .word _data_start   /* .data 段 VMA（RAM 中运行地址）起始 */

.global DATA_END
DATA_END: .word _data_end       /* .data 段 VMA 结束 */

/* ── .rodata 段：只读常量（位于 FLASH）──────────────────────────── */
.global RODATA_START
RODATA_START: .word _rodata_start  /* .rodata 段起始地址 */

.global RODATA_END
RODATA_END: .word _rodata_end      /* .rodata 段结束地址 */

/* ── .bss 段：未初始化全局/静态变量（位于 RAM，启动时由 start.S 清零）*/
.global BSS_START
BSS_START: .word _bss_start     /* .bss 段起始地址 */

.global BSS_END
BSS_END: .word _bss_end         /* .bss 段结束地址；同时也是堆的起始地址 */

/* ── 堆区：从 _bss_end 到 _stack_start，由 page_alloc/page_free 管理 */
.global HEAP_START
HEAP_START: .word _heap_start   /* 堆起始地址（= _bss_end） */

.global HEAP_SIZE
HEAP_SIZE: .word _heap_size     /* 堆总字节数（= _stack_start - _heap_start） */

/* ── 栈区：RAM 顶部固定大小区域（minios.ld 中 __stack_size = 2048 B）*/
.global STACK_START
STACK_START: .word _stack_start /* 栈底地址（低地址端，栈向高地址增长方向的起点）*/

.global STACK_END
STACK_END: .word _stack_end     /* 栈顶地址（= ORIGIN(RAM) + LENGTH(RAM)） */
