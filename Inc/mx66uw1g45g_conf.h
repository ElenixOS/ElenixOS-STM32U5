/* STM32U5G9J-DK2 configuration for the MX66UW1G45G NOR driver. */
#ifndef MX66UW1G45G_CONF_H
#define MX66UW1G45G_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32u5xx_hal.h"

#define CONF_OSPI_ODS                MX66UW1G45G_CR_ODS_24
#define DUMMY_CYCLES_READ            8U
#define DUMMY_CYCLES_READ_OCTAL      6U
#define DUMMY_CYCLES_READ_OCTAL_DTR  6U
#define DUMMY_CYCLES_REG_OCTAL       4U
#define DUMMY_CYCLES_REG_OCTAL_DTR   5U

#ifdef __cplusplus
}
#endif

#endif /* MX66UW1G45G_CONF_H */
