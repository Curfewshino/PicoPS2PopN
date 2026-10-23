/*
 * PS2 controller -> USB adapter for the Raspberry Pi Pico (Pico SDK + TinyUSB).
 *
 * Wiring:
 *   GP2 <- DATA (brown, 10k pull-up to 3V3)   GP3 -> COMMAND (orange)
 *   GP4 -> ATTENTION (yellow)                  GP5 -> CLOCK (blue)
 *   GP6 <- ACK (green, unused)                 3V3 -> VCC (red), GND -> GND (black)
 *
 * Output mode, picked from the buttons held when the controller first connects:
 *   nothing         Pop'n controller: HID controller (Button 1-9, Select, Start)
 *                   other PS2 pads:   HID gamepad
 *   SELECT          USB keyboard
 *   SELECT + START  reboot into the BOOTSEL bootloader (for flashing)
 */
#include <string.h>

#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "tusb.h"

#include "ps2_controller.h"
#include "usb_descriptors.h"

#define PIN_DATA  2
#define PIN_CMD   3
#define PIN_ATTN  4
#define PIN_CLK   5   /* ACK (GP6) is not used */

/* Gamepad mode: 1 = D-pad as a hat switch, 0 = D-pad as buttons 5-8. */
#ifndef DPAD_AS_HAT
#define DPAD_AS_HAT 0
#endif

/* Buttons are handled as one 16-bit, active-high mask:
 * bits 0-7 = packet byte 3 (Select..Left), bits 8-15 = byte 4 (L2..Square). */
#define BIT1(b) ((uint16_t)(1u << (b)))        /* byte-3 button */

typedef enum { MODE_NONE, MODE_POPN, MODE_GAMEPAD, MODE_KEYBOARD } out_mode_t;

/* ---- Pop'n Music controller ---- */

/* Pop'n controllers always report Left+Down+Right as held; that's how the
 * game recognises them. We use it the same way, then hide those bits. */
#define POPN_ID_MASK (BIT1(PS2_BTN_LEFT) | BIT1(PS2_BTN_DOWN) | BIT1(PS2_BTN_RIGHT))

/* Pop'n button number (1-9, 10 = SELECT, 11 = START) for each
 * bit of the button mask; 0 = not used by the Pop'n controller. */
static const uint8_t popn_button[16] = {
    [PS2_BTN_SELECT]       = 10,
    [PS2_BTN_START]        = 11,
    [PS2_BTN_UP]           = 8,
    [8 + PS2_BTN_TRIANGLE] = 1,
    [8 + PS2_BTN_CIRCLE]   = 2,
    [8 + PS2_BTN_R1]       = 3,
    [8 + PS2_BTN_X]        = 4,
    [8 + PS2_BTN_L1]       = 5,
    [8 + PS2_BTN_SQUARE]   = 6,
    [8 + PS2_BTN_R2]       = 7,
    [8 + PS2_BTN_L2]       = 9,
};

/* ---- Keyboard (HID key codes, must be below KEYBOARD_NUM_KEYS = 0x40) ---- */
static const uint8_t keymap[16] = {
    [PS2_BTN_SELECT]       = HID_KEY_SPACE,
    [PS2_BTN_LEFT_JOY]     = HID_KEY_Q,
    [PS2_BTN_RIGHT_JOY]    = HID_KEY_E,
    [PS2_BTN_START]        = HID_KEY_X,
    [PS2_BTN_UP]           = HID_KEY_W,
    [PS2_BTN_RIGHT]        = HID_KEY_D,
    [PS2_BTN_DOWN]         = HID_KEY_S,
    [PS2_BTN_LEFT]         = HID_KEY_A,
    [8 + PS2_BTN_L2]       = HID_KEY_R,
    [8 + PS2_BTN_R2]       = HID_KEY_T,
    [8 + PS2_BTN_L1]       = HID_KEY_F,
    [8 + PS2_BTN_R1]       = HID_KEY_G,
    [8 + PS2_BTN_TRIANGLE] = HID_KEY_I,
    [8 + PS2_BTN_CIRCLE]   = HID_KEY_O,
    [8 + PS2_BTN_X]        = HID_KEY_K,
    [8 + PS2_BTN_SQUARE]   = HID_KEY_L,
};

static ps2_t ps2;
static bool popn = false;

/* ---- Status LED: blinking = no controller, solid = connected ---- */

static void led_init(void) {
#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
#endif
}

static void led_set(bool on) {
#ifdef PICO_DEFAULT_LED_PIN
    gpio_put(PICO_DEFAULT_LED_PIN, on);
#else
    (void)on;
#endif
}

/* ---- Controller ---- */

static uint16_t raw_buttons(void) {
    return (uint16_t)~(ps2.data[PS2_IDX_BTN1] | (ps2.data[PS2_IDX_BTN2] << 8));
}

static uint16_t read_buttons(void) {
    uint16_t buttons = raw_buttons();
    return popn ? (uint16_t)(buttons & ~POPN_ID_MASK) : buttons;
}

static bool controller_setup(void) {
    if (!ps2_init(&ps2, PIN_DATA, PIN_CMD, PIN_ATTN, PIN_CLK))
        return false;
    popn = ps2.controller_mode == PS2_MODE_DIGITAL &&
           (raw_buttons() & POPN_ID_MASK) == POPN_ID_MASK;
    return true;
}

static out_mode_t pick_mode(uint16_t held) {
    const uint16_t sel = BIT1(PS2_BTN_SELECT), start = BIT1(PS2_BTN_START);
    if ((held & (sel | start)) == (sel | start))
        reset_usb_boot(0, 0);               /* never returns */
    if (held & sel)
        return MODE_KEYBOARD;
    return popn ? MODE_POPN : MODE_GAMEPAD;
}

/* ---- Outputs ---- */

/* Report bits 0-8 = Button 1-9, bit 9 = SELECT, bit 10 = START. */
static void send_popn(uint16_t buttons) {
    uint16_t report = 0;
    for (int b = 0; b < 16; b++)
        if ((buttons & (1u << b)) && popn_button[b])
            report |= (uint16_t)(1u << (popn_button[b] - 1));
    tud_hid_report(REPORT_ID_POPN, &report, sizeof report);
}

/* One bit per key code, so any number of keys can be held at once. */
static void send_keyboard(uint16_t buttons) {
    uint8_t report[KEYBOARD_NUM_KEYS / 8] = {0};
    for (int b = 0; b < 16; b++) {
        uint8_t key = keymap[b];
        if ((buttons & (1u << b)) && key && key < KEYBOARD_NUM_KEYS)
            report[key / 8] |= (uint8_t)(1u << (key % 8));
    }
    tud_hid_report(REPORT_ID_KEYBOARD, report, sizeof report);
}

static int8_t axis(uint8_t v) {
    return (int8_t)((int)v * 254 / 255 - 127);   /* 0..255 -> -127..127 */
}

static void send_gamepad(uint16_t buttons, bool connected) {
    hid_gamepad_report_t r = {.hat = GAMEPAD_HAT_CENTERED};
#if DPAD_AS_HAT
    static const uint8_t hat[16] = {   /* index: Up | Right<<1 | Down<<2 | Left<<3 */
        GAMEPAD_HAT_CENTERED, GAMEPAD_HAT_UP, GAMEPAD_HAT_RIGHT, GAMEPAD_HAT_UP_RIGHT,
        GAMEPAD_HAT_DOWN, GAMEPAD_HAT_CENTERED, GAMEPAD_HAT_DOWN_RIGHT, GAMEPAD_HAT_RIGHT,
        GAMEPAD_HAT_LEFT, GAMEPAD_HAT_UP_LEFT, GAMEPAD_HAT_CENTERED, GAMEPAD_HAT_UP,
        GAMEPAD_HAT_DOWN_LEFT, GAMEPAD_HAT_LEFT, GAMEPAD_HAT_DOWN, GAMEPAD_HAT_CENTERED,
    };
    r.hat = hat[(buttons >> PS2_BTN_UP) & 0x0F];   /* Up, Right, Down, Left are bits 4-7 */
    buttons &= (uint16_t)~0x00F0;
#endif
    r.buttons = buttons;
    if (connected) {   /* left stick on X/Y, right stick on Z/Rz */
        r.x  = axis(ps2.data[PS2_IDX_LX]);
        r.y  = axis(ps2.data[PS2_IDX_LY]);
        r.z  = axis(ps2.data[PS2_IDX_RX]);
        r.rz = axis(ps2.data[PS2_IDX_RY]);
    }
    tud_hid_report(REPORT_ID_GAMEPAD, &r, sizeof r);
}

/* ---- TinyUSB HID callbacks (required, unused) ---- */

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
}

/* ---- Main loop ---- */

int main(void) {
    led_init();
    tusb_init();

    out_mode_t mode = MODE_NONE;
    bool connected = false;
    bool wait_release = false;   /* ignore the mode-select buttons until let go */
    uint32_t next_read = 0;
    uint32_t next_retry = 1500;  /* let USB enumerate before the (blocking) setup */

    while (true) {
        tud_task();

        uint32_t now = to_ms_since_boot(get_absolute_time());

        if (!connected) {
            led_set((now / 250) & 1);
            if ((int32_t)(now - next_retry) >= 0) {
                next_retry = now + 1000;
                connected = controller_setup();
            }
        }

        if ((int32_t)(now - next_read) < 0)
            continue;
        next_read = now + PS2_READ_INTERVAL_MS;

        uint16_t buttons = 0;
        if (connected) {
            ps2_read(&ps2);
            connected = ps2_ok(&ps2);
        }
        if (connected) {
            buttons = read_buttons();
            if (mode == MODE_NONE) {
                mode = pick_mode(buttons);
                wait_release = buttons != 0;
            }
            if (wait_release) {
                wait_release = buttons != 0;
                buttons = 0;
            }
            led_set(true);
        }

        if (mode == MODE_NONE || !tud_hid_ready())
            continue;

        switch (mode) {
        case MODE_POPN:     send_popn(buttons); break;
        case MODE_KEYBOARD: send_keyboard(buttons); break;
        default:            send_gamepad(buttons, connected); break;
        }
    }
}
