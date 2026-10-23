#ifndef USB_DESCRIPTORS_H
#define USB_DESCRIPTORS_H

/* HID report IDs. All three live in one HID interface, so the USB descriptor
 * stays the same whichever output mode is picked at power-up. */
enum {
    REPORT_ID_KEYBOARD = 1,
    REPORT_ID_GAMEPAD  = 2,
    REPORT_ID_POPN     = 3,   /* Pop'n: Button 1-9 + Select/Start */
};

/* Keyboard report: one bit per key code 0x00..KEYBOARD_NUM_KEYS-1. */
#define KEYBOARD_NUM_KEYS 64

/* String descriptor indices */
enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
};

#endif
