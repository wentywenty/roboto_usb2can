#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#ifdef CONFIG_USB_DEVICE_STACK_NEXT
#include <zephyr/usb/usbd.h>
#else
#include <zephyr/usb/usb_device.h>
#endif
#include <zephyr/drivers/can.h>
#include <cannectivity/usb/class/gs_usb.h>
#include "status_led.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* CAN 错误帧监控配置 */
#define CAN_ERR_FRAME_THRESHOLD 50   /* 每秒最多允许 50 个错误帧 */
#define CAN_ERR_WINDOW_MS       1000 /* 统计窗口 1 秒 */
#define CAN_ERR_PASSIVE_LIMIT   10   /* ERROR_PASSIVE 累计 10 次后强制 Bus-Off */

/* CAN 错误监控器（每通道） */
struct can_error_monitor {
	uint32_t err_frame_count;   /* 当前窗口内错误帧数量 */
	uint32_t err_passive_count; /* ERROR_PASSIVE 累计次数 */
	int64_t window_start_ms;    /* 统计窗口起始时间 */
	bool throttled;             /* 是否已触发节流 */
	bool forced_busoff;         /* 是否已强制 Bus-Off */
};

static struct can_error_monitor err_monitors[1] = {0}; /* 单通道，可扩展 */
static const struct device *can_devices[] = {DEVICE_DT_GET(DT_NODELABEL(fdcan1))};

// static const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart1));

#ifdef CONFIG_USB_DEVICE_STACK_NEXT
/* 新版 USB 栈：定义 USB 设备上下文 */
USBD_DEVICE_DEFINE(usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0x1D50, 0x606F);

USBD_DESC_LANG_DEFINE(lang);
USBD_DESC_MANUFACTURER_DEFINE(mfr, "flora");
USBD_DESC_PRODUCT_DEFINE(product, "roboparty CAN FD");
USBD_DESC_SERIAL_NUMBER_DEFINE(sn);
USBD_DESC_CONFIG_DEFINE(fs_config_desc, "Full-Speed Configuration");

USBD_CONFIGURATION_DEFINE(fs_config, 0, 250, &fs_config_desc);
#endif

/* CAN 状态变化回调 - 错误监控与保护 */
static void can_state_change_callback(const struct device *dev, enum can_state state,
				      struct can_bus_err_cnt err_cnt, void *user_data)
{
	int ch = 0; /* 当前只支持单通道，多通道需要通过 user_data 或 dev 匹配 */
	struct can_error_monitor *mon = &err_monitors[ch];
	int64_t now = k_uptime_get();

	/* 重置统计窗口 */
	if ((now - mon->window_start_ms) > CAN_ERR_WINDOW_MS) {
		mon->err_frame_count = 0;
		mon->window_start_ms = now;
		mon->throttled = false;
	}

	/* 错误帧计数 */
	if (state != CAN_STATE_ERROR_ACTIVE) {
		mon->err_frame_count++;
	}

	/* 错误帧洪水检测 */
	if (mon->err_frame_count > CAN_ERR_FRAME_THRESHOLD) {
		if (!mon->throttled) {
			LOG_ERR("CH%d: Error frame flood detected (%u/s), throttling", ch,
				mon->err_frame_count);
			mon->throttled = true;
			status_led_set(LED_STATUS_ERROR);
		}

		/* 主动进入 Bus-Off 保护总线 */
		if (!mon->forced_busoff) {
			LOG_ERR("CH%d: Forcing Bus-Off to prevent bus freeze", ch);
			can_stop(dev);
			mon->forced_busoff = true;
			return;
		}
	}

	/* 处理不同错误状态 */
	switch (state) {
	case CAN_STATE_ERROR_ACTIVE:
		LOG_DBG("CH%d: CAN ERROR_ACTIVE (TEC=%u, REC=%u)", ch, err_cnt.tx_err_cnt,
			err_cnt.rx_err_cnt);
		mon->err_passive_count = 0; /* 恢复后重置计数 */
		mon->forced_busoff = false;
		break;

	case CAN_STATE_ERROR_WARNING:
		LOG_WRN("CH%d: CAN ERROR_WARNING (TEC=%u, REC=%u)", ch, err_cnt.tx_err_cnt,
			err_cnt.rx_err_cnt);
		break;

	case CAN_STATE_ERROR_PASSIVE:
		mon->err_passive_count++;
		LOG_WRN("CH%d: CAN ERROR_PASSIVE #%u (TEC=%u, REC=%u)", ch, mon->err_passive_count,
			err_cnt.tx_err_cnt, err_cnt.rx_err_cnt);

		/* ERROR_PASSIVE 持续出现 -> 主动 Bus-Off */
		if (mon->err_passive_count > CAN_ERR_PASSIVE_LIMIT) {
			LOG_ERR("CH%d: Persistent ERROR_PASSIVE (%u times), forcing Bus-Off", ch,
				mon->err_passive_count);
			can_stop(dev);
			mon->forced_busoff = true;
			status_led_set(LED_STATUS_ERROR);
		}
		break;

	case CAN_STATE_BUS_OFF:
		LOG_ERR("CH%d: CAN BUS_OFF (TEC=%u, REC=%u)", ch, err_cnt.tx_err_cnt,
			err_cnt.rx_err_cnt);
		mon->forced_busoff = true;
		status_led_set(LED_STATUS_ERROR);
		break;

	case CAN_STATE_STOPPED:
		LOG_INF("CH%d: CAN STOPPED", ch);
		break;
	}
}

int main(void)
{
	const struct device *gs_usb = DEVICE_DT_GET(DT_NODELABEL(gs_usb0));
	const struct device *channels[] = {DEVICE_DT_GET(DT_NODELABEL(fdcan1))};
	struct gs_usb_ops ops = {
		.event = status_led_event,
	};
	int err;

	/* 初始化状态 LED */
	err = status_led_init();
	if (err) {
		LOG_ERR("Failed to initialize status LED (err %d)", err);
	}

	printk("*** roboparty CAN FD adapter ***\n");

	/* 初始化 CAN 错误监控 */
	for (int i = 0; i < ARRAY_SIZE(can_devices); i++) {
		if (!device_is_ready(can_devices[i])) {
			LOG_ERR("CAN device %d not ready", i);
			continue;
		}

		/* 注册 CAN 状态变化回调 */
		can_set_state_change_callback(can_devices[i], can_state_change_callback,
					      (void *)(intptr_t)i);
		LOG_INF("CAN error monitoring enabled for channel %d", i);

		/* 初始化错误监控器 */
		err_monitors[i].window_start_ms = k_uptime_get();
	}

	if (!device_is_ready(gs_usb)) {
		LOG_ERR("gs_usb not ready");
		return -1;
	}

	/* 注册 gs_usb 设备和通道，并设置事件回调 */
	err = gs_usb_register(gs_usb, channels, ARRAY_SIZE(channels), &ops, NULL);
	if (err != 0) {
		LOG_ERR("failed to register gs_usb (err %d)", err);
		return err;
	}

#ifdef CONFIG_USB_DEVICE_STACK_NEXT
	/* 新版 USB 栈初始化 */
	err = usbd_add_descriptor(&usbd, &lang);
	if (err != 0) {
		LOG_ERR("failed to add language descriptor (err %d)", err);
		return err;
	}

	err = usbd_add_descriptor(&usbd, &mfr);
	if (err != 0) {
		LOG_ERR("failed to add manufacturer descriptor (err %d)", err);
		return err;
	}

	err = usbd_add_descriptor(&usbd, &product);
	if (err != 0) {
		LOG_ERR("failed to add product descriptor (err %d)", err);
		return err;
	}

	err = usbd_add_descriptor(&usbd, &sn);
	if (err != 0) {
		LOG_ERR("failed to add serial number descriptor (err %d)", err);
		return err;
	}

	err = usbd_add_configuration(&usbd, USBD_SPEED_FS, &fs_config);
	if (err != 0) {
		LOG_ERR("failed to add full-speed configuration (err %d)", err);
		return err;
	}

	err = usbd_register_class(&usbd, "gs_usb_0", USBD_SPEED_FS, 1);
	if (err != 0) {
		LOG_ERR("failed to register gs_usb class (err %d)", err);
		return err;
	}

	err = usbd_device_set_code_triple(&usbd, USBD_SPEED_FS, 0, 0, 0);
	if (err != 0) {
		LOG_ERR("failed to set code triple (err %d)", err);
		return err;
	}

	err = usbd_init(&usbd);
	if (err != 0) {
		LOG_ERR("failed to initialize USB device (err %d)", err);
		return err;
	}

	err = usbd_enable(&usbd);
	if (err != 0) {
		LOG_ERR("failed to enable USB device (err %d)", err);
		return err;
	}
#else
	/* 旧版 USB 栈 */
	err = usb_enable(NULL);
	if (err != 0) {
		LOG_ERR("failed to enable USB (err %d)", err);
		return err;
	}
#endif

	LOG_INF("roboparty CAN FD initialized with %u channels", ARRAY_SIZE(channels));

	/* 设置 LED 为 USB 就绪状态 */
	status_led_set(LED_STATUS_USB_READY);

	return 0;
}
