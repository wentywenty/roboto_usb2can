#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#ifdef CONFIG_USB_DEVICE_STACK_NEXT
#include <zephyr/usb/usbd.h>
#else
#include <zephyr/usb/usb_device.h>
#endif
#include <cannectivity/usb/class/gs_usb.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart1));

#ifdef CONFIG_USB_DEVICE_STACK_NEXT
/* 新版 USB 栈：定义 USB 设备上下文 */
USBD_DEVICE_DEFINE(usbd, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0x1D50, 0x606F);

USBD_DESC_LANG_DEFINE(lang);
USBD_DESC_MANUFACTURER_DEFINE(mfr, "flora");
USBD_DESC_PRODUCT_DEFINE(product, "roboparty CAN FD");
USBD_DESC_SERIAL_NUMBER_DEFINE(sn);
USBD_DESC_CONFIG_DEFINE(fs_config_desc, "Full-Speed Configuration");
USBD_DESC_CONFIG_DEFINE(hs_config_desc, "High-Speed Configuration");

USBD_CONFIGURATION_DEFINE(fs_config, 0, 250, &fs_config_desc);
USBD_CONFIGURATION_DEFINE(hs_config, 0, 250, &hs_config_desc);
#endif

static int uart_init(void)
{
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("UART device not ready");
        return -1;
    }

    printk("UART1 echo enabled on PA9(TX)/PA10(RX) @ 115200\n");
    return 0;
}

int main(void)
{
    const struct device *gs_usb = DEVICE_DT_GET(DT_NODELABEL(gs_usb0));
    const struct device *channels[] = {
        DEVICE_DT_GET(DT_NODELABEL(fdcan1)),
        DEVICE_DT_GET(DT_NODELABEL(fdcan2))
    };
    int err;

    printk("*** roboparty CAN FD adapter ***\n");

    /* 初始化 UART echo */
    err = uart_init();
    if (err != 0) {
        LOG_ERR("Failed to initialize UART");
    }

    if (!device_is_ready(gs_usb)) {
        LOG_ERR("gs_usb not ready");
        return -1;
    }

    /* 注册 gs_usb 设备和通道 */
    err = gs_usb_register(gs_usb, channels, ARRAY_SIZE(channels), NULL, NULL);
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

    return 0;
}