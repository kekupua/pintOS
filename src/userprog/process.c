#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "threads/malloc.h"
#include "userprog/ipc.h"
#include "vm/page.h"
#include "vm/frame.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);

static void extract_command_name(char * cmd_string, char *command_name);
static void extract_command_args(char * cmd_string, char* argv[], int *argc);
void process_close_all(void);


struct process_pid{
  int pid;
  struct list_elem elem;
};

void process_init(void)
{
  ipc_init();
  frame_init();
  // the kernel main thread(process) can have children
  list_init(&process_current()->children);
}

// abstraction over thread calls, to separate the two notions of process and thread despite that a process is mono-threaded in pintos.
pid_t process_pid(void)
{
  return thread_tid();
}
struct thread * process_current(void)
{
  return thread_current();
}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
pid_t
process_execute (const char *file_name)
{
  char *fn_copy;
  pid_t tid;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy = palloc_get_page (0);
  if (fn_copy == NULL)
    return TID_ERROR;
  strlcpy (fn_copy, file_name, PGSIZE);

  // make another copy of file name, which will be saved as a property in the process structure
  char *cmd_name = malloc (strlen(fn_copy)+1);
  if (cmd_name == NULL)
    return TID_ERROR;
  extract_command_name(fn_copy, cmd_name);
  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (file_name, PRI_DEFAULT, start_process, fn_copy);
  if (tid == TID_ERROR){
    palloc_free_page (fn_copy);
    free(cmd_name);
    return -1;
  }
  // update thread with userprog properties
  struct thread *t = thread_get(tid);
  t->next_fd = 2;
  t->prog_name = cmd_name;
  list_init(&t->desc_table);
  list_init(&t->children);

  int status = ipc_pipe_read("exec", tid);
  if (status != -1){
    // add the process as a child
    struct process_pid *p = malloc(sizeof(struct process_pid));
    p->pid = status;
    list_push_back(&process_current()->children, &p->elem);
  }
  return status;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  page_init();

  char *file_name = file_name_;

  struct intr_frame if_;
  bool success;

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (file_name, &if_.eip, &if_.esp);

  palloc_free_page (file_name);

  /* If load failed, quit. */
  if (!success){
    ipc_pipe_write("exec", process_pid(), -1);
    thread_exit (-1);
  }
  ipc_pipe_write("exec", process_pid(), process_pid());  
  
  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

static bool process_is_parent_of(pid_t pid){
  struct list_elem *e; 
  for (e = list_begin (&process_current()->children); e != list_end (&process_current()->children);
       e = list_next (e))
    {
      if(list_entry(e, struct process_pid, elem)->pid == pid){
        return true;
      };
    }
  return false;
}

static void remove_child(pid_t pid){
  struct list_elem *e = NULL; 
  for (e = list_begin (&process_current()->children); e != list_end (&process_current()->children);
       e = list_next (e))
    {
      if(list_entry(e, struct process_pid, elem)->pid == pid){
        break;
      };
    }
  if (e != NULL)
    list_remove(e);
}
/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */
int
process_wait (pid_t child_tid)
{
  if(!process_is_parent_of(child_tid))
    return -1;
  remove_child(child_tid); // hack: remove the child from a process children list to make sure a process can't wait for a child twice
  return ipc_pipe_read("wait", child_tid);
}


/* Free the current process's resources. */
void
process_exit (int status)
{
  struct thread *cur = process_current();
  ipc_pipe_write("wait", cur->tid, status);
  // return if it's a kernel thread
  if(process_pid() == 1){
    return;
  }
  // close open descriptors;
  process_close_all();
  printf("%s: exit(%d)\n", cur->prog_name, status);

  uint32_t *pd;

  //TODO: de-allocate 'children' and 'prog_name' attributes

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL)
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}


/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = process_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp, char **argv, int argc);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp)
{

  struct thread *t = process_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /*
    extract command line
   */
  // create a copy of file_name and operate on it (modifying it)
  char file_name_copy[CMD_LENGTH_MAX];
  strlcpy(file_name_copy, file_name, CMD_LENGTH_MAX);
  char *argv[CMD_ARGS_MAX];
  int argc;
  extract_command_args(file_name_copy, argv, &argc);

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL)
    goto done;
  process_activate ();

  /* Open executable file. */
  file = filesys_open (argv[0]);
  if (file == NULL)
    {
      printf ("load: %s: open failed\n", argv[0]);
      goto done;
    }

  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024)
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done;
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++)
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type)
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file))
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp, argv, argc))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
 if(success){
    process_current()->executable = file;
    // deny write to executables
    file_deny_write(file);
  }else
    file_close(file);
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file)
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
    return false;

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file))
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz)
    return false;

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;

  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}
 

 static bool load_page(struct file *file, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, off_t ofs, bool writable){

  /* Get a page of memory. */
  uint8_t *kpage = frame_get_page (false);
  if (kpage == NULL)
    PANIC("cannot allocate a free page");
  file_seek (file, ofs);
  /* Load this page. */
  if (file_read (file, kpage, read_bytes) != (int) read_bytes)
    {
      palloc_free_page (kpage);
      PANIC("cannot read the file");
    }
  memset (kpage + read_bytes, 0, zero_bytes);

  /* Add the page to the process's address space. */
  if (!install_page (upage, kpage, writable))
    {
      palloc_free_page (kpage);
      PANIC("cannot install the page");
    }
  return true;
}
static void file_page_fault_handler(void *upage, void *args)
{
  struct file *file = ((struct file **)args)[0];
  uint32_t read_bytes = ((uint32_t *)args)[1];
  uint32_t zero_bytes = ((uint32_t *)args)[2];
  off_t ofs = ((uint32_t *)args)[3];
  bool writable = ((uint32_t *)args)[4];

  load_page(file, upage, read_bytes, zero_bytes, ofs, writable);
} 

// load a page starting from the current offset of the file
static bool
lazy_load_page (struct file *file, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, off_t ofs, bool writable)
{
  // make sure it's only one page
  ASSERT ((read_bytes + zero_bytes) == PGSIZE);

  uint32_t* aux = malloc(20);
  ((struct file **)aux)[0] = file;
  aux[1] = read_bytes;
  aux[2] = zero_bytes;
  aux[3] = ofs;
  aux[4] = writable;
  page_lazy_load(upage, file_page_fault_handler, aux);
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable)
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  while (read_bytes > 0 || zero_bytes > 0)
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;
      if(! lazy_load_page(file, upage, page_read_bytes, page_zero_bytes, ofs, writable)){
        return false;
      }

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      upage += PGSIZE;
      ofs += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack (void **esp, char **argv, int argc)
{
  uint8_t *kpage;
  bool success = false;

  kpage = frame_get_page (true);
  if (kpage != NULL)
    {
      success = install_page (((uint8_t *) PHYS_BASE) - PGSIZE, kpage, true);
      if (success) {
        *esp = PHYS_BASE;
        int i = argc;
        // this array holds reference to differences arguments in the stack
        uint32_t * arr[argc];
        while(--i >= 0)
        {
          *esp = *esp - (strlen(argv[i])+1)*sizeof(char);
          arr[i] = (uint32_t *)*esp;
          memcpy(*esp,argv[i],strlen(argv[i])+1);
        }
        *esp = *esp - 4;
        (*(int *)(*esp)) = 0;//sentinel
        i = argc;
        while( --i >= 0)
        {
          *esp = *esp - 4;//32bit
          (*(uint32_t **)(*esp)) = arr[i];
        }
        *esp = *esp - 4;
        (*(uintptr_t  **)(*esp)) = (*esp+4);
        *esp = *esp - 4;
        *(int *)(*esp) = argc;
        *esp = *esp - 4;
        (*(int *)(*esp))=0;

      }else
        palloc_free_page (kpage);
    }
  return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = process_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}


static void
extract_command_name(char * cmd_string, char *command_name)
{
  char *save_ptr;
  strlcpy (command_name, cmd_string, PGSIZE);
  command_name = strtok_r(command_name, " ", &save_ptr);
}

static void
extract_command_args(char * cmd_string, char* argv[], int *argc)
{
  char *save_ptr;
  argv[0] = strtok_r(cmd_string, " ", &save_ptr);
  char *token;
  *argc = 1;
  while((token = strtok_r(NULL, " ", &save_ptr))!=NULL)
  {
    argv[(*argc)++] = token;
  }
}


// File descriptor manager

static int allocate_fd (void)
{
  return process_current()->next_fd++;
}

struct fd_entry
{
  int fd;
  struct file *file;
  struct list_elem elem;
};

static struct fd_entry* get_fd_entry(int fd)
{
  struct list_elem *e;
  struct fd_entry *fe = NULL;
  struct list *fd_table = &process_current()->desc_table;

  for (e = list_begin (fd_table); e != list_end (fd_table);
       e = list_next (e))
    {
      struct fd_entry *tmp = list_entry (e, struct fd_entry, elem);
      if(tmp->fd == fd){
        fe = tmp;
        break;
      }
    }

  return fe;
}

int process_open (const char *file_name)
{
  struct file * f = filesys_open (file_name);
  if (f == NULL)
    return -1;
  struct fd_entry *fd_entry = malloc (sizeof(struct fd_entry));
  if (fd_entry == NULL)
    return -1;
  fd_entry->fd = allocate_fd();
  fd_entry->file = f;
  list_push_back(&process_current()->desc_table, &fd_entry->elem);

  return fd_entry->fd;
}

int process_write(int fd, const void *buffer, unsigned size)
{
  if (fd == STDOUT_FILENO){
    putbuf((char *)buffer, (size_t)size);
    return (int)size;
  }else if (get_fd_entry(fd) != NULL){
    return (int)file_write(get_fd_entry(fd)->file, buffer, size);
  }
  return -1;
}

void process_close (int fd)
{
  if (get_fd_entry(fd) != NULL){
    struct fd_entry *fd_entry = get_fd_entry(fd);
    file_close(fd_entry->file);
    list_remove(&fd_entry->elem);
    free(fd_entry);
  }
}

int process_read (int fd, void *buffer, unsigned length)
{
  if (get_fd_entry(fd) != NULL){
    struct fd_entry *fd_entry = get_fd_entry(fd);
    return file_read(fd_entry->file, buffer, length);
  }
  return -1;
}

int process_filesize (int fd)
{
  if (get_fd_entry(fd) != NULL){
    struct fd_entry *fd_entry = get_fd_entry(fd);
    return file_length(fd_entry->file);
  }
  return -1;
}

int process_tell (int fd)
{
  if (get_fd_entry(fd) != NULL){
    struct fd_entry *fd_entry = get_fd_entry(fd);
    return file_tell(fd_entry->file);
  }
  return -1;
}
void process_seek (int fd, unsigned position){
  if (get_fd_entry(fd) != NULL){
    struct fd_entry *fd_entry = get_fd_entry(fd);
    file_seek(fd_entry->file, position);
  }
}

// close all open files (including the executable)
void process_close_all(void)
{
  struct list *fd_table = &process_current()->desc_table;
  struct list_elem *e = list_begin (fd_table);
  while (e != list_end (fd_table))
    {
      struct fd_entry *tmp = list_entry (e, struct fd_entry, elem);
      e = list_next (e);
      process_close(tmp->fd);
    }
  // close the executable
  file_close (process_current()->executable);
}