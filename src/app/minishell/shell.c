/*
 * shell.c — miniOS Shell 入口
 *
 * 职责：
 *   1. 打印 LOGO 和欢迎信息（welcome）
 *   2. 调用 busybox_mainloop() 进入命令交互主循环
 *
 * 调用链：
 *   内核初始化完成后 → init_shell()
 *                        ├── welcome()        打印 LOGO + 提示
 *                        └── busybox_mainloop() 命令行主循环（busybox.c）
 */
#include "logo.h"
#include "shell.h"
#include "minilib.h"

/* 打印启动 LOGO 和简单使用提示 */
static void welcome(const char *logo) {
  printf("%s", logo);
  printf("For help, type \"help\"\n");
}

/*
 * init_shell() — Shell 初始化入口，由内核在系统启动完成后调用
 *
 * 步骤：
 *   1. 调用 welcome() 输出欢迎界面
 *   2. 进入 busybox_mainloop()，该函数持续读取用户输入并分发到对应命令处理器，
 *      直到用户输入 "q" 命令才返回。
 */
void init_shell() {
	welcome(logo);
  busybox_mainloop();
}
