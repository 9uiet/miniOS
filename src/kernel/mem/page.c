#include <stddef.h>
#include <stdarg.h>
#include "types.h"
#include "minilib.h"
#include "mem.h"

/*
 * page.c — first-fit 页描述符分配器
 *
 * 内存布局（以 CH32V307 64 KB RAM 为例）：
 *
 *   HEAP_START
 *   ┌──────────────────────────────────┐
 *   │  页描述符数组（Page Descriptor）  │  ← PAGE_RESERVED 个 PAGE_SIZE
 *   │  每个 struct page 占 1 字节       │    用于存放描述符，防止与数据区重叠
 *   ├──────────────────────────────────┤  ← _alloc_start（4 KB 对齐）
 *   │  可分配物理页帧区（Page Frames）  │
 *   │  共 _num_pages 个 PAGE_SIZE 页    │
 *   └──────────────────────────────────┘  ← _alloc_end
 *
 * 数据结构：
 *   struct page — 单字节描述符，两个标志位：
 *     PAGE_TAKEN (bit0)：该页已被分配
 *     PAGE_LAST  (bit1)：该页是某次分配的最后一页（用于 page_free 定位边界）
 *
 * 分配策略：first-fit（首次适应）
 *   page_alloc(n) 从描述符数组头部开始线性扫描，找到第一段连续 n 个空闲
 *   描述符后，标记并返回对应物理地址。
 *
 * 接口：
 *   page_init()        — 初始化描述符数组与堆边界
 *   page_alloc(npages) — 分配 npages 个连续物理页，返回起始虚拟地址
 *   page_free(p)       — 释放以 p 为起始地址的物理页块
 */

/*
 * _alloc_start points to the actual start address of heap pool
 * _alloc_end points to the actual end address of heap pool
 * _num_pages holds the actual max number of pages we can allocate.
 */
static uint32_t _alloc_start = 0;
static uint32_t _alloc_end = 0;
static uint32_t _num_pages = 0;

#define PAGE_SIZE 4096 /* 4KB */
#define PAGE_MASK (~(PAGE_SIZE - 1))

/*
 * PAGE_RESERVED — 保留给页描述符数组本身的页数
 * 描述符数组从 HEAP_START 开始存放，占用 PAGE_RESERVED 个 PAGE_SIZE 空间，
 * 实际可分配页帧从 HEAP_START + PAGE_RESERVED * PAGE_SIZE（对齐后）开始。
 */
#define PAGE_RESERVED 1

/* 页描述符标志位 */
#define PAGE_TAKEN (uint8_t)(1 << 0)  /* bit0：该页已被分配 */
#define PAGE_LAST  (uint8_t)(1 << 1)  /* bit1：该页是本次分配块的最后一页 */

/*
 * Page Descriptor
 * flags:
 * - bit 0: flag if this page is taken(allocated)
 * - bit 1: flag if this page is the last page of the memory block allocated
 */
struct page {
  uint8_t flags;
};

/* 将描述符的 flags 全部清零，标记为空闲 */
static inline void _clear_page(struct page *page) {
  page->flags = 0;
}

/*
 * Check if the page is free
 */
static inline int _is_page_free(struct page *page) {
  return !(page->flags & PAGE_TAKEN);
}

/* 在描述符中置位指定标志（不影响其他位） */
static inline void _set_page_flag(struct page *page, uint8_t flag) {
  page->flags |= flag;
}

/* 检查该描述符是否标记为某次分配的最后一页 */
static inline int _is_page_last(struct page *page) {
  return page->flags & PAGE_LAST;
}

/*
 * align the address to the border of page(4K).
 */
static inline uint32_t _align_page(uint32_t addr) {
  /* 
   * Add PAGE_SIZE - 1 to the address. This increases the address
   * to the next page boundary if it's not already aligned.
   * Then perform a bitwise AND operation with PAGE_MASK to
   * "rounds down" the address to the nearest page boundary.
   */
  return (addr + PAGE_SIZE - 1) & PAGE_MASK;
}

/*
 * page_init() — 初始化 first-fit 页分配器
 *
 * 步骤：
 *   1. 计算可管理的页数 _num_pages：
 *        堆总页数 = HEAP_SIZE / PAGE_SIZE
 *        减去 PAGE_RESERVED（描述符数组自身占用）和 1（防止堆栈重叠）
 *   2. 将描述符数组起始于 HEAP_START，每个 struct page 对应一个物理页帧，
 *      初始时全部清零（空闲）。
 *   3. 计算实际可分配起始地址 _alloc_start：
 *        HEAP_START + PAGE_RESERVED * PAGE_SIZE，再向上对齐到 4 KB 边界。
 *   4. 计算 _alloc_end = _alloc_start + _num_pages * PAGE_SIZE。
 *
 * 内存示意（HEAP_START = _bss_end，大小由链接脚本决定）：
 *   [HEAP_START ... _alloc_start)  — 存放 struct page 描述符数组
 *   [_alloc_start ... _alloc_end)  — 实际物理页帧，由 page_alloc/page_free 管理
 */
void page_init() {
  /* 
   * We reserved PAGE_RESERVED pages to hold the Page structures.
   * Sub more 1 to avoid overlapping heap and stack.
  */
  _num_pages = (HEAP_SIZE / PAGE_SIZE) - PAGE_RESERVED - 1;

  printf("HEAP_START = %x, HEAP_SIZE = %x, num of pages = %d\n",
     HEAP_START, HEAP_SIZE, _num_pages);

  /* 清零所有页描述符，将它们初始化为空闲状态 */
  struct page *page = (struct page *)HEAP_START;
  for (int i = 0; i < _num_pages; i++) {
    _clear_page(page);
    page++;
  }

  /* 描述符区之后的第一个 4 KB 对齐地址作为可分配区起点 */
  _alloc_start = _align_page(HEAP_START + PAGE_RESERVED * PAGE_SIZE);
  _alloc_end = _alloc_start + _num_pages * PAGE_SIZE;

  printf("TEXT:   0x%x -> 0x%x\n", TEXT_START, TEXT_END);
  printf("RODATA: 0x%x -> 0x%x\n", RODATA_START, RODATA_END);
  printf("DATA:   0x%x -> 0x%x\n", DATA_START, DATA_END);
  printf("BSS:    0x%x -> 0x%x\n", BSS_START, BSS_END);
  printf("HEAP:   0x%x -> 0x%x\n", _alloc_start, _alloc_end);
  printf("STACK:  0x%x -> 0x%x\n", STACK_START, STACK_END);
}

/*
 * page_alloc() — first-fit 连续物理页分配
 *
 * 参数：
 *   npages — 需要分配的连续页数（每页 PAGE_SIZE = 4 KB）
 *
 * 返回：
 *   成功：分配块第一个页帧的起始虚拟地址（位于 [_alloc_start, _alloc_end)）
 *   失败：NULL（参数非法或没有足够连续空闲页）
 *
 * 算法（first-fit）：
 *   从描述符数组头部（对应 _alloc_start）线性扫描：
 *   ① 找到一个空闲描述符 page_i（对应第 i 个物理页帧）
 *   ② 继续检查其后 npages-1 个描述符是否也全部空闲
 *   ③ 若是，将这 npages 个描述符全部标记 PAGE_TAKEN，
 *      最后一个额外标记 PAGE_LAST，然后返回对应物理地址：
 *        _alloc_start + i * PAGE_SIZE
 *   ④ 若中途遇到已占用描述符，重置 found 并继续外层循环
 *
 * Allocate a memory block which is composed of contiguous physical pages
 * - npages: the number of PAGE_SIZE pages to allocate
 */
void *page_alloc(int npages) {
  if (npages <= 0 || _num_pages < npages) {
    return NULL;
  }

  /* Note we are searching the page descriptor bitmaps. */
  int found = 0;
  struct page *page_i = (struct page *)HEAP_START;
  for (int i = 0; i <= (_num_pages - npages); i++) {
    if (_is_page_free(page_i)) {
      found = 1;
      /* 
       * meet a free page, continue to check if the following
       * (npages - 1) pages are also unallocated.
       */
      struct page *page_j = page_i + 1;
      for (int j = i + 1; j < i + npages; j++) {
        if (!_is_page_free(page_j)) {
          found = 0;  /* 连续段不足，重新向后搜索 */
          break;
        }
        page_j++;
      }
      /*
       * get a memory block which is good enough for us,
       * take housekeeping, then return the actual start
       * address of the first page of this memory block.
       */
      if (found) {
        /* 将分配块内所有描述符标记为 PAGE_TAKEN */
        struct page *page_k = page_i;
        for (int k = i; k < i + npages; k++) {
          _set_page_flag(page_k, PAGE_TAKEN);
          page_k++;
        }
        /* 最后一个描述符额外标记 PAGE_LAST，供 page_free() 确定块边界 */
        page_k--;
        _set_page_flag(page_k, PAGE_LAST);
        /* 返回对应物理页帧的起始地址 */
        return (void *)(_alloc_start + i * PAGE_SIZE);
      }
    }
    page_i++;
  }
  return NULL;  /* 无法找到足够连续的空闲页 */
}

/*
 * page_free() — 释放由 page_alloc() 分配的物理页块
 *
 * 参数：
 *   p — page_alloc() 返回的页帧起始地址
 *
 * 实现：
 *   1. 由物理地址 p 反推描述符索引：
 *        index = (p - _alloc_start) / PAGE_SIZE
 *   2. 从该描述符开始向后遍历，将每个描述符清零（标记空闲），
 *      直到遇到 PAGE_LAST 标志的描述符为止（含该描述符）。
 *      这样无需显式存储块长度，PAGE_LAST 即为块终止标记。
 *
 * Free the memory block
 * - p: start address of the memory block
 */
void page_free(void *p) {
  /* 
   * Assert (TBD) if p is invalid
   */
  if (!p || (uint32_t)p >= _alloc_end) {
    return;
  }
  /* get the first page descriptor of this memory block */
  /* 由物理地址反推对应描述符：HEAP_START 处存有描述符数组，偏移量 = 页帧索引 */
  struct page *page = (struct page *)HEAP_START;
  page += ((uint32_t)p - _alloc_start) / PAGE_SIZE;
  /* loop and clear all the page descriptors of the memory block */
  /* 逐一清零描述符，遇到 PAGE_LAST 标记时说明已到块末尾，清零后退出 */
  while (!_is_page_free(page)) {
    _clear_page(page);
    if (_is_page_last(page)) {
      break;
    }
    page++;
  }
}
