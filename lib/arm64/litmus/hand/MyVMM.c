#include <libcflat.h>

#include "MyVMM.h"

#define BIT(x, i) ((x >> i) & 0x1)
#define BIT_SLICE(x, i, j) ((x >> i) & ((1 << (1 + j - i)) - 1))
#define IS_ALIGNED(v, bits) ((v & ((1UL << bits) - 1)) == 0)
#define ALIGN_TO(v, bits) (v & ~((1UL << bits) - 1))
#define ALIGN_UP(v, bits) ((v + ((1UL << bits) - 1)) & ~((1UL << bits) - 1))

uint64_t translate64k(uint64_t* root, uint64_t vaddr) {
    uint64_t bot = 0;
    uint64_t desc = root[BIT_SLICE(vaddr, 29, 41)];

    bot = 29;

    if ((desc & 0x1) == 0) {
        return 0;
    }

    if ((desc & 0x3) == 0x3) {
        uint64_t* root1 = (uint64_t*)(desc & ~((1UL << 16) - 1));
        desc = root1[BIT_SLICE(vaddr, 16, 28)];
        bot = 15;
    }

    if ((desc & 0x1) == 0) {
        return 0;
    }

    uint64_t mask = ((1UL << (48 - bot)) - 1)
                    << bot; /* OA mask for the descriptor */
    uint64_t pa = (desc & mask) | BIT_SLICE(vaddr, 0, bot);
    return pa;
}

uint64_t translate4k(uint64_t* root, uint64_t vaddr) {
    uint64_t level, desc;
    uint64_t offs, bot, top;

    level = 0;
    while (1) {
        uint64_t valid, table;
        switch (level) {
            case 0:
                top = 48;
                bot = 39;
                break;
            case 1:
                top = 38;
                bot = 30;
                break;
            case 2:
                top = 29;
                bot = 21;
                break;
            case 3:
                top = 20;
                bot = 12;
                break;
        }

        offs = BIT_SLICE(vaddr, bot, top);
        desc = root[offs];
        valid = (desc & 0x1) == 0x1;
        table = (desc & 0x2) == 0x2;

        if (!valid || (level == 3 && !table)) {
            return 0;
        }

        if ((level < 3 && !table) || (level == 3 && table)) {
            break;
        }

        if (level < 3 && table) {
            root = (uint64_t*)(desc & ~((1UL << 12) - 1));
            level++;
        } else {
            printf("! translate4k: unknown?\n");
            abort();
        }
    }

    uint64_t tw = (1UL << (48 - bot)) - 1;
    uint64_t pa = (desc & (tw << bot)) | BIT_SLICE(vaddr, 0, bot);
    return pa;
}

void ensure_pte(uint64_t* root, uint64_t vaddr) {
    ptable_set_or_ensure_block_or_page_4k(root, vaddr, 0, 0, 3, 1);
}

uint64_t pte4k(uint64_t* root, uint64_t vaddr) {
    /* assuming vaddr is already mapped by a level 3 table entry */

    uint64_t* lvl1 = root[BIT_SLICE(vaddr, 39, 48)];
    uint64_t* lvl2 = lvl1[BIT_SLICE(vaddr, 30, 38)];
    uint64_t* lvl3 = lvl2[BIT_SLICE(vaddr, 21, 29)];
    uint64_t* pte = &lvl3[BIT_SLICE(vaddr, 12, 20)];
    return pte;
}

// alloc
/* allocate a buffer with some 4k pages to play with */
#define NO_BUF_PAGES 5000
static uint64_t page_buf[512 * NO_BUF_PAGES] __attribute__((aligned(4096)));
static uint64_t* free_pages_start = &page_buf;
static uint64_t total_pages_allocated = 0;

uint64_t* alloc_page_aligned(void) {
    total_pages_allocated++;
    if (total_pages_allocated > NO_BUF_PAGES) {
        printf("! error: alloc_page_aligned: no free pages\n");
        abort();
    }

    uint64_t start = (uint64_t)free_pages_start;
    free_pages_start = (uint64_t*)(free_pages_start + 512);
    memset((uint64_t*)start, 0, 512);
    return (uint64_t*)start;
}

void free_aligned_pages(void) { free_pages_start = &page_buf; }

// 4k pages
static void ptable_set_or_ensure_block_or_page_4k(uint64_t* root_ptable,
                                                  uint64_t vaddr, uint64_t pa,
                                                  uint64_t prot,
                                                  uint64_t desired_level,
                                                  int ensure) {
    /* assume TCR_EL1 setup to start from level 0 and that vaddr is aligned for
     * the desired level */
    uint64_t final_desc = pa | prot | (1 << 10) | (3 << 8) | 0x1;

    uint64_t offs0 = BIT_SLICE(vaddr, 39, 48);
    uint64_t desc = root_ptable[offs0];
    uint64_t valid = desc & 0x1;
    uint64_t block = !(desc & 0x2);
    uint64_t table = valid && !block;

    /* if the level 0 entry is not already a pointer to another table
     * it must be invalid and so we must create a table with other invalid
     * entries first
     */
    if (!valid) {
        uint64_t* new_lvl1_table = alloc_page_aligned();
        memset(new_lvl1_table, 0,
               sizeof(uint64_t) * 512); /* make all entries invalid */
        uint64_t new_desc =
            (uint64_t)new_lvl1_table | 0x3; /* no prot on table */
        root_ptable[offs0] = new_desc;
        desc = new_desc;
    }

    /* it is now guaranteed that `desc` contains a level 0 table descriptor
     * pointing to a level 1 table */
    uint64_t* level1_table = (uint64_t*)(desc & ~((1UL << 12) - 1));
    uint64_t offs1 = BIT_SLICE(vaddr, 30, 38);

    if (desired_level == 1) {
        if (!ensure) level1_table[offs1] = final_desc;

        return;
    }

    desc = level1_table[offs1];
    valid = desc & 0x1;
    block = !(desc & 0x2);
    table = valid && !block;

    /* if level1 entry is not already a pointer to another translation table
     * then allocate a new table and copy entries over.
     */
    if (!table) {
        // alloc new level 2 table, initialised to 0.
        uint64_t* new_lvl2_table = alloc_page_aligned();
        memset(new_lvl2_table, 0,
               sizeof(uint64_t) * 512); /* make all entries invalid */
        uint64_t new_desc =
            (uint64_t)new_lvl2_table | 0x3; /* no prot on table */
        uint64_t old_desc = desc;
        level1_table[offs1] = new_desc;
        desc = new_desc;

        /* if the original entry was a valid translation
         * then we have to make sure all others match
         */
        if (valid) {
            for (uint64_t i = 0; i < 512; i++) {
                new_lvl2_table[i] = (offs0 << 39) | (offs1 << 30) | (i << 21) |
                                    (1 << 10) | (3 << 8) |
                                    ((old_desc >> 51) << 51) |
                                    (old_desc & 0xfffffc) | 1;
            }
        }
    }

    /* now `desc` guaranteed to be a level 1 table descriptor pointing to a
     * level 2 table */
    uint64_t* level2_table;
    level2_table = (uint64_t*)(desc & ~((1UL << 12) - 1));
    uint64_t offs2 = BIT_SLICE(vaddr, 21, 29);

    if (desired_level == 2) {
        if (!ensure) level2_table[offs2] = final_desc;

        return;
    }

    desc = level2_table[offs2];
    valid = desc & 0x1;
    block = !(desc & 0x2);
    table = valid && !block;

    /* as before, need to create a new level3 table */
    if (!table) {
        uint64_t* new_lvl3_table = alloc_page_aligned();
        memset(new_lvl3_table, 0,
               sizeof(uint64_t) * 512); /* make all entries invalid */
        uint64_t new_desc =
            (uint64_t)new_lvl3_table | 0x3; /* no prot on table */
        uint64_t old_desc = desc;
        level2_table[offs2] = new_desc;
        desc = new_desc;

        /* if the original entry was a valid translation
         * then we have to make sure the table is initialised valid with the
         * same permissions
         */
        if (valid) {
            for (uint64_t i = 0; i < 512; i++) {
                new_lvl3_table[i] = (offs0 << 39) | (offs1 << 30) |
                                    (offs2 << 21) | (i << 12) | (1 << 10) |
                                    (3 << 8) | ((old_desc >> 51) << 51) |
                                    (old_desc & 0xfffffc) | 3;
            }
        }
    }

    uint64_t* level3_table;
    level3_table = (uint64_t*)(desc & ~((1UL << 12) - 1));
    uint64_t offs3 = BIT_SLICE(vaddr, 12, 20);
    if (!ensure)
        level3_table[offs3] =
            final_desc | 0x2; /* set bit[1] for level3 page desc */
}

void ptable_set_range_4k(uint64_t* root, uint64_t va_start, uint64_t va_end,
                         uint64_t pa_start, uint64_t prot, uint64_t level) {
    uint64_t bot;
    switch (level) {
        case 1:
            bot = 30;
            break;
        case 2:
            bot = 21;
            break;
        case 3:
            bot = 12;
            break;
    }

    if (!IS_ALIGNED(va_start, bot)) {
        printf("! error: ptable_set_range_4k: got unaligned va_start\n");
        abort();
    }

    if (!IS_ALIGNED(va_end, bot)) {
        printf("! error: ptable_set_range_4k: got unaligned va_end\n");
        abort();
    }

    uint64_t va = va_start; /* see above: must be aligned */
    uint64_t pa = pa_start;
    for (; va < va_end; va += (1UL << bot), pa += (1UL << bot)) {
        ptable_set_block_or_page_4k(root, va, pa, prot, level);
    }
}

void ptable_set_block_or_page_4k(uint64_t* root_ptable, uint64_t vaddr,
                                 uint64_t pa, uint64_t prot,
                                 uint64_t desired_level) {
    ptable_set_or_ensure_block_or_page_4k(root_ptable, vaddr, pa, prot,
                                          desired_level, 0);
}

// TODO: maybe this could be when va_start and pa_start aren't aligned.
void ptable_set_range_4k_smart(uint64_t* root, uint64_t va_start,
                               uint64_t va_end, uint64_t pa_start,
                               uint64_t prot) {
    uint64_t level1 = 30, level2 = 21, level3 = 12;

    if (!IS_ALIGNED(va_start, level3)) {
        printf("! error: ptable_set_range_4k_smart: got unaligned va_start\n");
        abort();
    }

    if (!IS_ALIGNED(va_end, level3)) {
        printf("! error: ptable_set_range_4k_smart: got unaligned va_end\n");
        abort();
    }

    uint64_t va = va_start; /* see above: must be aligned on a page */
    uint64_t pa = pa_start;

    for (; !IS_ALIGNED(va, level2) && va < va_end;
         va += (1UL << level3), pa += (1UL << level3))
        ptable_set_block_or_page_4k(
            root, va, pa, prot,
            3);  // allocate 4k regions up to the first 2M region

    for (; !IS_ALIGNED(va, level1) && va < va_end;
         va += (1UL << level2), pa += (1UL << level2))
        ptable_set_block_or_page_4k(
            root, va, pa, prot,
            2);  // allocate 2M regions up to the first 1G region

    for (; va < ALIGN_TO(va_end, level1);
         va += (1UL << level1), pa += (1UL << level1))
        ptable_set_block_or_page_4k(root, va, pa, prot,
                                    1);  // Alloc as many 1G regions as possible

    for (; va < ALIGN_TO(va_end, level2);
         va += (1UL << level2), pa += (1UL << level2))
        ptable_set_block_or_page_4k(
            root, va, pa, prot,
            2);  // allocate as much of what's left as 2MB regions

    for (; va < va_end; va += (1UL << level3), pa += (1UL << level3))
        ptable_set_block_or_page_4k(
            root, va, pa, prot, 3);  // allocate whatever remains as 4k pages.
}

void ptable_set_idrange_4k_smart(uint64_t* root, uint64_t va_start,
                                 uint64_t va_end, uint64_t prot) {
    ptable_set_range_4k_smart(root, va_start, va_end, va_start, prot);
}
