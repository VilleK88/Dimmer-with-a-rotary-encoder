#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include <stdbool.h>

#define CLK_DIV 125
#define TOP 999

#define SW2 7 // right button - decreases brightness
#define SW1 8 // middle button - light switch
#define SW0 9 // left button - increases brightness
#define BUTTONS_SIZE 3 // how many buttons

#define D1 22 // right LED
#define D2 21 // middle LED
#define D3 20 // left LED
#define LEDS_SIZE 3 // how many LEDs

#define BR_RATE 50 // step size for brightness changes
#define BR_MID (TOP / 2) // 50% brightness level

void ini_buttons(const uint *buttons);
void ini_leds(const uint *leds);
bool light_switch(const uint *leds, uint brightness, bool on);
void set_brightness(const uint *leds, uint brightness);
uint clamp(int br);

int main() {
    const uint buttons[] = {SW2, SW1, SW0};
    const uint leds[] = {D1, D2, D3};
    uint brightness = BR_MID; // LEDs brightness value

    // Initialize chosen serial port
    stdio_init_all();
    // Initialize buttons
    ini_buttons(buttons);
    // Initialize LED pins
    ini_leds(leds);

    bool lightsOn = false;
    bool previous_state = true; // SW1 is pulled up, so "released" = true

    while (true) {
        const bool sw1_state = gpio_get(SW1);

        // released -> pressed
        if (previous_state && !sw1_state) {
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
            // Increase lighting
            if (!gpio_get(SW2)) {
                brightness = clamp((int)brightness - BR_RATE);
                set_brightness(leds, brightness);
            }
            // Decrease lighting
            if (!gpio_get(SW0)) {
                brightness = clamp((int)brightness + BR_RATE);
                set_brightness(leds, brightness);
            }
        }

        sleep_ms(200);
        previous_state = sw1_state;
    }
}

void ini_buttons(const uint *buttons) {
    for (int i = 0; i < BUTTONS_SIZE; i++) {
        gpio_init(buttons[i]);
        gpio_set_dir(buttons[i], GPIO_IN);
        gpio_pull_up(buttons[i]);
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
        pwm_set_enabled(leds[i], false);

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
        pwm_set_enabled(leds[i], true);
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