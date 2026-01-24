/*
 * Status LED management for roboparty CAN FD
 * LED STAT (PC11) - 统一状态指示
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <cannectivity/usb/class/gs_usb.h>
#include "status_led.h"

LOG_MODULE_REGISTER(status_led, LOG_LEVEL_INF);

/* LED 活动计时器 */
#define LED_TICK_MS        50 /* 定时器每50ms触发 */
#define LED_TICKS_ACTIVITY 2  /* 活动LED亮2个tick (100ms) */

static struct k_timer activity_tick_timer;
static k_timepoint_t last_activity_time;
static int activity_ticks = 0;

/* LED 闪烁模式 */
struct led_pattern {
	uint16_t on_ms;  // 亮持续时间
	uint16_t off_ms; // 灭持续时间
	int8_t repeat;   // 重复次数 (-1=无限循环)
};

/* 各状态对应的闪烁模式 */
static const struct led_pattern led_patterns[] = {
	[LED_STATUS_INIT] = {100, 100, 3},       // 快闪 3 次（初始化）
	[LED_STATUS_IDLE] = {100, 1900, -1},     // 慢闪（2秒周期）
	[LED_STATUS_USB_READY] = {500, 500, -1}, // 中速闪（1秒周期）
	[LED_STATUS_CAN_ACTIVE] = {50, 50, 1},   // 短闪 1 次（CAN 活动）
	[LED_STATUS_ERROR] = {100, 100, -1},     // 快速闪烁（错误）
};

/* LED 硬件定义 */
static const struct gpio_dt_spec status_led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec activity_led = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

/* 工作队列 */
static struct k_work_delayable led_work;
static struct k_work_delayable activity_work; // Activity LED 工作队列
static enum led_status current_status = LED_STATUS_INIT;
static int current_repeat_count = 0;
static bool led_state = false;

/* LED 闪烁任务 */
static void led_blink_work(struct k_work *work)
{
	const struct led_pattern *pattern = &led_patterns[current_status];
	uint32_t next_delay;

	/* 切换 LED 状态 */
	led_state = !led_state;
	gpio_pin_set_dt(&status_led, led_state);

	/* 计算下次触发时间 */
	if (led_state) {
		next_delay = pattern->on_ms;
	} else {
		next_delay = pattern->off_ms;

		/* 检查重复次数 */
		if (pattern->repeat > 0) {
			current_repeat_count++;
			if (current_repeat_count >= pattern->repeat) {
				/* 完成指定次数，停止 */
				gpio_pin_set_dt(&status_led, 0);
				return;
			}
		}
	}

	/* 重新调度任务 */
	k_work_reschedule(&led_work, K_MSEC(next_delay));
}

/* Activity LED 熄灭任务 */
static void activity_led_off_work(struct k_work *work)
{
	gpio_pin_set_dt(&activity_led, 0);
}

/* 活动 LED 定时器回调（每50ms触发） */
static void activity_tick_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	if (activity_ticks > 0) {
		activity_ticks--;
		if (activity_ticks == (LED_TICKS_ACTIVITY / 2)) {
			/* 中tick后点亮 */
			gpio_pin_set_dt(&activity_led, 1);
		} else if (activity_ticks == 0) {
			/* 结束时熄灭 */
			gpio_pin_set_dt(&activity_led, 0);
		}
	}
}

/* 初始化状态 LED */
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

	/* 初始化 Activity LED (绿灯) */
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

	/* 初始化工作队列 */
	k_work_init_delayable(&led_work, led_blink_work);

	/* 初始化活动LED定时器 */
	k_timer_init(&activity_tick_timer, activity_tick_handler, NULL);
	k_timer_start(&activity_tick_timer, K_MSEC(LED_TICK_MS), K_MSEC(LED_TICK_MS));
	last_activity_time = sys_timepoint_calc(K_NO_WAIT);

	/* 启动初始化闪烁 */
	status_led_set(LED_STATUS_INIT);

	LOG_INF("Status LED initialized");
	return 0;
}

/* 设置 LED 状态 */
void status_led_set(enum led_status status)
{
	if (status >= ARRAY_SIZE(led_patterns)) {
		LOG_ERR("Invalid LED status: %d", status);
		return;
	}

	/* 取消当前闪烁 */
	k_work_cancel_delayable(&led_work);

	current_status = status;
	current_repeat_count = 0;
	led_state = false;

	/* 启动新的闪烁模式 */
	const struct led_pattern *pattern = &led_patterns[status];
	gpio_pin_set_dt(&status_led, 1);
	k_work_reschedule(&led_work, K_MSEC(pattern->on_ms));

	LOG_DBG("LED status changed to %d", status);
}

/* CAN 活动指示（独立控制绿灯，不影响蓝灯） */
void status_led_can_activity(void)
{
	if (!gpio_is_ready_dt(&activity_led)) {
		return;
	}

	/* 使用定时器计数器方式 */
	activity_ticks = LED_TICKS_ACTIVITY;
}

/* gs_usb 事件回调函数 */
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
		/* 低通滤波：防止LED闪烁过快 */
		if (!sys_timepoint_expired(last_activity_time)) {
			return 0; /* 忽略过频繁的事件 */
		}
		last_activity_time = sys_timepoint_calc(K_MSEC(LED_TICK_MS * LED_TICKS_ACTIVITY));
		status_led_can_activity();
		break;

	case GS_USB_EVENT_CHANNEL_STARTED:
		LOG_DBG("Channel %u started", ch);
		status_led_set(LED_STATUS_USB_READY);
		break;

	case GS_USB_EVENT_CHANNEL_STOPPED:
		LOG_DBG("Channel %u stopped", ch);
		status_led_set(LED_STATUS_IDLE);
		break;

	default:
		/* 忽略其他事件 */
		break;
	}

	return 0;
}

/* 设置 LED 常亮/常灭 */
void status_led_set_static(bool on)
{
	k_work_cancel_delayable(&led_work);
	gpio_pin_set_dt(&status_led, on);
}
