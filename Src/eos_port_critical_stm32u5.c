#include "stm32u5xx_hal.h"
#include "eos_port_critical.h"

eos_critical_ctx_t eos_critical_enter(void)
{
    eos_critical_ctx_t previous = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return previous;
}

void eos_critical_leave(eos_critical_ctx_t previous)
{
    __DMB();
    __set_PRIMASK(previous);
}
