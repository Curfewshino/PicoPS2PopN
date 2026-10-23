#include <string.h>
#include "tusb.h"
#include "pico/unique_id.h"
#include "usb_descriptors.h"

/* pid.codes test VID/PID - change these if you distribute the device. */
#define USB_VID 0x1209
#define USB_PID 0x0006

/* ---- Device descriptor ---- */

static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,   /* class defined per interface */
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = STRID_MANUFACTURER,
    .iProduct           = STRID_PRODUCT,
    .iSerialNumber      = STRID_SERIAL,
    .bNumConfigurations = 1,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

/* ---- HID report descriptor ---- */

/* One 1-bit input button with the given usage (on the current usage page). */
#define ONE_BUTTON(usage) \
    0x09, (usage),          /* Usage                 */ \
    0x15, 0x00, 0x25, 0x01, /* Logical 0..1          */ \
    0x75, 0x01, 0x95, 0x01, /* Report size 1, count 1 */ \
    0x81, 0x02              /* Input (Data,Var,Abs)  */

static const uint8_t desc_hid_report[] = {
    /* Keyboard with one bit per key (no 6-key limit), covering key codes
     * 0x00-0x3F (letters, numbers, Enter, Esc, Space, ...). */
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_KEYBOARD),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
        HID_REPORT_ID(REPORT_ID_KEYBOARD)
        HID_USAGE_PAGE(HID_USAGE_PAGE_KEYBOARD),
        HID_USAGE_MIN(0x00),
        HID_USAGE_MAX(KEYBOARD_NUM_KEYS - 1),
        HID_LOGICAL_MIN(0),
        HID_LOGICAL_MAX(1),
        HID_REPORT_SIZE(1),
        HID_REPORT_COUNT(KEYBOARD_NUM_KEYS),
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),
    HID_COLLECTION_END,

    TUD_HID_REPORT_DESC_GAMEPAD(HID_REPORT_ID(REPORT_ID_GAMEPAD)),

    /* Pop'n controller: Button 1-9, then SELECT / START as the Generic
     * Desktop Select / Start, so input settings label them that way. */
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_GAMEPAD),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
        HID_REPORT_ID(REPORT_ID_POPN)
        HID_USAGE_PAGE(HID_USAGE_PAGE_BUTTON),
        ONE_BUTTON(1), ONE_BUTTON(2), ONE_BUTTON(3),
        ONE_BUTTON(4), ONE_BUTTON(5), ONE_BUTTON(6),
        ONE_BUTTON(7), ONE_BUTTON(8), ONE_BUTTON(9),
        HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
        ONE_BUTTON(0x3E),   /* SELECT */
        ONE_BUTTON(0x3D),   /* START  */
        0x75, 0x01, 0x95, 0x05, 0x81, 0x03,   /* 5 bits padding */
    HID_COLLECTION_END,
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return desc_hid_report;
}

/* ---- Configuration descriptor: one HID interface ---- */

enum { ITF_NUM_HID, ITF_NUM_TOTAL };

#define EPNUM_HID        0x81
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0, 100),
    /* interface, string, protocol, report desc len, EP in, EP size, poll interval (ms) */
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report),
                       EPNUM_HID, CFG_TUD_HID_EP_BUFSIZE, 1),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

/* ---- String descriptors ---- */

static const char *const strings[] = {
    [STRID_MANUFACTURER] = "PS2",   /* the product name starts with this, so tools that
                                       join the two show just "PS2 Pop'n Controller" */
    [STRID_PRODUCT]      = "PS2 Pop'n Controller",
};

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    static uint16_t desc[32];
    char serial[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    const char *str;

    if (index == STRID_LANGID) {
        desc[1] = 0x0409;   /* English (US) */
        desc[0] = (uint16_t)((TUSB_DESC_STRING << 8) | 4);
        return desc;
    }
    if (index == STRID_SERIAL) {
        pico_get_unique_board_id_string(serial, sizeof serial);
        str = serial;
    } else if (index < sizeof strings / sizeof strings[0] && strings[index]) {
        str = strings[index];
    } else {
        return NULL;
    }

    size_t len = strlen(str);
    if (len > 31)
        len = 31;
    for (size_t i = 0; i < len; i++)
        desc[1 + i] = (uint16_t)str[i];
    desc[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * len + 2));
    return desc;
}
