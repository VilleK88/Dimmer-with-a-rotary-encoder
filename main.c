#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include <stdbool.h>
#include "pico/util/queue.h"

#define CLK_DIV 125 // PWM clock divider
#define TOP 999 // PWM counter top value

#define ROT_A 10 // Rotary encoder input without pull-up/pull-down
#define ROT_B 11 // Rotary encoder input without pull-up/pull-down
#define ROT_SW 12 // Rotary encoder input with pull-up

#define DEBOUNCE_MS 20 // Debounce delay in milliseconds

#define LED_R 22 // right LED pin
#define LED_M 21 // middle LED pin
#define LED_L 20 // left LED pin
#define LEDS_SIZE 3 // number of LEDs

#define BR_RATE 50 // step size for brightness change
#define MAX_BR (TOP + 1) // max brightness
#define BR_MID (MAX_BR / 2) // 50% brightness level

// Type of event coming from the interrupt callback
typedef enum { EVENT_BUTTON, EVENT_ENCODER } event_type;

// Generic event passed from ISR to main loop through a queue
typedef struct {
    event_type type; // EVENT_BUTTON or EVENT_ENCODER
    int32_t data; // BUTTON: 1 = press, 0 = release; ENCODER: +1 or -1 step
} event_t;

// Global event queue used by ISR (Interrupt Service Routine) and main loop
static queue_t events;

void gpio_callback(uint gpio, uint32_t event_mask);
void ini_rot(const uint *rots); // Initialize rotary encoder
void ini_leds(const uint *leds); // Initialize LED pins and PWM
bool light_switch(const uint *leds, uint brightness, bool on); // Turn lights on/off
void set_brightness(const uint *leds, uint brightness); // Increase/decrease lighting
uint clamp(int br); // returns value between 0 and TOP

int main() {
    // LED and rotary encoder pin arrays for easier iteration
    const uint leds[] = {LED_R, LED_M, LED_L};
    const uint rots[] = {ROT_A, ROT_B, ROT_SW};

    uint brightness = BR_MID; // Current LEDs brightness value
    static bool lightsOn = false; // Indicates if LEDs are on or off

    // Initialize chosen serial port
    stdio_init_all();
    // Initialize LED pins and PWM
    ini_leds(leds);
    // Initialize rotary encoder pins
    ini_rot(rots);

    event_t event;
    while (true) {

        // Process all pending events from the queue
        while (queue_try_remove(&events, &event)) {

            // Handle button events
            if (event.type == EVENT_BUTTON && event.data == 1) {
                // Turn lights on
                if (!lightsOn) {
                    lightsOn = light_switch(leds, brightness, true);
                }
                else {
                    // If LEDs are on and brightness is 0%, restore to 50%
                    if (brightness <= 0) {
                        brightness = BR_MID;
                        set_brightness(leds, BR_MID);
                    }
                    // Otherwise turn lights off
                    else {
                        lightsOn = light_switch(leds, 0, false);
                    }
                }
            }

            // Handle encoder rotation events only when lights are on
            if (event.type == EVENT_ENCODER && lightsOn) {
                // Update brightness according to rotation direction and clamp to valid range
                brightness = clamp((int)brightness + (int)(event.data * BR_RATE));
                set_brightness(leds, brightness);
            }
        }

        sleep_ms(10); // 10 ms delay (0.01 second) to reduce CPU usage
    }
}
// Interrupt callback for pressing ROT_SW and rotary encoder
void gpio_callback(uint const gpio, uint32_t const event_mask) {
    // Button press/release with debounce to ensure one physical press counts as one event
    if (gpio == ROT_SW) {
        static uint32_t last_ms = 0; // Store last interrupt time
        const uint32_t now = to_ms_since_boot(get_absolute_time());

        // Detect button release (rising edge)
        if (event_mask & GPIO_IRQ_EDGE_RISE && now - last_ms >= DEBOUNCE_MS) {
            last_ms = now;
            const event_t event = { .type = EVENT_BUTTON, .data = 0 };
            queue_try_add(&events, &event); // Add event to queue
        }

        // Detect button press (falling edge)
        if (event_mask & GPIO_IRQ_EDGE_FALL && now - last_ms >= DEBOUNCE_MS){
            last_ms = now;
            const event_t event = { .type = EVENT_BUTTON, .data = 1 };
            queue_try_add(&events, &event); // Add event to queue
        }
    }

    // Rotary encoder rotation direction detection
    if (gpio == ROT_A && event_mask & GPIO_IRQ_EDGE_RISE) {
        const bool rot_b_state = gpio_get(ROT_B); // Read state of second encoder pin to determine rotation direction
        const event_t event = { .type = EVENT_ENCODER, .data = rot_b_state ? -1 : +1 }; // Determine rotation direction
        queue_try_add(&events, &event); // Add event to queue
    }
}

void ini_rot(const uint *rots) {
    // Initialize rotary switch with internal pull-up
    gpio_init(ROT_SW);
    gpio_set_dir(ROT_SW, GPIO_IN);
    gpio_pull_up(ROT_SW);

    // Initialize rotary encoder pins A and B without pull-ups
    for (int i = 0; i < 2; i++) {
        gpio_init(rots[i]);
        gpio_set_dir(rots[i], GPIO_IN);
        gpio_disable_pulls(rots[i]);
    }

    // Initialize event queue for Interrupt Service Routine (ISR)
    // 32 chosen as a safe buffer size: large enough to handle bursts of interrupts
    // without losing events, yet small enough to keep RAM usage minimal.
    queue_init(&events, sizeof(event_t), 32);

    // Configure button interrupt and callback
    gpio_set_irq_enabled_with_callback(ROT_SW, GPIO_IRQ_EDGE_FALL |
        GPIO_IRQ_EDGE_RISE, true, &gpio_callback);

    // Enable rising edge interrupt for encoder A and B
    for (int i = 0; i < 2; i++) {
        gpio_set_irq_enabled(rots[i], GPIO_IRQ_EDGE_RISE, true);
    }
}

void ini_leds(const uint *leds) {
    // Track which PWM slices (0-7) have been initialized
    bool slice_ini[8] = {false};

    // Get default PWM configuration
    pwm_config config = pwm_get_default_config();
    // Set clock divider
    pwm_config_set_clkdiv_int(&config, CLK_DIV);
    // Set wrap (TOP)
    pwm_config_set_wrap(&config, TOP);

    // Configure PWM for each LED pin
    for (int i = 0; i < LEDS_SIZE; i++) {
        // Get PWM slice and channel for this GPIO pin
        const uint slice = pwm_gpio_to_slice_num(leds[i]);
        const uint chan = pwm_gpio_to_channel(leds[i]);

        // Stop PWM
        pwm_set_enabled(slice, false);

        // Initialize each slice once
        if (!slice_ini[slice]) {
            pwm_init(slice, &config, false); // Start set to false
            slice_ini[slice] = true;
        }

        // Set compare value (CC) to define duty cycle
        pwm_set_chan_level(slice, chan, 0);
        // Select PWM model for your pin
        gpio_set_function(leds[i], GPIO_FUNC_PWM);
        // Start PWM
        pwm_set_enabled(slice, true);
    }
}

bool light_switch(const uint *leds, const uint brightness, const bool on) {
    if (on) {
        set_brightness(leds, brightness);
        return true;
    }
    set_brightness(leds, 0);
    return false;
}

void set_brightness(const uint *leds, const uint brightness) {
    // Update duty cycle for all LED channels
    for (int i = 0; i < LEDS_SIZE; i++) {
        const uint slice = pwm_gpio_to_slice_num(leds[i]);
        const uint chan  = pwm_gpio_to_channel(leds[i]);
        pwm_set_chan_level(slice, chan, brightness);
    }
}

uint clamp(const int br) {
    // Limit brightness value to valid PWM range [0, TOP]
    if (br < 0) return 0; // Lower bound
    if (br > MAX_BR) return MAX_BR; // Upper bound
    return br; // Within range
}