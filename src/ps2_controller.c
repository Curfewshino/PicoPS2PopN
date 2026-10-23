/*
 * Bit-banged PlayStation 2 controller driver for the Raspberry Pi Pico (Pico SDK).
 *
 * The PS2 protocol is SPI mode 3, LSB first: the clock idles high, the
 * command bit is set while the clock is high and the data bit is read while
 * it is low. DATA is open-drain and needs a pull-up.
 */
#include "ps2_controller.h"

#include <string.h>
#include "pico/stdlib.h"

static const uint8_t cmd_poll[]             = {0x01, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t cmd_enter_config[]     = {0x01, 0x43, 0x00, 0x01, 0x00};
static const uint8_t cmd_set_analog_lock[]  = {0x01, 0x44, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00};
static const uint8_t cmd_exit_config[]      = {0x01, 0x43, 0x00, 0x00, 0x5A, 0x5A, 0x5A, 0x5A, 0x5A};

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static uint8_t transfer_byte(const ps2_t *ps2, uint8_t out) {
    uint8_t in = 0;
    for (int i = 0; i < 8; i++) {
        gpio_put(ps2->cmd_pin, (out >> i) & 1u);
        sleep_us(1);
        gpio_put(ps2->clk_pin, 0);
        sleep_us(PS2_CLK_DELAY_US);
        if (gpio_get(ps2->data_pin))
            in |= (uint8_t)(1u << i);
        gpio_put(ps2->clk_pin, 1);
        sleep_us(PS2_CLK_DELAY_US);
    }
    gpio_put(ps2->cmd_pin, 1);
    sleep_us(PS2_BYTE_DELAY_US);
    return in;
}

/* One packet with ATTENTION held low. The reply (up to PS2_DATA_LEN bytes)
 * goes to ps2->data. */
static void transfer(ps2_t *ps2, const uint8_t *cmd, size_t len) {
    uint32_t since = now_ms() - ps2->last_read_ms;
    if (since < ps2->read_delay_ms)
        sleep_ms(ps2->read_delay_ms - since);

    gpio_put(ps2->attn_pin, 0);
    sleep_us(PS2_BYTE_DELAY_US);
    for (size_t i = 0; i < len; i++) {
        uint8_t in = transfer_byte(ps2, cmd[i]);
        if (i < PS2_DATA_LEN)
            ps2->data[i] = in;
    }
    gpio_put(ps2->attn_pin, 1);
    ps2->last_read_ms = now_ms();
}

static void pin_out(uint pin) {
    gpio_init(pin);
    gpio_put(pin, 1);
    gpio_set_dir(pin, GPIO_OUT);
}

bool ps2_init(ps2_t *ps2, uint data_pin, uint cmd_pin, uint attn_pin, uint clk_pin) {
    memset(ps2, 0, sizeof *ps2);
    ps2->data_pin = data_pin;
    ps2->cmd_pin  = cmd_pin;
    ps2->attn_pin = attn_pin;
    ps2->clk_pin  = clk_pin;
    ps2->read_delay_ms = 1;

    gpio_init(data_pin);
    gpio_set_dir(data_pin, GPIO_IN);
    gpio_pull_up(data_pin);
    pin_out(cmd_pin);
    pin_out(clk_pin);
    pin_out(attn_pin);

    /* Wake the controller up. */
    transfer(ps2, cmd_poll, sizeof cmd_poll);
    transfer(ps2, cmd_poll, sizeof cmd_poll);

    /* Try to lock analog mode, growing the delay between packets until the
     * controller accepts it (wireless pads can need a few ms). */
    for (;;) {
        transfer(ps2, cmd_enter_config, sizeof cmd_enter_config);
        transfer(ps2, cmd_set_analog_lock, sizeof cmd_set_analog_lock);
        transfer(ps2, cmd_exit_config, sizeof cmd_exit_config);
        transfer(ps2, cmd_poll, sizeof cmd_poll);
        if (ps2->data[2] == 0x5A && ps2->data[PS2_IDX_MODE] == PS2_MODE_ANALOG) {
            ps2->controller_mode = PS2_MODE_ANALOG;
            return true;
        }
        if (ps2->read_delay_ms >= PS2_MAX_READ_DELAY_MS)
            break;
        ps2->read_delay_ms++;
    }

    /* No analog mode: fine if it answers as a digital controller. */
    ps2->read_delay_ms = 1;
    transfer(ps2, cmd_poll, sizeof cmd_poll);
    ps2->controller_mode = PS2_MODE_DIGITAL;
    return ps2_ok(ps2);
}

void ps2_read(ps2_t *ps2) {
    transfer(ps2, cmd_poll, sizeof cmd_poll);
}
