#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <user/syscall.h>
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"

#define ERROR -1
#define MAX_ARGS 4

static void syscall_handler (struct intr_frame *);
void sys_halt (void);
void sys_exit (int status);
void sys_exec (struct  intr_frame *f);
int sys_write(struct  intr_frame *f);
int sys_wait (struct intr_frame *f);
int sys_open(struct intr_frame *f);
int sys_close(struct intr_frame *f);
static bool is_valid_pointer(void * esp, uint8_t argc);


/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  if(!is_user_vaddr(uaddr))
    return -1;
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}

void
syscall_init (void) {
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) {
  int systemCall, result = 0;
  if (!is_valid_pointer(f->esp, MAX_ARGS)){
    sys_exit(ERROR);
    return;
  }
  systemCall = * (int *)f->esp;

  switch (systemCall) {
    case SYS_HALT:
      sys_halt();
      break;
    case SYS_EXEC:
      sys_exec(f);
      break;
    case SYS_WRITE:
      result = sys_write(f);
      break;
    case SYS_WAIT:
      result = sys_wait(f);
      break;
    case SYS_OPEN:
      result = sys_open(f);
      break;
    case SYS_CLOSE:
      result = sys_close(f);
      break;
    case SYS_EXIT:
      if (!is_valid_pointer(f->esp + 4, MAX_ARGS)){
        thread_exit(ERROR);
      }
      result = *((int*)f->esp+1);
      if (result < 0) result = -1;
      sys_exit(result);
      break;
    default:
      sys_exit(ERROR);
      break;
  }
}

static bool is_valid_pointer(void * esp, uint8_t argc) {
  for (uint8_t i = 0; i < argc; ++i) {
    if (get_user(((uint8_t *)esp)+i) == -1){
      return false;
    }
  }
  return true;

}

static bool is_valid_string(void * str) {
  int ch = -1;
  while((ch = get_user((int*)str++)) != '\0' && ch != -1);
    if (ch == '\0') return true;
    else return false;
}

void sys_halt (void) {
  shutdown_power_off();
}

void sys_exit (int status){
  thread_exit (status);
}

void sys_exec (struct  intr_frame *f){
  if (!is_valid_pointer(f->esp + 8, 12)){
    return -1;
  }
  void * page_dir;
  int args[1];
  args[0] = *((int *)f->esp + 1);
  page_dir = pagedir_get_page(thread_current()->pagedir, (const void *) args[0]);
  if (page_dir == NULL){
    return -1;
  }
  args[0] = (int) page_dir;
  f->eax = process_execute((const char *) args[0]);
}

int sys_write(struct intr_frame *f) {
  if (!is_valid_pointer(f->esp + 8, 12)){
    return -1;
  }
  int fd = *(int *)(f->esp + 8);
  void *buffer = *(char**)(f->esp + 24);
  unsigned size = *(unsigned *)(f->esp + 12);

  if (!is_valid_pointer(buffer, 1) || !is_valid_pointer(buffer + size, 1)){
    return -1;
  }
  int written_size = process_write(fd, buffer, size);
  f->eax = written_size;
  return 0;
}

int sys_wait (struct intr_frame *f) {
  pid_t pid;
  if (!is_valid_pointer(f->esp + 4, MAX_ARGS)){
    return ERROR;
  }
  pid =  *(int *)(f->esp + 4);
  f->eax =  process_wait(pid);
  return 0;
}

int sys_open(struct intr_frame *f) {
  if (!is_valid_pointer(f->esp + 4, 4) || !is_valid_string(*(char **)(f->esp + 4))){
    return -1;
  }
  char *str = *(char **)(f->esp + 4);
  f->eax = process_open(str);
  return 0;
}

int sys_close(struct intr_frame *f) {
  if (!is_valid_pointer(f->esp + 4, MAX_ARGS)){
    return -1;
  }
  int fd = *(int *)(f->esp + 4);
  process_close(fd);
  return 0;
}
