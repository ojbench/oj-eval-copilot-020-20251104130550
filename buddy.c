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
static char page_rank_map[MAX_PAGES]; // Store rank for each allocated page

static inline int is_power_of_two(int n) {
    return n > 0 && (n & (n - 1)) == 0;
}

static inline int log2_floor(int n) {
    int log = 0;
    while (n > 1) {
        n >>= 1;
        log++;
    }
    return log;
}

static inline int pages_for_rank(int rank) {
    return 1 << (rank - 1);
}

static inline int page_index(void *p) {
    return ((char*)p - (char*)base_addr) / PAGE_SIZE;
}

static inline void *page_addr(int idx) {
    return (char*)base_addr + idx * PAGE_SIZE;
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
    }
    
    // Find the largest power of 2 that fits in pgcount
    int remaining = pgcount;
    int idx = 0;
    
    while (remaining > 0) {
        int log = log2_floor(remaining);
        int rank = log + 1;
        if (rank > MAX_RANK) rank = MAX_RANK;
        
        int pages_needed = pages_for_rank(rank);
        while (pages_needed > remaining) {
            rank--;
            pages_needed = pages_for_rank(rank);
        }
        
        // Add this block to free list
        free_block_t *block = (free_block_t*)page_addr(idx);
        block->next = free_lists[rank];
        free_lists[rank] = block;
        
        // Mark pages as free with this rank
        for (int i = 0; i < pages_needed; i++) {
            page_rank_map[idx + i] = rank;
        }
        
        idx += pages_needed;
        remaining -= pages_needed;
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
    
    // Split block if necessary
    while (current_rank > rank) {
        current_rank--;
        int buddy_idx = page_index(block) + pages_for_rank(current_rank);
        free_block_t *buddy = (free_block_t*)page_addr(buddy_idx);
        buddy->next = free_lists[current_rank];
        free_lists[current_rank] = buddy;
        
        // Mark buddy pages
        for (int i = 0; i < pages_for_rank(current_rank); i++) {
            page_rank_map[buddy_idx + i] = current_rank;
        }
    }
    
    // Mark allocated pages
    int idx = page_index(block);
    for (int i = 0; i < pages_for_rank(rank); i++) {
        page_rank_map[idx + i] = rank;
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
    
    int rank = page_rank_map[idx];
    if (rank < 1 || rank > MAX_RANK) {
        return -EINVAL;
    }
    
    // Merge with buddy if possible
    while (rank < MAX_RANK) {
        int pages = pages_for_rank(rank);
        int buddy_idx;
        
        // Calculate buddy index
        int block_idx = idx / pages;
        if (block_idx % 2 == 0) {
            buddy_idx = idx + pages;
        } else {
            buddy_idx = idx - pages;
        }
        
        // Check if buddy exists and is free
        if (buddy_idx < 0 || buddy_idx + pages > total_pages) {
            break;
        }
        
        // Check if buddy is free and has same rank
        int buddy_free = 0;
        free_block_t **prev = &free_lists[rank];
        while (*prev != NULL) {
            if (page_index(*prev) == buddy_idx) {
                // Remove buddy from free list
                free_block_t *buddy = *prev;
                *prev = buddy->next;
                buddy_free = 1;
                break;
            }
            prev = &((*prev)->next);
        }
        
        if (!buddy_free) {
            break;
        }
        
        // Merge with buddy
        if (buddy_idx < idx) {
            idx = buddy_idx;
            p = page_addr(idx);
        }
        rank++;
    }
    
    // Add to free list
    free_block_t *block = (free_block_t*)page_addr(idx);
    block->next = free_lists[rank];
    free_lists[rank] = block;
    
    // Update page rank map
    for (int i = 0; i < pages_for_rank(rank); i++) {
        page_rank_map[idx + i] = rank;
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
