#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include <stdbool.h>
#include "pico/util/queue.h"

#define CLK_DIV 125 // PWM clock divider
#define TOP 999 // PWM counter top value

#define ROT_A 10 // Encoder input without pull-up/pull-down
#define ROT_B 11 // Encoder input without pull-up/pull-down
#define ROT_SW 12 // Encoder input with pull-up

#define DEBOUNCE_MS 20 // Debounce delay in milliseconds

#define D1 22 // right LED pin
#define D2 21 // middle LED pin
#define D3 20 // left LED pin
#define LEDS_SIZE 3 // number of LEDs

#define BR_RATE 50 // step size for brightness change
#define BR_MID (TOP / 2) // 50% brightness level

typedef enum { EVENT_BUTTON, EVENT_ENCODER } event_type;

typedef struct {
    event_type type; // EVENT_BUTTON or EVENT_ENCODER
    int32_t data; // 1 = press, 0 = release
} event_t;

static queue_t events;

void gpio_callback(uint gpio, uint32_t event_mask);
void ini_rot();
void ini_leds(const uint *leds);
bool light_switch(const uint *leds, uint brightness, bool on);
void set_brightness(const uint *leds, uint brightness);
uint clamp(int br);

int main() {
    const uint leds[] = {D1, D2, D3};
    uint brightness = BR_MID; // LEDs brightness value
    static bool lightsOn = false; // Indicates if LEDs are on or off

    // Initialize chosen serial port
    stdio_init_all();
    // Initialize LED pins
    ini_leds(leds);
    // Initialize rotary encoder pins
    ini_rot();

    event_t ev;
    while (true) {
        while (queue_try_remove(&events, &ev)) {
            if (ev.type == EVENT_BUTTON && ev.data == 1) {
                // Turn lights on
                if (!lightsOn) {
                    lightsOn = light_switch(leds, brightness, true);
                }
                else {
                    // If LEDs are on and dimmed to 0% then set 50% brightness
                    if (brightness <= 0) {
                        brightness = BR_MID;
                        set_brightness(leds, BR_MID);
                    }
                    // Turn lights off
                    else {
                        lightsOn = light_switch(leds, 0, false);
                    }
                }
            }
            // Increase/decrease lighting when lights are on
            if (ev.type == EVENT_ENCODER && lightsOn) {
                brightness = clamp((int)brightness + (int)(ev.data * BR_RATE));
                set_brightness(leds, brightness);
            }
        }
        sleep_ms(10); // loop delay
    }
}
// Interrupt callback for pressing ROT_SW and rotary encoder
void gpio_callback(uint const gpio, uint32_t const event_mask) {
    // Button press/release with debounce to ensure one physical press counts as one event
    if (gpio == ROT_SW) {
        static uint32_t last_ms = 0;
        const uint32_t now = to_ms_since_boot(get_absolute_time());

        if (event_mask & GPIO_IRQ_EDGE_RISE && now - last_ms >= DEBOUNCE_MS) {
            last_ms = now;
            const event_t event = { .type = EVENT_BUTTON, .data = 0 };
            queue_try_add(&events, &event);
        }
        if (event_mask & GPIO_IRQ_EDGE_FALL && now - last_ms >= DEBOUNCE_MS){
            last_ms = now;
            const event_t event = { .type = EVENT_BUTTON, .data = 1 };
            queue_try_add(&events, &event);
        }
    }
    // Rotary encoder rotation direction detection
    if (gpio == ROT_A && event_mask & GPIO_IRQ_EDGE_RISE) {
        const bool b = gpio_get(ROT_B);
        const event_t event = { .type = EVENT_ENCODER, .data = b ? -1 : +1 };
        queue_try_add(&events, &event);
    }
}

void ini_rot() {
    gpio_init(ROT_SW);
    gpio_set_dir(ROT_SW, GPIO_IN);
    gpio_pull_up(ROT_SW);

    const uint rot[] = {ROT_A, ROT_B};
    for (int i = 0; i < 2; i++) {
        gpio_init(rot[i]);
        gpio_set_dir(rot[i], GPIO_IN);
        gpio_disable_pulls(rot[i]);
    }

    // Initialize event queue for Interrupt Service Routine (ISR)
    queue_init(&events, sizeof(event_t), 32);

    // Configure button interrupt and callback
    gpio_set_irq_enabled_with_callback(ROT_SW, GPIO_IRQ_EDGE_FALL |
        GPIO_IRQ_EDGE_RISE, true, &gpio_callback);

    // Enable rising edge interrupt for encoder A and B
    gpio_set_irq_enabled(ROT_A, GPIO_IRQ_EDGE_RISE, true);
    gpio_set_irq_enabled(ROT_B, GPIO_IRQ_EDGE_RISE, true);
}

void ini_leds(const uint *leds) {
    bool slice_ini[8] = {false}; // Track which PWM slices have been initialized

    // Get default PWM configuration
    pwm_config config = pwm_get_default_config();
    // Set clock divider
    pwm_config_set_clkdiv_int(&config, CLK_DIV);
    // Set wrap (TOP)
    pwm_config_set_wrap(&config, TOP);

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
    // Set duty for all LED channels
    for (int i = 0; i < LEDS_SIZE; i++) {
        const uint slice = pwm_gpio_to_slice_num(leds[i]);
        const uint chan = pwm_gpio_to_channel(leds[i]);
        pwm_set_chan_level(slice, chan, on ? brightness : 0);
    }
    return on;
}

void set_brightness(const uint *leds, const uint brightness) {
    // Update duty cycle for all LED channels
    for (int i = 0; i < LEDS_SIZE; i++) {
        const uint slice = pwm_gpio_to_slice_num(leds[i]);
        const uint chan  = pwm_gpio_to_channel(leds[i]);
        pwm_set_chan_level(slice, chan, brightness);
    }
}
// Returns clamped brightness by ensuring the value stays within 0-TOP
uint clamp(const int br) {
    if (br < 0) return 0;
    if (br > TOP) return TOP;
    return br;
}