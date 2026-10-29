#ifndef HOST_STM32F1XX_H
#define HOST_STM32F1XX_H
#include <stdint.h>
extern uint32_t mock_ipsr;
extern uint32_t mock_primask;
static inline uint32_t __get_IPSR(void) { return mock_ipsr; }
static inline uint32_t __get_PRIMASK(void) { return mock_primask; }
static inline void __disable_irq(void) { mock_primask = 1u; }
static inline void __set_PRIMASK(uint32_t value) { mock_primask = value; }
#endif
