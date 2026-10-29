#include "bsp_transport.h"
#include "can.h"
#include "usart.h"
#include <stddef.h>
#include <string.h>

static uint8_t uart_rx_byte = 0u;
static volatile uint8_t uart_rx_restart_required = 0u;
static BspUartByteHandler uart_byte_handler = NULL;

uint8_t Bsp_CanInit(void)
{
    CAN_FilterTypeDef filter = {0};
    filter.FilterBank = 0u;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    filter.FilterActivation = ENABLE;

    return (HAL_CAN_ConfigFilter(&hcan, &filter) == HAL_OK)
        && (HAL_CAN_Start(&hcan) == HAL_OK);
}

int32_t Bsp_CanReceive(BspCanFrame *frame)
{
    CAN_RxHeaderTypeDef header = {0};
    BspCanFrame received = {0};

    if (frame == NULL)
    {
        return -1;
    }
    if (HAL_CAN_GetRxFifoFillLevel(&hcan, CAN_RX_FIFO0) == 0u)
    {
        return 0;
    }
    if (HAL_CAN_GetRxMessage(
            &hcan, CAN_RX_FIFO0, &header, received.data) != HAL_OK)
    {
        return -1;
    }

    received.extended = header.IDE == CAN_ID_EXT ? 1u : 0u;
    received.remote = header.RTR == CAN_RTR_REMOTE ? 1u : 0u;
    received.id = received.extended != 0u ? header.ExtId : header.StdId;
    received.length = (uint8_t)header.DLC;
    *frame = received;
    return 1;
}

uint32_t Bsp_CanFreeMailboxes(void)
{
    return HAL_CAN_GetTxMailboxesFreeLevel(&hcan);
}

uint8_t Bsp_CanSend(const BspCanFrame *frame)
{
    CAN_TxHeaderTypeDef header = {0};
    uint32_t mailbox = 0u;
    uint8_t data[8];

    if ((frame == NULL) || (frame->length > 8u)
        || (frame->extended != 0u) || (frame->remote != 0u)
        || (frame->id > 0x7FFu))
    {
        return 0u;
    }
    header.StdId = frame->id;
    header.IDE = CAN_ID_STD;
    header.RTR = CAN_RTR_DATA;
    header.DLC = frame->length;
    memcpy(data, frame->data, sizeof(data));
    return HAL_CAN_AddTxMessage(&hcan, &header, data, &mailbox) == HAL_OK;
}

uint8_t Bsp_UartRxInit(BspUartByteHandler handler)
{
    if (handler == NULL)
    {
        return 0u;
    }
    uart_byte_handler = handler;
    return HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1u) == HAL_OK;
}

void Bsp_UartRxPoll(void)
{
    if (uart_rx_restart_required != 0u)
    {
        uart_rx_restart_required = 0u;
        if ((HAL_UART_AbortReceive(&huart1) != HAL_OK)
            || (HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1u) != HAL_OK))
        {
            uart_rx_restart_required = 1u;
        }
    }
}

uint8_t Bsp_UartWrite(const uint8_t *data, uint16_t length)
{
    if ((data == NULL) || (length == 0u))
    {
        return 0u;
    }
    return HAL_UART_Transmit(&huart1, data, length, 100u) == HAL_OK;
}

void Bsp_TransportOnUartRx(UART_HandleTypeDef *uart)
{
    if ((uart == NULL) || (uart->Instance != USART1))
    {
        return;
    }
    if ((uart->ErrorCode != HAL_UART_ERROR_NONE)
        || (uart_rx_restart_required != 0u))
    {
        uart_rx_restart_required = 1u;
        return;
    }
    if (uart_byte_handler != NULL)
    {
        uart_byte_handler(uart_rx_byte);
    }
    if (HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1u) != HAL_OK)
    {
        uart_rx_restart_required = 1u;
    }
}

void Bsp_TransportOnUartError(UART_HandleTypeDef *uart)
{
    if ((uart != NULL) && (uart->Instance == USART1))
    {
        uart_rx_restart_required = 1u;
    }
}
