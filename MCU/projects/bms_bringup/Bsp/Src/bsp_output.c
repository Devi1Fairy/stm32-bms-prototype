#include "bsp_output.h"
#include "main.h"

void Bsp_OutputApply(uint8_t load_enabled, uint8_t fault_locked)
{
    GPIO_PinState load_level =
        load_enabled == 1u ? GPIO_PIN_SET : GPIO_PIN_RESET;
    GPIO_PinState run_level =
        load_enabled == 1u ? GPIO_PIN_RESET : GPIO_PIN_SET;
    GPIO_PinState fault_level =
        fault_locked == 1u ? GPIO_PIN_RESET : GPIO_PIN_SET;

    /* 先处理负载，再更新状态灯。 */
    HAL_GPIO_WritePin(LOAD_EN_GPIO_Port, LOAD_EN_Pin, load_level);
    HAL_GPIO_WritePin(LED_RUN_GPIO_Port, LED_RUN_Pin, run_level);
    HAL_GPIO_WritePin(LED_FAULT_GPIO_Port, LED_FAULT_Pin, fault_level);
}

void Bsp_OutputToggleHeartbeat(void)
{
    HAL_GPIO_TogglePin(LED_STATUS_GPIO_Port, LED_STATUS_Pin);
}

void Bsp_OutputForceOff(void)
{
    /* 错误可能发生在GPIO初始化之前，先确保实际所在的GPIOB时钟打开。 */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    HAL_GPIO_WritePin(LOAD_EN_GPIO_Port, LOAD_EN_Pin, GPIO_PIN_RESET);
}
