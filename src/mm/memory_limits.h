/* Shared bounds for the experimental pool, VM manager and NT queries. */
#define PAGE_POOL_COUNT 2048U
#define VM_REGION_PAGES 4096U
#define VM_WINDOW_START (PG_WINDOW+0x200000ULL)
#define VM_WINDOW_BYTES (64ULL*1024*1024)
#define VM_WINDOW_END (VM_WINDOW_START+VM_WINDOW_BYTES)
_Static_assert(VM_WINDOW_END<=PG_WINDOW+PG_WINDOW_SIZE,"VM window fits page-table window");
_Static_assert(PAGE_POOL_COUNT<=VM_REGION_PAGES,"exhaustion test fits one reservation");
