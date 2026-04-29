/*
 * readline/readline.c — 行编辑器
 *
 * 提供两个对外接口（声明于 readline.h）：
 *   readline(prompt)   — 显示提示符，读取并编辑一行用户输入，返回该行字符串
 *   add_history(line)  — 将一行字符串追加到历史缓冲区
 *
 * 历史记录：
 *   使用固定大小的二维数组 history[MAX_HISTORY][MAX_LINE_LEN] 存储历史。
 *   当历史条目达到上限 MAX_HISTORY 时，采用"循环覆盖"策略：
 *   将 history[0..MAX_HISTORY-2] 整体向前移位一格，新条目写入末尾，
 *   从而始终保留最近 MAX_HISTORY 条历史。
 *   history_pos 追踪当前"历史浏览光标"，每次新命令提交后重置到末尾。
 *
 * 行编辑（readline 内部）：
 *   使用 stdio_has_char() / stdio_popc() 轮询底层串口，逐字符处理：
 *   ┌────────────────────┬──────────────────────────────────────────────────┐
 *   │ 输入               │ 处理动作                                          │
 *   ├────────────────────┼──────────────────────────────────────────────────┤
 *   │ '\n' / '\r'        │ 结束输入；非空行同步写入历史并返回                  │
 *   │ '\b' / 0x7F (DEL)  │ 退格：pos-- 并发送 "\b \b" 擦除终端显示            │
 *   │ ESC [ A (↑)        │ 历史上翻：history_pos-- → 清除当前行 → 回显历史    │
 *   │ ESC [ B (↓)        │ 历史下翻：history_pos++ → 清除当前行 → 回显历史    │
 *   │                    │  （pos==history_count 时显示空行）                 │
 *   │ 可打印字符(32~126) │ 追加到 line[pos++] 并回显                          │
 *   └────────────────────┴──────────────────────────────────────────────────┘
 *
 * 注意：
 *   - line[] 为 static 局部变量，每次调用 readline 均复用同一缓冲区，
 *     调用方在下次调用 readline 之前必须完成对返回字符串的处理或拷贝。
 *   - ESC 序列处理仅覆盖标准 VT100 上/下方向键，不支持左右移动光标。
 */
#include "minilib.h"
#include "readline.h"
#include "platform.h"

/* 历史记录最多保存条数 */
#define MAX_HISTORY 100
/* 每行最大字符数（含结束符 '\0'） */
#define MAX_LINE_LEN 256

/* 历史记录存储数组及相关索引 */
static char history[MAX_HISTORY][MAX_LINE_LEN];
static int history_count = 0;  /* 当前已存历史条数（0 ~ MAX_HISTORY） */
static int history_pos = 0;    /* 历史浏览光标，指向"当前显示的历史条目"索引 */

/*
 * add_history() — 将一行输入追加到历史缓冲区
 *
 * 若历史未满（history_count < MAX_HISTORY），直接写入并递增计数。
 * 若历史已满，将 history[1..MAX_HISTORY-1] 整体前移一格（FIFO 溢出策略），
 * 覆盖最旧的一条，然后将新行写入 history[MAX_HISTORY-1]。
 * 每次调用后将 history_pos 重置到末尾，以便下次输入时方向键从最新历史开始。
 */
void add_history(const char *line) {
  if (history_count < MAX_HISTORY) {
    strcpy(history[history_count], line);
    history_count++;
  } else {
    // If history is full, remove the oldest record
    for (int i = 0; i < MAX_HISTORY - 1; i++) {
      strcpy(history[i], history[i + 1]);
    }
    strcpy(history[MAX_HISTORY - 1], line);
  }
  /* 重置浏览光标到末尾，下次按 ↑ 时从最新记录开始回溯 */
  history_pos = history_count;
}

/*
 * readline() — 交互式行编辑器
 *
 * 参数：
 *   prompt — 显示在输入行前的提示字符串（如 "(miniOS) "）
 *
 * 返回：
 *   指向内部静态缓冲区 line[] 的指针（输入非空时）
 *   NULL（用户直接按回车，输入为空）
 *
 * 实现细节见文件头注释中的处理动作表。
 */
char *readline(const char *prompt) {
  static char line[MAX_LINE_LEN];  /* 静态缓冲区，跨调用复用 */
  int pos = 0;   /* 当前光标在 line[] 中的位置（也等于已输入字符数） */
  char c;

  printf("%s", prompt);
  
  while (1) {
    // Wait for any character
    while (!stdio_has_char()) {
      /* 轮询等待串口有字符可读 */
    }
    
    c = stdio_popc();  /* 从串口缓冲区取出一个字符 */
    
    // Handle enter key
    if (c == '\n' || c == '\r') {
      printf("\n");
      line[pos] = '\0';  /* NUL 终止字符串 */
      if (pos > 0) {
        add_history(line);  /* 非空行写入历史 */
      }
      return pos > 0 ? line : NULL;
    }
    
    // Handle backspace
    if (c == '\b' || c == 127) {
      if (pos > 0) {
        pos--;
        /* 向终端发送 "\b \b"：光标左移→覆盖为空格→再左移，擦除末尾字符显示 */
        printf("\b \b");
      }
      continue;
    }
    
    // Handle up arrow (history)
    if (c == 27) {  /* ESC — VT100 转义序列起始字节 */
      if (stdio_has_char()) {
        c = stdio_popc();
        if (c == '[') {  /* CSI（Control Sequence Introducer），第二字节 */
          if (stdio_has_char()) {
            c = stdio_popc();  /* 第三字节决定具体功能 */
            if (c == 'A') {  // Up arrow — 向历史较旧方向移动
              if (history_pos > 0) {
                history_pos--;
                // Clear current line
                /* 用退格序列逐字符擦除当前已显示的输入内容 */
                for (int i = 0; i < pos; i++) {
                  printf("\b \b");
                }
                strcpy(line, history[history_pos]);
                pos = strlen(line);
                printf("%s", line);  /* 回显历史行 */
              }
              continue;
            }
            if (c == 'B') {  // Down arrow — 向历史较新方向移动
              if (history_pos < history_count) {
                history_pos++;
                // Clear current line
                /* 擦除当前显示内容 */
                for (int i = 0; i < pos; i++) {
                  printf("\b \b");
                }
                if (history_pos < history_count) {
                  strcpy(line, history[history_pos]);
                  pos = strlen(line);
                  printf("%s", line);  /* 回显更新的历史行 */
                } else {
                  /* 已超过最新历史，显示空行（相当于清空输入） */
                  pos = 0;
                  line[0] = '\0';
                }
              }
              continue;
            }
          }
        }
      }
    }
    
    // Handle normal characters
    /* 只接受可打印 ASCII（0x20 ~ 0x7E），防止缓冲区溢出 */
    if (pos < MAX_LINE_LEN - 1 && c >= 32 && c <= 126) {
      line[pos++] = c;
      printf("%c", c);  /* 本地回显 */
    }
  }
}

