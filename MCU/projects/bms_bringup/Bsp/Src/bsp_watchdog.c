#include "bsp_watchdog.h"
#include "iwdg.h"
#include "bsp_transport.h"
#include <stdio.h>

static volatile uint32_t bsp_watchdog_reset_flags = 0u;
static volatile uint8_t bsp_watchdog_iwdg_reset = 0u;

void Bsp_WatchdogCaptureReset(void)
{
    /* 必须先保存再清除，否则会丢失本次复位原因。 */
    bsp_watchdog_reset_flags = RCC->CSR;

    bsp_watchdog_iwdg_reset =
        (bsp_watchdog_reset_flags & RCC_CSR_IWDGRSTF) != 0u ? 1u : 0u;

    __HAL_RCC_CLEAR_RESET_FLAGS();

    /* 只在调试器暂停CPU时冻结；正常运行时继续计数。 */
    __HAL_DBGMCU_FREEZE_IWDG();
    __HAL_DBGMCU_FREEZE_TIM4();
}

void Bsp_WatchdogLogReset(void)
{
    char text[64];

    int length = snprintf(text, sizeof(text),
        "BOOT reset_flags=0x%08lX iwdg=%u\r\n",
        (unsigned long)bsp_watchdog_reset_flags,
        (unsigned int)bsp_watchdog_iwdg_reset);

    if ((length > 0) && ((size_t)length < sizeof(text)))
        (void)Bsp_UartWrite((const uint8_t *)text, (uint16_t)length);
}

uint8_t Bsp_WatchdogRefresh(void)
{
    return HAL_IWDG_Refresh(&hiwdg) == HAL_OK ? 1u : 0u;
}
