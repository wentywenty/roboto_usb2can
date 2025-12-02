#ifndef STATUS_LED_H_
#define STATUS_LED_H_

/* LED 状态枚举 */
enum led_status {
    LED_STATUS_INIT,           // 初始化中（快闪3次）
    LED_STATUS_IDLE,           // 空闲（慢闪，2秒周期）
    LED_STATUS_USB_READY,      // USB 已连接（中速闪，1秒周期）
    LED_STATUS_CAN_ACTIVE,     // CAN 活动（短闪）
    LED_STATUS_ERROR,          // 错误（快速闪烁）
};

/* 初始化状态 LED */
int status_led_init(void);

/* 设置 LED 状态模式 */
void status_led_set(enum led_status status);

/* CAN 活动指示 */
void status_led_can_activity(void);

/* 设置 LED 静态状态 */
void status_led_set_static(bool on);

#endif /* STATUS_LED_H_ */