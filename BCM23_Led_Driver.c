#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <asm/current.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include "BCM23_Led_Driver.h"

MODULE_LICENSE("Dual MIT/GPL");

#define NUM_BUFFER 256                      // やり取りするデータサイズ
#define DRIVER_NAME "BCM23_Led"				// デバイス名

/* ペリフェラルレジスタの物理アドレス(BCM2711の仕様書より) */
#define REG_ADDR_BASE              (0xFE000000)
#define REG_ADDR_GPIO_BASE         (REG_ADDR_BASE + 0x00200000)
#define REG_ADDR_GPIO_GPFSEL_2     0x0008
#define REG_ADDR_GPIO_OUTPUT_SET_0 0x001C
#define REG_ADDR_GPIO_OUTPUT_CLR_0 0x0028
#define REG_ADDR_GPIO_LEVEL_0      0x0034

#define REG(addr) (*((volatile unsigned int*)(addr)))
#define DUMP_REG(addr) printk("%08X\n", REG(addr));

struct file_data  {
	unsigned char buffer[NUM_BUFFER];
};

static const unsigned int MINOR_BASE = 0;	// マイナー番号の開始番号
static const unsigned int MINOR_NUM  = 1;	// デバイス数

static unsigned int  led23_major;			  // メジャー番号
static struct cdev   led23_cdev;			  // キャラクタデバイスオブジェクト
static struct class *device_class = NULL;	  // デバイスドライバのクラスオブジェクト
static struct bcm23_led_values stored_values; // ioctl値保持変数

// システムコール
static int     bcm23_led_open(struct inode *inode, struct file *file);                                  // open時に呼ばれる関数
static int     bcm23_led_release(struct inode *inode, struct file *file);                               // release時に呼ばれる関数
static ssize_t bcm23_led_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos);        // read時に呼ばれる関数
static ssize_t bcm23_led_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos); // write時に呼ばれる関数
static long    bcm23_led_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);                 // ioctl時に呼ばれる

static int     bcm23_led_init(void);                                                                    // load(insmod)時に呼ばれる関数
static void    bcm23_led_exit(void);                                                                    // unload(rmmod)時に呼ばれる関数

// 各種システムコールに対応するハンドラテーブル
static struct file_operations bcm23_led_fops = {
	.open           = bcm23_led_open,
	.release        = bcm23_led_release,
	.read           = bcm23_led_read,
	.write          = bcm23_led_write,
	.unlocked_ioctl = bcm23_led_ioctl,  // 64bit ioctl
	.compat_ioctl   = bcm23_led_ioctl,	// 32bit ioctl
};

module_init(bcm23_led_init);
module_exit(bcm23_led_exit);

static int bcm23_led_open(struct inode *inode, struct file *file)
{
	struct file_data* p = kmalloc(sizeof(struct file_data ), GFP_KERNEL);
	if(p==NULL)
		return -ENOMEM;
	strlcat(p->buffer, "dummy", 5);
	file->private_data = p;

    // 物理メモリアドレスをカーネル仮想アドレスへマッピングする
    int address = (int)ioremap_cache(REG_ADDR_GPIO_BASE , PAGE_SIZE);
    // GPIO23を出力に設定
    REG(address + REG_ADDR_GPIO_GPFSEL_2) |= 1 << 9;
	// I/Oメモリアクセス終了時呼び出し関数
    iounmap((void*)address);

	return 0;
}

static int bcm23_led_release(struct inode *inode, struct file *file)
{
	if(file->private_data==NULL)
        return 0;
    kfree(file->private_data);
    file->private_data = NULL;
	return 0;
}

static ssize_t bcm23_led_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	if(count>NUM_BUFFER) count = NUM_BUFFER;
	
	/* ARM(CPU)から見た物理アドレス → 仮想アドレス(カーネル空間)へのマッピング */
    int address = (int)ioremap_cache(REG_ADDR_GPIO_BASE, PAGE_SIZE);
    int val = (REG(address + REG_ADDR_GPIO_LEVEL_0) & (1 << 23)) != 0;

    struct file_data* p = filp->private_data;
	p->buffer[0] = val + '0';
	p->buffer[1] = '\0';
	if (copy_to_user(buf, p->buffer, count)!=0)
		return -EFAULT;

    iounmap((void*)address);
    
	return count;
}

static ssize_t bcm23_led_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	int address;
	struct file_data* p = filp->private_data;
	if (copy_from_user(p->buffer, buf, count) != 0){
		return -EFAULT;
	}
    //GPIO23のHIGH/LOWを取得
    if ( count != 1){
        return count;
    }

	address = (int)ioremap_cache(REG_ADDR_GPIO_BASE, PAGE_SIZE);
    switch(p->buffer[0]){
    case '0': // LOW
		REG(address+REG_ADDR_GPIO_OUTPUT_CLR_0) |= 1 << 23;
        break;
    case '1': // HIGH
		REG(address+REG_ADDR_GPIO_OUTPUT_SET_0) |= 1 << 23;
        break;
    default:
        break;
    }
	DUMP_REG(address + REG_ADDR_GPIO_LEVEL_0);
	iounmap((void*)address);

	return count;
}

static long bcm23_led_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case DEVICE_SET_VALUES:
		if (copy_from_user(&stored_values, (void __user *)arg, sizeof(stored_values)))
			return -EFAULT;
		break;
	case DEVICE_GET_VALUES:
		if (copy_to_user((void __user *)arg, &stored_values, sizeof(stored_values)))
			return -EFAULT;
		break;
	default:
		printk(KERN_WARNING "unsupported command %d\n", cmd);
		return -EFAULT;
	}
	return 0;
}

static int bcm23_led_init(void)
{
	int alloc_ret = 0;
	int cdev_err = 0;
	dev_t dev;

	/* 空いているメジャー番号を確保する */
	alloc_ret = alloc_chrdev_region(&dev, MINOR_BASE, MINOR_NUM, DRIVER_NAME);
	if (alloc_ret != 0) {
		printk(KERN_ERR  "alloc_chrdev_region = %d\n", alloc_ret);
		return -1;
	}

	/* 取得したdev( = メジャー番号 + マイナー番号)からメジャー番号を取得して保持しておく */
	led23_major = MAJOR(dev);
	dev = MKDEV(led23_major, MINOR_BASE);	/* 不要? */

	/* cdev構造体の初期化とシステムコールハンドラテーブルの登録 */
	cdev_init(&led23_cdev, &bcm23_led_fops);
	led23_cdev.owner = THIS_MODULE;

	/* このデバイスドライバ(cdev)をカーネルに登録する */
	cdev_err = cdev_add(&led23_cdev, dev, MINOR_NUM);
	if (cdev_err != 0) {
		unregister_chrdev_region(dev, MINOR_NUM);
		return -1;
	}

	/* このデバイスのクラス登録をする(/sys/class/bcm23_led/ を作る) */
	device_class = class_create(THIS_MODULE, "bcm23_led");
	if (IS_ERR(device_class)) {
		cdev_del(&led23_cdev);
		unregister_chrdev_region(dev, MINOR_NUM);
		return -1;
	}

	/* /sys/class/bcm23_led/bcm23_led* を作る */
	for (int minor = MINOR_BASE; minor < MINOR_BASE + MINOR_NUM; minor++)
		device_create(device_class, NULL, MKDEV(led23_major, minor), NULL, "bcm23_led%d", minor);

	return 0;
}

static void bcm23_led_exit(void)
{
	dev_t dev = MKDEV(led23_major, MINOR_BASE);
	/* /sys/class/bcm23_led/bcm23_led* を削除する */
	for (int minor = MINOR_BASE; minor < MINOR_BASE + MINOR_NUM; minor++)
		device_destroy(device_class, MKDEV(led23_major, minor));
	/* このデバイスのクラス登録を取り除く(/sys/class/bcm23_led/を削除する) */
	class_destroy(device_class);
	/* このデバイスドライバ(cdev)をカーネルから取り除く */
	cdev_del(&led23_cdev);
	/* このデバイスドライバで使用していたメジャー番号の登録を取り除く */
	unregister_chrdev_region(dev, MINOR_NUM);

}

