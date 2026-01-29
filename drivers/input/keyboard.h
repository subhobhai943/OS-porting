/**
 * AAAos Kernel - PS/2 Keyboard Driver
 *
 * Provides PS/2 keyboard support with scancode translation,
 * key press/release detection, and input buffering.
 */

#ifndef _AAAOS_DRIVER_KEYBOARD_H
#define _AAAOS_DRIVER_KEYBOARD_H

#include "../../kernel/include/types.h"
#include "../../kernel/arch/x86_64/include/idt.h"

/* PS/2 Controller I/O Ports */
#define PS2_DATA_PORT       0x60    /* Data port (read/write) */
#define PS2_STATUS_PORT     0x64    /* Status register (read) */
#define PS2_COMMAND_PORT    0x64    /* Command register (write) */

/* PS/2 Controller Status Register Bits */
#define PS2_STATUS_OUTPUT   0x01    /* Output buffer full (data ready) */
#define PS2_STATUS_INPUT    0x02    /* Input buffer full (don't write) */
#define PS2_STATUS_SYSTEM   0x04    /* System flag */
#define PS2_STATUS_CMD      0x08    /* Command/data (0 = data, 1 = command) */
#define PS2_STATUS_TIMEOUT  0x40    /* Timeout error */
#define PS2_STATUS_PARITY   0x80    /* Parity error */

/* PS/2 Controller Commands */
#define PS2_CMD_READ_CONFIG     0x20    /* Read configuration byte */
#define PS2_CMD_WRITE_CONFIG    0x60    /* Write configuration byte */
#define PS2_CMD_DISABLE_PORT2   0xA7    /* Disable second PS/2 port */
#define PS2_CMD_ENABLE_PORT2    0xA8    /* Enable second PS/2 port */
#define PS2_CMD_TEST_PORT2      0xA9    /* Test second PS/2 port */
#define PS2_CMD_SELF_TEST       0xAA    /* Controller self test */
#define PS2_CMD_TEST_PORT1      0xAB    /* Test first PS/2 port */
#define PS2_CMD_DISABLE_PORT1   0xAD    /* Disable first PS/2 port */
#define PS2_CMD_ENABLE_PORT1    0xAE    /* Enable first PS/2 port */

/* PS/2 Configuration Byte Bits */
#define PS2_CONFIG_INT1         0x01    /* First port interrupt enable */
#define PS2_CONFIG_INT2         0x02    /* Second port interrupt enable */
#define PS2_CONFIG_SYSTEM       0x04    /* System flag */
#define PS2_CONFIG_CLOCK1       0x10    /* First port clock disable */
#define PS2_CONFIG_CLOCK2       0x20    /* Second port clock disable */
#define PS2_CONFIG_TRANSLATE    0x40    /* First port translation */

/* Keyboard Commands */
#define KB_CMD_SET_LEDS         0xED    /* Set LEDs */
#define KB_CMD_ECHO             0xEE    /* Echo */
#define KB_CMD_SCANCODE_SET     0xF0    /* Get/set scancode set */
#define KB_CMD_IDENTIFY         0xF2    /* Identify keyboard */
#define KB_CMD_TYPEMATIC        0xF3    /* Set typematic rate/delay */
#define KB_CMD_ENABLE           0xF4    /* Enable scanning */
#define KB_CMD_DISABLE          0xF5    /* Disable scanning */
#define KB_CMD_DEFAULT          0xF6    /* Set default parameters */
#define KB_CMD_RESET            0xFF    /* Reset keyboard */

/* Keyboard Response Codes */
#define KB_ACK                  0xFA    /* Command acknowledged */
#define KB_RESEND               0xFE    /* Resend last command */
#define KB_SELF_TEST_PASS       0xAA    /* Self test passed */
#define KB_ECHO_RESPONSE        0xEE    /* Echo response */

/* Scancode Constants */
#define SCANCODE_RELEASE        0x80    /* Key release bit */
#define SCANCODE_EXTENDED       0xE0    /* Extended scancode prefix */
#define SCANCODE_EXTENDED2      0xE1    /* Second extended prefix (Pause) */

/* Special Scancodes (Set 1) */
#define SC_ESCAPE               0x01
#define SC_BACKSPACE            0x0E
#define SC_TAB                  0x0F
#define SC_ENTER                0x1C
#define SC_LCTRL                0x1D
#define SC_LSHIFT               0x2A
#define SC_RSHIFT               0x36
#define SC_LALT                 0x38
#define SC_SPACE                0x39
#define SC_CAPSLOCK             0x3A
#define SC_F1                   0x3B
#define SC_F2                   0x3C
#define SC_F3                   0x3D
#define SC_F4                   0x3E
#define SC_F5                   0x3F
#define SC_F6                   0x40
#define SC_F7                   0x41
#define SC_F8                   0x42
#define SC_F9                   0x43
#define SC_F10                  0x44
#define SC_NUMLOCK              0x45
#define SC_SCROLLLOCK           0x46
#define SC_F11                  0x57
#define SC_F12                  0x58

/* Extended Scancodes (after 0xE0 prefix) */
#define SC_EXT_RCTRL            0x1D
#define SC_EXT_RALT             0x38
#define SC_EXT_HOME             0x47
#define SC_EXT_UP               0x48
#define SC_EXT_PAGEUP           0x49
#define SC_EXT_LEFT             0x4B
#define SC_EXT_RIGHT            0x4D
#define SC_EXT_END              0x4F
#define SC_EXT_DOWN             0x50
#define SC_EXT_PAGEDOWN         0x51
#define SC_EXT_INSERT           0x52
#define SC_EXT_DELETE           0x53

/* Virtual Keycodes (translated from scancodes) */
typedef enum {
    KEY_NONE = 0,

    /* Printable characters (ASCII) */
    KEY_SPACE = 32,
    KEY_EXCLAIM = 33,
    KEY_DQUOTE = 34,
    KEY_HASH = 35,
    KEY_DOLLAR = 36,
    KEY_PERCENT = 37,
    KEY_AMPERSAND = 38,
    KEY_QUOTE = 39,
    KEY_LPAREN = 40,
    KEY_RPAREN = 41,
    KEY_ASTERISK = 42,
    KEY_PLUS = 43,
    KEY_COMMA = 44,
    KEY_MINUS = 45,
    KEY_PERIOD = 46,
    KEY_SLASH = 47,
    KEY_0 = 48, KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    KEY_COLON = 58,
    KEY_SEMICOLON = 59,
    KEY_LESS = 60,
    KEY_EQUALS = 61,
    KEY_GREATER = 62,
    KEY_QUESTION = 63,
    KEY_AT = 64,
    KEY_A = 65, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
    KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
    KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    KEY_LBRACKET = 91,
    KEY_BACKSLASH = 92,
    KEY_RBRACKET = 93,
    KEY_CARET = 94,
    KEY_UNDERSCORE = 95,
    KEY_BACKTICK = 96,

    /* Lower-case letters share ASCII values with upper-case (shift-sensitive) */

    KEY_LBRACE = 123,
    KEY_PIPE = 124,
    KEY_RBRACE = 125,
    KEY_TILDE = 126,

    /* Special keys (non-ASCII, 128+) */
    KEY_ESCAPE = 128,
    KEY_BACKSPACE,
    KEY_TAB,
    KEY_ENTER,
    KEY_LCTRL,
    KEY_RCTRL,
    KEY_LSHIFT,
    KEY_RSHIFT,
    KEY_LALT,
    KEY_RALT,
    KEY_CAPSLOCK,
    KEY_NUMLOCK,
    KEY_SCROLLLOCK,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_HOME,
    KEY_END,
    KEY_INSERT,
    KEY_DELETE,
    KEY_PAGEUP,
    KEY_PAGEDOWN,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_PRINTSCREEN,
    KEY_PAUSE,

    /* Keypad keys */
    KEY_KP_0, KEY_KP_1, KEY_KP_2, KEY_KP_3, KEY_KP_4,
    KEY_KP_5, KEY_KP_6, KEY_KP_7, KEY_KP_8, KEY_KP_9,
    KEY_KP_PERIOD,
    KEY_KP_PLUS,
    KEY_KP_MINUS,
    KEY_KP_ASTERISK,
    KEY_KP_SLASH,
    KEY_KP_ENTER,

    KEY_MAX
} keycode_t;

/* Modifier key flags */
#define MOD_LSHIFT      0x01
#define MOD_RSHIFT      0x02
#define MOD_SHIFT       (MOD_LSHIFT | MOD_RSHIFT)
#define MOD_LCTRL       0x04
#define MOD_RCTRL       0x08
#define MOD_CTRL        (MOD_LCTRL | MOD_RCTRL)
#define MOD_LALT        0x10
#define MOD_RALT        0x20
#define MOD_ALT         (MOD_LALT | MOD_RALT)
#define MOD_CAPSLOCK    0x40
#define MOD_NUMLOCK     0x80
#define MOD_SCROLLLOCK  0x100

/* Key event structure */
typedef struct {
    keycode_t   keycode;        /* Virtual keycode */
    uint8_t     scancode;       /* Raw scancode */
    uint16_t    modifiers;      /* Active modifiers */
    bool        pressed;        /* true = press, false = release */
    bool        extended;       /* true if extended scancode */
    char        ascii;          /* ASCII character (0 if non-printable) */
} key_event_t;

/* LED bits */
#define LED_SCROLLLOCK  0x01
#define LED_NUMLOCK     0x02
#define LED_CAPSLOCK    0x04

/* Keyboard buffer size (must be power of 2) */
#define KEYBOARD_BUFFER_SIZE    256

/**
 * Initialize the PS/2 keyboard driver
 * Sets up the controller, registers IRQ handler, and enables the keyboard.
 * @return true on success, false on failure
 */
bool keyboard_init(void);

/**
 * Keyboard interrupt handler
 * Called by the IDT when IRQ1 fires.
 * @param frame Interrupt frame
 */
void keyboard_handler(interrupt_frame_t *frame);

/**
 * Get the next character from keyboard (blocking)
 * Waits until a printable character is available.
 * @return ASCII character
 */
char keyboard_getchar(void);

/**
 * Check if keyboard input is available
 * @return true if characters are waiting in the buffer
 */
bool keyboard_has_input(void);

/**
 * Get the next raw scancode (non-blocking)
 * @return Scancode or 0 if no input available
 */
uint8_t keyboard_get_scancode(void);

/**
 * Get the next key event (blocking)
 * Includes full key information (keycode, modifiers, etc.)
 * @param event Pointer to key_event_t structure to fill
 * @return true if event was retrieved, false if interrupted
 */
bool keyboard_get_event(key_event_t *event);

/**
 * Get the next key event (non-blocking)
 * @param event Pointer to key_event_t structure to fill
 * @return true if event was available, false otherwise
 */
bool keyboard_poll_event(key_event_t *event);

/**
 * Get current modifier key state
 * @return Bitmask of active modifier keys
 */
uint16_t keyboard_get_modifiers(void);

/**
 * Check if a specific key is currently pressed
 * @param keycode Key to check
 * @return true if key is pressed
 */
bool keyboard_is_key_pressed(keycode_t keycode);

/**
 * Set keyboard LEDs
 * @param leds Bitmask of LED_SCROLLLOCK, LED_NUMLOCK, LED_CAPSLOCK
 */
void keyboard_set_leds(uint8_t leds);

/**
 * Flush the keyboard buffer
 * Discards all pending input.
 */
void keyboard_flush(void);

/**
 * Get a human-readable name for a keycode
 * @param keycode Key to get name for
 * @return String name of the key
 */
const char* keyboard_keycode_name(keycode_t keycode);

#endif /* _AAAOS_DRIVER_KEYBOARD_H */
