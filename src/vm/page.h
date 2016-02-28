#ifndef VM_PAGE_H
#define VM_PAGE_H

#include <debug.h>
#include <stdint.h>
#include <hash.h>
#include "filesys/file.h"
#include "threads/thread.h"

/* Supplemental page table entry (spte) struct.
 * Each thread will have its own spte. The entry holds additional 
 * data about each page for virtual memory management. */
struct spte {
    // see load_segment() in process.c for more about how these fields
    // are used
    void *upaddr;  /*!< user page address, used as key. */
    struct file *file; 
    off_t ofs;
    uint32_t read_bytes;
    uint32_t zero_bytes;
    bool writable;
    struct hash_elem hash_elem; /*!< To put spte in spt. */
};

bool spt_init (struct thread *t);

unsigned spte_hash (const struct hash_elem *p_, void *aux UNUSED);

bool spte_less (const struct hash_elem *a_, const struct hash_elem *b_,
                void *aux UNUSED);

bool spte_insert (struct thread* t, 
                  uint8_t *upage, struct file *file, off_t ofs, 
                  uint32_t read_bytes, uint32_t zero_bytes, bool writable);

#endif /* vm/page.h */
