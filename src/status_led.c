/*
 * Status LED management for roboto_usb2can
 */

#include "status_led.h"
#include <cannectivity/usb/class/gs_usb.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(status_led, LOG_LEVEL_INF);

/* Spurious STOPPED event filtering (prevent Linux 5/6.1 kernel bug) */
#define MIN_STOP_INTERVAL_MS 1000 /* STOP events within 1 second are considered abnormal */
static k_timepoint_t last_stopped_time;

/* LED activity timer */
#define LED_TICK_MS        50 /* Timer triggers every 50ms */
#define LED_TICKS_ACTIVITY 2  /* Activity LED on for 2 ticks (100ms) */

static struct k_timer activity_tick_timer;
static k_timepoint_t last_activity_time;
static int activity_ticks = 0;

/* LED blink patterns */
struct led_pattern {
	uint16_t on_ms;  // Duration on
	uint16_t off_ms; // Duration off
	int8_t repeat;   // Repeat count (-1=infinite loop)
};

/* Blink patterns for each status */
static const struct led_pattern led_patterns[] = {
	[LED_STATUS_INIT] = {100, 100, 3},       // Fast blink 3 times (initialization)
	[LED_STATUS_IDLE] = {100, 1900, -1},     // Slow blink (2s period)
	[LED_STATUS_USB_READY] = {500, 500, -1}, // Medium blink (1s period)
	[LED_STATUS_CAN_ACTIVE] = {50, 50, 1},   // Short blink 1 time (CAN activity)
	[LED_STATUS_ERROR] = {100, 100, -1},     // Fast blink (error)
};

/* LED hardware definitions */
static const struct gpio_dt_spec status_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec activity_led = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

/* Work queue */
static struct k_work_delayable led_work;
static struct k_work_delayable activity_work; // Activity LED work queue
static enum led_status current_status = LED_STATUS_INIT;
static int current_repeat_count = 0;
static bool led_state = false;

/* LED blink work */
static void led_blink_work(struct k_work *work)
{
	const struct led_pattern *pattern = &led_patterns[current_status];
	uint32_t next_delay;

	/* Toggle LED state */
	led_state = !led_state;
	gpio_pin_set_dt(&status_led, led_state);

	/* Calculate next trigger time */
	if (led_state) {
		next_delay = pattern->on_ms;
	} else {
		next_delay = pattern->off_ms;

		/* Check repeat count */
		if (pattern->repeat > 0) {
			current_repeat_count++;
			if (current_repeat_count >= pattern->repeat) {
				/* Completed specified count, stop */
				gpio_pin_set_dt(&status_led, 0);
				return;
			}
		}
	}

	/* Reschedule work */
	k_work_reschedule(&led_work, K_MSEC(next_delay));
}

/* Activity LED turn off work */
static void activity_led_off_work(struct k_work *work)
{
	gpio_pin_set_dt(&activity_led, 0);
}

/* Activity LED timer callback (triggers every 50ms) */
static void activity_tick_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	if (activity_ticks > 0) {
		activity_ticks--;
		if (activity_ticks == (LED_TICKS_ACTIVITY / 2)) {
			/* Turn on after middle tick */
			gpio_pin_set_dt(&activity_led, 1);
		} else if (activity_ticks == 0) {
			/* Turn off at end */
			gpio_pin_set_dt(&activity_led, 0);
		}
	}
}

/* Initialize status LED */
int status_led_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&status_led)) {
		LOG_ERR("Status LED GPIO not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure status LED: %d", ret);
		return ret;
	}

	/* Initialize Activity LED (Green) */
	if (!gpio_is_ready_dt(&activity_led)) {
		LOG_WRN("Activity LED GPIO not ready");
	} else {
		ret = gpio_pin_configure_dt(&activity_led, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_WRN("Failed to configure activity LED: %d", ret);
		} else {
			k_work_init_delayable(&activity_work, activity_led_off_work);
			LOG_INF("Activity LED initialized");
		}
	}

	/* Initialize work queue */
	k_work_init_delayable(&led_work, led_blink_work);

	/* Initialize activity LED timer */
	k_timer_init(&activity_tick_timer, activity_tick_handler, NULL);
	k_timer_start(&activity_tick_timer, K_MSEC(LED_TICK_MS), K_MSEC(LED_TICK_MS));
	last_activity_time = sys_timepoint_calc(K_NO_WAIT);
	last_stopped_time = sys_timepoint_calc(K_NO_WAIT); /* Initialize STOPPED filter */

	/* Start initialization blink */
	status_led_set(LED_STATUS_INIT);

	LOG_INF("Status LED initialized");
	return 0;
}

/* Set LED status */
void status_led_set(enum led_status status)
{
	if (status >= ARRAY_SIZE(led_patterns)) {
		LOG_ERR("Invalid LED status: %d", status);
		return;
	}

	/* Cancel current blink */
	k_work_cancel_delayable(&led_work);

	current_status = status;
	current_repeat_count = 0;
	led_state = false;

	/* Start new blink pattern */
	const struct led_pattern *pattern = &led_patterns[status];
	gpio_pin_set_dt(&status_led, 1);
	k_work_reschedule(&led_work, K_MSEC(pattern->on_ms));

	LOG_DBG("LED status changed to %d", status);
}

/* CAN activity indication (independently controls green LED, does not affect blue LED) */
void status_led_can_activity(void)
{
	if (!gpio_is_ready_dt(&activity_led)) {
		return;
	}

	/* Use timer counter method */
	activity_ticks = LED_TICKS_ACTIVITY;
}

/* gs_usb event callback function */
int status_led_event(const struct device *dev, uint16_t ch, enum gs_usb_event event,
		     void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(ch);
	ARG_UNUSED(user_data);

	switch (event) {
	case GS_USB_EVENT_CHANNEL_ACTIVITY_RX:
		__fallthrough;
	case GS_USB_EVENT_CHANNEL_ACTIVITY_TX:
		/* Low-pass filter: prevent LED from blinking too fast */
		if (!sys_timepoint_expired(last_activity_time)) {
			return 0; /* Ignore frequent events */
		}
		last_activity_time = sys_timepoint_calc(K_MSEC(LED_TICK_MS * LED_TICKS_ACTIVITY));
		status_led_can_activity();
		break;

	case GS_USB_EVENT_CHANNEL_STARTED:
		LOG_DBG("Channel %u started", ch);
		last_stopped_time = sys_timepoint_calc(K_NO_WAIT); /* Reset STOPPED filter */
		status_led_set(LED_STATUS_USB_READY);
		break;

	case GS_USB_EVENT_CHANNEL_STOPPED:
		/* Filter spurious STOP events from Linux 5/6.1 */
		if (!sys_timepoint_expired(last_stopped_time)) {
			LOG_WRN("Channel %u: Ignoring spurious STOPPED event (possible Linux "
				"kernel bug)",
				ch);
			return 0; /* Ignore spurious events */
		}
		last_stopped_time = sys_timepoint_calc(K_MSEC(MIN_STOP_INTERVAL_MS));
		LOG_DBG("Channel %u stopped", ch);
		status_led_set(LED_STATUS_IDLE);
		break;

	default:
		/* Ignore other events */
		break;
	}

	return 0;
}

/* Set LED on or off constantly */
void status_led_set_static(bool on)
{
	k_work_cancel_delayable(&led_work);
	gpio_pin_set_dt(&status_led, on);
}
