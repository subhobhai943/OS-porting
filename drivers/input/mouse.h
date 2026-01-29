/**
 * AAAos Kernel - PS/2 Mouse Driver
 *
 * Provides PS/2 mouse support with movement tracking,
 * button state detection, and event buffering.
 */

#ifndef _AAAOS_DRIVER_MOUSE_H
#define _AAAOS_DRIVER_MOUSE_H

#include "../../kernel/include/types.h"
#include "../../kernel/arch/x86_64/include/idt.h"

/* PS/2 Controller I/O Ports (shared with keyboard) */
#define PS2_DATA_PORT       0x60    /* Data port (read/write) */
#define PS2_STATUS_PORT     0x64    /* Status register (read) */
#define PS2_COMMAND_PORT    0x64    /* Command register (write) */

/* PS/2 Controller Status Register Bits */
#define PS2_STATUS_OUTPUT   0x01    /* Output buffer full (data ready) */
#define PS2_STATUS_INPUT    0x02    /* Input buffer full (don't write) */

/* PS/2 Controller Commands for Mouse */
#define PS2_CMD_ENABLE_AUX      0xA8    /* Enable auxiliary device (mouse) */
#define PS2_CMD_DISABLE_AUX     0xA7    /* Disable auxiliary device */
#define PS2_CMD_TEST_AUX        0xA9    /* Test auxiliary device */
#define PS2_CMD_READ_CONFIG     0x20    /* Read configuration byte */
#define PS2_CMD_WRITE_CONFIG    0x60    /* Write configuration byte */
#define PS2_CMD_WRITE_AUX       0xD4    /* Write to auxiliary device (mouse) */

/* PS/2 Configuration Byte Bits */
#define PS2_CONFIG_INT2         0x02    /* Second port (mouse) interrupt enable */
#define PS2_CONFIG_CLOCK2       0x20    /* Second port clock disable */

/* Mouse Commands */
#define MOUSE_CMD_RESET         0xFF    /* Reset mouse */
#define MOUSE_CMD_RESEND        0xFE    /* Resend last packet */
#define MOUSE_CMD_SET_DEFAULTS  0xF6    /* Set default parameters */
#define MOUSE_CMD_DISABLE       0xF5    /* Disable data reporting */
#define MOUSE_CMD_ENABLE        0xF4    /* Enable data reporting */
#define MOUSE_CMD_SET_RATE      0xF3    /* Set sample rate */
#define MOUSE_CMD_GET_ID        0xF2    /* Get device ID */
#define MOUSE_CMD_SET_REMOTE    0xF0    /* Set remote mode */
#define MOUSE_CMD_SET_WRAP      0xEE    /* Set wrap mode */
#define MOUSE_CMD_RESET_WRAP    0xEC    /* Reset wrap mode */
#define MOUSE_CMD_READ_DATA     0xEB    /* Read data packet */
#define MOUSE_CMD_SET_STREAM    0xEA    /* Set stream mode */
#define MOUSE_CMD_STATUS_REQ    0xE9    /* Status request */
#define MOUSE_CMD_SET_RES       0xE8    /* Set resolution */
#define MOUSE_CMD_SET_SCALE21   0xE7    /* Set scaling 2:1 */
#define MOUSE_CMD_SET_SCALE11   0xE6    /* Set scaling 1:1 */

/* Mouse Response Codes */
#define MOUSE_ACK               0xFA    /* Command acknowledged */
#define MOUSE_RESEND            0xFE    /* Resend last command */
#define MOUSE_SELF_TEST_PASS    0xAA    /* Self test passed */
#define MOUSE_DEVICE_ID         0x00    /* Standard PS/2 mouse ID */

/* Mouse Packet Byte 0 Bits */
#define MOUSE_PKT_LEFT          0x01    /* Left button pressed */
#define MOUSE_PKT_RIGHT         0x02    /* Right button pressed */
#define MOUSE_PKT_MIDDLE        0x04    /* Middle button pressed */
#define MOUSE_PKT_ALWAYS_ONE    0x08    /* Always set to 1 */
#define MOUSE_PKT_X_SIGN        0x10    /* X movement sign (1 = negative) */
#define MOUSE_PKT_Y_SIGN        0x20    /* Y movement sign (1 = negative) */
#define MOUSE_PKT_X_OVERFLOW    0x40    /* X overflow */
#define MOUSE_PKT_Y_OVERFLOW    0x80    /* Y overflow */

/* Mouse event buffer size (must be power of 2) */
#define MOUSE_BUFFER_SIZE       256

/* Default screen bounds */
#define MOUSE_DEFAULT_MAX_X     1024
#define MOUSE_DEFAULT_MAX_Y     768

/* Button flags */
#define MOUSE_BTN_LEFT          0x01
#define MOUSE_BTN_RIGHT         0x02
#define MOUSE_BTN_MIDDLE        0x04

/* Mouse event types */
typedef enum {
    MOUSE_EVENT_NONE = 0,
    MOUSE_EVENT_MOVE,           /* Mouse moved */
    MOUSE_EVENT_BUTTON_DOWN,    /* Button pressed */
    MOUSE_EVENT_BUTTON_UP,      /* Button released */
    MOUSE_EVENT_DRAG            /* Move while button held */
} mouse_event_type_t;

/**
 * Mouse state structure
 * Tracks current absolute position and button state
 */
typedef struct {
    int32_t x;              /* Current X position */
    int32_t y;              /* Current Y position */
    uint8_t buttons;        /* Button state (MOUSE_BTN_LEFT/RIGHT/MIDDLE) */
} mouse_state_t;

/**
 * Mouse event structure
 * Represents a single mouse event (movement or button change)
 */
typedef struct {
    int32_t dx;                     /* X delta (relative movement) */
    int32_t dy;                     /* Y delta (relative movement) */
    uint8_t buttons;                /* Current button state */
    mouse_event_type_t event_type;  /* Type of event */
} mouse_event_t;

/**
 * Initialize the PS/2 mouse driver
 * Sets up the controller, registers IRQ handler, and enables the mouse.
 * @return true on success, false on failure
 */
bool mouse_init(void);

/**
 * Mouse interrupt handler
 * Called by the IDT when IRQ12 fires.
 * @param frame Interrupt frame
 */
void mouse_handler(interrupt_frame_t *frame);

/**
 * Get the current mouse state
 * Provides current position and button status.
 * @param state Pointer to mouse_state_t structure to fill
 */
void mouse_get_state(mouse_state_t *state);

/**
 * Get the next mouse event from the queue
 * Retrieves and removes the oldest pending event.
 * @param event Pointer to mouse_event_t structure to fill
 * @return true if event was available, false if queue empty
 */
bool mouse_get_event(mouse_event_t *event);

/**
 * Check if mouse events are available
 * @return true if events are waiting in the queue
 */
bool mouse_has_event(void);

/**
 * Set mouse movement bounds
 * Constrains mouse position to the specified rectangle.
 * @param min_x Minimum X coordinate
 * @param min_y Minimum Y coordinate
 * @param max_x Maximum X coordinate
 * @param max_y Maximum Y coordinate
 */
void mouse_set_bounds(int32_t min_x, int32_t min_y, int32_t max_x, int32_t max_y);

/**
 * Set mouse position
 * Moves mouse cursor to specified coordinates.
 * @param x New X coordinate
 * @param y New Y coordinate
 */
void mouse_set_position(int32_t x, int32_t y);

/**
 * Flush the mouse event queue
 * Discards all pending events.
 */
void mouse_flush(void);

/**
 * Get human-readable name for button state
 * @param buttons Button bitmask
 * @return String describing pressed buttons
 */
const char* mouse_button_name(uint8_t buttons);

/**
 * Get human-readable name for event type
 * @param type Event type
 * @return String describing event type
 */
const char* mouse_event_name(mouse_event_type_t type);

#endif /* _AAAOS_DRIVER_MOUSE_H */
