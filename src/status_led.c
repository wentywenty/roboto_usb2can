/*
 * Status LED management for roboparty CAN FD
 * LED STAT (PC11) - 统一状态指示
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(status_led, LOG_LEVEL_INF);

/* LED 状态定义 */
enum led_status {
    LED_STATUS_INIT,           // 初始化中
    LED_STATUS_IDLE,           // 空闲（USB 未连接）
    LED_STATUS_USB_READY,      // USB 已连接
    LED_STATUS_CAN_ACTIVE,     // CAN 通信活动
    LED_STATUS_ERROR,          // 错误状态
};

/* LED 闪烁模式 */
struct led_pattern {
    uint16_t on_ms;    // 亮持续时间
    uint16_t off_ms;   // 灭持续时间
    int8_t repeat;     // 重复次数 (-1=无限循环)
};

/* 各状态对应的闪烁模式 */
static const struct led_pattern led_patterns[] = {
    [LED_STATUS_INIT]       = {100, 100, 3},     // 快闪 3 次（初始化）
    [LED_STATUS_IDLE]       = {100, 1900, -1},   // 慢闪（2秒周期）
    [LED_STATUS_USB_READY]  = {500, 500, -1},    // 中速闪（1秒周期）
    [LED_STATUS_CAN_ACTIVE] = {50, 50, 1},       // 短闪 1 次（CAN 活动）
    [LED_STATUS_ERROR]      = {100, 100, 20},    // 快速闪烁 20 次（约 4 秒）
};

/* LED 硬件定义 */
static const struct gpio_dt_spec status_led = 
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

/* 工作队列 */
static struct k_work_delayable led_work;
static enum led_status current_status = LED_STATUS_INIT;
static enum led_status previous_status = LED_STATUS_IDLE;  // ✅ 记录进入错误前的状态
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
                /* 完成指定次数 */
                gpio_pin_set_dt(&status_led, 0);
                
                /* ✅ 错误状态超时后自动恢复 */
                if (current_status == LED_STATUS_ERROR) {
                    LOG_INF("Error indication timeout, restoring to %s",
                            previous_status == LED_STATUS_USB_READY ? "USB_READY" : "IDLE");
                    status_led_set(previous_status);
                }
                return;
            }
        }
    }

    /* 重新调度任务 */
    k_work_reschedule(&led_work, K_MSEC(next_delay));
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

    /* 初始化工作队列 */
    k_work_init_delayable(&led_work, led_blink_work);

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

    /* ✅ 进入错误状态前保存当前状态 */
    if (status == LED_STATUS_ERROR && current_status != LED_STATUS_ERROR) {
        previous_status = current_status;
        LOG_DBG("Entering error state, saved previous: %d", previous_status);
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

/* CAN 活动指示（短闪一次） */
void status_led_can_activity(void)
{
    /* 仅在 USB_READY 状态下响应 CAN 活动 */
    if (current_status == LED_STATUS_USB_READY) {
        /* 取消当前闪烁 */
        k_work_cancel_delayable(&led_work);
        
        /* 短闪 50ms */
        current_status = LED_STATUS_CAN_ACTIVE;
        current_repeat_count = 0;
        gpio_pin_set_dt(&status_led, 1);
        k_work_reschedule(&led_work, K_MSEC(50));
        
        /* 50ms 后恢复 USB_READY 状态 */
        k_work_schedule(&led_work, K_MSEC(100));
        current_status = LED_STATUS_USB_READY;
    }
}

/* 设置 LED 常亮/常灭 */
void status_led_set_static(bool on)
{
    k_work_cancel_delayable(&led_work);
    gpio_pin_set_dt(&status_led, on);
}