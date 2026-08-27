#include "stm32u5xx_hal.h"
#include "eos_vcp.h"

static UART_HandleTypeDef h_vcp_uart;
static bool last_output_was_cr;

bool eos_vcp_init(void)
{
    h_vcp_uart.Instance = USART1;
    h_vcp_uart.Init.BaudRate = 115200U;
    h_vcp_uart.Init.WordLength = UART_WORDLENGTH_8B;
    h_vcp_uart.Init.StopBits = UART_STOPBITS_1;
    h_vcp_uart.Init.Parity = UART_PARITY_NONE;
    h_vcp_uart.Init.Mode = UART_MODE_TX_RX;
    h_vcp_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    h_vcp_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    h_vcp_uart.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    h_vcp_uart.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    h_vcp_uart.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    return HAL_UART_Init(&h_vcp_uart) == HAL_OK;
}

/* Newlib/picolibc uses this hook through _write(). */
int __io_putchar(int ch)
{
    uint8_t byte;

    /* Normalize LF to CRLF without duplicating an existing CRLF sequence. */
    if (ch == '\n')
    {
        if (!last_output_was_cr)
        {
            byte = '\r';
            if (HAL_UART_Transmit(&h_vcp_uart, &byte, 1U, 1000U) != HAL_OK)
            {
                return -1;
            }
        }

        byte = '\n';
        last_output_was_cr = false;
    }
    else
    {
        byte = (uint8_t)ch;
        last_output_was_cr = (ch == '\r');
    }

    if (HAL_UART_Transmit(&h_vcp_uart, &byte, 1U, 1000U) != HAL_OK)
    {
        return -1;
    }

    return ch;
}
