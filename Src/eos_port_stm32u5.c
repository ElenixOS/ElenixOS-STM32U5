#include "stm32u5xx_hal.h"
#include "eos_port.h"
#include "eos_mem.h"
#include "eos_service_cache.h"
#include "eos_platform_config.h"

#include <stddef.h>
#include <stdint.h>

#define EOS_U5_CACHE_ALLOC_MAGIC 0x454F5343U
#define EOS_U5_SNAPSHOT_SLOT_COUNT 2U
#define EOS_U5_SNAPSHOT_SLOT_SIZE \
    ((size_t)EOS_DISPLAY_WIDTH * (size_t)EOS_DISPLAY_HEIGHT * sizeof(uint16_t))
#define EOS_U5_SNAPSHOT_SLOT_STORAGE_SIZE ((EOS_U5_SNAPSHOT_SLOT_SIZE + 31U) & ~(size_t)31U)

typedef struct
{
    uint32_t magic;
    uint32_t from_lvgl;
} eos_u5_cache_alloc_header_t;

/* Two deterministic full-screen RGB565 snapshot slots.  Keeping these out of
 * both the C heap and LVGL's TLSF pool prevents fragmentation from making the
 * second transition snapshot fail. */
static uint8_t eos_u5_snapshot_slots[EOS_U5_SNAPSHOT_SLOT_COUNT][EOS_U5_SNAPSHOT_SLOT_STORAGE_SIZE]
    __attribute__((section(".snapshot_framebuffers"), aligned(32), used));
static bool eos_u5_snapshot_slot_used[EOS_U5_SNAPSHOT_SLOT_COUNT];

static void *eos_u5_snapshot_slot_alloc(size_t size)
{
    if (size != EOS_U5_SNAPSHOT_SLOT_SIZE)
        return NULL;

    for (size_t i = 0U; i < EOS_U5_SNAPSHOT_SLOT_COUNT; ++i)
    {
        if (!eos_u5_snapshot_slot_used[i])
        {
            eos_u5_snapshot_slot_used[i] = true;
            return eos_u5_snapshot_slots[i];
        }
    }

    return NULL;
}

static bool eos_u5_snapshot_slot_free(void *ptr)
{
    for (size_t i = 0U; i < EOS_U5_SNAPSHOT_SLOT_COUNT; ++i)
    {
        if (ptr == eos_u5_snapshot_slots[i])
        {
            eos_u5_snapshot_slot_used[i] = false;
            return true;
        }
    }

    return false;
}

/*
 * Snapshot and other cache pixel buffers can be much larger than a normal
 * LVGL object allocation.  The generic cache port uses lv_malloc(), which is
 * backed by LV_MEM_SIZE (512 KiB on this board).  Keep those buffers in the
 * U5 C heap first; if the C heap is fragmented, fall back to LVGL's pool.
 * The small header records which allocator owns the returned pointer.
 */
void *eos_cache_buf_alloc(size_t size)
{
    void *snapshot_slot = eos_u5_snapshot_slot_alloc(size);
    if (snapshot_slot)
        return snapshot_slot;

    if (size > SIZE_MAX - sizeof(eos_u5_cache_alloc_header_t))
        return NULL;

    size_t alloc_size = size + sizeof(eos_u5_cache_alloc_header_t);
    eos_u5_cache_alloc_header_t *header = eos_malloc(alloc_size);
    if (header)
    {
        header->magic = EOS_U5_CACHE_ALLOC_MAGIC;
        header->from_lvgl = 0U;
        return (void *)(header + 1);
    }

    header = lv_malloc(alloc_size);
    if (header)
    {
        header->magic = EOS_U5_CACHE_ALLOC_MAGIC;
        header->from_lvgl = 1U;
        return (void *)(header + 1);
    }

    return NULL;
}

void eos_cache_buf_free(void *ptr)
{
    if (!ptr)
        return;

    if (eos_u5_snapshot_slot_free(ptr))
        return;

    eos_u5_cache_alloc_header_t *header = ((eos_u5_cache_alloc_header_t *)ptr) - 1;
    if (header->magic != EOS_U5_CACHE_ALLOC_MAGIC)
        return;

    if (header->from_lvgl)
        lv_free(header);
    else
        eos_free(header);
}

void eos_delay(uint32_t ms) { HAL_Delay(ms); }

void eos_cpu_reset(void)
{
    NVIC_SystemReset();
    for (;;)
    {
    }
}

size_t eos_port_get_free_mem(void)
{
    extern void *_sbrk(ptrdiff_t incr);
    extern uint8_t _estack;
    extern uint32_t _Min_Stack_Size;

    uintptr_t heap_end = (uintptr_t)_sbrk(0);
    uintptr_t heap_limit = (uintptr_t)&_estack - (uintptr_t)&_Min_Stack_Size;
    return heap_end < heap_limit ? (size_t)(heap_limit - heap_end) : 0U;
}
