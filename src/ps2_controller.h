/*
 * Bit-banged PlayStation 2 controller driver for the Raspberry Pi Pico (Pico SDK).
 */
#ifndef PS2_CONTROLLER_H
#define PS2_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>
#include "pico/types.h"

/* Half clock period (us). Needs an external ~10k pull-up on DATA; with only
 * the Pico's weak internal pull-up, raise this to ~50. */
#ifndef PS2_CLK_DELAY_US
#define PS2_CLK_DELAY_US     10
#endif
#define PS2_BYTE_DELAY_US    10
#define PS2_MAX_READ_DELAY_MS 10   /* give up configuring analog mode after this */
#define PS2_READ_INTERVAL_MS 2     /* how often main polls the controller */

/* Controller modes (byte 1 of the reply) */
#define PS2_MODE_DIGITAL     0x41
#define PS2_MODE_ANALOG      0x73

/* Bit positions in byte 3 */
#define PS2_BTN_SELECT     0
#define PS2_BTN_LEFT_JOY   1
#define PS2_BTN_RIGHT_JOY  2
#define PS2_BTN_START      3
#define PS2_BTN_UP         4
#define PS2_BTN_RIGHT      5
#define PS2_BTN_DOWN       6
#define PS2_BTN_LEFT       7
/* Bit positions in byte 4 */
#define PS2_BTN_L2         0
#define PS2_BTN_R2         1
#define PS2_BTN_L1         2
#define PS2_BTN_R1         3
#define PS2_BTN_TRIANGLE   4
#define PS2_BTN_CIRCLE     5
#define PS2_BTN_X          6
#define PS2_BTN_SQUARE     7

/* Byte offsets in the reply */
#define PS2_IDX_MODE   1
#define PS2_IDX_BTN1   3   /* active-low */
#define PS2_IDX_BTN2   4   /* active-low */
#define PS2_IDX_RX     5
#define PS2_IDX_RY     6
#define PS2_IDX_LX     7
#define PS2_IDX_LY     8

#define PS2_DATA_LEN   9

typedef struct {
    uint data_pin, cmd_pin, attn_pin, clk_pin;
    uint32_t read_delay_ms;      /* minimum gap between transfers */
    uint32_t last_read_ms;
    uint8_t  controller_mode;    /* PS2_MODE_ANALOG or PS2_MODE_DIGITAL */
    uint8_t  data[PS2_DATA_LEN]; /* last reply */
} ps2_t;

/* Set up the pins and the controller. Locks it in analog mode if it supports
 * that, otherwise accepts digital mode (Pop'n Music, PS1 pads).
 * Returns false if no controller answers. */
bool ps2_init(ps2_t *ps2, uint data_pin, uint cmd_pin, uint attn_pin, uint clk_pin);

/* Poll the controller; the reply lands in ps2->data. */
void ps2_read(ps2_t *ps2);

/* True if the last reply is valid and in the mode set up by ps2_init.
 * Goes false when the controller is unplugged. */
static inline bool ps2_ok(const ps2_t *ps2) {
    return ps2->data[2] == 0x5A && ps2->data[PS2_IDX_MODE] == ps2->controller_mode;
}

#endif
