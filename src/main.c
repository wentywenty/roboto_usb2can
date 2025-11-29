#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>
#include <cannectivity/usb/class/gs_usb.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static const struct device *uart_dev = DEVICE_DT_GET(DT_NODELABEL(usart1));

// static void uart_rx_callback(const struct device *dev, void *user_data)
// {
//     uint8_t c;

//     if (!uart_irq_update(uart_dev)) {
//         return;
//     }

//     if (!uart_irq_rx_ready(uart_dev)) {
//         return;
//     }

//     /* Read received data and echo back */
//     while (uart_fifo_read(uart_dev, &c, 1) == 1) {
//         uart_poll_out(uart_dev, c);  /* Echo */
//     }
// }

static int uart_init(void)
{
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("UART device not ready");
        return -1;
    }

    /* Configure interrupt driven mode */
    // uart_irq_callback_set(uart_dev, uart_rx_callback);
    // uart_irq_rx_enable(uart_dev);

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

    /* 初始化 USB 设备 */
    err = usb_enable(NULL);
    if (err != 0) {
        LOG_ERR("failed to enable USB (err %d)", err);
        return err;
    }

    LOG_INF("roboparty CAN FD initialized with %u channels", ARRAY_SIZE(channels));

    return 0;
}