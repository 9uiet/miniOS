/*
 * busybox.c — miniOS 命令集合与主循环
 *
 * 架构概览：
 *   ┌─────────────────────────────────────┐
 *   │  busybox_mainloop()                 │  ← Shell 主循环
 *   │    ├── rl_gets()                    │  调用 readline 读取一行输入
 *   │    ├── strtok() 解析命令名与参数    │
 *   │    └── cmd_table[] 查表分发         │  线性查找，调用对应 handler
 *   └─────────────────────────────────────┘
 *
 * 命令注册：
 *   cmd_table[] 是一个静态命令描述符数组，每项包含：
 *     name        — 命令字符串（用于匹配用户输入）
 *     description — 帮助文本（由 cmd_help 展示）
 *     handler     — 命令处理函数指针，签名为 int handler(char *args)
 *                   返回 < 0 表示请求退出主循环（目前仅 cmd_q 使用）
 *
 * 新增命令只需：
 *   1. 声明并实现 static int cmd_xxx(char *args)
 *   2. 在 cmd_table[] 中追加一条记录
 */
#include "utils/macro.h"
#include "readline.h"
#include "minilib.h"
#include "shell.h"
#include "kernel.h"


/*
 * rl_gets() — 封装 readline，读取一行用户输入
 *
 * 显示提示符 "(miniOS) "，等待用户输入完整一行后返回。
 * 若输入非空，自动将其追加到历史记录，以便方向键回调。
 * 返回指向静态缓冲区的指针；空行或 EOF 时返回 NULL。
 */
static char* rl_gets() {
  static char *line_read = NULL;
  line_read = readline("(miniOS) "); 

  if (line_read && *line_read) {
    add_history(line_read);
  }

  return line_read;
}

/* 命令处理函数前向声明 */
static int cmd_help(char *args);
static int cmd_lscpu(char *args);
static int cmd_free(char *args);
static int cmd_q(char *args);

/*
 * cmd_table[] — 命令描述符表（命令注册中心）
 *
 * 每项结构体字段：
 *   name        — 命令名（用户在提示符后输入的字符串）
 *   description — 简短说明，由 help 命令展示
 *   handler     — 处理函数：int handler(char *args)
 *                 args 指向命令名之后的剩余参数字符串（可为 NULL）
 *                 返回值 < 0 时 busybox_mainloop 退出
 */
static struct {
  const char *name;
  const char *description;
  int (*handler) (char *);
} cmd_table [] = {
  { "help"	, "Display informations about all supported commands", cmd_help  },
  { "lscpu"	, "Display information about the CPU", 								 cmd_lscpu },
  { "free"	, "Display information about the memory", 						 cmd_free  },
  { "q"			, "Exit MiniOS", 																			 cmd_q 		 },
};

/* 命令总数，由宏 ARRLEN 在编译期计算，避免硬编码 */
#define NR_CMD ARRLEN(cmd_table)

/*
 * cmd_help() — 内置 help 命令
 *
 * 无参数时：列出 cmd_table 中所有命令及其描述。
 * 有参数时：查找并打印指定命令的描述；未找到则提示 Unknown command。
 *
 * 注意：此处 strtok(NULL, " ") 延续 busybox_mainloop 中对同一字符串的
 * 分割状态，取出 help 之后的第一个 token 作为目标命令名。
 */
static int cmd_help(char *args) {
	/* extract the first argument */
	char *arg = strtok(NULL, " ");
	int i;

	if (arg == NULL) {
		/* no argument given */
		printf("Common options:\n");
		for (i = 0; i < NR_CMD; i++) {
			printf("\t%-4s - %s\n", cmd_table[i].name, cmd_table[i].description);
		}
	} else {
		for (i = 0; i < NR_CMD; i++) {
			if (strcmp(arg, cmd_table[i].name) == 0) {
				printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
				return 0;
			}
		}
		printf("Unknown command '%s'\n", arg);
	}
	return 0;
}

/*
 * cmd_lscpu() — 内置 lscpu 命令
 *
 * 调用 getarchid() 读取芯片 ID 寄存器，并以十六进制格式打印。
 * getarchid() 定义在 kernel.h/kernel.c，封装了平台相关寄存器访问。
 */
static int cmd_lscpu(char *args) {
  unsigned int archid = -1;
	int ret = -1;
  ret = getarchid(&archid);
  if (ret) {
    printf("getarchid() failed, return: %d\n", ret);
  } else {
    printf("ChipID:%08x\r\n", archid);
  }
  return 0;
}

/*
 * cmd_free() — 内置 free 命令
 *
 * 打印各内存段的起止地址，数据来自 mem.S 导出的链接脚本符号：
 *   TEXT_START/END   — 代码段（FLASH）
 *   RODATA_START/END — 只读数据段（FLASH）
 *   DATA_START/END   — 已初始化数据段（RAM VMA）
 *   BSS_START/END    — 未初始化数据段（RAM）
 *   HEAP_START/SIZE  — 堆区（由 page 分配器管理）
 *   STACK_START/END  — 栈区（RAM 顶部固定区域）
 */
static int cmd_free(char *args) {
  printf("Memory info\n");
  printf("TEXT:   0x%x -> 0x%x\n", TEXT_START, TEXT_END);
  printf("RODATA: 0x%x -> 0x%x\n", RODATA_START, RODATA_END);
  printf("DATA:   0x%x -> 0x%x\n", DATA_START, DATA_END);
  printf("BSS:  	0x%x -> 0x%x\n", BSS_START, BSS_END);
  printf("HEAP:   0x%x -> 0x%x\n", HEAP_START, HEAP_START + HEAP_SIZE);
  printf("STACK:  0x%x -> 0x%x\n", STACK_START, STACK_END);
  return 0;
}

/*
 * cmd_q() — 内置 q（quit）命令
 *
 * 返回 -1 通知 busybox_mainloop 退出主循环，
 * 最终导致 init_shell() 返回并回到内核上下文。
 */
static int cmd_q(char *args) {
  return -1;
}

/*
 * busybox_mainloop() — 命令行主循环（命令解析与分发）
 *
 * 流程：
 *   1. 调用 rl_gets() 获取用户输入行（阻塞直到用户按回车）
 *   2. 用 strtok(str, " ") 将输入拆分为命令名 cmd 和剩余参数 args
 *      - args 指向 cmd 结束符 '\0' 之后的第一个字节；
 *        若 cmd 已到字符串末尾则 args = NULL（无参数）
 *   3. 线性遍历 cmd_table[]，匹配命令名后调用对应 handler(args)
 *      - handler 返回值 < 0 时立即退出循环（由 cmd_q 触发）
 *      - 遍历到最后一项仍未命中时打印 "Unknown command"
 *   4. rl_gets() 返回 NULL（空行）时 for 循环自然退出
 */
void busybox_mainloop() {
	for (char *str; (str = rl_gets()) != NULL; ) { 
		char *str_end = str + strlen(str);

		// Splitting a string into a set of strings
		char *cmd = strtok(str, " ");  /* 取出第一个以空格分隔的 token 作为命令名 */
		if (cmd == NULL) { continue; }

		/* args 指向命令名结束符之后的位置，即剩余参数字符串 */
		char *args = cmd + strlen(cmd) + 1;
		if (args >= str_end) {
			args = NULL;  /* 命令名后无更多内容，参数为空 */
		}

		/* 线性查找 cmd_table，匹配命令名并分发到对应处理函数 */
		for (int i = 0; i < NR_CMD; i ++) {
			if (strcmp(cmd, cmd_table[i].name) == 0) {
				if (cmd_table[i].handler(args) < 0) { return; }  /* handler 返回 <0 则退出 */
				break;
			}
			if (i == NR_CMD - 1) { printf("Unknown command '%s'\n", cmd); }
		}
	}
	// while (1) {
  //   printf("Busybox: Running...\n");
  //   task_delay(1000);
  // }
}
