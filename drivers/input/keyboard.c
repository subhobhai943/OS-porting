/**
 * AAAos Kernel - PS/2 Keyboard Driver Implementation
 *
 * Handles PS/2 keyboard initialization, interrupt handling,
 * scancode translation (US QWERTY), and input buffering.
 */

#include "keyboard.h"
#include "../../kernel/arch/x86_64/io.h"
#include "../../kernel/include/serial.h"

/* Forward declaration of pic_eoi from idt.c */
extern void pic_eoi(uint8_t irq);

/*
 * Scancode Set 1 to ASCII translation tables (US QWERTY layout)
 */

/* Normal (no modifiers) */
static const char scancode_ascii_normal[128] = {
    0,    0,   '1', '2', '3', '4', '5', '6',     /* 0x00-0x07 */
    '7', '8', '9', '0', '-', '=', '\b', '\t',    /* 0x08-0x0F */
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',      /* 0x10-0x17 */
    'o', 'p', '[', ']', '\n', 0,   'a', 's',     /* 0x18-0x1F */
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',      /* 0x20-0x27 */
    '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',    /* 0x28-0x2F */
    'b', 'n', 'm', ',', '.', '/', 0,   '*',      /* 0x30-0x37 */
    0,   ' ', 0,   0,   0,   0,   0,   0,        /* 0x38-0x3F */
    0,   0,   0,   0,   0,   0,   0,   '7',      /* 0x40-0x47 */
    '8', '9', '-', '4', '5', '6', '+', '1',      /* 0x48-0x4F */
    '2', '3', '0', '.', 0,   0,   0,   0,        /* 0x50-0x57 */
    0,   0,   0,   0,   0,   0,   0,   0,        /* 0x58-0x5F */
    0,   0,   0,   0,   0,   0,   0,   0,        /* 0x60-0x67 */
    0,   0,   0,   0,   0,   0,   0,   0,        /* 0x68-0x6F */
    0,   0,   0,   0,   0,   0,   0,   0,        /* 0x70-0x77 */
    0,   0,   0,   0,   0,   0,   0,   0         /* 0x78-0x7F */
};

/* Shifted */
static const char scancode_ascii_shift[128] = {
    0,    0,   '!', '@', '#', '$', '%', '^',     /* 0x00-0x07 */
    '&', '*', '(', ')', '_', '+', '\b', '\t',    /* 0x08-0x0F */
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',      /* 0x10-0x17 */
    'O', 'P', '{', '}', '\n', 0,   'A', 'S',     /* 0x18-0x1F */
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',      /* 0x20-0x27 */
    '"', '~', 0,   '|', 'Z', 'X', 'C', 'V',      /* 0x28-0x2F */
    'B', 'N', 'M', '<', '>', '?', 0,   '*',      /* 0x30-0x37 */
    0,   ' ', 0,   0,   0,   0,   0,   0,        /* 0x38-0x3F */
    0,   0,   0,   0,   0,   0,   0,   '7',      /* 0x40-0x47 */
    '8', '9', '-', '4', '5', '6', '+', '1',      /* 0x48-0x4F */
    '2', '3', '0', '.', 0,   0,   0,   0,        /* 0x50-0x57 */
    0,   0,   0,   0,   0,   0,   0,   0,        /* 0x58-0x5F */
    0,   0,   0,   0,   0,   0,   0,   0,        /* 0x60-0x67 */
    0,   0,   0,   0,   0,   0,   0,   0,        /* 0x68-0x6F */
    0,   0,   0,   0,   0,   0,   0,   0,        /* 0x70-0x77 */
    0,   0,   0,   0,   0,   0,   0,   0         /* 0x78-0x7F */
};

/* Scancode to keycode translation (non-extended) */
static const keycode_t scancode_to_keycode[128] = {
    KEY_NONE,     KEY_ESCAPE,   KEY_1,        KEY_2,        /* 0x00-0x03 */
    KEY_3,        KEY_4,        KEY_5,        KEY_6,        /* 0x04-0x07 */
    KEY_7,        KEY_8,        KEY_9,        KEY_0,        /* 0x08-0x0B */
    KEY_MINUS,    KEY_EQUALS,   KEY_BACKSPACE, KEY_TAB,     /* 0x0C-0x0F */
    KEY_Q,        KEY_W,        KEY_E,        KEY_R,        /* 0x10-0x13 */
    KEY_T,        KEY_Y,        KEY_U,        KEY_I,        /* 0x14-0x17 */
    KEY_O,        KEY_P,        KEY_LBRACKET, KEY_RBRACKET, /* 0x18-0x1B */
    KEY_ENTER,    KEY_LCTRL,    KEY_A,        KEY_S,        /* 0x1C-0x1F */
    KEY_D,        KEY_F,        KEY_G,        KEY_H,        /* 0x20-0x23 */
    KEY_J,        KEY_K,        KEY_L,        KEY_SEMICOLON,/* 0x24-0x27 */
    KEY_QUOTE,    KEY_BACKTICK, KEY_LSHIFT,   KEY_BACKSLASH,/* 0x28-0x2B */
    KEY_Z,        KEY_X,        KEY_C,        KEY_V,        /* 0x2C-0x2F */
    KEY_B,        KEY_N,        KEY_M,        KEY_COMMA,    /* 0x30-0x33 */
    KEY_PERIOD,   KEY_SLASH,    KEY_RSHIFT,   KEY_KP_ASTERISK, /* 0x34-0x37 */
    KEY_LALT,     KEY_SPACE,    KEY_CAPSLOCK, KEY_F1,       /* 0x38-0x3B */
    KEY_F2,       KEY_F3,       KEY_F4,       KEY_F5,       /* 0x3C-0x3F */
    KEY_F6,       KEY_F7,       KEY_F8,       KEY_F9,       /* 0x40-0x43 */
    KEY_F10,      KEY_NUMLOCK,  KEY_SCROLLLOCK, KEY_KP_7,   /* 0x44-0x47 */
    KEY_KP_8,     KEY_KP_9,     KEY_KP_MINUS, KEY_KP_4,     /* 0x48-0x4B */
    KEY_KP_5,     KEY_KP_6,     KEY_KP_PLUS,  KEY_KP_1,     /* 0x4C-0x4F */
    KEY_KP_2,     KEY_KP_3,     KEY_KP_0,     KEY_KP_PERIOD,/* 0x50-0x53 */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_F11,      /* 0x54-0x57 */
    KEY_F12,      KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x58-0x5B */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x5C-0x5F */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x60-0x63 */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x64-0x67 */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x68-0x6B */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x6C-0x6F */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x70-0x73 */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x74-0x77 */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x78-0x7B */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE      /* 0x7C-0x7F */
};

/* Extended scancode to keycode translation (after 0xE0) */
static const keycode_t extended_scancode_to_keycode[128] = {
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x00-0x03 */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x04-0x07 */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x08-0x0B */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x0C-0x0F */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x10-0x13 */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x14-0x17 */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x18-0x1B */
    KEY_KP_ENTER, KEY_RCTRL,    KEY_NONE,     KEY_NONE,     /* 0x1C-0x1F */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x20-0x23 */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x24-0x27 */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x28-0x2B */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x2C-0x2F */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x30-0x33 */
    KEY_NONE,     KEY_KP_SLASH, KEY_NONE,     KEY_PRINTSCREEN, /* 0x34-0x37 */
    KEY_RALT,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x38-0x3B */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x3C-0x3F */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x40-0x43 */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_HOME,     /* 0x44-0x47 */
    KEY_UP,       KEY_PAGEUP,   KEY_NONE,     KEY_LEFT,     /* 0x48-0x4B */
    KEY_NONE,     KEY_RIGHT,    KEY_NONE,     KEY_END,      /* 0x4C-0x4F */
    KEY_DOWN,     KEY_PAGEDOWN, KEY_INSERT,   KEY_DELETE,   /* 0x50-0x53 */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x54-0x57 */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x58-0x5B */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x5C-0x5F */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x60-0x63 */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x64-0x67 */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x68-0x6B */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x6C-0x6F */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x70-0x73 */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x74-0x77 */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE,     /* 0x78-0x7B */
    KEY_NONE,     KEY_NONE,     KEY_NONE,     KEY_NONE      /* 0x7C-0x7F */
};

/*
 * Keyboard state
 */

/* Circular buffer for key events */
static key_event_t event_buffer[KEYBOARD_BUFFER_SIZE];
static volatile uint32_t buffer_head = 0;
static volatile uint32_t buffer_tail = 0;

/* Current modifier state */
static volatile uint16_t current_modifiers = 0;

/* Track which keys are currently pressed */
static volatile bool key_pressed_state[KEY_MAX];

/* Extended scancode state machine */
static volatile bool expecting_extended = false;
static volatile bool expecting_extended2 = false;

/* LED state */
static volatile uint8_t led_state = 0;

/* Driver initialized flag */
static volatile bool keyboard_initialized = false;

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
 * Send data to the PS/2 data port (for keyboard commands)
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
 * Send a command to the keyboard and wait for ACK
 */
static bool keyboard_send_cmd(uint8_t cmd) {
    int retries = 3;

    while (retries-- > 0) {
        ps2_send_data(cmd);

        /* Wait for response */
        if (ps2_wait_output()) {
            uint8_t response = inb(PS2_DATA_PORT);
            if (response == KB_ACK) {
                return true;
            }
            if (response == KB_RESEND) {
                continue;  /* Try again */
            }
        }
    }

    return false;
}

/**
 * Update keyboard LEDs
 */
static void update_leds(void) {
    uint8_t leds = 0;

    if (current_modifiers & MOD_SCROLLLOCK) leds |= LED_SCROLLLOCK;
    if (current_modifiers & MOD_NUMLOCK)    leds |= LED_NUMLOCK;
    if (current_modifiers & MOD_CAPSLOCK)   leds |= LED_CAPSLOCK;

    if (leds != led_state) {
        led_state = leds;
        keyboard_send_cmd(KB_CMD_SET_LEDS);
        ps2_send_data(leds);
    }
}

/**
 * Add a key event to the buffer
 */
static void buffer_add_event(key_event_t *event) {
    uint32_t next_head = (buffer_head + 1) & (KEYBOARD_BUFFER_SIZE - 1);

    /* Drop event if buffer is full */
    if (next_head == buffer_tail) {
        kprintf("[KB] Warning: event buffer full, dropping event\n");
        return;
    }

    event_buffer[buffer_head] = *event;
    buffer_head = next_head;
}

/**
 * Get a key event from the buffer (non-blocking)
 */
static bool buffer_get_event(key_event_t *event) {
    if (buffer_head == buffer_tail) {
        return false;  /* Buffer empty */
    }

    *event = event_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) & (KEYBOARD_BUFFER_SIZE - 1);
    return true;
}

/**
 * Translate scancode to ASCII character
 */
static char scancode_to_ascii(uint8_t scancode, uint16_t modifiers) {
    if (scancode >= 128) {
        return 0;
    }

    bool shift = (modifiers & MOD_SHIFT) != 0;
    bool capslock = (modifiers & MOD_CAPSLOCK) != 0;

    char c;
    if (shift) {
        c = scancode_ascii_shift[scancode];
    } else {
        c = scancode_ascii_normal[scancode];
    }

    /* Apply caps lock (only to letters) */
    if (capslock && c >= 'a' && c <= 'z') {
        c = c - 'a' + 'A';
    } else if (capslock && c >= 'A' && c <= 'Z') {
        c = c - 'A' + 'a';
    }

    return c;
}

/**
 * Process a scancode and generate key event
 */
static void process_scancode(uint8_t scancode) {
    key_event_t event;
    bool is_release = (scancode & SCANCODE_RELEASE) != 0;
    uint8_t key_scancode = scancode & 0x7F;

    /* Handle extended scancode prefixes */
    if (scancode == SCANCODE_EXTENDED) {
        expecting_extended = true;
        return;
    }

    if (scancode == SCANCODE_EXTENDED2) {
        expecting_extended2 = true;
        return;
    }

    /* Determine keycode based on whether this is an extended scancode */
    keycode_t keycode;
    if (expecting_extended) {
        keycode = extended_scancode_to_keycode[key_scancode];
        expecting_extended = false;
    } else if (expecting_extended2) {
        /* Handle Pause key (E1 1D 45 E1 9D C5) - simplified */
        keycode = KEY_PAUSE;
        expecting_extended2 = false;
    } else {
        keycode = scancode_to_keycode[key_scancode];
    }

    /* Update modifier state */
    switch (keycode) {
        case KEY_LSHIFT:
            if (is_release) current_modifiers &= ~MOD_LSHIFT;
            else current_modifiers |= MOD_LSHIFT;
            break;
        case KEY_RSHIFT:
            if (is_release) current_modifiers &= ~MOD_RSHIFT;
            else current_modifiers |= MOD_RSHIFT;
            break;
        case KEY_LCTRL:
            if (is_release) current_modifiers &= ~MOD_LCTRL;
            else current_modifiers |= MOD_LCTRL;
            break;
        case KEY_RCTRL:
            if (is_release) current_modifiers &= ~MOD_RCTRL;
            else current_modifiers |= MOD_RCTRL;
            break;
        case KEY_LALT:
            if (is_release) current_modifiers &= ~MOD_LALT;
            else current_modifiers |= MOD_LALT;
            break;
        case KEY_RALT:
            if (is_release) current_modifiers &= ~MOD_RALT;
            else current_modifiers |= MOD_RALT;
            break;
        case KEY_CAPSLOCK:
            if (!is_release) {
                current_modifiers ^= MOD_CAPSLOCK;
                update_leds();
            }
            break;
        case KEY_NUMLOCK:
            if (!is_release) {
                current_modifiers ^= MOD_NUMLOCK;
                update_leds();
            }
            break;
        case KEY_SCROLLLOCK:
            if (!is_release) {
                current_modifiers ^= MOD_SCROLLLOCK;
                update_leds();
            }
            break;
        default:
            break;
    }

    /* Update key pressed state */
    if (keycode < KEY_MAX) {
        key_pressed_state[keycode] = !is_release;
    }

    /* Build event */
    event.keycode = keycode;
    event.scancode = scancode;
    event.modifiers = current_modifiers;
    event.pressed = !is_release;
    event.extended = (scancode == SCANCODE_EXTENDED);

    /* Get ASCII character */
    if (!event.extended && event.pressed) {
        event.ascii = scancode_to_ascii(key_scancode, current_modifiers);
    } else {
        event.ascii = 0;
    }

    /* Add to buffer */
    buffer_add_event(&event);
}

/*
 * Public API
 */

bool keyboard_init(void) {
    kprintf("[KB] Initializing PS/2 keyboard driver...\n");

    /* Disable devices during initialization */
    ps2_send_command(PS2_CMD_DISABLE_PORT1);
    ps2_send_command(PS2_CMD_DISABLE_PORT2);

    /* Flush output buffer */
    while (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT) {
        inb(PS2_DATA_PORT);
    }

    /* Read and modify controller configuration */
    ps2_send_command(PS2_CMD_READ_CONFIG);
    uint8_t config = ps2_read_data();
    kprintf("[KB] PS/2 config byte: 0x%02X\n", config);

    /* Disable translation and IRQs temporarily */
    config &= ~(PS2_CONFIG_INT1 | PS2_CONFIG_INT2 | PS2_CONFIG_TRANSLATE);
    ps2_send_command(PS2_CMD_WRITE_CONFIG);
    ps2_send_data(config);

    /* Perform controller self-test */
    ps2_send_command(PS2_CMD_SELF_TEST);
    uint8_t result = ps2_read_data();
    if (result != 0x55) {
        kprintf("[KB] ERROR: PS/2 controller self-test failed (0x%02X)\n", result);
        return false;
    }
    kprintf("[KB] PS/2 controller self-test passed\n");

    /* Restore config (self-test may reset it) */
    ps2_send_command(PS2_CMD_WRITE_CONFIG);
    ps2_send_data(config);

    /* Test first PS/2 port */
    ps2_send_command(PS2_CMD_TEST_PORT1);
    result = ps2_read_data();
    if (result != 0x00) {
        kprintf("[KB] ERROR: PS/2 port 1 test failed (0x%02X)\n", result);
        return false;
    }
    kprintf("[KB] PS/2 port 1 test passed\n");

    /* Enable first PS/2 port */
    ps2_send_command(PS2_CMD_ENABLE_PORT1);

    /* Enable interrupts for port 1 */
    ps2_send_command(PS2_CMD_READ_CONFIG);
    config = ps2_read_data();
    config |= PS2_CONFIG_INT1;
    ps2_send_command(PS2_CMD_WRITE_CONFIG);
    ps2_send_data(config);

    /* Reset keyboard */
    if (!keyboard_send_cmd(KB_CMD_RESET)) {
        kprintf("[KB] Warning: keyboard reset command failed\n");
    } else {
        /* Wait for self-test result */
        if (ps2_wait_output()) {
            result = inb(PS2_DATA_PORT);
            if (result == KB_SELF_TEST_PASS) {
                kprintf("[KB] Keyboard self-test passed\n");
            } else {
                kprintf("[KB] Warning: keyboard self-test returned 0x%02X\n", result);
            }
        }
    }

    /* Enable keyboard scanning */
    if (!keyboard_send_cmd(KB_CMD_ENABLE)) {
        kprintf("[KB] Warning: failed to enable keyboard scanning\n");
    }

    /* Initialize state */
    buffer_head = 0;
    buffer_tail = 0;
    current_modifiers = MOD_NUMLOCK;  /* Num Lock on by default */
    expecting_extended = false;
    expecting_extended2 = false;

    for (int i = 0; i < KEY_MAX; i++) {
        key_pressed_state[i] = false;
    }

    /* Set initial LED state */
    update_leds();

    /* Register interrupt handler */
    idt_register_handler(IRQ_KEYBOARD, keyboard_handler);
    kprintf("[KB] Registered handler for IRQ1 (vector %d)\n", IRQ_KEYBOARD);

    keyboard_initialized = true;
    kprintf("[KB] PS/2 keyboard driver initialized successfully\n");

    return true;
}

void keyboard_handler(interrupt_frame_t *frame) {
    UNUSED(frame);

    /* Read scancode from keyboard */
    uint8_t scancode = inb(PS2_DATA_PORT);

    /* Process the scancode */
    process_scancode(scancode);

    /* EOI is sent by the common interrupt handler in idt.c */
}

char keyboard_getchar(void) {
    key_event_t event;

    while (1) {
        /* Wait for input */
        while (buffer_head == buffer_tail) {
            /* Enable interrupts while waiting */
            __asm__ __volatile__("sti; hlt");
        }

        /* Get event */
        if (buffer_get_event(&event)) {
            /* Only return on key press with valid ASCII */
            if (event.pressed && event.ascii != 0) {
                return event.ascii;
            }
        }
    }
}

bool keyboard_has_input(void) {
    return buffer_head != buffer_tail;
}

uint8_t keyboard_get_scancode(void) {
    if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT) {
        return inb(PS2_DATA_PORT);
    }
    return 0;
}

bool keyboard_get_event(key_event_t *event) {
    while (1) {
        /* Wait for input */
        while (buffer_head == buffer_tail) {
            __asm__ __volatile__("sti; hlt");
        }

        if (buffer_get_event(event)) {
            return true;
        }
    }
}

bool keyboard_poll_event(key_event_t *event) {
    return buffer_get_event(event);
}

uint16_t keyboard_get_modifiers(void) {
    return current_modifiers;
}

bool keyboard_is_key_pressed(keycode_t keycode) {
    if (keycode >= KEY_MAX) {
        return false;
    }
    return key_pressed_state[keycode];
}

void keyboard_set_leds(uint8_t leds) {
    /* Update modifier state to match LEDs */
    if (leds & LED_SCROLLLOCK) current_modifiers |= MOD_SCROLLLOCK;
    else current_modifiers &= ~MOD_SCROLLLOCK;

    if (leds & LED_NUMLOCK) current_modifiers |= MOD_NUMLOCK;
    else current_modifiers &= ~MOD_NUMLOCK;

    if (leds & LED_CAPSLOCK) current_modifiers |= MOD_CAPSLOCK;
    else current_modifiers &= ~MOD_CAPSLOCK;

    update_leds();
}

void keyboard_flush(void) {
    /* Disable interrupts while flushing */
    interrupts_disable();

    buffer_head = 0;
    buffer_tail = 0;

    /* Also flush PS/2 controller buffer */
    while (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT) {
        inb(PS2_DATA_PORT);
    }

    interrupts_enable();
}

const char* keyboard_keycode_name(keycode_t keycode) {
    static const char* names[] = {
        [KEY_NONE] = "NONE",
        [KEY_ESCAPE] = "ESCAPE",
        [KEY_BACKSPACE] = "BACKSPACE",
        [KEY_TAB] = "TAB",
        [KEY_ENTER] = "ENTER",
        [KEY_LCTRL] = "LCTRL",
        [KEY_RCTRL] = "RCTRL",
        [KEY_LSHIFT] = "LSHIFT",
        [KEY_RSHIFT] = "RSHIFT",
        [KEY_LALT] = "LALT",
        [KEY_RALT] = "RALT",
        [KEY_CAPSLOCK] = "CAPSLOCK",
        [KEY_NUMLOCK] = "NUMLOCK",
        [KEY_SCROLLLOCK] = "SCROLLLOCK",
        [KEY_F1] = "F1", [KEY_F2] = "F2", [KEY_F3] = "F3", [KEY_F4] = "F4",
        [KEY_F5] = "F5", [KEY_F6] = "F6", [KEY_F7] = "F7", [KEY_F8] = "F8",
        [KEY_F9] = "F9", [KEY_F10] = "F10", [KEY_F11] = "F11", [KEY_F12] = "F12",
        [KEY_HOME] = "HOME",
        [KEY_END] = "END",
        [KEY_INSERT] = "INSERT",
        [KEY_DELETE] = "DELETE",
        [KEY_PAGEUP] = "PAGEUP",
        [KEY_PAGEDOWN] = "PAGEDOWN",
        [KEY_UP] = "UP",
        [KEY_DOWN] = "DOWN",
        [KEY_LEFT] = "LEFT",
        [KEY_RIGHT] = "RIGHT",
        [KEY_SPACE] = "SPACE",
        [KEY_PRINTSCREEN] = "PRINTSCREEN",
        [KEY_PAUSE] = "PAUSE",
    };

    /* For letters */
    if (keycode >= KEY_A && keycode <= KEY_Z) {
        static char letter[2] = {0, 0};
        letter[0] = 'A' + (keycode - KEY_A);
        return letter;
    }

    /* For numbers */
    if (keycode >= KEY_0 && keycode <= KEY_9) {
        static char digit[2] = {0, 0};
        digit[0] = '0' + (keycode - KEY_0);
        return digit;
    }

    /* Check named keys */
    if (keycode < sizeof(names) / sizeof(names[0]) && names[keycode] != NULL) {
        return names[keycode];
    }

    return "UNKNOWN";
}
