#include "camtcp_capture.h"
#include "netconf.h"
#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
#include <string.h>

/* camera -> tcp handoff (written by camera ISR) */
volatile uint8_t  g_camtcp_cap_ready = 0;
volatile uint32_t g_camtcp_cap_seq   = 0;
volatile uint8_t* g_camtcp_cap_ptr   = 0;
volatile uint32_t g_camtcp_cap_len   = 0;

/* result from PC */
volatile uint8_t g_camtcp_result_ready = 0;
camtcp_result_t g_camtcp_result;

/* capture request flag (set by main, consumed by camera ISR) */
volatile uint8_t g_camtcp_cap_request = 0;

static struct tcp_pcb* s_pcb = 0;
static uint8_t s_connected = 0;

typedef enum
{
  TX_IDLE = 0,
  TX_HDR,
  TX_META,
  TX_IMG
} tx_state_t;

static tx_state_t s_tx_state = TX_IDLE;
static const uint8_t* s_tx_ptr = 0;
static uint32_t s_tx_remain = 0;
static uint32_t s_tx_seq = 0;

/* header(16) + meta(6) */
static uint8_t s_hdr[16];
static uint8_t s_meta[6];

#define RXBUF_SZ 512
static uint8_t s_rxbuf[RXBUF_SZ];
static uint16_t s_rxlen = 0;

static void wr_u16be(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)(v & 0xFF); }
static void wr_u32be(uint8_t* p, uint32_t v)
{
  p[0] = (uint8_t)(v >> 24);
  p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);
  p[3] = (uint8_t)(v & 0xFF);
}
static uint16_t rd_u16be(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd_u32be(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static void build_header(uint16_t type, uint32_t seq, uint32_t payload_len)
{
  wr_u32be(&s_hdr[0], CAMTCP_MAGIC);
  wr_u16be(&s_hdr[4], type);
  wr_u16be(&s_hdr[6], 0);
  wr_u32be(&s_hdr[8], seq);
  wr_u32be(&s_hdr[12], payload_len);
}

static void build_meta(void)
{
  wr_u16be(&s_meta[0], (uint16_t)CAMTCP_W);
  wr_u16be(&s_meta[2], (uint16_t)CAMTCP_H);
  s_meta[4] = (uint8_t)CAMTCP_FMT_RGB565;
  s_meta[5] = 0;
}

static void close_conn(struct tcp_pcb* tpcb)
{
  if (!tpcb) return;
  tcp_arg(tpcb, NULL);
  tcp_sent(tpcb, NULL);
  tcp_recv(tpcb, NULL);
  tcp_poll(tpcb, NULL, 0);
  tcp_err(tpcb, NULL);
  tcp_close(tpcb);

  s_pcb = 0;
  s_connected = 0;
  s_tx_state = TX_IDLE;
  s_rxlen = 0;
}

static void tx_kick(struct tcp_pcb* tpcb)
{
  if (!s_connected || !tpcb) return;

  while (1)
  {
    if (s_tx_state == TX_IDLE) return;

    uint16_t snd = tcp_sndbuf(tpcb);
    if (snd == 0) return;

    if (s_tx_remain == 0)
    {
      if (s_tx_state == TX_HDR)
      {
        s_tx_state = TX_META;
        s_tx_ptr = s_meta;
        s_tx_remain = sizeof(s_meta);
        continue;
      }
      if (s_tx_state == TX_META)
      {
        s_tx_state = TX_IMG;
        s_tx_ptr = (const uint8_t*)g_camtcp_cap_ptr;
        s_tx_remain = g_camtcp_cap_len;
        continue;
      }
      s_tx_state = TX_IDLE;
      return;
    }

    uint32_t can = s_tx_remain;
    if (can > snd) can = snd;
    if (can > 4096) can = 4096;

    err_t e = tcp_write(tpcb, s_tx_ptr, (u16_t)can, TCP_WRITE_FLAG_COPY);
    if (e == ERR_OK)
    {
      s_tx_ptr += can;
      s_tx_remain -= can;
      tcp_output(tpcb);
      continue;
    }
    if (e == ERR_MEM)
    {
      return;
    }

    close_conn(tpcb);
    return;
  }
}

static void rx_parse(void)
{
  while (s_rxlen >= 16)
  {
    if (rd_u32be(&s_rxbuf[0]) != CAMTCP_MAGIC)
    {
      memmove(s_rxbuf, s_rxbuf + 1, s_rxlen - 1);
      s_rxlen -= 1;
      continue;
    }

    uint16_t type = rd_u16be(&s_rxbuf[4]);
    uint32_t len = rd_u32be(&s_rxbuf[12]);
    if (len > (RXBUF_SZ - 16))
    {
      s_rxlen = 0;
      return;
    }
    if (s_rxlen < (uint16_t)(16 + len)) return;

    const uint8_t* payload = &s_rxbuf[16];
    if (type == CAMTCP_TYPE_RESULT)
    {
      if (len >= 8)
      {
        camtcp_result_t r;
        memset(&r, 0, sizeof(r));
        r.frame_seq = rd_u32be(&payload[0]);
        r.okng = payload[4];
        r.num_boxes = payload[5];
        if (r.num_boxes > 32) r.num_boxes = 32;

        uint32_t off = 8;
        for (uint8_t i = 0; i < r.num_boxes; i++)
        {
          if (off + 10 > len) break;
          r.box[i].x = rd_u16be(&payload[off + 0]);
          r.box[i].y = rd_u16be(&payload[off + 2]);
          r.box[i].w = rd_u16be(&payload[off + 4]);
          r.box[i].h = rd_u16be(&payload[off + 6]);
          r.box[i].cls = payload[off + 8];
          r.box[i].score = payload[off + 9];
          off += 10;
        }
        g_camtcp_result = r;
        g_camtcp_result_ready = 1;
      }
    }

    uint16_t consumed = (uint16_t)(16 + len);
    memmove(s_rxbuf, s_rxbuf + consumed, s_rxlen - consumed);
    s_rxlen -= consumed;
  }
}

static err_t on_connected(void* arg, struct tcp_pcb* tpcb, err_t err)
{
  (void)arg;
  if (err != ERR_OK)
  {
    close_conn(tpcb);
    return err;
  }
  s_connected = 1;
  s_tx_state = TX_IDLE;
  s_rxlen = 0;
  return ERR_OK;
}

static err_t on_sent(void* arg, struct tcp_pcb* tpcb, u16_t len)
{
  (void)arg; (void)len;
  tx_kick(tpcb);
  return ERR_OK;
}

static err_t on_poll(void* arg, struct tcp_pcb* tpcb)
{
  (void)arg;
  tx_kick(tpcb);
  return ERR_OK;
}

static err_t on_recv(void* arg, struct tcp_pcb* tpcb, struct pbuf* p, err_t err)
{
  (void)arg;
  if (err != ERR_OK)
  {
    if (p) pbuf_free(p);
    return err;
  }
  if (p == NULL)
  {
    close_conn(tpcb);
    return ERR_OK;
  }

  tcp_recved(tpcb, p->tot_len);

  for (struct pbuf* q = p; q; q = q->next)
  {
    uint16_t room = (uint16_t)(RXBUF_SZ - s_rxlen);
    uint16_t cp = q->len;
    if (cp > room) cp = room;
    if (cp > 0)
    {
      memcpy(&s_rxbuf[s_rxlen], q->payload, cp);
      s_rxlen += cp;
    }
    else
    {
      s_rxlen = 0;
    }
  }

  pbuf_free(p);
  rx_parse();
  return ERR_OK;
}

static void on_err(void* arg, err_t err)
{
  (void)arg; (void)err;
  /* pcb already freed by lwIP */
  s_pcb = 0;
  s_connected = 0;
  s_tx_state = TX_IDLE;
  s_rxlen = 0;
}

void camtcp_init(void)
{
  ip_addr_t dest;
  IP4_ADDR(&dest, DEST_IP_ADDR0, DEST_IP_ADDR1, DEST_IP_ADDR2, DEST_IP_ADDR3);

  if (s_pcb)
  {
    tcp_abort(s_pcb);
    s_pcb = 0;
  }

  s_pcb = tcp_new();
  if (!s_pcb) return;

  tcp_arg(s_pcb, NULL);
  tcp_poll(s_pcb, on_poll, 2);
  tcp_recv(s_pcb, on_recv);
  tcp_sent(s_pcb, on_sent);
  tcp_err(s_pcb, on_err);

  tcp_connect(s_pcb, &dest, DEST_PORT, on_connected);
}

void camtcp_poll(void)
{
  if (!s_connected || !s_pcb) return;

  if (s_tx_state == TX_IDLE && g_camtcp_cap_ready && g_camtcp_cap_ptr && g_camtcp_cap_len == CAMTCP_FRAME_BYTES)
  {
    g_camtcp_cap_ready = 0;
    s_tx_seq = g_camtcp_cap_seq;

    build_meta();
    build_header(CAMTCP_TYPE_IMAGE, s_tx_seq, (uint32_t)(sizeof(s_meta) + g_camtcp_cap_len));

    s_tx_state = TX_HDR;
    s_tx_ptr = s_hdr;
    s_tx_remain = sizeof(s_hdr);
    tx_kick(s_pcb);
    return;
  }

  tx_kick(s_pcb);
}

void camtcp_request_capture(void)
{
  if (g_camtcp_cap_request) return;
  if (g_camtcp_cap_ready) return;
  g_camtcp_cap_request = 1;
}

uint8_t camtcp_is_connected(void) { return s_connected; }
uint8_t camtcp_is_busy(void) { return (uint8_t)(s_tx_state != TX_IDLE); }

