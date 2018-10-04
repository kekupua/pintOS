#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"

#define USER_VADDR_BOTTOM ((void *) 0x08048000)
struct lock filesys_lock;

#include "threads/init.h"
static void syscall_handler (struct intr_frame *);
int write (int fd, const void *buffer, unsigned size);
void exit (int status);
void check_addr (const void *vaddr);
void get_arg (struct intr_frame *f, int *arg, int n);
void check_valid_buffer (void* buffer, unsigned size);

void
syscall_init (void) {
  lock_init(&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) {
  printf ("system call!\n");
  int arg[3];
  int * p = f->esp;
  int system_call = * p;
  check_addr((const void*) f->esp);
  printf("VALUE: %d\n", system_call);
  switch (SYS_HALT) {
    case SYS_HALT:
      halt();
      break;
    // case SYS_WRITE:
    //   get_arg(f, &arg[0], 3);
    //   check_valid_buffer((void *) arg[1], (unsigned) arg[2]);
    //   arg[1] = user_to_kernel_ptr((const void *) arg[1]);
    //   f->eax = write(arg[0], (const void *) arg[1], (unsigned) arg[2]);
    //   break;
  }
  thread_exit ();
}

void halt (void) {
  printf("HALT\n");
  shutdown_power_off();
}

void exit (int status) {
  struct thread *cur = thread_current();
  if (thread_alive(cur->parent))
    {
      cur->cp->status = status;
    }
  printf ("%s: exit(%d)\n", cur->name, status);
  thread_exit();
}

// int write (int fd, const void *buffer, unsigned size){
//   if (fd == STDOUT_FILENO)
//     {
//       putbuf(buffer, size);
//       return size;
//     }
//   lock_acquire(&filesys_lock);
//   struct file *f = process_get_file(fd);
//   if (!f)
//     {
//       lock_release(&filesys_lock);
//       return ERROR;
//     }
//   int bytes = file_write(f, buffer, size);
//   lock_release(&filesys_lock);
//   return bytes;
// }
//
void check_addr (const void *vaddr)
{
  if (!is_user_vaddr(vaddr) || vaddr < USER_VADDR_BOTTOM)
    {
      exit(-1);
    }
}
//
// struct child_process* add_child_process (int pid) {
//   struct child_process* cp = malloc(sizeof(struct child_process));
//   cp->pid = pid;
//   cp->load = 0;
//   cp->wait = false;
//   cp->exit = false;
//   lock_init(&cp->wait_lock);
//   list_push_back(&thread_current()->child_list,
// 		 &cp->elem);
//   return cp;
// }
//
// struct child_process* get_child_process (int pid){
//   struct thread *t = thread_current();
//   struct list_elem *e;
//
//   for (e = list_begin (&t->child_list); e != list_end (&t->child_list);
//        e = list_next (e))
//         {
//           struct child_process *cp = list_entry (e, struct child_process, elem);
//           if (pid == cp->pid)
// 	    {
// 	      return cp;
// 	    }
//         }
//   return NULL;
// }
//
// void remove_child_process (struct child_process *cp){
//   list_remove(&cp->elem);
//   free(cp);
// }
//
// void get_arg (struct intr_frame *f, int *arg, int n) {
//   int i;
//   int *ptr;
//   for (i = 0; i < n; i++)
//     {
//       ptr = (int *) f->esp + i + 1;
//       check_addr((const void *) ptr);
//       arg[i] = *ptr;
//     }
// }
//
// void check_valid_buffer (void* buffer, unsigned size)
// {
//   unsigned i;
//   char* local_buffer = (char *) buffer;
//   for (i = 0; i < size; i++)
//     {
//       check_addr((const void*) local_buffer);
//       local_buffer++;
//     }
// }
//
// int user_to_kernel_ptr(const void *vaddr)
// {
//   // TO DO: Need to check if all bytes within range are correct
//   // for strings + buffers
//   check_addr(vaddr);
//   void *ptr = pagedir_get_page(thread_current()->pagedir, vaddr);
//   if (!ptr)
//     {
//       exit(ERROR);
//     }
//   return (int) ptr;
// }
//

// pid_t sys_exec(const char *cmd_line){
//   tid_t pid;
//   pid = process_execute(cmd_line);
//   return pid;
// }
