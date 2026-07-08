#include "led.h"

#include "hal/gpio.h"
#include "hal/pwm.h"
#include "hal/tasks.h"
#include "hal/timer.h"

#include <stdio.h>

// Fade granularity: update the PWM duty every this many ms while transitioning.
#define LED_FADE_STEP_MS    20

// Drive the physical output to a duty (0..255). For a plain on/off led any
// non-zero duty means "on"; for a dimmable led it sets the PWM level.
static void led_write_raw(led_t *led, uint8_t duty) {
    if (led->dimmable) {
        led->cur_duty = duty;
        hal_pwm_set_duty(led->pwm_channel, duty);
    } else {
        hal_gpio_write(led->pin, duty ? led->on_high : !led->on_high);
    }
}

// Level used for the "on" phase (blink / plain on) of a dimmable led.
static uint8_t led_on_level(led_t *led) {
    if (!led->dimmable)
        return 255;

    return led->brightness ? led->brightness : 255;
}

static void led_fade_handler(void *arg) {
    led_t *led = (led_t *)arg;

    if (led->cur_duty == led->target_duty)
        return;

    uint16_t steps = led->transition_ms / LED_FADE_STEP_MS;
    if (steps == 0) {
        led_write_raw(led, led->target_duty);
        return;
    }

    int diff = (int)led->target_duty - (int)led->cur_duty;
    int step = diff / (int)steps;
    if (step == 0)
        step = (diff > 0) ? 1 : -1;

    int next = (int)led->cur_duty + step;
    if ((step > 0 && next > led->target_duty) ||
        (step < 0 && next < led->target_duty)) {
        next = led->target_duty;
    }
    led_write_raw(led, (uint8_t)next);

    if (led->cur_duty != led->target_duty)
        hal_tasks_schedule(&led->fade_task, LED_FADE_STEP_MS);
}

static void led_fade_to(led_t *led, uint8_t target) {
    led->target_duty = target;
    hal_tasks_unschedule(&led->fade_task);

    if (led->transition_ms == 0 || led->cur_duty == target) {
        led_write_raw(led, target);
        return;
    }

    led->fade_task.handler = led_fade_handler;
    led->fade_task.arg     = led;
    hal_tasks_init(&led->fade_task);
    hal_tasks_schedule(&led->fade_task, LED_FADE_STEP_MS);
}

void led_init(led_t *led) {
    if (led->dimmable) {
        if (led->brightness == 0)
            led->brightness = 255; // default to full until configured

        // The HAL picks the PWM channel from the pin (on TLSR8258 each pin
        // supports at most one specific PWM function). If the pin has no PWM
        // at all, fall back to plain on/off GPIO control.
        int8_t channel = hal_pwm_init(led->pin, !led->on_high);
        if (channel < 0) {
            led->dimmable = 0;
        } else {
            led->pwm_channel = (uint8_t)channel;
            led->cur_duty    = 0;
            led->target_duty = 0;
        }
    }
    led_off(led);
}

void led_on(led_t *led) {
    led->on = 1;
    led->blink_times_left = 0;
    if (led->dimmable) {
        led_fade_to(led, led->brightness);
    } else {
        hal_gpio_write(led->pin, led->on_high);
    }
}

void led_off(led_t *led) {
    led->on = 0;
    led->blink_times_left = 0;
    if (led->dimmable) {
        led_fade_to(led, 0);
    } else {
        hal_gpio_write(led->pin, !led->on_high);
    }
}

void led_set_brightness(led_t *led, uint8_t brightness) {
    led->brightness = brightness;
    // Live dimming: re-fade to the new level if currently on.
    if (led->dimmable && led->on)
        led_fade_to(led, brightness);
}

void led_set_transition(led_t *led, uint16_t transition_ms) {
    led->transition_ms = transition_ms;
}

static void led_blink_handler(void *arg) {
    led_t *led = (led_t *)arg;

    if (led->blink_times_left == 0)
        return;

    if (led->on) {
        led->on = 0;
        led_write_raw(led, 0);
        if (led->blink_times_left != LED_BLINK_FOREVER) {
            led->blink_times_left--;
        }
        hal_tasks_schedule(&led->blink_task, led->blink_time_off);
    } else {
        led->on = 1;
        led_write_raw(led, led_on_level(led));
        hal_tasks_schedule(&led->blink_task, led->blink_time_on);
    }
}

void led_blink(led_t *led, uint16_t on_time_ms, uint16_t off_time_ms,
               uint16_t times) {
    // Always set new durations
    led->blink_time_on  = on_time_ms;
    led->blink_time_off = off_time_ms;

    if (led->blink_times_left != 0) {
        // If we already blinking, do not reschedule
        // to unnecessary avoid jumps in blinking pace
        led->blink_times_left = times;
        return;
    }

    hal_tasks_unschedule(&led->fade_task); // a blink overrides any fade in flight
    led_write_raw(led, led_on_level(led));
    led->on = 1;
    led->blink_times_left   = times;
    led->blink_task.handler = led_blink_handler;
    led->blink_task.arg     = led;
    hal_tasks_init(&led->blink_task);
    hal_tasks_schedule(&led->blink_task, on_time_ms);
}
