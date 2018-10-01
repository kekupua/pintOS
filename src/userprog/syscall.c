#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/init.h"
#include "devices/shutdown.h"
static void syscall_handler (struct intr_frame *);

void
syscall_init (void)
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED)
{
  printf ("system call!\n");
  halt();
  thread_exit ();
}


static void halt(void){
  printf("Halt!\n");
  shutdown_power_off();
}

// pid_t sys_exec(const char *cmd_line){
//   tid_t pid;
//   pid = process_execute(cmd_line);
//   return pid;
// }
