/**
 * @file eos_nema_gfx_stm32_hal.c
 * @brief STM32U5-owned NemaGFX HAL adapter with bounded GPU waits.
 *
 * This replaces LVGL's stock STM32 adapter at build time.  The GPU2D error
 * IRQ must never enter the HAL weak infinite-loop handler: a failed command
 * list is detected by the next wait, the peripheral is reset, and LVGL's
 * refresh loop can continue on the next frame.
 */

#include "stm32u5xx_hal.h"
#include "draw/nema_gfx/lv_draw_nema_gfx_utils.h"

#if LV_USE_NEMA_GFX

#define EOS_NEMA_RING_SIZE 1024U
#define EOS_NEMA_POOL_SIZE 10240U
#define EOS_NEMA_WAIT_TIMEOUT_MS 50U

extern GPU2D_HandleTypeDef hgpu2d;

static uint8_t s_nemagfx_pool[EOS_NEMA_POOL_SIZE] LV_NEMA_STM32_HAL_ATTRIBUTE_POOL_MEM;
static nema_ringbuffer_t s_ring_buffer;
static volatile int s_completed_cl_id;
static volatile bool s_gpu_fault;
static volatile uint32_t s_gpu_fault_count;
static volatile uint32_t s_gpu_timeout_count;
static volatile uint32_t s_gpu_last_error;

static bool _timeout_expired(uint32_t start)
{
    return (uint32_t)(HAL_GetTick() - start) >= EOS_NEMA_WAIT_TIMEOUT_MS;
}

/* This runs only from the LVGL/main context, never from an IRQ. */
static void _recover_gpu2d(void)
{
    if (!s_gpu_fault)
        return;

    HAL_NVIC_DisableIRQ(GPU2D_IRQn);
    HAL_NVIC_DisableIRQ(GPU2D_ER_IRQn);

    __HAL_RCC_GPU2D_FORCE_RESET();
    __DSB();
    __HAL_RCC_GPU2D_RELEASE_RESET();
    __DSB();

    hgpu2d.State = HAL_GPU2D_STATE_RESET;
    hgpu2d.Lock = HAL_UNLOCKED;
    hgpu2d.ErrorCode = HAL_GPU2D_ERROR_NONE;
    (void)HAL_GPU2D_Init(&hgpu2d);

    s_completed_cl_id = 0;
    s_gpu_fault = false;
}

void HAL_GPU2D_CommandListCpltCallback(GPU2D_HandleTypeDef *gpu, uint32_t command_list_id)
{
    (void)gpu;
    s_completed_cl_id = (int)command_list_id;
    __DMB();
}

/* Override the HAL weak default, which otherwise loops forever in IRQ context. */
void HAL_GPU2D_ErrorCallback(GPU2D_HandleTypeDef *gpu)
{
    s_gpu_last_error = gpu ? gpu->ErrorCode : HAL_GPU2D_ERROR_TIMEOUT;
    s_gpu_fault_count++;
    s_gpu_fault = true;
    /* The HAL error IRQ does not clear the GPU error source.  Mask it here so
     * the pending reset can run from the main context instead of retriggering
     * this callback indefinitely.  HAL_GPU2D_Init() enables it again. */
    HAL_NVIC_DisableIRQ(GPU2D_ER_IRQn);
    __DMB();
}

int32_t nema_sys_init(void)
{
    int error_code;

    error_code = tsi_malloc_init_pool_aligned(0, s_nemagfx_pool, (uintptr_t)s_nemagfx_pool,
                                              EOS_NEMA_POOL_SIZE, 1, 8);
    if (error_code != 0)
        return error_code;

    s_ring_buffer.bo = nema_buffer_create((int)EOS_NEMA_RING_SIZE);
    if (!s_ring_buffer.bo.base_virt)
        return -1;

    error_code = nema_rb_init(&s_ring_buffer, 1);
    if (error_code < 0)
        return error_code;

    s_completed_cl_id = 0;
    s_gpu_fault = false;
    return 0;
}

uint32_t nema_reg_read(uint32_t reg)
{
    return HAL_GPU2D_ReadRegister(&hgpu2d, reg);
}

void nema_reg_write(uint32_t reg, uint32_t value)
{
    (void)HAL_GPU2D_WriteRegister(&hgpu2d, reg, value);
}

int nema_wait_irq(void)
{
    uint32_t start = HAL_GetTick();
    while (!s_gpu_fault && !_timeout_expired(start))
    {
        __WFI();
        return 0;
    }

    if (_timeout_expired(start))
        s_gpu_timeout_count++;
    _recover_gpu2d();
    return -1;
}

int nema_wait_irq_cl(int command_list_id)
{
    uint32_t start = HAL_GetTick();

    while (s_completed_cl_id < command_list_id)
    {
        if (s_gpu_fault)
        {
            _recover_gpu2d();
            return -1;
        }
        if (_timeout_expired(start))
        {
            s_gpu_timeout_count++;
            s_gpu_fault = true;
            _recover_gpu2d();
            return -1;
        }
        __WFI();
    }

    return 0;
}

int nema_wait_irq_brk(int breakpoint_id)
{
    uint32_t start = HAL_GetTick();
    (void)breakpoint_id;

    while (nema_reg_read(GPU2D_BREAKPOINT) == 0U)
    {
        if (s_gpu_fault || _timeout_expired(start))
        {
            if (!s_gpu_fault)
                s_gpu_timeout_count++;
            s_gpu_fault = true;
            _recover_gpu2d();
            return -1;
        }
        __WFI();
    }

    return 0;
}

void nema_host_free(void *ptr)
{
    tsi_free(ptr);
}

void *nema_host_malloc(unsigned size)
{
    return tsi_malloc((int)size);
}

nema_buffer_t nema_buffer_create(int size)
{
    nema_buffer_t buffer = {0};
    buffer.base_virt = tsi_malloc(size);
    buffer.base_phys = (uintptr_t)buffer.base_virt;
    buffer.size = size;
    return buffer;
}

nema_buffer_t nema_buffer_create_pool(int pool, int size)
{
    (void)pool;
    return nema_buffer_create(size);
}

void *nema_buffer_map(nema_buffer_t *buffer)
{
    return buffer ? buffer->base_virt : NULL;
}

void nema_buffer_unmap(nema_buffer_t *buffer)
{
    (void)buffer;
}

void nema_buffer_destroy(nema_buffer_t *buffer)
{
    if (!buffer || !buffer->base_virt)
        return;

    tsi_free(buffer->base_virt);
    buffer->base_virt = NULL;
    buffer->base_phys = 0U;
    buffer->size = 0;
    buffer->fd = -1;
}

uintptr_t nema_buffer_phys(nema_buffer_t *buffer)
{
    return buffer ? buffer->base_phys : 0U;
}

void nema_buffer_flush(nema_buffer_t *buffer)
{
    (void)buffer;
}

int nema_mutex_lock(int mutex_id)
{
    (void)mutex_id;
    return 0;
}

int nema_mutex_unlock(int mutex_id)
{
    (void)mutex_id;
    return 0;
}

void platform_disable_cache(void)
{
}

void platform_invalidate_cache(void)
{
}

#endif /* LV_USE_NEMA_GFX */
