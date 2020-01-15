/* shared virtual memory manager
for 4k pages.
*/

#ifndef _MVMM_H
#define _MVMM_H

#include <stdint.h>

#define ATTR_MEM (0b100 << 2)
//#define ATTR_S   (0b1   << 5)
#define ATTR_AP_RWX (0b000 << 6)
#define ATTR_SH_ISH (0b111 << 8)
#define ATTR_AF (0b1 << 10)
#define ATTR_DEFAULT (ATTR_MEM | ATTR_AP_RWX | ATTR_SH_ISH | ATTR_AF)

// translation functions
uint64_t translate64k(uint64_t* root, uint64_t vaddr);
uint64_t translate4k(uint64_t* root, uint64_t vaddr);

// partial translate functions
uint64_t ref_pte4k(uint64_t* root, uint64_t vaddr);
uint64_t make_pte4k(uint64_t paddr);  // construct a default readable/writeable PTE for some physical addr.

uint64_t ref_pte64k(uint64_t* root, uint64_t vaddr);

// alloc
uint64_t* alloc_page_aligned(void);
void free_aligned_pages(void);

// pagetable management
void ptable_set_block_or_page_4k(uint64_t* root_ptable, uint64_t vaddr,
                                 uint64_t pa, uint64_t prot,
                                 uint64_t desired_level);
void ptable_set_range_4k(uint64_t* root, uint64_t va_start, uint64_t va_end,
                         uint64_t pa_start, uint64_t prot, uint64_t level);
void ptable_set_range_4k_smart(uint64_t* root, uint64_t va_start,
                               uint64_t va_end, uint64_t pa_start,
                               uint64_t prot);
void ptable_set_idrange_4k_smart(uint64_t* root, uint64_t va_start,
                                 uint64_t va_end, uint64_t prot);

void vmm_flush_tlb_vaddr(uint64_t va);
void flush_tlb(void);

/* test pagetable management */
// global pagetable root
uint64_t read_tcr(void);
uint64_t read_ttbr(void);

uint64_t* alloc_new_idmap_4k(void);
void set_new_ttable(uint64_t ttbr, uint64_t tcr);
void set_new_id_translation(uint64_t* pgtable);
void restore_old_id_translation(uint64_t ttbr, uint64_t tcr);

#endif
