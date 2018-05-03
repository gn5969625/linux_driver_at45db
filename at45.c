#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/compat.h>
#include <linux/mman.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/io.h>
#include<linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/workqueue.h>
#include <linux/spi/spidev.h>
#include <asm/uaccess.h>
#include <linux/kernel.h>

#define GPIO_HIGH 1
#define GPIO_LOW 0
static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);
#define SPIDEV_MAJOR			153	/* assigned */
#define N_SPI_MINORS			32	/* ... up to 256 */
static unsigned bufsiz = 4096;
#define DESC(x)  1


#if DESC("This should define in .h file")
struct spidev_data {
	dev_t				devt;
	spinlock_t			spi_lock;
	struct spi_device	*spi;
	struct list_head	device_entry;
	struct mutex		buf_lock;
	unsigned long		users;
};

typedef struct SPI_Flash_Ctrl
{
	unsigned char *cmd;
	unsigned char *read_buf;
	unsigned char *w_data;
	unsigned char  cmd_len;
	unsigned char  read_buf_len;
	unsigned char  w_len;
	unsigned char  resv;
}FLASH_C;

#endif


#if DESC("Global para")
static DECLARE_BITMAP(minors, N_SPI_MINORS);
static int At45db_nwp;
static int At45db_nrst;
static int global_major;
static struct class  *spiflash_cls;
static struct device *spiflash_dev;
static struct spi_device *flash_spi = NULL;

#endif



static void spidev_complete(void *arg)
{
	complete(arg);
}


static ssize_t
spidev_sync(struct spidev_data *spidev, struct spi_message *message)
{
	DECLARE_COMPLETION_ONSTACK(done);
	int status;

#if 1
	message->complete = spidev_complete;
	message->context = &done;
#endif 
	spin_lock_irq(&spidev->spi_lock);
	if (spidev->spi == NULL)
		status = -ESHUTDOWN;
	else
		status = spi_async(spidev->spi, message);
	spin_unlock_irq(&spidev->spi_lock);

	if (status == 0) {
		wait_for_completion(&done);
		status = message->status;
		if (status == 0)
			status = message->actual_length;
	}
	return status;
}


static inline ssize_t 
spidev_sync_write(struct spidev_data *spidev, FLASH_C *flash_c, size_t len)
{
	struct spi_transfer	t[] = {
		{
			.tx_buf		= flash_c->cmd,
			.len		= len,
		},
		{
			.tx_buf     = flash_c->w_data,
			.len        = flash_c->w_len,
		},
	};
	struct spi_message	m;

	spi_message_init(&m);
	spi_message_add_tail(&t[0], &m);	
	spi_message_add_tail(&t[1], &m);
	return spidev_sync(spidev, &m);
}

static ssize_t flash_write(struct spidev_data *spidev, FLASH_C *flash_c)
{

	gpio_direction_output(At45db_nwp, GPIO_HIGH);
	return spidev_sync_write(spidev, flash_c, flash_c->cmd_len);

	
}

/*spi的read函数，spi flash在read之前需要先写入执行，所以spi_transfer结构体中需要包含tx和rx*/
static inline ssize_t
spidev_sync_read(struct spidev_data *spidev, FLASH_C *flash_c)
{
	struct spi_transfer	t[] = {
		{
			.tx_buf		= flash_c->cmd,
			.len		= flash_c->cmd_len,
		},
		{
			.rx_buf     = flash_c->read_buf,
			.len        = flash_c->read_buf_len,
		}
	};
	struct spi_message	m;

	spi_message_init(&m);
	spi_message_add_tail(&t[0], &m);
	spi_message_add_tail(&t[1], &m);
	return spidev_sync(spidev, &m);
}

static int flashspi_release(struct inode *inode, struct file *filp)
{
	struct spidev_data	*spidev;
	int			status = -ENXIO;

	
	printk("%s \n", __FUNCTION__);

	mutex_lock(&device_list_lock);

	list_for_each_entry(spidev, &device_list, device_entry) {
		if (spidev->devt == inode->i_rdev) {
			status = 0;
			break;
		}
	}
	if (status == 0) {

		spidev->users--;
		filp->private_data = NULL;
	} else
		pr_debug("spidev: nothing for minor %d\n", iminor(inode));

	mutex_unlock(&device_list_lock);
	return status;
}

static int flashspi_open(struct inode *inode, struct file *filp)
{
	struct spidev_data	*spidev;
	int			status = -ENXIO;

	
	printk("%s \n", __FUNCTION__);
	
	mutex_lock(&device_list_lock);

	list_for_each_entry(spidev, &device_list, device_entry) {
		if (spidev->devt == inode->i_rdev) {
			status = 0;
			break;
		}
	}
	if (status == 0) {

		spidev->users++;
		filp->private_data = spidev;
		//nonseekable_open(inode, filp);
	} else
		pr_debug("spidev: nothing for minor %d\n", iminor(inode));

	mutex_unlock(&device_list_lock);
	return status;
}


/*调用read的时候需要将命令也传下来*/
static ssize_t
flashspi_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct spidev_data	*spidev;
	ssize_t			status = 0;
	FLASH_C *tmp = (FLASH_C *)buf;
	unsigned long	missing;
	FLASH_C flash_c;

	/* chipselect only toggles at start or end of operation */
	if (count > bufsiz)
		return -EMSGSIZE;

	
	printk("%s \n", __FUNCTION__);

	spidev = filp->private_data;
	flash_c.cmd_len = tmp->cmd_len;

	flash_c.cmd = kmalloc(flash_c.cmd_len, GFP_KERNEL);
	memcpy(flash_c.cmd, tmp->cmd, flash_c.cmd_len);

	flash_c.read_buf = kmalloc(count, GFP_KERNEL);
	flash_c.read_buf_len = tmp->read_buf_len;
	
	/*读取操作加锁,防止其他线程进入*/
	status = spidev_sync_read(spidev, &flash_c);
	#if 1
	if (status > 0) {
		missing = copy_to_user(((FLASH_C *)buf)->read_buf, flash_c.read_buf, count);
		if (missing == status)
			status = -EFAULT;
		else
			status = status - missing;
	}
	#endif
	
	kfree(flash_c.cmd);
	kfree(flash_c.read_buf);


	return status;
}

/* Write-only message with current device setup */
static ssize_t
flashspi_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *f_pos)
{
	struct spidev_data	*spidev;
	ssize_t			status = 0;
	unsigned long		missing;
	FLASH_C flash_c;
	FLASH_C *tmp = (FLASH_C *)buf;

	printk("%s \n", __FUNCTION__);
	
	/* chipselect only toggles at start or end of operation */
	if (count > bufsiz)
		return -EMSGSIZE;

	spidev = filp->private_data;
	flash_c.cmd_len = tmp->cmd_len;
	flash_c.w_len   = tmp->w_len;

	flash_c.cmd = kmalloc(flash_c.cmd_len, GFP_KERNEL);
	flash_c.w_data = kmalloc(flash_c.w_len, GFP_KERNEL);

	mutex_lock(&spidev->buf_lock);
	missing = copy_from_user(flash_c.cmd, tmp->cmd, flash_c.cmd_len);
	missing += copy_from_user(flash_c.w_data, tmp->w_data, flash_c.w_len);
	if (missing == 0) {
		status = flash_write(spidev, &flash_c);
	} else
		status = -EFAULT;
	mutex_unlock(&spidev->buf_lock);

	kfree(flash_c.cmd);
	kfree(flash_c.w_data);
	return status;
}

static int At45db_flash_init(struct spidev_data	*spidev)
{
	unsigned char init_cmd[] = {0xc7, 0x94, 0x80, 0x9a};
	struct spi_transfer	t = {
			.tx_buf		= init_cmd,
			.len		= 4,
		};
	struct spi_message	m;

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spidev_sync(spidev, &m);
}

static int flash_spi_probe(struct spi_device *spi)
{
	struct spidev_data	*spidev;
	struct device_node  *np = spi->dev.of_node;   /*Get device tree node*/
	int					 status;
	unsigned long		 minor;
	int                  ret;
	
	printk("%s \n", __FUNCTION__);

	printk("AT45DB chip found\n");
	/* Allocate driver data */
	spidev = kzalloc(sizeof(*spidev), GFP_KERNEL);
	if (!spidev)
		return -ENOMEM;

	/* Initialize the driver data */
	spi->mode = 0;
	spi->max_speed_hz = 1000 * 1000 * 10;
	spi->bits_per_word = 8;
	spidev->spi = spi;

	/*inin lock*/
	spin_lock_init(&spidev->spi_lock);
	mutex_init(&spidev->buf_lock);

	
	/*Reset register condition*/
	At45db_nrst = of_get_named_gpio(np, "At45db_nrst", 0);

	ret = gpio_request(At45db_nrst, "At45db_nrst");
	if (ret)
		return -ENODEV;
	gpio_direction_output(At45db_nrst, GPIO_LOW);
	gpio_direction_output(At45db_nrst, GPIO_HIGH);

	/*Turn off write protect*/
	At45db_nwp = of_get_named_gpio(np, "At45db_nwp", 0);
	ret = gpio_request(At45db_nwp, "At45db_nwp");
	if (ret)
		return -ENODEV;

	//At45db_flash_init(spidev);

	INIT_LIST_HEAD(&spidev->device_entry);

	/* If we can allocate a minor number, hook up this device.
	 * Reusing minors is fine so long as udev or mdev is working.
	 */
	mutex_lock(&device_list_lock);
	minor = find_first_zero_bit(minors, N_SPI_MINORS);
	if (minor < N_SPI_MINORS) {
		struct device *dev;

		spidev->devt = MKDEV(SPIDEV_MAJOR, minor);
		dev = device_create(spiflash_cls, &spi->dev, spidev->devt,
				    spidev, "spidev%d.%d",
				    spi->master->bus_num, spi->chip_select);
		status = PTR_RET(dev);
	} else {
		dev_dbg(&spi->dev, "no minor number available!\n");
		status = -ENODEV;
	}
	if (status == 0) {
		set_bit(minor, minors);
		list_add(&spidev->device_entry, &device_list);
	}
	mutex_unlock(&device_list_lock);

	if (status == 0)
		spi_set_drvdata(spi, spidev);
	else
		kfree(spidev);

	return status;
}


static int flash_spi_remove(struct spi_device *spi)
{
	struct spidev_data	*spidev = spi_get_drvdata(spi);
	
	printk("%s \n", __FUNCTION__);

	/* make sure ops on existing fds can abort cleanly */
	spin_lock_irq(&spidev->spi_lock);
	spidev->spi = NULL;
	spi_set_drvdata(spi, NULL);
	spin_unlock_irq(&spidev->spi_lock);

	/* prevent new opens */
	mutex_lock(&device_list_lock);
	list_del(&spidev->device_entry);
	device_destroy(spiflash_cls, spidev->devt);
	clear_bit(MINOR(spidev->devt), minors);
	if (spidev->users == 0)
		kfree(spidev);
	mutex_unlock(&device_list_lock);

	return 0;
}

static const struct of_device_id flash_spi_match[] = {
		{.compatible = "atmel,at45db041d"},
		{},
};

MODULE_DEVICE_TABLE(of, flash_spi_match);

static struct spi_driver flash_spi_driver = {
	.driver = {		
		.name =		"flashspi",//spidev		
		.owner =	THIS_MODULE,		
		.of_match_table = of_match_ptr(flash_spi_match),	
	},	
	.probe =	flash_spi_probe,	
	.remove =	flash_spi_remove,
};

static struct file_operations flashspi_ops = {
	.owner   = THIS_MODULE,
	.read    = flashspi_read,
	.write   = flashspi_write,
	.open    = flashspi_open,
	.release = flashspi_release,
};

static int __init flash_spi_init(void)
{
	/*system will auto allocate major*/
	 register_chrdev(SPIDEV_MAJOR, "spiflash", &flashspi_ops);

	/*create class*/
	spiflash_cls = class_create(THIS_MODULE, "spiflash");
	
	spi_register_driver(&flash_spi_driver);
}

module_init(flash_spi_init);

static void __exit flash_spi_exit(void)
{
	return ;
}

module_exit(flash_spi_exit);
MODULE_LICENSE("GPL");
