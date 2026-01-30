#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#ifdef CONFIG_USB_DEVICE_STACK_NEXT
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/msos_desc.h>
#include <zephyr/sys/byteorder.h>
#else
#include <zephyr/usb/usb_device.h>
#endif
#include <zephyr/drivers/can.h>
#include <cannectivity/usb/class/gs_usb.h>
#include "status_led.h"
#include "version.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* MSOS 2.0 Descriptors for WinUSB Support */
#define COMPATIBLE_ID_WINUSB 'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00
/* gs_usb DeviceInterfaceGUID (candleLight compatible) */
#define GS_USB_DEVICE_INTERFACE_GUID                                                               \
	'{', 0x00, 'c', 0x00, '6', 0x00, 'e', 0x00, '5', 0x00, '1', 0x00, '5', 0x00, 'a', 0x00,    \
		'2', 0x00, '-', 0x00, '8', 0x00, 'd', 0x00, 'c', 0x00, '6', 0x00, '-', 0x00, '4',  \
		0x00, 'f', 0x00, 'c', 0x00, '4', 0x00, '-', 0x00, 'a', 0x00, '0', 0x00, '3', 0x00, \
		'c', 0x00, '-', 0x00, '9', 0x00, '3', 0x00, '2', 0x00, '5', 0x00, '5', 0x00, '5',  \
		0x00, 'd', 0x00, '6', 0x00, '8', 0x00, 'e', 0x00, '6', 0x00, '}', 0x00, 0x00, 0x00

/* MSOS 2.0 Descriptor Structure */
struct msos2_descriptor {
	struct msosv2_descriptor_set_header header;
	struct msosv2_compatible_id compatible_id;
	struct msosv2_guids_property guids_property;
} __packed;

static const struct msos2_descriptor msos2_desc = {
	.header =
		{
			.wLength = sizeof(struct msosv2_descriptor_set_header),
			.wDescriptorType = MS_OS_20_SET_HEADER_DESCRIPTOR,
			.dwWindowsVersion = 0x06030000, /* Windows 8.1+ */
			.wTotalLength = sizeof(struct msos2_descriptor),
		},
	.compatible_id =
		{
			.wLength = sizeof(struct msosv2_compatible_id),
			.wDescriptorType = MS_OS_20_FEATURE_COMPATIBLE_ID,
			.CompatibleID = {COMPATIBLE_ID_WINUSB},
		},
	.guids_property =
		{
			.wLength = sizeof(struct msosv2_guids_property),
			.wDescriptorType = MS_OS_20_FEATURE_REG_PROPERTY,
			.wPropertyDataType = MS_OS_20_PROPERTY_DATA_REG_MULTI_SZ,
			.wPropertyNameLength = 42,
			.PropertyName = {DEVICE_INTERFACE_GUIDS_PROPERTY_NAME},
			.wPropertyDataLength = 80,
			.bPropertyData = {GS_USB_DEVICE_INTERFACE_GUID},
		},
};

/* BOS Descriptor: USB 2.0 Extension */
static const struct usb_bos_capability_lpm bos_cap_lpm = {
	.bLength = sizeof(struct usb_bos_capability_lpm),
	.bDescriptorType = USB_DESC_DEVICE_CAPABILITY,
	.bDevCapabilityType = USB_BOS_CAPABILITY_EXTENSION,
	.bmAttributes = 0UL,
};

/* BOS Descriptor: Microsoft OS 2.0 Platform */
struct usb_bos_msosv2 {
	struct usb_bos_platform_descriptor platform;
	struct usb_bos_capability_msos cap;
} __packed;

static const struct usb_bos_msosv2 bos_cap_msosv2 = {
	.platform =
		{
			.bLength = sizeof(struct usb_bos_msosv2),
			.bDescriptorType = USB_DESC_DEVICE_CAPABILITY,
			.bDevCapabilityType = USB_BOS_CAPABILITY_PLATFORM,
			.bReserved = 0,
			.PlatformCapabilityUUID =
				{
					/* MS OS 2.0 Platform Capability ID */
					0xDF,
					0x60,
					0xDD,
					0xD8,
					0x89,
					0x45,
					0xC7,
					0x4C,
					0x9C,
					0xD2,
					0x65,
					0x9D,
					0x9E,
					0x64,
					0x8A,
					0x9F,
				},
		},
	.cap =
		{
			.dwWindowsVersion = sys_cpu_to_le32(0x06030000),
			.wMSOSDescriptorSetTotalLength = sys_cpu_to_le16(sizeof(msos2_desc)),
			.bMS_VendorCode = 0x01, /* Vendor Request Code */
			.bAltEnumCode = 0x00,
		},
};

/* Vendor Request Handler */
static int msos_vendor_handler(const struct usbd_context *const ctx,
			       const struct usb_setup_packet *const setup,
			       struct net_buf *const buf)
{
	if (setup->bRequest == 0x01 && setup->wIndex == MS_OS_20_DESCRIPTOR_INDEX) {
		size_t len = sizeof(msos2_desc);
		net_buf_add_mem(buf, &msos2_desc, MIN(net_buf_tailroom(buf), len));
		LOG_INF("Windows requested MSOS2 descriptor");
		return 0;
	}
	return -ENOTSUP;
}

/* Register BOS Descriptors */
USBD_DESC_BOS_DEFINE(bos_lpm, sizeof(bos_cap_lpm), &bos_cap_lpm);
USBD_DESC_BOS_VREQ_DEFINE(bos_msosv2, sizeof(bos_cap_msosv2), &bos_cap_msosv2, 0x01,
			  msos_vendor_handler, NULL);

/* CAN error frame monitoring configuration */
#define CAN_ERR_FRAME_THRESHOLD 50   /* Max 50 error frames allowed per second */
#define CAN_ERR_WINDOW_MS       1000 /* Statistics window 1 second */
#define CAN_ERR_PASSIVE_LIMIT   10   /* Force Bus-Off after 10 accumulated ERROR_PASSIVE */

/* CAN error monitor (per channel) */
struct can_error_monitor {
	uint32_t err_frame_count;   /* Error frame count in current window */
	uint32_t err_passive_count; /* Accumulated ERROR_PASSIVE count */
	int64_t window_start_ms;    /* Statistics window start time */
	bool throttled;             /* Whether throttling triggered */
	bool forced_busoff;         /* Whether Bus-Off forced */
};

static struct can_error_monitor err_monitors[1] = {0}; /* Single channel, extensible */
static const struct device *can_devices[] = {DEVICE_DT_GET(DT_NODELABEL(fdcan1))};

#ifdef CONFIG_USB_DEVICE_STACK_NEXT

/* New USB stack: Define USB device context */
USBD_DEVICE_DEFINE(usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0x1D50, 0x606F);

USBD_DESC_LANG_DEFINE(lang);
USBD_DESC_MANUFACTURER_DEFINE(mfr, "wentywenty");
USBD_DESC_PRODUCT_DEFINE(product, "roboto_usb2can");
USBD_DESC_SERIAL_NUMBER_DEFINE(sn);
USBD_DESC_CONFIG_DEFINE(fs_config_desc, "Full-Speed Configuration");
USBD_CONFIGURATION_DEFINE(fs_config, 0, 250, &fs_config_desc);
#endif

/* CAN state change callback - Error monitoring and protection */
static void can_state_change_callback(const struct device *dev, enum can_state state,
				      struct can_bus_err_cnt err_cnt, void *user_data)
{
	int ch = 0; /* Currently only supports single channel, multi-channel needs matching via
		       user_data or dev */
	struct can_error_monitor *mon = &err_monitors[ch];
	int64_t now = k_uptime_get();

	/* Reset statistics window */
	if ((now - mon->window_start_ms) > CAN_ERR_WINDOW_MS) {
		mon->err_frame_count = 0;
		mon->window_start_ms = now;
		mon->throttled = false;
	}

	/* Error frame count */
	if (state != CAN_STATE_ERROR_ACTIVE) {
		mon->err_frame_count++;
	}

	/* Error frame flood detection */
	if (mon->err_frame_count > CAN_ERR_FRAME_THRESHOLD) {
		if (!mon->throttled) {
			LOG_ERR("CH%d: Error frame flood detected (%u/s), throttling", ch,
				mon->err_frame_count);
			mon->throttled = true;
			status_led_set(LED_STATUS_ERROR);
		}

		/* Actively enter Bus-Off to protect bus */
		if (!mon->forced_busoff) {
			LOG_ERR("CH%d: Forcing Bus-Off to prevent bus freeze", ch);
			can_stop(dev);
			mon->forced_busoff = true;
			return;
		}
	}

	/* Handle different error states */
	switch (state) {
	case CAN_STATE_ERROR_ACTIVE:
		LOG_DBG("CH%d: CAN ERROR_ACTIVE (TEC=%u, REC=%u)", ch, err_cnt.tx_err_cnt,
			err_cnt.rx_err_cnt);
		mon->err_passive_count = 0; /* Reset count after recovery */
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

		/* Persistent ERROR_PASSIVE -> active Bus-Off */
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

	/* Initialize status LED */
	err = status_led_init();
	if (err) {
		LOG_ERR("Failed to initialize status LED (err %d)", err);
	}

	printk("*** roboto_usb2can adapter v%s ***\n", APP_VERSION_STR);

	/* Initialize CAN error monitoring */
	for (int i = 0; i < ARRAY_SIZE(can_devices); i++) {
		if (!device_is_ready(can_devices[i])) {
			LOG_ERR("CAN device %d not ready", i);
			continue;
		}

		/* Register CAN state change callback */
		can_set_state_change_callback(can_devices[i], can_state_change_callback,
					      (void *)(intptr_t)i);
		LOG_INF("CAN error monitoring enabled for channel %d", i);

		/* Initialize error monitor */
		err_monitors[i].window_start_ms = k_uptime_get();
	}

	if (!device_is_ready(gs_usb)) {
		LOG_ERR("gs_usb not ready");
		return -1;
	}

	/* Register gs_usb device and channels, and set event callback */
	err = gs_usb_register(gs_usb, channels, ARRAY_SIZE(channels), &ops, NULL);
	if (err != 0) {
		LOG_ERR("failed to register gs_usb (err %d)", err);
		return err;
	}

#ifdef CONFIG_USB_DEVICE_STACK_NEXT
	/* New USB stack initialization */
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

	/* Set USB version to 2.0.1 to trigger BOS descriptor read (required for WinUSB) */
	err = usbd_device_set_bcd_usb(&usbd, USBD_SPEED_FS, USB_SRN_2_0_1);
	if (err != 0) {
		LOG_ERR("failed to set FS bcdUSB (err %d)", err);
		return err;
	}

	/* Set device version */
	err = usbd_device_set_bcd_device(&usbd, APP_VERSION_BCD);
	if (err != 0) {
		LOG_ERR("failed to set bcdDevice (err %d)", err);
		return err;
	}

	err = usbd_add_descriptor(&usbd, &bos_lpm);
	if (err != 0) {
		LOG_ERR("failed to add BOS LPM descriptor (err %d)", err);
		return err;
	}

	err = usbd_add_descriptor(&usbd, &bos_msosv2);
	if (err != 0) {
		LOG_ERR("failed to add BOS MSOS2 descriptor (err %d)", err);
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
	/* Old USB stack */
	err = usb_enable(NULL);
	if (err != 0) {
		LOG_ERR("failed to enable USB (err %d)", err);
		return err;
	}
#endif

	LOG_INF("roboto_usb2can initialized with %u channels", ARRAY_SIZE(channels));

	/* Set LED to USB ready state */
	status_led_set(LED_STATUS_USB_READY);
	LOG_INF("WinUSB support enabled - plug and play on Windows 8.1+");

	return 0;
}
