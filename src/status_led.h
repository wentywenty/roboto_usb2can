#ifndef STATUS_LED_H_
#define STATUS_LED_H_

#include <cannectivity/usb/class/gs_usb.h>

/* LED Status Enumeration */
enum led_status {
	LED_STATUS_INIT,       // Initializing (3 quick blinks)
	LED_STATUS_IDLE,       // Idle (slow blink, 2s cycle)
	LED_STATUS_USB_READY,  // USB connected (medium-speed blink, 1s cycle)
	LED_STATUS_CAN_ACTIVE, // CAN active (short blinks)
	LED_STATUS_ERROR,      // Error (rapid blinking)
};

/* Initialize the status LED */
int status_led_init(void);

/* Set LED status mode */
void status_led_set(enum led_status status);

/* Indicate CAN bus activity */
void status_led_can_activity(void);

/* GS-USB event callback (For use with gs_usb_register) */
int status_led_event(const struct device *dev, uint16_t ch, enum gs_usb_event event,
		     void *user_data);

/* Set LED static on/off state */
void status_led_set_static(bool on);

#endif /* STATUS_LED_H_ */
