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
#define USER_VADDR_BOTTOM ((void *) 0x08048000)

#include "threads/init.h"
static void syscall_handler (struct intr_frame *);
void sys_exit (int status);
int sys_write(struct  intr_frame *f);
static bool is_valid_pointer(void * esp, uint8_t argc);
void get_arg (struct intr_frame *f, int *arg, int n);
void check_valid_buffer (void* buffer, unsigned size);
void check_valid_ptr (const void *vaddr);


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

/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  if(!is_user_vaddr(udst))
    return false;
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

void
syscall_init (void) {
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) {
  printf ("system call!\n");
  int systemCall, result = 0;

  if (!is_valid_pointer(f->esp, MAX_ARGS)){
    sys_exit(ERROR);
    return;
  }
  systemCall = * (int *)f->esp;

  switch (systemCall) {
    case SYS_HALT:
      halt();
      break;
    case SYS_WRITE:
      result = sys_write(f);
      break;
    default:
      sys_exit(ERROR);
      break;
  }
  sys_exit(result);
}

static bool is_valid_pointer(void * esp, uint8_t argc){
  for (uint8_t i = 0; i < argc; ++i) {
    if (get_user(((uint8_t *)esp)+i) == -1){
      return false;
    }
  }
  return true;
}

void halt (void) {
  shutdown_power_off();
}

void
sys_exit (int status){
  thread_exit (status);
}

int sys_write(struct intr_frame *f) {
  if (!is_valid_pointer(f->esp + 4, 12)){
    return -1;
  }
  int fd = *(int *)(f->esp + 4);
  void *buffer = *(char**)(f->esp + 8);
  unsigned size = *(unsigned *)(f->esp + 12);
  printf("BUFFER %x\n", (uint8_t *)(buffer+size));
  printf("BASE: %x\n", (uint8_t *)PHYS_BASE);
  printf("COMP: %d\n", (uint8_t *)(buffer) < (uint8_t *)PHYS_BASE);
  printf("FIRST: %d, SECOND %d\n", !is_valid_pointer(buffer, 1), !is_valid_pointer(buffer + size, 1));
  if (!is_valid_pointer(buffer, 1) || !is_valid_pointer(buffer + size, 1)){
    return -1;
  }
  printf("WRITE2\n");
  int written_size = process_write(fd, buffer, size);
  printf("WRITE3\n");
  f->eax = written_size;
  return 0;
}

void check_valid_ptr (const void *vaddr)
{
  if (!is_user_vaddr(vaddr) || vaddr < USER_VADDR_BOTTOM)
    {
      sys_exit(ERROR);
    }
}

void get_arg (struct intr_frame *f, int *arg, int n) {
  int i;
  int *ptr;
  for (i = 0; i < n; i++)
    {
      ptr = (int *) f->esp + i + 1;
      check_valid_ptr(ptr);
      arg[i] = *ptr;
    }
}

void check_valid_buffer (void* buffer, unsigned size)
{
  unsigned i;
  char* local_buffer = (char *) buffer;
  for (i = 0; i < size; i++)
    {
      check_valid_ptr((const void*) local_buffer);
      local_buffer++;
    }
}
