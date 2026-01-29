/**
 * AAAos Kernel - PS/2 Mouse Driver Implementation
 *
 * Handles PS/2 mouse initialization, interrupt handling,
 * position tracking, and event buffering.
 */

#include "mouse.h"
#include "../../kernel/arch/x86_64/io.h"
#include "../../kernel/include/serial.h"

/* Forward declaration of pic_eoi from idt.c */
extern void pic_eoi(uint8_t irq);

/*
 * Mouse state
 */

/* Current mouse position and button state */
static volatile mouse_state_t current_state = {0, 0, 0};

/* Movement bounds */
static volatile int32_t bounds_min_x = 0;
static volatile int32_t bounds_min_y = 0;
static volatile int32_t bounds_max_x = MOUSE_DEFAULT_MAX_X;
static volatile int32_t bounds_max_y = MOUSE_DEFAULT_MAX_Y;

/* Previous button state (for detecting button changes) */
static volatile uint8_t prev_buttons = 0;

/* Packet buffer (PS/2 mouse sends 3-byte packets) */
static volatile uint8_t packet_buffer[3];
static volatile uint8_t packet_index = 0;

/* Event queue (circular buffer) */
static mouse_event_t event_buffer[MOUSE_BUFFER_SIZE];
static volatile uint32_t buffer_head = 0;
static volatile uint32_t buffer_tail = 0;

/* Driver initialized flag */
static volatile bool mouse_initialized = false;

/*
 * Internal helper functions
 */

/**
 * Wait for PS/2 controller input buffer to be ready for writing
 */
static bool ps2_wait_input(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT) == 0) {
            return true;
        }
    }
    return false;
}

/**
 * Wait for PS/2 controller output buffer to have data
 */
static bool ps2_wait_output(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if ((inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT) != 0) {
            return true;
        }
    }
    return false;
}

/**
 * Send a command to the PS/2 controller
 */
static void ps2_send_command(uint8_t cmd) {
    ps2_wait_input();
    outb(PS2_COMMAND_PORT, cmd);
}

/**
 * Send data to the PS/2 data port
 */
static void ps2_send_data(uint8_t data) {
    ps2_wait_input();
    outb(PS2_DATA_PORT, data);
}

/**
 * Read data from the PS/2 data port
 */
static uint8_t ps2_read_data(void) {
    ps2_wait_output();
    return inb(PS2_DATA_PORT);
}

/**
 * Send a command to the mouse (via auxiliary port)
 * Uses 0xD4 command to route data to the mouse
 */
static bool mouse_send_cmd(uint8_t cmd) {
    int retries = 3;

    while (retries-- > 0) {
        /* Tell controller to send next byte to mouse */
        ps2_send_command(PS2_CMD_WRITE_AUX);
        ps2_send_data(cmd);

        /* Wait for response */
        if (ps2_wait_output()) {
            uint8_t response = inb(PS2_DATA_PORT);
            if (response == MOUSE_ACK) {
                return true;
            }
            if (response == MOUSE_RESEND) {
                continue;  /* Try again */
            }
            kprintf("[MOUSE] Unexpected response: 0x%02X\n", response);
        }
    }

    return false;
}

/**
 * Add a mouse event to the buffer
 */
static void buffer_add_event(mouse_event_t *event) {
    uint32_t next_head = (buffer_head + 1) & (MOUSE_BUFFER_SIZE - 1);

    /* Drop event if buffer is full */
    if (next_head == buffer_tail) {
        /* Don't spam logs - just silently drop */
        return;
    }

    event_buffer[buffer_head] = *event;
    buffer_head = next_head;
}

/**
 * Get a mouse event from the buffer (non-blocking)
 */
static bool buffer_get_event(mouse_event_t *event) {
    if (buffer_head == buffer_tail) {
        return false;  /* Buffer empty */
    }

    *event = event_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) & (MOUSE_BUFFER_SIZE - 1);
    return true;
}

/**
 * Process a complete 3-byte mouse packet
 */
static void process_packet(void) {
    uint8_t status = packet_buffer[0];
    int32_t dx = (int32_t)packet_buffer[1];
    int32_t dy = (int32_t)packet_buffer[2];

    /* Verify packet validity - bit 3 should always be 1 */
    if ((status & MOUSE_PKT_ALWAYS_ONE) == 0) {
        kprintf("[MOUSE] Invalid packet (bit 3 not set), resynchronizing\n");
        packet_index = 0;
        return;
    }

    /* Check for overflow - discard packet if overflow occurred */
    if (status & (MOUSE_PKT_X_OVERFLOW | MOUSE_PKT_Y_OVERFLOW)) {
        return;
    }

    /* Apply sign extension based on sign bits */
    if (status & MOUSE_PKT_X_SIGN) {
        dx |= 0xFFFFFF00;  /* Sign extend to negative */
    }
    if (status & MOUSE_PKT_Y_SIGN) {
        dy |= 0xFFFFFF00;  /* Sign extend to negative */
    }

    /* Invert Y axis (PS/2 reports Y increasing upward, we want downward) */
    dy = -dy;

    /* Update position with bounds checking */
    int32_t new_x = current_state.x + dx;
    int32_t new_y = current_state.y + dy;

    /* Clamp to bounds */
    if (new_x < bounds_min_x) new_x = bounds_min_x;
    if (new_x > bounds_max_x) new_x = bounds_max_x;
    if (new_y < bounds_min_y) new_y = bounds_min_y;
    if (new_y > bounds_max_y) new_y = bounds_max_y;

    current_state.x = new_x;
    current_state.y = new_y;

    /* Extract button state */
    uint8_t buttons = 0;
    if (status & MOUSE_PKT_LEFT)   buttons |= MOUSE_BTN_LEFT;
    if (status & MOUSE_PKT_RIGHT)  buttons |= MOUSE_BTN_RIGHT;
    if (status & MOUSE_PKT_MIDDLE) buttons |= MOUSE_BTN_MIDDLE;

    current_state.buttons = buttons;

    /* Generate events */
    mouse_event_t event;
    event.dx = dx;
    event.dy = dy;
    event.buttons = buttons;

    /* Check for button changes */
    uint8_t buttons_changed = buttons ^ prev_buttons;
    uint8_t buttons_pressed = buttons_changed & buttons;
    uint8_t buttons_released = buttons_changed & prev_buttons;

    /* Generate button down events */
    if (buttons_pressed) {
        event.event_type = MOUSE_EVENT_BUTTON_DOWN;
        buffer_add_event(&event);
    }

    /* Generate button up events */
    if (buttons_released) {
        event.event_type = MOUSE_EVENT_BUTTON_UP;
        buffer_add_event(&event);
    }

    /* Generate movement events */
    if (dx != 0 || dy != 0) {
        if (buttons) {
            event.event_type = MOUSE_EVENT_DRAG;
        } else {
            event.event_type = MOUSE_EVENT_MOVE;
        }
        buffer_add_event(&event);
    }

    prev_buttons = buttons;
}

/*
 * Public API
 */

bool mouse_init(void) {
    kprintf("[MOUSE] Initializing PS/2 mouse driver...\n");

    /* Step 1: Enable auxiliary device (mouse port) */
    kprintf("[MOUSE] Enabling auxiliary device...\n");
    ps2_send_command(PS2_CMD_ENABLE_AUX);

    /* Step 2: Enable IRQ12 for mouse */
    kprintf("[MOUSE] Enabling IRQ12...\n");
    ps2_send_command(PS2_CMD_READ_CONFIG);
    uint8_t config = ps2_read_data();
    kprintf("[MOUSE] PS/2 config byte: 0x%02X\n", config);

    /* Enable mouse interrupt (bit 1) and ensure mouse clock is not disabled */
    config |= PS2_CONFIG_INT2;      /* Enable IRQ12 */
    config &= ~PS2_CONFIG_CLOCK2;   /* Enable mouse clock */

    ps2_send_command(PS2_CMD_WRITE_CONFIG);
    ps2_send_data(config);

    /* Verify configuration was written */
    ps2_send_command(PS2_CMD_READ_CONFIG);
    uint8_t verify_config = ps2_read_data();
    kprintf("[MOUSE] PS/2 config byte after write: 0x%02X\n", verify_config);

    /* Step 3: Test auxiliary port */
    kprintf("[MOUSE] Testing auxiliary port...\n");
    ps2_send_command(PS2_CMD_TEST_AUX);
    uint8_t result = ps2_read_data();
    if (result != 0x00) {
        kprintf("[MOUSE] Warning: auxiliary port test returned 0x%02X (expected 0x00)\n", result);
        /* Continue anyway - some systems fail this test but mouse still works */
    } else {
        kprintf("[MOUSE] Auxiliary port test passed\n");
    }

    /* Re-enable auxiliary device after test (test may disable it) */
    ps2_send_command(PS2_CMD_ENABLE_AUX);

    /* Step 4: Reset mouse */
    kprintf("[MOUSE] Resetting mouse...\n");
    if (!mouse_send_cmd(MOUSE_CMD_RESET)) {
        kprintf("[MOUSE] Warning: mouse reset command failed\n");
        /* Continue anyway */
    } else {
        /* Wait for self-test result (0xAA) and device ID (0x00) */
        if (ps2_wait_output()) {
            result = inb(PS2_DATA_PORT);
            if (result == MOUSE_SELF_TEST_PASS) {
                kprintf("[MOUSE] Mouse self-test passed\n");
                /* Read device ID */
                if (ps2_wait_output()) {
                    uint8_t device_id = inb(PS2_DATA_PORT);
                    kprintf("[MOUSE] Device ID: 0x%02X\n", device_id);
                }
            } else {
                kprintf("[MOUSE] Warning: mouse self-test returned 0x%02X\n", result);
            }
        }
    }

    /* Step 5: Set defaults */
    kprintf("[MOUSE] Setting defaults...\n");
    if (!mouse_send_cmd(MOUSE_CMD_SET_DEFAULTS)) {
        kprintf("[MOUSE] Warning: set defaults command failed\n");
    }

    /* Step 6: Enable data reporting */
    kprintf("[MOUSE] Enabling data reporting...\n");
    if (!mouse_send_cmd(MOUSE_CMD_ENABLE)) {
        kprintf("[MOUSE] ERROR: failed to enable data reporting\n");
        return false;
    }
    kprintf("[MOUSE] Data reporting enabled\n");

    /* Initialize state */
    current_state.x = bounds_max_x / 2;  /* Center of screen */
    current_state.y = bounds_max_y / 2;
    current_state.buttons = 0;
    prev_buttons = 0;
    packet_index = 0;
    buffer_head = 0;
    buffer_tail = 0;

    /* Register interrupt handler */
    idt_register_handler(IRQ_MOUSE, mouse_handler);
    kprintf("[MOUSE] Registered handler for IRQ12 (vector %d)\n", IRQ_MOUSE);

    mouse_initialized = true;
    kprintf("[MOUSE] PS/2 mouse driver initialized successfully\n");
    kprintf("[MOUSE] Initial position: (%d, %d)\n", current_state.x, current_state.y);
    kprintf("[MOUSE] Bounds: (%d, %d) to (%d, %d)\n",
            bounds_min_x, bounds_min_y, bounds_max_x, bounds_max_y);

    return true;
}

void mouse_handler(interrupt_frame_t *frame) {
    UNUSED(frame);

    /* Read data byte from mouse */
    uint8_t data = inb(PS2_DATA_PORT);

    /* Check if this is likely the first byte of a packet */
    /* The first byte should have bit 3 (ALWAYS_ONE) set */
    if (packet_index == 0 && (data & MOUSE_PKT_ALWAYS_ONE) == 0) {
        /* Invalid first byte, discard and wait for valid packet start */
        return;
    }

    /* Add byte to packet buffer */
    packet_buffer[packet_index++] = data;

    /* Check if we have a complete packet (3 bytes for standard PS/2 mouse) */
    if (packet_index >= 3) {
        process_packet();
        packet_index = 0;
    }

    /* EOI is sent by the common interrupt handler in idt.c */
}

void mouse_get_state(mouse_state_t *state) {
    if (state == NULL) {
        return;
    }

    /* Disable interrupts briefly for consistent read */
    interrupts_disable();
    state->x = current_state.x;
    state->y = current_state.y;
    state->buttons = current_state.buttons;
    interrupts_enable();
}

bool mouse_get_event(mouse_event_t *event) {
    if (event == NULL) {
        return false;
    }
    return buffer_get_event(event);
}

bool mouse_has_event(void) {
    return buffer_head != buffer_tail;
}

void mouse_set_bounds(int32_t min_x, int32_t min_y, int32_t max_x, int32_t max_y) {
    /* Validate bounds */
    if (min_x > max_x || min_y > max_y) {
        kprintf("[MOUSE] Warning: invalid bounds ignored\n");
        return;
    }

    interrupts_disable();

    bounds_min_x = min_x;
    bounds_min_y = min_y;
    bounds_max_x = max_x;
    bounds_max_y = max_y;

    /* Clamp current position to new bounds */
    if (current_state.x < bounds_min_x) current_state.x = bounds_min_x;
    if (current_state.x > bounds_max_x) current_state.x = bounds_max_x;
    if (current_state.y < bounds_min_y) current_state.y = bounds_min_y;
    if (current_state.y > bounds_max_y) current_state.y = bounds_max_y;

    interrupts_enable();

    kprintf("[MOUSE] Bounds set to (%d, %d) - (%d, %d)\n",
            min_x, min_y, max_x, max_y);
}

void mouse_set_position(int32_t x, int32_t y) {
    interrupts_disable();

    /* Clamp to bounds */
    if (x < bounds_min_x) x = bounds_min_x;
    if (x > bounds_max_x) x = bounds_max_x;
    if (y < bounds_min_y) y = bounds_min_y;
    if (y > bounds_max_y) y = bounds_max_y;

    current_state.x = x;
    current_state.y = y;

    interrupts_enable();
}

void mouse_flush(void) {
    interrupts_disable();

    buffer_head = 0;
    buffer_tail = 0;
    packet_index = 0;

    interrupts_enable();
}

const char* mouse_button_name(uint8_t buttons) {
    static char name_buffer[32];
    int pos = 0;

    if (buttons == 0) {
        return "NONE";
    }

    if (buttons & MOUSE_BTN_LEFT) {
        pos += 4;
        name_buffer[0] = 'L';
        name_buffer[1] = 'E';
        name_buffer[2] = 'F';
        name_buffer[3] = 'T';
    }

    if (buttons & MOUSE_BTN_MIDDLE) {
        if (pos > 0) {
            name_buffer[pos++] = '+';
        }
        name_buffer[pos++] = 'M';
        name_buffer[pos++] = 'I';
        name_buffer[pos++] = 'D';
        name_buffer[pos++] = 'D';
        name_buffer[pos++] = 'L';
        name_buffer[pos++] = 'E';
    }

    if (buttons & MOUSE_BTN_RIGHT) {
        if (pos > 0) {
            name_buffer[pos++] = '+';
        }
        name_buffer[pos++] = 'R';
        name_buffer[pos++] = 'I';
        name_buffer[pos++] = 'G';
        name_buffer[pos++] = 'H';
        name_buffer[pos++] = 'T';
    }

    name_buffer[pos] = '\0';
    return name_buffer;
}

const char* mouse_event_name(mouse_event_type_t type) {
    switch (type) {
        case MOUSE_EVENT_NONE:        return "NONE";
        case MOUSE_EVENT_MOVE:        return "MOVE";
        case MOUSE_EVENT_BUTTON_DOWN: return "BUTTON_DOWN";
        case MOUSE_EVENT_BUTTON_UP:   return "BUTTON_UP";
        case MOUSE_EVENT_DRAG:        return "DRAG";
        default:                      return "UNKNOWN";
    }
}
