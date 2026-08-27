#include "stm32u5xx_hal.h"
#include "stm32u5g9j_discovery.h"
#include "stm32u5g9j_discovery_lcd.h"
#include "stm32u5g9j_discovery_ts.h"
#include "lvgl.h"
#include "elenix_os.h"
#include "eos_dev_display.h"
#include "eos_dev_time.h"
#include "eos_vcp.h"
#include "eos_crown.h"
#include "eos_fs_port_stm32u5.h"
#include "eos_developer_options.h"

#include <string.h>
#include <stdint.h>

#define LCD_WIDTH       800U
#define LCD_HEIGHT      480U
#define LCD_BPP         2U
#define LOGICAL_WIDTH   390U
#define LOGICAL_HEIGHT  450U
#define X_OFFSET        ((LCD_WIDTH - LOGICAL_WIDTH) / 2U)
#define Y_OFFSET        ((LCD_HEIGHT - LOGICAL_HEIGHT) / 2U)
#define FRAMEBUFFER_A   ((uint16_t *)LCD_LAYER_0_ADDRESS)

/* The second scanout buffer is placed after the fixed first framebuffer by
 * the linker.  LVGL renders into it while LTDC scans the other buffer. */
static uint16_t framebuffer_b[LOGICAL_WIDTH * LOGICAL_HEIGHT]
    __attribute__((section(".lcd_framebuffer_back"), aligned(32), used));
GPU2D_HandleTypeDef hgpu2d;
DCACHE_HandleTypeDef hdcache2;

static volatile bool ltdc_reload_complete;
static volatile bool ltdc_reload_error;

static void fatal_error(void);
void eos_lvgl_draw_buf_port_init(void);

extern uint8_t __font_bitmap_load__;
extern uint8_t __font_bitmap_start__;
extern uint8_t __font_bitmap_end__;

static void font_bitmap_init(void)
{
    uint32_t size = (uint32_t)(&__font_bitmap_end__ - &__font_bitmap_start__);
    memcpy(&__font_bitmap_start__, &__font_bitmap_load__, size);
    __DMB();
}

static void gpu2d_init(void)
{
    hdcache2.Instance = DCACHE2;
    hdcache2.Init.ReadBurstType = DCACHE_READ_BURST_INCR;
    if (HAL_DCACHE_Init(&hdcache2) != HAL_OK)
    {
        fatal_error();
    }

    __HAL_RCC_SYSCFG_CLK_ENABLE();
    HAL_SYSCFG_DisableSRAMCached();

    hgpu2d.Instance = GPU2D;
    if (HAL_GPU2D_Init(&hgpu2d) != HAL_OK)
    {
        fatal_error();
    }
}

static void system_clock_config(void)
{
    RCC_ClkInitTypeDef clock_config = {0};
    RCC_OscInitTypeDef oscillator_config = {0};

    __HAL_RCC_PWR_CLK_ENABLE();
    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK ||
        HAL_PWREx_ConfigSupply(PWR_SMPS_SUPPLY) != HAL_OK)
    {
        fatal_error();
    }
    __HAL_RCC_PWR_CLK_DISABLE();

    oscillator_config.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_MSI;
    oscillator_config.HSEState = RCC_HSE_ON;
    oscillator_config.MSIState = RCC_MSI_ON;
    oscillator_config.MSIClockRange = RCC_MSIRANGE_4;
    oscillator_config.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
    oscillator_config.PLL.PLLState = RCC_PLL_ON;
    oscillator_config.PLL.PLLSource = RCC_PLLSOURCE_MSI;
    oscillator_config.PLL.PLLMBOOST = RCC_PLLMBOOST_DIV1;
    oscillator_config.PLL.PLLM = 1U;
    oscillator_config.PLL.PLLN = 80U;
    oscillator_config.PLL.PLLR = 2U;
    oscillator_config.PLL.PLLP = 2U;
    oscillator_config.PLL.PLLQ = 2U;
    oscillator_config.PLL.PLLFRACN = 0U;
    oscillator_config.PLL.PLLRGE = RCC_PLLVCIRANGE_0;

    if (HAL_RCC_OscConfig(&oscillator_config) != HAL_OK)
    {
        fatal_error();
    }

    clock_config.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK |
                             RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 |
                             RCC_CLOCKTYPE_PCLK3;
    clock_config.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clock_config.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clock_config.APB1CLKDivider = RCC_HCLK_DIV1;
    clock_config.APB2CLKDivider = RCC_HCLK_DIV1;
    clock_config.APB3CLKDivider = RCC_HCLK_DIV1;

    if (HAL_RCC_ClockConfig(&clock_config, FLASH_LATENCY_4) != HAL_OK)
    {
        fatal_error();
    }
}

static void fatal_error(void)
{
    __disable_irq();
    for (;;)
    {
    }
}

static uint32_t lvgl_tick_get(void)
{
    return HAL_GetTick();
}

static void lcd_flush(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t flush_start = HAL_GetTick();

    /* In direct mode LVGL renders dirty areas into the active full-screen
     * buffer.  Only the final area represents a complete new frame. */
    if (lv_display_flush_is_last(display))
    {
        ltdc_reload_complete = false;
        ltdc_reload_error = false;

        if (HAL_LTDC_SetAddress_NoReload(&hlcd_ltdc,
                                         (uint32_t)(uintptr_t)px_map, 0U) != HAL_OK ||
            HAL_LTDC_Reload(&hlcd_ltdc, LTDC_RELOAD_VERTICAL_BLANKING) != HAL_OK)
        {
            ltdc_reload_error = true;
        }

        /* LTDC IRQ remains enabled while waiting, so the reload is applied
         * at the next vertical blank and the old buffer is not reused early. */
        while (!ltdc_reload_complete && !ltdc_reload_error)
        {
            __WFI();
        }

        if (ltdc_reload_error)
        {
            fatal_error();
        }
    }

    (void)area;
    eos_developer_options_record_flush_ms(HAL_GetTick() - flush_start);
    lv_display_flush_ready(display);
}

void HAL_LTDC_ReloadEventCallback(LTDC_HandleTypeDef *hltdc)
{
    if (hltdc == &hlcd_ltdc)
    {
        ltdc_reload_complete = true;
    }
}

void HAL_LTDC_ErrorCallback(LTDC_HandleTypeDef *hltdc)
{
    if (hltdc == &hlcd_ltdc)
    {
        ltdc_reload_error = true;
    }
}

static void display_set_brightness(uint8_t brightness)
{
    (void)BSP_LCD_SetBrightness(0U, brightness);
}

static void display_power_on(void) { (void)BSP_LCD_DisplayOn(0U); }
static void display_power_off(void) { (void)BSP_LCD_DisplayOff(0U); }

static bool touch_available;
static lv_point_t last_touch_point;

#define USER_BUTTON_LONG_PRESS_MS 800U
#define USER_BUTTON_DEBOUNCE_MS    30U

static volatile bool user_button_pressed;
static volatile uint32_t user_button_pressed_at;
static volatile uint32_t user_button_last_edge;

/* Called by the official BSP from the EXTI13 ISR. Do not touch LVGL here;
 * eos_crown_button_report() only queues a small dispatcher item, which is
 * consumed by the normal ElenixOS main loop. */
void BSP_PB_Callback(Button_TypeDef button)
{
    if (button != BUTTON_USER)
    {
        return;
    }

    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - user_button_last_edge) < USER_BUTTON_DEBOUNCE_MS)
    {
        return;
    }
    user_button_last_edge = now;

    if (BSP_PB_GetState(BUTTON_USER) == BUTTON_PRESSED)
    {
        user_button_pressed = true;
        user_button_pressed_at = now;
        eos_crown_button_report(EOS_BUTTON_STATE_PRESSED);
    }
    else if (user_button_pressed)
    {
        if ((uint32_t)(now - user_button_pressed_at) >= USER_BUTTON_LONG_PRESS_MS)
        {
            eos_crown_button_report(EOS_BUTTON_STATE_LONG_PRESSED);
        }
        else
        {
            eos_crown_button_report(EOS_BUTTON_STATE_CLICKED);
        }
        eos_crown_button_report(EOS_BUTTON_STATE_RELEASED);
        user_button_pressed = false;
    }
}

static void user_button_init(void)
{
    if (BSP_PB_Init(BUTTON_USER, BUTTON_MODE_EXTI) != BSP_ERROR_NONE)
    {
        fatal_error();
    }
}

static bool touch_init(void)
{
    TS_Init_t init = {
        .Width = LCD_WIDTH,
        .Height = LCD_HEIGHT,
        .Orientation = TS_SWAP_NONE,
        .Accuracy = 0U,
    };

    return BSP_TS_Init(0U, &init) == BSP_ERROR_NONE;
}

static void touch_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint32_t touch_start = HAL_GetTick();
    (void)indev;
    /* Keep the last valid position while the touch controller reports the
     * release state. Bubble-grid click handling compares press and release
     * coordinates to distinguish a tap from a drag. */
    data->point = last_touch_point;
    data->state = LV_INDEV_STATE_RELEASED;

    if (touch_available)
    {
        TS_State_t state = {0};
        if (BSP_TS_GetState(0U, &state) == BSP_ERROR_NONE && state.TouchDetected != 0U &&
            state.TouchX >= X_OFFSET && state.TouchX < X_OFFSET + LOGICAL_WIDTH &&
            state.TouchY >= Y_OFFSET && state.TouchY < Y_OFFSET + LOGICAL_HEIGHT)
        {
            data->point.x = (lv_coord_t)(state.TouchX - X_OFFSET);
            data->point.y = (lv_coord_t)(state.TouchY - Y_OFFSET);
            last_touch_point = data->point;
            data->state = LV_INDEV_STATE_PRESSED;
        }
    }

    eos_developer_options_record_touch_ms(HAL_GetTick() - touch_start);
}

static void input_compat_init(lv_display_t *display)
{
    lv_indev_t *indev = lv_indev_create();
    if (indev == NULL)
    {
        fatal_error();
    }

    touch_available = touch_init();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read);
    lv_indev_set_display(indev, display);
}

static const eos_dev_display_ops_t display_ops = {
    .set_brightness = display_set_brightness,
    .power_on = display_power_on,
    .power_off = display_power_off,
};

static eos_datetime_t time_get(void)
{
    eos_datetime_t now = {.year = 2025U, .month = 1U, .day = 1U};
    uint32_t seconds = HAL_GetTick() / 1000U;
    now.hour = (uint8_t)((seconds / 3600U) % 24U);
    now.min = (uint8_t)((seconds / 60U) % 60U);
    now.sec = (uint8_t)(seconds % 60U);
    return now;
}

static const eos_dev_time_ops_t time_ops = {.get_datetime = time_get};

static lv_display_t *display_init(void)
{
    /* BSP initialization briefly configures an 800x480 layer.  Clear that
     * range before enabling the centered logical layer. */
    memset(FRAMEBUFFER_A, 0, LCD_WIDTH * LCD_HEIGHT * LCD_BPP);

    if (BSP_LCD_InitEx(0U, LCD_ORIENTATION_LANDSCAPE, LCD_PIXEL_FORMAT_RGB565,
                       LCD_WIDTH, LCD_HEIGHT) != BSP_ERROR_NONE)
    {
        fatal_error();
    }

    /* The LTDC background remains black outside this centered layer.  A
     * logical-width pitch reclaims enough SRAM for two fixed screenshots. */
    if (BSP_LCD_SetLayerWindow(0U, 0U, X_OFFSET, Y_OFFSET,
                               LOGICAL_WIDTH, LOGICAL_HEIGHT) != BSP_ERROR_NONE)
    {
        fatal_error();
    }
    memset(FRAMEBUFFER_A, 0, LOGICAL_WIDTH * LOGICAL_HEIGHT * LCD_BPP);
    memset(framebuffer_b, 0, sizeof(framebuffer_b));

    (void)BSP_LCD_SetBrightness(0U, 100U);
    (void)BSP_LCD_DisplayOn(0U);
    HAL_NVIC_SetPriority(LTDC_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(LTDC_IRQn);
    HAL_NVIC_SetPriority(LTDC_ER_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(LTDC_ER_IRQn);

    lv_display_t *display = lv_display_create(LOGICAL_WIDTH, LOGICAL_HEIGHT);
    if (display == NULL)
    {
        fatal_error();
    }
    /* LTDC starts on A.  Make B LVGL's first render target so the first
     * refresh also obeys the no-write-to-scanout rule. */
    lv_display_set_buffers(display, framebuffer_b, FRAMEBUFFER_A,
                           sizeof(framebuffer_b), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, lcd_flush);
    return display;
}

int main(void)
{
    HAL_Init();
    system_clock_config();
    font_bitmap_init();
    if (!eos_vcp_init())
    {
        fatal_error();
    }
    gpu2d_init();
    lv_init();
    eos_lvgl_draw_buf_port_init();
    lv_tick_set_cb(lvgl_tick_get);
    lv_display_t *display = display_init();
    input_compat_init(display);

    if (eos_dev_display_register(&display_ops) != EOS_OK ||
        eos_dev_time_register(&time_ops) != EOS_OK)
    {
        fatal_error();
    }

    /* Keep built-in applications available if a board has an unreadable
     * external flash; the storage port will report I/O failures to callers. */
    (void)eos_fs_port_init();
    eos_init();
    user_button_init();

    for (;;)
    {
        static bool loop_started;
        static uint32_t previous_loop_start;
        uint32_t loop_start = HAL_GetTick();
        uint32_t wait_ms = eos_main_loop();
        if (loop_started)
        {
            eos_developer_options_record_loop(loop_start - previous_loop_start, wait_ms);
        }
        loop_started = true;
        previous_loop_start = loop_start;
        HAL_Delay(wait_ms == 0U ? 1U : wait_ms);
    }
}
