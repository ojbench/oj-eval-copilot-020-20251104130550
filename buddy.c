#include "buddy.h"
#define NULL ((void *)0)

#define MAX_RANK 16
#define PAGE_SIZE 4096
#define MAX_PAGES (128 * 1024 / 4)  // Maximum possible pages

// Free list for each rank
typedef struct free_block {
    struct free_block *next;
} free_block_t;

static free_block_t *free_lists[MAX_RANK + 1];
static void *base_addr = NULL;
static int total_pages = 0;
static unsigned char page_rank_map[MAX_PAGES]; // Store rank for each page (0 = not allocated, negative = free)
static unsigned char page_allocated[MAX_PAGES]; // 1 = allocated, 0 = free

static inline int pages_for_rank(int rank) {
    return 1 << (rank - 1);
}

static inline int page_index(void *p) {
    return ((char*)p - (char*)base_addr) / PAGE_SIZE;
}

static inline void *page_addr(int idx) {
    return (char*)base_addr + idx * PAGE_SIZE;
}

static inline int get_buddy_index(int idx, int rank) {
    int pages = pages_for_rank(rank);
    int block_idx = idx / pages;
    if (block_idx % 2 == 0) {
        return idx + pages;
    } else {
        return idx - pages;
    }
}

int init_page(void *p, int pgcount) {
    base_addr = p;
    total_pages = pgcount;
    
    // Initialize free lists
    for (int i = 0; i <= MAX_RANK; i++) {
        free_lists[i] = NULL;
    }
    
    // Initialize page rank map
    for (int i = 0; i < pgcount; i++) {
        page_rank_map[i] = 0;
        page_allocated[i] = 0;
    }
    
    // Build free blocks from largest to smallest
    int idx = 0;
    while (idx < pgcount) {
        // Find largest rank that fits
        int rank = MAX_RANK;
        int pages = pages_for_rank(rank);
        while (pages > pgcount - idx) {
            rank--;
            pages = pages_for_rank(rank);
        }
        
        // Add this block to free list
        free_block_t *block = (free_block_t*)page_addr(idx);
        block->next = free_lists[rank];
        free_lists[rank] = block;
        
        // Mark pages
        for (int i = 0; i < pages; i++) {
            page_rank_map[idx + i] = rank;
            page_allocated[idx + i] = 0;
        }
        
        idx += pages;
    }
    
    return OK;
}

void *alloc_pages(int rank) {
    if (rank < 1 || rank > MAX_RANK) {
        return ERR_PTR(-EINVAL);
    }
    
    // Find the smallest available block >= rank
    int current_rank = rank;
    while (current_rank <= MAX_RANK && free_lists[current_rank] == NULL) {
        current_rank++;
    }
    
    if (current_rank > MAX_RANK) {
        return ERR_PTR(-ENOSPC);
    }
    
    // Remove block from free list
    free_block_t *block = free_lists[current_rank];
    free_lists[current_rank] = block->next;
    
    int idx = page_index(block);
    
    // Split block if necessary
    while (current_rank > rank) {
        current_rank--;
        int buddy_idx = idx + pages_for_rank(current_rank);
        free_block_t *buddy = (free_block_t*)page_addr(buddy_idx);
        buddy->next = free_lists[current_rank];
        free_lists[current_rank] = buddy;
        
        // Mark buddy pages as free with the new rank
        int buddy_pages = pages_for_rank(current_rank);
        for (int i = 0; i < buddy_pages; i++) {
            page_rank_map[buddy_idx + i] = current_rank;
            page_allocated[buddy_idx + i] = 0;
        }
    }
    
    // Mark allocated pages
    int pages = pages_for_rank(rank);
    for (int i = 0; i < pages; i++) {
        page_rank_map[idx + i] = rank;
        page_allocated[idx + i] = 1;
    }
    
    return block;
}

int return_pages(void *p) {
    if (p == NULL || (char*)p < (char*)base_addr || 
        (char*)p >= (char*)base_addr + total_pages * PAGE_SIZE) {
        return -EINVAL;
    }
    
    int idx = page_index(p);
    if (idx < 0 || idx >= total_pages) {
        return -EINVAL;
    }
    
    // Check if page is aligned
    if (((char*)p - (char*)base_addr) % PAGE_SIZE != 0) {
        return -EINVAL;
    }
    
    if (!page_allocated[idx]) {
        return -EINVAL;
    }
    
    int rank = page_rank_map[idx];
    if (rank < 1 || rank > MAX_RANK) {
        return -EINVAL;
    }
    
    // Merge with buddy if possible
    while (rank < MAX_RANK) {
        int buddy_idx = get_buddy_index(idx, rank);
        
        // Check if buddy exists
        if (buddy_idx < 0 || buddy_idx >= total_pages) {
            break;
        }
        
        int pages = pages_for_rank(rank);
        if (buddy_idx + pages > total_pages) {
            break;
        }
        
        // Check if buddy is free and has same rank
        if (page_allocated[buddy_idx] || page_rank_map[buddy_idx] != rank) {
            break;
        }
        
        // Remove buddy from free list
        free_block_t **prev = &free_lists[rank];
        int found = 0;
        while (*prev != NULL) {
            if (page_index(*prev) == buddy_idx) {
                *prev = (*prev)->next;
                found = 1;
                break;
            }
            prev = &((*prev)->next);
        }
        
        if (!found) {
            break;
        }
        
        // Merge with buddy
        if (buddy_idx < idx) {
            idx = buddy_idx;
        }
        rank++;
    }
    
    // Add to free list
    free_block_t *block = (free_block_t*)page_addr(idx);
    block->next = free_lists[rank];
    free_lists[rank] = block;
    
    // Mark as free
    int pages = pages_for_rank(rank);
    for (int i = 0; i < pages; i++) {
        page_rank_map[idx + i] = rank;
        page_allocated[idx + i] = 0;
    }
    
    return OK;
}

int query_ranks(void *p) {
    if (p == NULL || (char*)p < (char*)base_addr || 
        (char*)p >= (char*)base_addr + total_pages * PAGE_SIZE) {
        return -EINVAL;
    }
    
    int idx = page_index(p);
    if (idx < 0 || idx >= total_pages) {
        return -EINVAL;
    }
    
    return page_rank_map[idx];
}

int query_page_counts(int rank) {
    if (rank < 1 || rank > MAX_RANK) {
        return -EINVAL;
    }
    
    int count = 0;
    free_block_t *current = free_lists[rank];
    while (current != NULL) {
        count++;
        current = current->next;
    }
    
    return count;
}
