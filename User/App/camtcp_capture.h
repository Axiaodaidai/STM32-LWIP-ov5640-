#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fixed image format for this project */
#define CAMTCP_W 480u
#define CAMTCP_H 272u
#define CAMTCP_FMT_RGB565 1u
#define CAMTCP_FRAME_BYTES (CAMTCP_W * CAMTCP_H * 2u)

/* TCP framed protocol */
#define CAMTCP_MAGIC 0xA55AA55Au
#define CAMTCP_TYPE_IMAGE  1u
#define CAMTCP_TYPE_RESULT 2u

typedef struct
{
  uint32_t frame_seq;
  uint8_t okng;        /* 0=NG, 1=OK */
  uint8_t num_boxes;
  struct
  {
    uint16_t x, y, w, h;
    uint8_t cls;
    uint8_t score;     /* 0..255 */
  } box[32];
} camtcp_result_t;

/* camera -> tcp handoff (written by camera ISR) */
extern volatile uint8_t  g_camtcp_cap_ready;
extern volatile uint32_t g_camtcp_cap_seq;
extern volatile uint8_t* g_camtcp_cap_ptr;
extern volatile uint32_t g_camtcp_cap_len;

/* result from PC (written by tcp recv parser) */
extern volatile uint8_t g_camtcp_result_ready;
extern camtcp_result_t g_camtcp_result;

void camtcp_init(void);      /* start/connect */
void camtcp_poll(void);      /* call frequently in main loop */

/* request next captured frame to be sent once */
void camtcp_request_capture(void);

uint8_t camtcp_is_connected(void);
uint8_t camtcp_is_busy(void); /* currently sending */

#ifdef __cplusplus
}
#endif

