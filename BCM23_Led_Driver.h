#ifndef BCM23_LED_DRIVER_H_
#define BCM23_LED_DRIVER_H_

#include <linux/ioctl.h>

/*** ioctl用パラメータ(第3引数)の定義 ***/
struct bcm23_led_values {
	int val1;
	int val2;
};

#define IOC_TYPE 1

// 値設定コマンド
#define DEVICE_SET_VALUES _IOW(IOC_TYPE, 1, struct bcm23_led_values)

// 値取得コマンド
#define DEVICE_GET_VALUES _IOR(IOC_TYPE, 2, struct bcm23_led_values)


#endif /* BCM23_LED_DRIVER_H_ */