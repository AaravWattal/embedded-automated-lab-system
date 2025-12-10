#pragma once
#include <stdint.h>
#include "stm32l4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Pin defaults (match your wiring) --- */
#ifndef W5500_CS_GPIO_Port
#define W5500_CS_GPIO_Port   GPIOD
#define W5500_CS_Pin         GPIO_PIN_14
#endif

#ifndef W5500_RST_GPIO_Port
#define W5500_RST_GPIO_Port  GPIOD
#define W5500_RST_Pin        GPIO_PIN_15
#endif

#ifndef W5500_INT_GPIO_Port
#define W5500_INT_GPIO_Port  GPIOF
#define W5500_INT_Pin        GPIO_PIN_15
#endif

/* --- Config --- */
#ifndef NET_SERVER_PORT
#define NET_SERVER_PORT      5000
#endif

#ifndef NET_RX_BUFSZ
#define NET_RX_BUFSZ         1024   /* ring buffer for received bytes */
#endif

/* ===== One-time bring-up ===== */
int  w5500_chip_init(void);     /* SPI callbacks + CW_INIT_WIZCHIP */
void ethernet_start(void);      /* DHCP, then static fallback */
void net_print_info(void);

/* ===== TCP server lifecycle ===== */
void net_start_server(uint16_t port);    /* begin listening (one client) */
void net_poll(void);                     /* call often in main loop */
int  net_is_connected(void);             /* 1 if a client is connected */
void net_close_client(void);             /* drop the current client */

/* ===== Minimal send/receive helpers ===== */
int  net_send(const void *data, uint16_t len);      /* returns bytes sent or <0 */
int  net_send_str(const char *s);                   /* convenience */

int  net_recv_available(void);                      /* unread bytes in buffer */
int  net_recv(uint8_t *dst, uint16_t maxlen);       /* pop up to maxlen bytes */

/* Read a '\n'-terminated line (without the trailing '\n').
 * If timeout_ms==0, it is non-blocking (returns 0 if no full line yet).
 * Returns length (>=1) on success, 0 on timeout/no line, <0 on error.
 */
int  net_recv_line(char *dst, uint16_t maxlen, uint32_t timeout_ms);

/* Call this from HAL_GPIO_EXTI_Callback when W5500 INT asserts. */
void net_w5500_irq_handler(void);

#ifdef __cplusplus
}
#endif
