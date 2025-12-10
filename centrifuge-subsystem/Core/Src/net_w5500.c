#include "net_w5500.h"

/* WIZnet ioLibrary (you placed headers in Inc/ and sources in Src/) */
#include "wizchip_conf.h"
#include "w5500.h"
#include "socket.h"
#include "dhcp.h"
#include "dns.h"

#include <stdio.h>
#include <string.h>

/* Cube-generated handles */
extern SPI_HandleTypeDef  hspi1;
extern UART_HandleTypeDef hlpuart1;

/* ===== SPI ↔ W5500 glue ===== */

static inline void wizchip_select(void)   { HAL_GPIO_WritePin(W5500_CS_GPIO_Port,  W5500_CS_Pin,  GPIO_PIN_RESET); }
static inline void wizchip_deselect(void) { HAL_GPIO_WritePin(W5500_CS_GPIO_Port,  W5500_CS_Pin,  GPIO_PIN_SET);   }

static uint8_t wizchip_read(void) {
  uint8_t tx=0xFF, rx=0x00;
  HAL_SPI_TransmitReceive(&hspi1, &tx, &rx, 1, HAL_MAX_DELAY);
  return rx;
}
static void wizchip_write(uint8_t wb) {
  uint8_t dummy;
  HAL_SPI_TransmitReceive(&hspi1, &wb, &dummy, 1, HAL_MAX_DELAY);
}

static void wizchip_readburst(uint8_t *p, uint16_t l)  {
  // To READ 'l' bytes, we must SEND 'l' dummy bytes.
  // We can send the (garbage) contents of the 'p' buffer as dummy data.
  // The 'p' buffer will be OVERWRITTEN with the data we receive.
  HAL_SPI_TransmitReceive(&hspi1, p, p, l, HAL_MAX_DELAY);
}
static void wizchip_writeburst(uint8_t *p, uint16_t l) {
  // To WRITE 'l' bytes, we must also READ 'l' dummy bytes.
  // We send the data from 'p'.
  // We can receive the (garbage) dummy data back into the 'p' buffer.
  HAL_SPI_TransmitReceive(&hspi1, p, p, l, HAL_MAX_DELAY);
}

static void wizchip_hw_reset(void) {
  HAL_GPIO_WritePin(W5500_RST_GPIO_Port, W5500_RST_Pin, GPIO_PIN_RESET);
  HAL_Delay(2);    /* >= 500us */
  HAL_GPIO_WritePin(W5500_RST_GPIO_Port, W5500_RST_Pin, GPIO_PIN_SET);
  HAL_Delay(50);   /* allow PLL/PHY settle */
}

/* ===== Basic bring-up ===== */

int w5500_chip_init(void)
{
  uint8_t memsize[2][8] = { /* TX and RX per socket in KB */
    {2,2,2,2,2,2,2,2},
    {2,2,2,2,2,2,2,2}
  };

  wizchip_hw_reset();

  reg_wizchip_cs_cbfunc     (wizchip_select, wizchip_deselect);
  reg_wizchip_spi_cbfunc    (wizchip_read, wizchip_write);
  reg_wizchip_spiburst_cbfunc(wizchip_readburst, wizchip_writeburst);

  /* Some ioLibrary versions return 0 on success (no RET_OK macro) */
  int8_t rc = ctlwizchip(CW_INIT_WIZCHIP, (void*)memsize);
  if (rc != 0) {
    printf("CW_INIT_WIZCHIP failed (rc=%d)\r\n", rc);
    return -1;
  }

  /* Not fatal if this differs, but helpful sanity check */
//  if (getVERSIONR() != 0x04) {
//    printf("W5500 VERSIONR=0x%02X\r\n", getVERSIONR());
//  }
  return 0;
}

/* ===== Networking (DHCP → static fallback) ===== */

static uint8_t dhcp_workbuf[548];

static wiz_NetInfo g_netinfo = {
  .mac  = {0x02,0xAB,0xCD,0x01,0x00,0x02}, /* choose your own unique MACs */
  .ip   = {0,0,0,0},
  .sn   = {255,255,255,0},
  .gw   = {0,0,0,0},
  .dns  = {8,8,8,8},
  .dhcp = NETINFO_DHCP
};

static const wiz_NetInfo g_static_fallback = {
  .mac  = {0x02,0xAB,0xCD,0x01,0,2},
  .ip   = {192,168,1,49},
  .sn   = {255,255,255,0},
  .gw   = {192,168,1,1},
  .dns  = {8,8,8,8},
  .dhcp = NETINFO_STATIC
};

void net_print_info(void)
{
  wiz_NetInfo n; ctlnetwork(CN_GET_NETINFO, &n);
  printf("MAC %02X:%02X:%02X:%02X:%02X:%02X  "
         "IP %d.%d.%d.%d  GW %d.%d.%d.%d  SN %d.%d.%d.%d  %s\r\n",
         n.mac[0],n.mac[1],n.mac[2],n.mac[3],n.mac[4],n.mac[5],
         n.ip[0],n.ip[1],n.ip[2],n.ip[3],
         n.gw[0],n.gw[1],n.gw[2],n.gw[3],
         n.sn[0],n.sn[1],n.sn[2],n.sn[3],
         (n.dhcp==NETINFO_DHCP) ? "DHCP" : "STATIC");
}

static inline void dhcp_tick_if_needed(uint32_t *last_ms) {
  uint32_t now = HAL_GetTick();
  if (now - *last_ms >= 1000U) {
    DHCP_time_handler();
    *last_ms += 1000U;
  }
}

void ethernet_start(void)
{
  printf("1. Initializing W5500 chip...\r\n");
  if (w5500_chip_init() != 0) {
    printf("   FATAL: W5500 chip init failed.\r\n");
    return;
  }

  // WE NEED THIS DELAY! DO NOT REMOVE IT!
  // do i know why this delay is needed? NO.
  HAL_Delay(1000);

  /* Force static configuration */
  wiz_NetInfo ni = g_static_fallback;   // or fill a local struct here
  ni.dhcp = NETINFO_STATIC;             // make sure it's marked STATIC
  ctlnetwork(CN_SET_NETINFO, &ni);

  /* Optional: wait for link (PHYCFGR.LNK) before continuing */
  // while ((getPHYCFGR() & PHYCFGR_LNK_ON) == 0) HAL_Delay(10);

  printf("2. Static IP configured:\r\n");
  net_print_info();
}

//void ethernet_start(void)
//{
//  printf("1. Initializing W5500 chip...\r\n");
//  if (w5500_chip_init() != 0) {
//    printf("   FATAL: W5500 chip init failed.\r\n");
//    return;
//  }
//
//  // 1. Set network config for DHCP
//  ctlnetwork(CN_SET_NETINFO, &g_netinfo);
//  printf("2. Attempting DHCP (15s timeout)...\r\n");
//
//  DHCP_init(0, dhcp_workbuf);
//
//  uint32_t start = HAL_GetTick(), last = start;
//  int leased = 0; // 0=running, 1=leased, -1=failed
//
//  while ((HAL_GetTick() - start) < 15000U) {
//    dhcp_tick_if_needed(&last);
//    switch (DHCP_run()) {
//      case DHCP_IP_LEASED:
//        leased = 1;
//        printf("   DHCP IP Leased!\r\n");
//        break;
//      case DHCP_FAILED:
//      case DHCP_STOPPED:
//        leased = -1;
//        printf("   DHCP Failed.\r\n");
//        break;
//      default: break;
//    }
//    if (leased != 0) break;
//    HAL_Delay(50);
//  }
//
//  // 2. If DHCP failed or timed out, apply static fallback
//  if (leased <= 0) {
//    if (leased == 0) printf("   DHCP Timeout.\r\n");
//    printf("3. Setting static fallback IP.\r\n");
//    ctlnetwork(CN_SET_NETINFO, (void*)&g_static_fallback);
//  }
//
//  // 3. Print final network info
//  printf("4. Network configuration complete:\r\n");
//  net_print_info();
//}

/* ===== Minimal single-client TCP server + ring buffer ===== */

static const uint8_t S_SRV = 1;               /* socket index */
static uint16_t      g_port = NET_SERVER_PORT;

static uint8_t  rx_ring[NET_RX_BUFSZ];
static uint16_t rx_head = 0, rx_tail = 0;     /* head: write, tail: read */

static inline uint16_t rb_count(void) {
  return (uint16_t)((NET_RX_BUFSZ + rx_head - rx_tail) % NET_RX_BUFSZ);
}
static inline uint16_t rb_space(void) {
  return (uint16_t)(NET_RX_BUFSZ - 1 - rb_count());
}
static inline void rb_push_byte(uint8_t b) {
  if (rb_space() == 0) return;                /* drop on overflow */
  rx_ring[rx_head] = b;
  rx_head = (uint16_t)((rx_head + 1) % NET_RX_BUFSZ);
}
static inline int rb_pop_byte(uint8_t *out) {
  if (rb_count() == 0) return 0;
  *out = rx_ring[rx_tail];
  rx_tail = (uint16_t)((rx_tail + 1) % NET_RX_BUFSZ);
  return 1;
}

void net_start_server(uint16_t port)
{
  g_port = port;
  rx_head = rx_tail = 0;

  if (getSn_SR(S_SRV) != SOCK_CLOSED) {
    close(S_SRV);
  }
  if (socket(S_SRV, Sn_MR_TCP, g_port, 0) == S_SRV) {
    listen(S_SRV);
    printf("TCP server listening on :%u\r\n", (unsigned)g_port);
  } else {
    printf("socket(%u) failed\r\n", (unsigned)g_port);
  }
}

int net_is_connected(void)
{
  uint8_t st = getSn_SR(S_SRV);
  return (st == SOCK_ESTABLISHED) ? 1 : 0;
}

void net_close_client(void)
{
  disconnect(S_SRV);
  close(S_SRV);
  /* Reopen listening socket */
  if (socket(S_SRV, Sn_MR_TCP, g_port, 0) == S_SRV) {
    listen(S_SRV);
  }
}

void net_poll(void)
{
  uint8_t st = getSn_SR(S_SRV);

  if (st == SOCK_CLOSED) {
    /* Reopen & listen */
    if (socket(S_SRV, Sn_MR_TCP, g_port, 0) == S_SRV) listen(S_SRV);
    return;
  }

  if (st == SOCK_INIT) {
    listen(S_SRV);
    return;
  }

  if (st == SOCK_CLOSE_WAIT) {
    /* Finish and reset */
    disconnect(S_SRV);
    close(S_SRV);
    socket(S_SRV, Sn_MR_TCP, g_port, 0);
    listen(S_SRV);
    return;
  }

  if (st == SOCK_ESTABLISHED) {
    for (;;) {
      uint16_t room = rb_space();
      if (room == 0) break;

      /* how many bytes are waiting in the W5500 RX buffer? */
      uint16_t avail = getSn_RX_RSR(S_SRV);
      if (avail == 0) break;

      /* take the smaller of (avail, our ring space, a temp cap) */
      uint16_t take = avail;
      if (take > room)   take = room;
      if (take > 256)    take = 256;

      uint8_t tmp[256];
      int32_t r = recv(S_SRV, tmp, take);

      if (r > 0) {
        for (int32_t i = 0; i < r; i++) rb_push_byte(tmp[i]);
        /* loop again in case more data is already queued */
        continue;
      }

      /* r <= 0: if it's just busy/len, try again later; otherwise reset socket */
      /* Typical values: SOCK_BUSY or SOCKERR_DATALEN. Either way, stop for now. */
      break;
    }
  }
}

/* ===== Send/Receive APIs ===== */

int net_send(const void *data, uint16_t len)
{
  if (!net_is_connected()) return -1;

  const uint8_t *p = (const uint8_t*)data;
  uint16_t left = len;
  uint32_t deadline = HAL_GetTick() + 300;  // try for up to ~300 ms

  while (left) {
    /* ensure there is TX space */
    uint16_t fre = getSn_TX_FSR(S_SRV);
    if (fre == 0) {
      if (HAL_GetTick() >= deadline) break;
      HAL_Delay(1);
      continue;
    }

    uint16_t chunk = left;
    if (chunk > fre)  chunk = fre;   // don't exceed TX free space
    if (chunk > 1024) chunk = 1024;  // reasonable cap per write

    int32_t n = send(S_SRV, (uint8_t*)p, chunk);
    if (n > 0) { p += n; left -= (uint16_t)n; continue; }

    /* n <= 0: usually SOCK_BUSY. Give it a moment and retry. */
    if (HAL_GetTick() >= deadline) break;
    HAL_Delay(1);
  }

  return (int)(len - left);
}

int net_send_str(const char *s)
{
  return net_send(s, (uint16_t)strlen(s));
}

int net_recv_available(void)
{
  return (int)rb_count();
}

int net_recv(uint8_t *dst, uint16_t maxlen)
{
  uint16_t n = 0;
  while (n < maxlen) {
    uint8_t b;
    if (!rb_pop_byte(&b)) break;
    dst[n++] = b;
  }
  return (int)n;
}

/* Helpers for peeking/dropping without consuming prematurely */
static inline uint8_t rb_peek_at(uint16_t off) {
  return rx_ring[(uint16_t)((rx_tail + off) % NET_RX_BUFSZ)];
}
static inline void rb_drop_n(uint16_t n) {
  rx_tail = (uint16_t)((rx_tail + n) % NET_RX_BUFSZ);
}

int net_recv_line(char *dst, uint16_t maxlen, uint32_t timeout_ms)
{
  if (maxlen < 2) return -2;
  uint32_t start = HAL_GetTick();

  for (;;) {
    uint16_t cnt = rb_count();
    int16_t term_pos = -1;
    for (uint16_t i = 0; i < cnt; i++) {
      uint8_t c = rb_peek_at(i);
      if (c == '\n' || c == '\r') { term_pos = (int16_t)i; break; }
    }

    if (term_pos >= 0) {
      uint8_t term_char = rb_peek_at((uint16_t)term_pos);   // remember CR vs LF

      uint16_t copy_len = (uint16_t)term_pos;
      if (copy_len >= (uint16_t)(maxlen - 1)) copy_len = (uint16_t)(maxlen - 1);

      for (uint16_t j = 0; j < copy_len; j++) dst[j] = rb_peek_at(j);
      dst[copy_len] = '\0';

      rb_drop_n((uint16_t)term_pos + 1); // drop line + terminator

      /* If terminator was CR and next is LF, drop LF too */
      if (term_char == '\r' && rb_count() && rb_peek_at(0) == '\n') {
        rb_drop_n(1);
      }

      return (int)copy_len;
    }

    if (timeout_ms == 0) return 0;
    if ((HAL_GetTick() - start) >= timeout_ms) return 0;

    net_poll();
    HAL_Delay(1);
  }
}

/* ===== Interrupt hook ===== */
void net_w5500_irq_handler(void)
{
  /* See which sockets fired */
  uint8_t sir_mask = getSIR();  /* bit i=1 → socket i has pending IRQ */

  /* Clear per-socket interrupt flags: write '1' to the bits to clear */
  for (uint8_t s = 0; s < 8; s++) {
    if ((sir_mask & (1u << s)) == 0) continue;           /* skip if no IRQ */
    uint8_t sir = getSn_IR(s);
    if (sir) setSn_IR(s, sir);                           /* two args: (sn, ir) */
  }

  /* Clear common interrupt register last (write-1-to-clear) */
  uint8_t cir = getIR();
  if (cir) setIR(cir);
}
