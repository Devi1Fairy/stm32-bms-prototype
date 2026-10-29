#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bsp_analog.h"
#include "bsp_output.h"
#include "bsp_transport.h"
#include "main.h"
#include "adc.h"
#include "can.h"
#include "usart.h"

ADC_HandleTypeDef hadc1;
CAN_HandleTypeDef hcan;
GPIO_TypeDef mock_gpio_b;
unsigned int mock_usart1;
uint32_t mock_gpio_b_clock;
UART_HandleTypeDef huart1 = {USART1, 0u};
static uint32_t selected, raw_value = 2048u, stops;
static HAL_StatusTypeDef poll_result = HAL_OK, stop_result = HAL_OK;
static uint16_t pins[8];
static GPIO_PinState levels[8];
static unsigned int writes, toggles, uart_arms, uart_aborts, command_count;
static uint8_t *rx_byte;
static uint8_t last_command;
static HAL_StatusTypeDef arm_result = HAL_OK;
static uint32_t rx_pending;
static HAL_StatusTypeDef can_read_result = HAL_OK;
static CAN_RxHeaderTypeDef rx_header;
static CAN_TxHeaderTypeDef tx_header;

HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *adc)
{ assert(adc == &hadc1); return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *adc, ADC_ChannelConfTypeDef *config)
{
    assert(adc == &hadc1 && config->Rank == ADC_REGULAR_RANK_1);
    assert(config->SamplingTime == ADC_SAMPLETIME_239CYCLES_5);
    selected = config->Channel;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *adc) { (void)adc; return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *adc) { (void)adc; stops++; return stop_result; }
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *adc, uint32_t timeout)
{ (void)adc; assert(timeout == 2u); return poll_result; }
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *adc) { (void)adc; return raw_value; }
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState level)
{
    assert(port == GPIOB && writes < 8u);
    pins[writes] = pin; levels[writes++] = level;
}
void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin)
{ assert(port == GPIOB && pin == GPIO_PIN_5); toggles++; }
HAL_StatusTypeDef HAL_CAN_ConfigFilter(CAN_HandleTypeDef *can, CAN_FilterTypeDef *filter)
{
    assert(can == &hcan && filter->FilterBank == 0u && filter->FilterActivation == ENABLE);
    assert(filter->FilterIdHigh == 0u && filter->FilterMaskIdLow == 0u);
    return HAL_OK;
}
HAL_StatusTypeDef HAL_CAN_Start(CAN_HandleTypeDef *can) { assert(can == &hcan); return HAL_OK; }
uint32_t HAL_CAN_GetRxFifoFillLevel(CAN_HandleTypeDef *can, uint32_t fifo)
{ assert(can == &hcan && fifo == CAN_RX_FIFO0); return rx_pending; }
HAL_StatusTypeDef HAL_CAN_GetRxMessage(
    CAN_HandleTypeDef *can, uint32_t fifo, CAN_RxHeaderTypeDef *header, uint8_t *data)
{
    (void)can; (void)fifo;
    *header = rx_header;
    memset(data, 0x55, 8u);
    return can_read_result;
}
uint32_t HAL_CAN_GetTxMailboxesFreeLevel(CAN_HandleTypeDef *can) { (void)can; return 3u; }
HAL_StatusTypeDef HAL_CAN_AddTxMessage(
    CAN_HandleTypeDef *can, CAN_TxHeaderTypeDef *header, uint8_t *data, uint32_t *mailbox)
{ assert(can == &hcan && data[0] == 1u); tx_header = *header; *mailbox = 0u; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *uart, uint8_t *byte, uint16_t length)
{ assert(uart == &huart1 && length == 1u); rx_byte = byte; uart_arms++; return arm_result; }
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *uart)
{ assert(uart == &huart1); uart_aborts++; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart, const uint8_t *data, uint16_t length, uint32_t timeout)
{ assert(uart == &huart1 && data && length == 1u && timeout == 100u); return HAL_OK; }
static void OnByte(uint8_t byte) { last_command = byte; command_count++; }

int main(void)
{
    uint32_t raw = 999u;
    BspCanFrame frame = {0x201u, 0u, 0u, 1u, {1u}}, old;
    UART_HandleTypeDef other = {NULL, 0u};
    assert(Bsp_AnalogInit());
    assert(Bsp_AnalogRead(BSP_ANALOG_POT, &raw) && raw == 2048u && selected == 10u);
    assert(Bsp_AnalogRead(BSP_ANALOG_NTC, &raw) && selected == 11u);
    poll_result = HAL_TIMEOUT;
    assert(!Bsp_AnalogRead(BSP_ANALOG_POT, &raw) && stops == 3u && raw == 2048u);
    poll_result = HAL_OK; stop_result = HAL_ERROR;
    assert(!Bsp_AnalogRead(BSP_ANALOG_POT, &raw) && raw == 2048u);
    stop_result = HAL_OK; raw_value = 4096u;
    assert(!Bsp_AnalogRead(BSP_ANALOG_POT, &raw) && raw == 2048u);
    assert(!Bsp_AnalogRead((BspAnalogInput)99, &raw));
    assert(!Bsp_AnalogRead(BSP_ANALOG_POT, NULL));
    Bsp_OutputApply(1u, 0u);
    assert(pins[0] == GPIO_PIN_10 && levels[0] == GPIO_PIN_SET);
    assert(pins[1] == GPIO_PIN_0 && levels[1] == GPIO_PIN_RESET);
    assert(pins[2] == GPIO_PIN_1 && levels[2] == GPIO_PIN_SET);
    Bsp_OutputApply(0u, 1u);
    assert(levels[3] == GPIO_PIN_RESET && levels[4] == GPIO_PIN_SET && levels[5] == GPIO_PIN_RESET);
    Bsp_OutputForceOff();
    assert(mock_gpio_b_clock == 1u && pins[6] == GPIO_PIN_10 && levels[6] == GPIO_PIN_RESET);
    Bsp_OutputToggleHeartbeat(); assert(toggles == 1u);
    assert(Bsp_CanInit() && Bsp_CanFreeMailboxes() == 3u);
    assert(Bsp_CanSend(&frame) && tx_header.StdId == 0x201u && tx_header.DLC == 1u);
    frame.extended = 1u; assert(!Bsp_CanSend(&frame)); frame.extended = 0u;
    frame.id = 0x800u; assert(!Bsp_CanSend(&frame)); frame.id = 0x201u;
    old = frame;
    assert(Bsp_CanReceive(&frame) == 0 && memcmp(&old, &frame, sizeof(frame)) == 0);
    rx_pending = 1u; can_read_result = HAL_ERROR;
    assert(Bsp_CanReceive(&frame) == -1 && memcmp(&old, &frame, sizeof(frame)) == 0);
    can_read_result = HAL_OK;
    rx_header = (CAN_RxHeaderTypeDef){0x201u, 0x12345u, CAN_ID_EXT, CAN_RTR_REMOTE, 8u};
    assert(Bsp_CanReceive(&frame) == 1 && frame.id == 0x12345u && frame.extended && frame.remote);
    assert(Bsp_CanReceive(NULL) == -1);
    assert(!Bsp_UartRxInit(NULL));
    assert(Bsp_UartRxInit(OnByte) && uart_arms == 1u);
    *rx_byte = 'S'; Bsp_TransportOnUartRx(&huart1);
    assert(command_count == 1u && last_command == 'S' && uart_arms == 2u);
    Bsp_TransportOnUartRx(&other); assert(command_count == 1u);
    huart1.ErrorCode = 1u; Bsp_TransportOnUartRx(&huart1);
    assert(command_count == 1u);
    huart1.ErrorCode = 0u; Bsp_UartRxPoll();
    assert(uart_aborts == 1u && uart_arms == 3u);
    arm_result = HAL_BUSY;
    *rx_byte = 'T'; Bsp_TransportOnUartRx(&huart1);
    assert(command_count == 2u);
    Bsp_UartRxPoll(); assert(uart_aborts == 2u);
    arm_result = HAL_OK; Bsp_UartRxPoll(); assert(uart_aborts == 3u);
    Bsp_UartRxPoll(); assert(uart_aborts == 3u);
    assert(Bsp_UartWrite((const uint8_t *)"S", 1u));
    assert(!Bsp_UartWrite(NULL, 1u) && !Bsp_UartWrite((const uint8_t *)"", 0u));
    puts("ADC failure cleanup, PB10 safe-off, CAN metadata and UART RX recovery passed.");
    return 0;
}
