#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include <stdbool.h>

#define CLK_DIV 125
#define TOP 999

#define ROT_A 10 // input without pull-up/pull-down
#define ROT_B 11 // input without pull-up/pull-down
#define ROT_SW 12 // input with pull-up

#define D1 22 // right LED
#define D2 21 // middle LED
#define D3 20 // left LED
#define LEDS_SIZE 3 // how many LEDs

#define BR_RATE 50 // step size for brightness changes
#define BR_MID (TOP / 2) // 50% brightness level

static int32_t enc_delta = 0;

static bool pressed = false; // prevents double presses
static bool toggle_req = false;
static bool lightsOn = false;

void gpio_callback(uint gpio, uint32_t events);
void ini_leds(const uint *leds);
bool rot_sw_pressed();
bool light_switch(const uint *leds, uint brightness, bool on);
void set_brightness(const uint *leds, uint brightness);
uint clamp(int br);

int main() {
    const uint leds[] = {D1, D2, D3};
    uint brightness = BR_MID; // LEDs brightness value

    // Initialize chosen serial port
    stdio_init_all();
    // Initialize LED pins
    ini_leds(leds);

    gpio_init(ROT_SW);
    gpio_set_dir(ROT_SW, GPIO_IN);
    gpio_pull_up(ROT_SW);

    gpio_init(ROT_A);
    gpio_set_dir(ROT_A, GPIO_IN);
    gpio_disable_pulls(ROT_A);

    gpio_init(ROT_B);
    gpio_set_dir(ROT_B, GPIO_IN);
    gpio_disable_pulls(ROT_B);

    gpio_set_irq_enabled_with_callback(ROT_SW, GPIO_IRQ_EDGE_FALL |
        GPIO_IRQ_EDGE_RISE, true, &gpio_callback);

    gpio_set_irq_enabled_with_callback(ROT_A, GPIO_IRQ_EDGE_RISE, true, &gpio_callback);

    while (true) {

        if (rot_sw_pressed()) {
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

        if (lightsOn) {
            const int32_t steps = enc_delta;
            if (steps != 0) {
                enc_delta -= steps;
                brightness = clamp((int)brightness + (int)(steps * BR_RATE));
                set_brightness(leds, brightness);
            }
        }

        sleep_ms(10);
    }
}

void gpio_callback(uint const gpio, uint32_t const events) {
    if (gpio == ROT_SW) {
        static uint32_t last_ms = 0;
        const uint32_t now = to_ms_since_boot(get_absolute_time());

        if (events & GPIO_IRQ_EDGE_RISE && now - last_ms >= 20) {
            pressed = false;
            last_ms = now;
        }
        if (events & GPIO_IRQ_EDGE_FALL && now - last_ms >= 20){
            pressed = true;
            toggle_req = true;
            last_ms = now;
        }
    }

    if (lightsOn) {
        if (gpio == ROT_A && events & GPIO_IRQ_EDGE_RISE) {
            const bool b = gpio_get(ROT_B);
            if (b) {
                printf("rotating left\r\n");
            }
            else {
                printf("rotating right\r\n");
            }
            enc_delta += b ? - 1 : + 1;
        }
    }
}

void ini_leds(const uint *leds) {
    bool slice_ini[8] = {false};

    // Get default PWM configuration
    pwm_config config = pwm_get_default_config();
    // Set clock divider
    pwm_config_set_clkdiv_int(&config, CLK_DIV);
    // Set wrap (TOP)
    pwm_config_set_wrap(&config, TOP);

    for (int i = 0; i < LEDS_SIZE; i++) {
        // Get slice and channel your GPIO pin
        const uint slice = pwm_gpio_to_slice_num(leds[i]);
        const uint chan = pwm_gpio_to_channel(leds[i]);

        // Stop PWM
        pwm_set_enabled(slice, false);

        // Initialize each slice once
        if (!slice_ini[slice]) {
            // Start set to true
            pwm_init(slice, &config, true);
            slice_ini[slice] = true;
        }

        // Set level to (CC) -> duty cycle
        pwm_set_chan_level(slice, chan, 0);
        // Select PWM model for your pin
        gpio_set_function(leds[i], GPIO_FUNC_PWM);
        // Start PWM
        pwm_set_enabled(slice, true);
    }
}

bool rot_sw_pressed() {
    if (pressed && toggle_req) {
        toggle_req = false;
        return true;
    }
    return false;
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
    // Update duty for all LED channels
    for (int i = 0; i < LEDS_SIZE; i++) {
        const uint slice = pwm_gpio_to_slice_num(leds[i]);
        const uint chan  = pwm_gpio_to_channel(leds[i]);
        pwm_set_chan_level(slice, chan, brightness);
    }
}

uint clamp(const int br) {
    if (br < 0) return 0;
    if (br > TOP) return TOP;
    return br;
}