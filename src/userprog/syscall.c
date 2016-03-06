#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "lib/debug.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "lib/user/syscall.h"
#include "userprog/process.h"
#include "threads/malloc.h"

#include "vm/page.h"
#include "userprog/exception.h"

bool debug_mode = false;

static void syscall_handler(struct intr_frame *);

/* Checking pointer validity */
static void *valid_pointer(void **pointer, int size, struct intr_frame *f);
static void *valid_numeric(void *addr, int size);

/* Conversion functions */
static void *validate_arg(void *addr, enum conversion_type ct, int size,
                          struct intr_frame *f);

/* Specific handlers */
void sys_halt(void);
void sys_exit(int status);
pid_t sys_exec(const char *cmd_line);
int sys_wait(pid_t pid);
bool sys_create(const char *file, unsigned initial_size);
bool sys_remove(const char *file);
int sys_filesize(int fd);
int sys_read(int fd, void *buffer, unsigned size);
int sys_write(int fd, const void *buffer, unsigned size);
void sys_seek(int fd, unsigned position);
unsigned sys_tell(int fd);
mapid_t sys_mmap(int fd, void *addr);
void sys_munmap(mapid_t mapping);

/* Helper functions */
struct fd_elem *get_file(int fd);
bool can_write(void *buffer, int size);

void syscall_init(void) {
    lock_init(&file_lock);
    intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler(struct intr_frame *f) {

    /* Getting the callers stack pointer */
    void *caller_esp = f->esp;
    int call_number = * (int *) validate_arg(caller_esp, CONVERT_NUMERIC,
                                             sizeof(int), f);
    if (debug_mode)
        printf("The stack pointer: %p\n", f->esp);

    /* Getting the args, the calls at most have 3 */
    void *arg0 = f->esp + 4;
    void *arg1 = f->esp + 8;
    void *arg2 = f->esp + 12;

    /* Some variables to put dereferenced args into. */
    int int_arg;
    const char *cchar_arg;
    unsigned unsigned_arg;
    void *void_arg;
    const void *cvoid_arg;

    if (debug_mode) {
        printf("system call!: %d\n", call_number);
    }

    /* Calling the appropriate handler */
    switch (call_number) {
        case SYS_HALT:
            sys_halt();
            break;
        case SYS_EXIT:
            int_arg = * (int *) validate_arg(arg0, CONVERT_NUMERIC,
                                             sizeof(int), f);
            sys_exit(int_arg);
            break;
        case SYS_EXEC:
            cchar_arg = * (const char **) validate_arg(arg0, CONVERT_POINTER,
                                                       -1, f);
            f->eax = sys_exec(cchar_arg);
            break;
        case SYS_WAIT:
            int_arg = * (int *) validate_arg(arg0, CONVERT_NUMERIC,
                                             sizeof(int), f);
            f->eax = sys_wait(int_arg);
            break;
        case SYS_CREATE:
            cchar_arg = * (const char **) validate_arg(arg0, CONVERT_POINTER,
                                                       -1, f);
            unsigned_arg = * (unsigned *) validate_arg(arg1, CONVERT_NUMERIC,
                                                       sizeof(unsigned), f);
            f->eax = sys_create(cchar_arg, unsigned_arg);
            break;
        case SYS_REMOVE:
            cchar_arg = * (const char **) validate_arg(arg0, CONVERT_POINTER,
                                                       -1, f);
            f->eax = sys_remove(cchar_arg);
            break;
        case SYS_FILESIZE:
            int_arg = * (int *) validate_arg(arg0, CONVERT_NUMERIC,
                                             sizeof(int), f);
            f->eax = sys_filesize(int_arg);
            break;
        case SYS_OPEN:
            cchar_arg = * (const char **) validate_arg(arg0, CONVERT_POINTER,
                                                       -1, f);
            f->eax = sys_open(cchar_arg);
            break;
        case SYS_READ:
            int_arg = * (int *) validate_arg(arg0, CONVERT_NUMERIC,
                                             sizeof(int), f);
            unsigned_arg = * (unsigned *) validate_arg(arg2, CONVERT_NUMERIC,
                                                       sizeof(unsigned), f);
            void_arg = * (void **) validate_arg(arg1, CONVERT_POINTER,
                                                unsigned_arg, f);
            f->eax = sys_read(int_arg, void_arg, unsigned_arg);
            break;
        case SYS_WRITE:
            int_arg = * (int *) validate_arg(arg0, CONVERT_NUMERIC,
                                             sizeof(int), f);
            unsigned_arg = * (unsigned *) validate_arg(arg2, CONVERT_NUMERIC,
                                                       sizeof(unsigned), f);
            cvoid_arg = * (const void **) validate_arg(arg1, CONVERT_POINTER,
                                                       unsigned_arg, f);
            f->eax = sys_write(int_arg, cvoid_arg, unsigned_arg);
            break;
        case SYS_SEEK:
            int_arg = * (int *) validate_arg(arg0, CONVERT_NUMERIC,
                                             sizeof(int), f);
            unsigned_arg = * (unsigned *) validate_arg(arg1, CONVERT_NUMERIC,
                                                       sizeof(unsigned), f);
            sys_seek(int_arg, unsigned_arg);
            break;
        case SYS_TELL:
            int_arg = * (int *) validate_arg(arg0, CONVERT_NUMERIC,
                                             sizeof(int), f);
            f->eax = sys_tell(int_arg);
            break;
        case SYS_CLOSE:
            int_arg = * (int *) validate_arg(arg0, CONVERT_NUMERIC,
                                             sizeof(int), f);
            sys_close(int_arg);
            break;
        case SYS_MMAP:
            int_arg = * (int *) validate_arg(arg0, CONVERT_NUMERIC,
                                             sizeof(int), f);
            /* TODO This should be unmapped, validate as numeric? */
            void_arg = * (void **) validate_arg(arg1, CONVERT_NUMERIC,
                                                sizeof(void *), f);
            f->eax = sys_mmap(int_arg, void_arg);
            break;
        case SYS_MUNMAP:
            int_arg = * (int *) validate_arg(arg0, CONVERT_NUMERIC,
                                             sizeof(int), f);
            sys_munmap(int_arg);
            break;
        default:
            printf("Call: %d Went to default\n", call_number);
            thread_exit();
    }
}

/* Returns true if addr to addr + size is valid */
void *valid_pointer(void **pointer, int size, struct intr_frame *f) {
    int i = 0;
    void *kernel_addr = NULL;

    /* Pointer is a pointer to a pointer, first we want to get the actual
     * pointer */
    void *addr = *(pointer);

    /* Need to check that its in user memory */
    if (!is_user_vaddr(addr)) {
        return NULL;
    }

    /* Get the kernel address, if its unmapped return NULL */
    kernel_addr = pagedir_get_page(thread_current()->pagedir, addr);

    if (kernel_addr == NULL) {
        /* See if the address is in the supplemental page table */
        if (!spte_lookup(addr)) {
            /* Check if the stack should be expanded */
            if (addr < f->esp && addr != f->esp - 4 && addr != f->esp - 32) {
                return NULL;
            }
            if (addr > f->esp && addr < STACK_FLOOR) {
                return NULL;
            }
            /* If you reach this point it means that you want to expand the
             * stack. */
            /* TODO find a way to trigger a pagefault on addr. Or find a way
             * to get a new page for the stack. */
            expand_stack(f, addr);
        }
    }

    /* See if the address is in the supplemental page table */
    /*if (spte_lookup(addr)) {*/
        /*return addr;*/
    /*}*/

    /* Now to check the rest of the data from the start to size - 1 */
    /* If size is -1 then its for a char * and we want to check memory until
     * we reach a NULL byte */
    if (size == -1) {
        char byte_read;

        /* Check each byte of the char * until we read a null byte */
        i = 1;
        do {
            addr = * (pointer) + i;

            if (!is_user_vaddr(addr)) {
                return NULL;
            }

            if (pagedir_get_page(thread_current()->pagedir, addr) == NULL) {
                if (!spte_lookup(addr)) {
                    /* Check if the stack should be expanded */
                    if (addr < f->esp && addr != f->esp - 4 &&
                        addr != f->esp - 32) {
                        return NULL;
                    }
                    if (addr > f->esp && addr < STACK_FLOOR) {
                        return NULL;
                    }
                    expand_stack(f, addr);
                }
            }

            byte_read = * (char *) addr;
            i++;
        } while (byte_read != '\0');


    } else {
        /* Check each byte in the specified byte range. */
        if (debug_mode)
            printf("Size of area to check: %d\n", size);
        for (i = 1; i < size; i++) {
            addr = *(pointer) + i;
            /*printf("The address being checked: %p\n", addr);*/

            if (!is_user_vaddr(addr)) {
                return NULL;
            }

            if (pagedir_get_page(thread_current()->pagedir, addr) == NULL) {
                if (!spte_lookup(addr)) {
                    /* Check if the stack should be expanded */
                    if (addr < f->esp && addr != f->esp - 4 &&
                        addr != f->esp - 32) {
                        return NULL;
                    }
                    if (addr > f->esp && addr < STACK_FLOOR) {
                        return NULL;
                    }
                    expand_stack(f, addr);
                }
            }
        }
    }
    return addr;
}

/* Returns true if addr to addr + size is valid */
void *valid_numeric(void *addr, int size) {
    void *kernel_addr = NULL;

    /* Making sure the memory is in user space */
    if (!is_user_vaddr(addr) || !is_user_vaddr(addr + size - 1)) {
        return kernel_addr;
    }

    /* Getting the kernel virtual address if the address is mapped */
    kernel_addr = pagedir_get_page(thread_current()->pagedir, addr);

    /* Making sure then end of the memory is also mapped */
    if (pagedir_get_page(thread_current()->pagedir, addr + size - 1) == NULL) {
        return NULL;
    }

    return kernel_addr;
}

static void *validate_arg(void *addr, enum conversion_type ct, int size,
                          struct intr_frame *f) {
    void *kernel_addr;

    switch(ct) {

        case(CONVERT_NUMERIC):

            kernel_addr = valid_numeric(addr, size);

            if (kernel_addr == NULL) {
                if (debug_mode)
                    printf("Invalid numeric argument\n");
                thread_exit();
            }

            return addr;

        case(CONVERT_POINTER):

            kernel_addr = valid_pointer(addr, size, f);

            if (kernel_addr == NULL) {
                if (debug_mode) {
                    printf("Invalid pointer argument at: %p\n", addr);
                    printf("The stack is at: %p\n", f->esp);
                }
                thread_exit();
            }

            return addr;

        default:
            thread_exit();
    }
}

void sys_halt(void) {
    shutdown_power_off();
}

void sys_exit(int status) {
    /* Have to close all fds */
    if (debug_mode)
        printf("Status: %d\n", status);

    struct thread *t = thread_current();
    t->return_status = status;

    if (t->info != NULL) {
        t->info->return_status = status;
        t->info->terminated = true;
    }
    thread_exit();
}

pid_t sys_exec(const char *cmd_line) {
    tid_t tid = process_execute(cmd_line);
    if (tid == -1) {
        if (debug_mode)
            printf("Could not create thread\n");
        return -1;
    }

    return tid;
    /*return (int) get_thread(tid);*/
}

int sys_wait(pid_t pid) {
    return process_wait(pid);
}

/* Creating a file with an initial size */
bool sys_create(const char *file, unsigned initial_size) {
    if (debug_mode)
        printf("In sys_create\n");
    bool return_value;
    lock_acquire(&file_lock);

    if (file == NULL) {
        lock_release(&file_lock);
        thread_exit();
    }
    return_value = filesys_create(file, initial_size);
    lock_release(&file_lock);
    return return_value;
}

/* Removes a file */
bool sys_remove(const char *file) {
    bool return_value;
    lock_acquire(&file_lock);
    return_value = filesys_remove(file);
    lock_release(&file_lock);
    return return_value;
}

/* Gets the size of a file in bytes */
int sys_filesize(int fd) {
    int size;
    lock_acquire(&file_lock);
    /* Getting the file struct from the fd */
    struct file *file = get_file(fd)->file_struct;
    if (file == NULL) {
        size = 0;
    }
    else {
        size = file_length(file);
    }
    lock_release(&file_lock);
    return size;

}


/* Opens a file */
int sys_open(const char *file) {

    if (debug_mode) {
        printf("In sys_open\n");
        printf("Filename: %s and thread: %d\n", file, thread_current()->tid);
    }

    /* Getting the file struct */
    lock_acquire(&file_lock);
    if (debug_mode)
        printf("Opening file in thread: %d\n", thread_current()->tid);
    struct file *file_struct = filesys_open(file);

    /* If the file can't be opened then put -1 in eax */
    if (file_struct == NULL) {
        if (debug_mode)
            printf("File could not be opened\n");
        lock_release(&file_lock);
        return -1;
    }

    if (debug_mode)
        printf("It opened!\n");

    /* Figure out what fd to give the file */
    int fd = thread_current()->max_fd + 1;
    thread_current()->max_fd++;

    struct fd_elem *new_fd = malloc(sizeof(struct fd_elem));
    new_fd->fd = fd;
    new_fd->file_struct = file_struct;
    list_push_back(&thread_current()->fd_list, &new_fd->elem);
    lock_release(&file_lock);

    /* Putting the fd in eax */
    if (debug_mode) {
        printf("With fd: %d\n", fd);
    }
    return fd;
}

int sys_read(int fd, void *buffer, unsigned size) {

    unsigned bytes_read = 0;
    lock_acquire(&file_lock);

    if (debug_mode)
        printf("in sys_read with thread: %d\n", thread_current()->tid);

    if (!can_write(buffer, size)) {
        lock_release(&file_lock);
        thread_exit();
    }

    if (fd == 0) {
        /* Switching to a char * buffer */
        char *buff = (char *) buffer;

        /* Reading in bytes using input_getc() */
        int i = 0;
        for(; i * sizeof(char) < size; i++) {
            char key = input_getc();
            buff[i] = key;
            bytes_read += sizeof(char);
        }
    }
    else {
        struct fd_elem *file_info= get_file(fd);
        if (file_info != NULL) {
            struct file *file = file_info->file_struct;
            bytes_read = file_read(file, buffer, size);
            if (bytes_read == 0) {
                if (debug_mode)
                    printf("No bytes were read\n");
            }
        }
        else {
            if (debug_mode)
                printf("The file struct was null\n");
            lock_release(&file_lock);
            thread_exit();
        }
    }

    lock_release(&file_lock);
    return bytes_read;
}

int sys_write(int fd, const void *buffer, unsigned size) {

    unsigned bytes_written = 0;

    lock_acquire(&file_lock);

    if (debug_mode)
        printf("in sys_write with thread: %d\n", thread_current()->tid);

    if (fd == 1) {
        unsigned i = 0;
        unsigned max_write = 300;
        for (; i < size; i += max_write) {
            if (size - bytes_written > max_write) {
                putbuf(buffer, max_write);
                buffer += max_write;
                bytes_written += max_write;
            }

            else {
                putbuf(buffer, size);
                bytes_written += size;
            }

        }
    }

    else {
        struct fd_elem *file_info = get_file(fd);
        if (file_info != NULL) {
            struct file *file = file_info->file_struct;
            bytes_written = file_write(file, buffer, size);
        }
        else {
            lock_release(&file_lock);
            thread_exit();
        }
    }

    lock_release(&file_lock);
    return bytes_written;

}

/* Changes the next byte to be read to position */
void sys_seek(int fd, unsigned position) {
    lock_acquire(&file_lock);
    struct file *file = get_file(fd)->file_struct;
    if (file == NULL) {
        lock_release(&file_lock);
        thread_exit();
    }
    file_seek(file, position);
    lock_release(&file_lock);
}

/* Returns the position of the next byte to read */
unsigned sys_tell(int fd) {
    off_t return_value;
    lock_acquire(&file_lock);
    struct file *file = get_file(fd)->file_struct;
    if (file == NULL) {
        lock_acquire(&file_lock);
        thread_exit();
    }
    return_value = file_tell(file);
    lock_release(&file_lock);
    return return_value;
}

/* Closes a given file */
void sys_close(int fd) {
    if (debug_mode)
        printf("In sys close! Closing: %d\n", fd);

    lock_acquire(&file_lock);

    struct fd_elem *file_info = get_file(fd);
    if (debug_mode)
        printf("Closing file with fd_elem: %p\n", file_info);
    if (file_info == NULL) {
        if (debug_mode)
            printf("Killing process due to invalid file access.\n");
        lock_release(&file_lock);
        thread_exit();
    }

    list_remove(&file_info->elem);
    file_close(file_info->file_struct);
    free(file_info);

    if (debug_mode)
        printf("Done closing file.\n");
    lock_release(&file_lock);
}

/* Make a memory mapping for the given file. */
mapid_t sys_mmap(int fd, void *addr) {

    /* Fail if the addr is not page aligned. */
    if (addr != pg_round_down(addr))
        return MAP_FAILED;

    /* Fail if the address is 0. */
    if (addr == 0)
        return MAP_FAILED;

    /* Locking the filesys. */
    lock_acquire(&file_lock);

    /* Getting the fd struct */
    struct fd_elem *file_info = get_file(fd);
    /* If the file wasn't found just release the lock and kill the process. */
    if (file_info == NULL) {
        lock_release(&file_lock);
        return MAP_FAILED;
    }

    /* Loop through and take pagesize chunks of the file and put them in
     * the supplemental page table. */

    /* Variables to use. */
    void * page_addr = addr;
    uint32_t page_offset = 0;
    uint32_t read_bytes, zero_bytes;
    int bytes_loaded = 0;
    struct file *file = file_info->file_struct;
    int size = file_length(file);

    /* Fail if the file has no size */
    if (size == 0) {
        lock_release(&file_lock);
        return MAP_FAILED;
    }

    /* Checking if any of the page mapping exists. */
    int i = 0;
    for (; i < size; i++) {
        if (spte_lookup(addr + i) != NULL) {
            lock_release(&file_lock);
            return MAP_FAILED;
        }
    }

    /* Associate the thread with this mapped file. */
    struct thread *cur = thread_current();
    struct mmap_fileinfo *mf = malloc(sizeof (struct mmap_fileinfo));
    mf->addr = addr;
    mf->num_pgs = size/PGSIZE + 1;
    mf->mapid = cur->num_mfiles;

    while(bytes_loaded < size) {
        /* The variables for this supplemental page table entry */
        read_bytes = bytes_loaded + PGSIZE <= size ? 
            PGSIZE : size - bytes_loaded;
        zero_bytes = PGSIZE - read_bytes;

        /* Inserting into the supplemental page table */
        spte_insert(thread_current(), (uint8_t *) page_addr, file, page_offset,
                    read_bytes, zero_bytes, false, file->deny_write);

        /* Updating variables. */
        bytes_loaded += read_bytes+ zero_bytes;
        page_offset += PGSIZE;
        page_addr += PGSIZE;
    }
    
    /* Now we can insert the element into the list of mapped files. */
    list_push_back(&cur->mmap_files, &mf->elem);
    cur->num_mfiles++;
    if (debug_mode)
        printf("Thread %s mapped fd %d with size %d to vm addr %p.\n", 
               cur->name, fd, size, addr);
    lock_release(&file_lock);

    return mf->mapid;
}


/* Unmaps the mapping designated by mapping, which must be a 
   mapping ID returned by a previous call to mmap by the same process 
   that has not yet been unmapped. */
void sys_munmap(mapid_t mapping) {
    /* Locking the filesys. */
    lock_acquire(&file_lock);
    if (debug_mode)
        printf("Unmap the mapping %d\n", (int) mapping);
    struct thread *cur = thread_current();
    // First check if the mapping is valid
    if (mapping < cur->num_mfiles){
        struct list *mmap_files = &cur->mmap_files;

        // If it is, look up which file it corresponds to
        struct list_elem *e;
        struct mmap_fileinfo *mf = NULL;
        for (e = list_begin (mmap_files); e != list_end (mmap_files);
             e = list_next (e)) {
            mf = list_entry (e, struct mmap_fileinfo, elem);
            if (mf->mapid == mapping) break;
        }
        ASSERT(mf != NULL);
        // Look up the address in the SPT 
        void *addr = mf->addr;
        struct spte *spte = spte_lookup(addr);
        ASSERT(spte != NULL);
        
        struct file *f = file_reopen(spte->file);
        if (debug_mode){
            printf("Corresponding file at %p with size %d.\n", f, mf->num_pgs);
        }

        // for each page in the file, write if necessary, then clear, free it
        int i;
        for (i = 0; i < mf->num_pgs; i++){
            if (debug_mode){
                printf("Found spte at %p.\n", spte);
            }
            // check if the corresponding page has been modified
            if (pagedir_is_dirty(cur->pagedir, addr)){
            }
            // remove page from the process's page directory
            pagedir_clear_page(cur->pagedir, addr);
            spte_remove(cur, spte);
            spte = spte_lookup(addr + PGSIZE);
        }
        file_close(f);
        list_remove(&(mf->elem));
        free(mf);
        if (debug_mode){
            printf("Unmapped file %p.\n", f);
        }

    }
    lock_release(&file_lock);
    return;
}


/* Returns the file struct for a given fd */
struct fd_elem *get_file(int fd) {
    struct list_elem *e = list_begin(&thread_current()->fd_list);
    for (; e != list_end(&thread_current()->fd_list); e = list_next(e)) {
        struct fd_elem *curr_fd = list_entry(e, struct fd_elem, elem);
        if (curr_fd->fd == fd) {
            return curr_fd;
        }
    }
    return NULL;
}

/* A function that verifies that you can write to an area of memory. Should
 * have already been checked that it is mapped. */
bool can_write(void *addr, int size) {
    int i = 0;
    for (; i < size; i++) {
        struct spte *page_entry = spte_lookup(addr);
        ASSERT(page_entry != NULL);
        if (page_entry->writable != true)
            return false;
    }
    return true;
}
