#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/msos_desc.h>
#include <zephyr/sys/byteorder.h>
#include <cannectivity/usb/class/gs_usb.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart1));

/* ==================== WinUSB 支持 ==================== */

/* Microsoft OS 2.0 描述符 */
#define COMPATIBLE_ID_WINUSB 'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00

/* gs_usb DeviceInterfaceGUID (candleLight 兼容) */
#define GS_USB_DEVICE_INTERFACE_GUID \
    '{', 0x00, 'c', 0x00, '6', 0x00, 'e', 0x00, '5', 0x00, '1', 0x00, \
    '5', 0x00, 'a', 0x00, '2', 0x00, '-', 0x00, '8', 0x00, 'd', 0x00, \
    'c', 0x00, '6', 0x00, '-', 0x00, '4', 0x00, 'f', 0x00, 'c', 0x00, \
    '4', 0x00, '-', 0x00, 'a', 0x00, '0', 0x00, '3', 0x00, 'c', 0x00, \
    '-', 0x00, '9', 0x00, '3', 0x00, '2', 0x00, '5', 0x00, '5', 0x00, \
    '5', 0x00, 'd', 0x00, '6', 0x00, '8', 0x00, 'e', 0x00, '6', 0x00, \
    '}', 0x00, 0x00, 0x00

/* MSOS 2.0 描述符结构 */
struct msos2_descriptor {
    struct msosv2_descriptor_set_header header;
    struct msosv2_compatible_id compatible_id;
    struct msosv2_guids_property guids_property;
} __packed;

static const struct msos2_descriptor msos2_desc = {
    .header = {
        .wLength = sizeof(struct msosv2_descriptor_set_header),
        .wDescriptorType = MS_OS_20_SET_HEADER_DESCRIPTOR,
        .dwWindowsVersion = 0x06030000,  /* Windows 8.1+ */
        .wTotalLength = sizeof(struct msos2_descriptor),
    },
    .compatible_id = {
        .wLength = sizeof(struct msosv2_compatible_id),
        .wDescriptorType = MS_OS_20_FEATURE_COMPATIBLE_ID,
        .CompatibleID = {COMPATIBLE_ID_WINUSB},
    },
    .guids_property = {
        .wLength = sizeof(struct msosv2_guids_property),
        .wDescriptorType = MS_OS_20_FEATURE_REG_PROPERTY,
        .wPropertyDataType = MS_OS_20_PROPERTY_DATA_REG_MULTI_SZ,
        .wPropertyNameLength = 42,
        .PropertyName = {DEVICE_INTERFACE_GUIDS_PROPERTY_NAME},
        .wPropertyDataLength = 80,
        .bPropertyData = {GS_USB_DEVICE_INTERFACE_GUID},
    },
};

/* BOS 描述符：USB 2.0 Extension */
static const struct usb_bos_capability_lpm bos_cap_lpm = {
    .bLength = sizeof(struct usb_bos_capability_lpm),
    .bDescriptorType = USB_DESC_DEVICE_CAPABILITY,
    .bDevCapabilityType = USB_BOS_CAPABILITY_EXTENSION,
    .bmAttributes = 0UL,
};

/* BOS 描述符：Microsoft OS 2.0 Platform */
struct usb_bos_msosv2 {
    struct usb_bos_platform_descriptor platform;
    struct usb_bos_capability_msos cap;
} __packed;

static const struct usb_bos_msosv2 bos_cap_msosv2 = {
    .platform = {
        .bLength = sizeof(struct usb_bos_msosv2),
        .bDescriptorType = USB_DESC_DEVICE_CAPABILITY,
        .bDevCapabilityType = USB_BOS_CAPABILITY_PLATFORM,
        .bReserved = 0,
        .PlatformCapabilityUUID = {
            /* MS OS 2.0 Platform Capability ID */
            0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C,
            0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F,
        },
    },
    .cap = {
        .dwWindowsVersion = sys_cpu_to_le32(0x06030000),
        .wMSOSDescriptorSetTotalLength = sys_cpu_to_le16(sizeof(msos2_desc)),
        .bMS_VendorCode = 0x01,  /* Vendor Request 编号 */
        .bAltEnumCode = 0x00,
    },
};

/* Vendor Request 处理函数 */
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

/* 注册 BOS 描述符 */
USBD_DESC_BOS_DEFINE(bos_lpm, sizeof(bos_cap_lpm), &bos_cap_lpm);
USBD_DESC_BOS_VREQ_DEFINE(bos_msosv2, sizeof(bos_cap_msosv2), &bos_cap_msosv2,
              0x01, msos_vendor_handler, NULL);

/* ==================== USB 设备定义 ==================== */

USBD_DEVICE_DEFINE(usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0x1D50, 0x606F);

USBD_DESC_LANG_DEFINE(lang);
USBD_DESC_MANUFACTURER_DEFINE(mfr, "flora");
USBD_DESC_PRODUCT_DEFINE(product, "roboparty CAN FD");
USBD_DESC_SERIAL_NUMBER_DEFINE(sn);
USBD_DESC_CONFIG_DEFINE(fs_config_desc, "Full-Speed Configuration");
USBD_DESC_CONFIG_DEFINE(hs_config_desc, "High-Speed Configuration");

USBD_CONFIGURATION_DEFINE(fs_config, USB_SCD_SELF_POWERED, 125, &fs_config_desc);
USBD_CONFIGURATION_DEFINE(hs_config, USB_SCD_SELF_POWERED, 125, &hs_config_desc);

/* ==================== UART 初始化 ==================== */

static int uart_init(void)
{
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("UART device not ready");
        return -1;
    }

    printk("UART1 echo enabled on PA9(TX)/PA10(RX) @ 115200\n");
    return 0;
}

/* ==================== 主函数 ==================== */

int main(void)
{
    const struct device *gs_usb = DEVICE_DT_GET(DT_NODELABEL(gs_usb0));
    const struct device *channels[] = {
        DEVICE_DT_GET(DT_NODELABEL(fdcan1)),
        DEVICE_DT_GET(DT_NODELABEL(fdcan2))
    };
    int err;

    printk("*** roboparty CAN FD adapter with WinUSB ***\n");

    /* 初始化 UART */
    err = uart_init();
    if (err != 0) {
        LOG_ERR("Failed to initialize UART");
    }

    /* 检查 gs_usb 设备 */
    if (!device_is_ready(gs_usb)) {
        LOG_ERR("gs_usb not ready");
        return -1;
    }

    /* 注册 gs_usb 通道 */
    err = gs_usb_register(gs_usb, channels, ARRAY_SIZE(channels), NULL, NULL);
    if (err != 0) {
        LOG_ERR("failed to register gs_usb (err %d)", err);
        return err;
    }

    /* ==================== USB 初始化 ==================== */

    /* 添加字符串描述符 */
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

    /* 配置高速模式（如果支持） */
    if (usbd_caps_speed(&usbd) == USBD_SPEED_HS) {
        err = usbd_add_configuration(&usbd, USBD_SPEED_HS, &hs_config);
        if (err != 0) {
            LOG_ERR("failed to add HS configuration (err %d)", err);
            return err;
        }

        err = usbd_register_class(&usbd, "gs_usb_0", USBD_SPEED_HS, 1);
        if (err != 0) {
            LOG_ERR("failed to register HS gs_usb (err %d)", err);
            return err;
        }

        err = usbd_device_set_code_triple(&usbd, USBD_SPEED_HS, 0, 0, 0);
        if (err != 0) {
            LOG_ERR("failed to set HS code triple (err %d)", err);
            return err;
        }

        err = usbd_device_set_bcd_usb(&usbd, USBD_SPEED_HS, USB_SRN_2_0_1);
        if (err != 0) {
            LOG_ERR("failed to set HS bcdUSB (err %d)", err);
            return err;
        }
    }

    /* 配置全速模式 */
    err = usbd_add_configuration(&usbd, USBD_SPEED_FS, &fs_config);
    if (err != 0) {
        LOG_ERR("failed to add FS configuration (err %d)", err);
        return err;
    }

    err = usbd_register_class(&usbd, "gs_usb_0", USBD_SPEED_FS, 1);
    if (err != 0) {
        LOG_ERR("failed to register FS gs_usb (err %d)", err);
        return err;
    }

    err = usbd_device_set_code_triple(&usbd, USBD_SPEED_FS, 0, 0, 0);
    if (err != 0) {
        LOG_ERR("failed to set FS code triple (err %d)", err);
        return err;
    }

    err = usbd_device_set_bcd_usb(&usbd, USBD_SPEED_FS, USB_SRN_2_0_1);
    if (err != 0) {
        LOG_ERR("failed to set FS bcdUSB (err %d)", err);
        return err;
    }

    /* 设置设备版本 */
    err = usbd_device_set_bcd_device(&usbd, sys_cpu_to_le16(0x0100));
    if (err != 0) {
        LOG_ERR("failed to set bcdDevice (err %d)", err);
        return err;
    }

    /* ✅ 添加 BOS 描述符（WinUSB 支持） */
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

    /* 初始化 USB 子系统 */
    err = usbd_init(&usbd);
    if (err != 0) {
        LOG_ERR("failed to initialize USB device (err %d)", err);
        return err;
    }

    /* 启动 USB */
    err = usbd_enable(&usbd);
    if (err != 0) {
        LOG_ERR("failed to enable USB device (err %d)", err);
        return err;
    }

    LOG_INF("roboparty CAN FD initialized with %u channels", ARRAY_SIZE(channels));
    LOG_INF("WinUSB support enabled - plug and play on Windows 8.1+");

    return 0;
}