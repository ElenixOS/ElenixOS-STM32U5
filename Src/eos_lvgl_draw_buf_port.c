/**
 * @file eos_lvgl_draw_buf_port.c
 * @brief Board-owned LVGL draw-buffer allocator.
 *
 * LVGL's default draw-buffer allocator is confined to LV_MEM_SIZE.  Large
 * transient layers can therefore fail despite contiguous C heap being
 * available, leaving a bare-metal dispatcher with no unit able to take the
 * task.  This adapter keeps LVGL's pool as the first choice and uses the C
 * heap only as a fallback for draw-buffer pixel data.
 */

#include "lvgl.h"
#include "eos_mem.h"

#include <stdint.h>
#include <string.h>

#define EOS_LV_DRAW_BUF_MAGIC 0x454F5344U
#define EOS_LV_DRAW_BUF_OWNER_LVGL 0U
#define EOS_LV_DRAW_BUF_OWNER_EOS 1U

typedef struct
{
    uint32_t magic;
    uint32_t owner;
} eos_lv_draw_buf_header_t;

static void *_draw_buf_alloc(size_t size, lv_color_format_t color_format)
{
    (void)color_format;

    if (size > SIZE_MAX - sizeof(eos_lv_draw_buf_header_t) - (LV_DRAW_BUF_ALIGN - 1U))
        return NULL;

    const size_t alloc_size = size + sizeof(eos_lv_draw_buf_header_t) + (LV_DRAW_BUF_ALIGN - 1U);
    eos_lv_draw_buf_header_t *header = lv_malloc(alloc_size);
    if (header)
    {
        header->magic = EOS_LV_DRAW_BUF_MAGIC;
        header->owner = EOS_LV_DRAW_BUF_OWNER_LVGL;
        return header + 1;
    }

    header = eos_malloc(alloc_size);
    if (header)
    {
        header->magic = EOS_LV_DRAW_BUF_MAGIC;
        header->owner = EOS_LV_DRAW_BUF_OWNER_EOS;
        return header + 1;
    }

    return NULL;
}

static void _draw_buf_free(void *buffer)
{
    if (!buffer)
        return;

    eos_lv_draw_buf_header_t *header = ((eos_lv_draw_buf_header_t *)buffer) - 1;
    if (header->magic != EOS_LV_DRAW_BUF_MAGIC)
        return;

    if (header->owner == EOS_LV_DRAW_BUF_OWNER_EOS)
        eos_free(header);
    else
        lv_free(header);
}

static void *_draw_buf_align(void *buffer, lv_color_format_t color_format)
{
    (void)color_format;
    return (void *)(((uintptr_t)buffer + (LV_DRAW_BUF_ALIGN - 1U)) & ~(uintptr_t)(LV_DRAW_BUF_ALIGN - 1U));
}

static uint32_t _draw_buf_stride(uint32_t width, lv_color_format_t color_format)
{
    uint32_t bytes = (width * lv_color_format_get_bpp(color_format) + 7U) >> 3U;
    return (bytes + LV_DRAW_BUF_STRIDE_ALIGN - 1U) / LV_DRAW_BUF_STRIDE_ALIGN * LV_DRAW_BUF_STRIDE_ALIGN;
}

static void _draw_buf_copy(lv_draw_buf_t *dest, const lv_area_t *dest_area,
                           const lv_draw_buf_t *src, const lv_area_t *src_area)
{
    const int32_t width = dest_area ? lv_area_get_width(dest_area) : (int32_t)dest->header.w;
    const int32_t height = dest_area ? lv_area_get_height(dest_area) : (int32_t)dest->header.h;
    const uint32_t bytes_per_line = ((uint32_t)width * lv_color_format_get_bpp(dest->header.cf) + 7U) >> 3U;
    uint8_t *dest_ptr = dest_area ? lv_draw_buf_goto_xy(dest, dest_area->x1, dest_area->y1)
                                  : lv_draw_buf_goto_xy(dest, 0, 0);
    const uint8_t *src_ptr = src_area ? lv_draw_buf_goto_xy(src, src_area->x1, src_area->y1)
                                      : lv_draw_buf_goto_xy(src, 0, 0);

    for (int32_t y = 0; y < height; y++)
    {
        memcpy(dest_ptr, src_ptr, bytes_per_line);
        dest_ptr += dest->header.stride;
        src_ptr += src->header.stride;
    }
}

void eos_lvgl_draw_buf_port_init(void)
{
    lv_draw_buf_handlers_init(lv_draw_buf_get_handlers(),
                              _draw_buf_alloc,
                              _draw_buf_free,
                              _draw_buf_copy,
                              _draw_buf_align,
                              NULL,
                              NULL,
                              _draw_buf_stride);
}
