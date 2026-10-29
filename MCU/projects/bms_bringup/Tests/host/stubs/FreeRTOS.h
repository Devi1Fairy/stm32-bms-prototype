#ifndef HOST_FREERTOS_H
#define HOST_FREERTOS_H
#include <stddef.h>
#include <stdint.h>
typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
#define pdPASS 1
#define pdFALSE 0
#define portMAX_DELAY UINT32_MAX
extern uint32_t mock_isr_sends;
extern uint32_t mock_task_sends;
extern uint32_t mock_yields;
#define portYIELD_FROM_ISR(woken) do { (void)(woken); mock_yields++; } while (0)
#endif
