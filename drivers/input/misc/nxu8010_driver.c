#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>

#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/of.h>

#ifdef LYCONFIG_EXCHANGE_BACK_FRONT_CAMERA_SUPPORT
#define LENS_I2C_BUSNUM 3
#else
#define LENS_I2C_BUSNUM 9
#endif

typedef struct {
	int16_t r_data;
	int16_t g_data;
	int16_t b_data;
	int16_t w_data;
} GET_NEXU8010_DATA;

struct nxu8010_data {
	struct miscdevice miscdev;
	struct mutex work_mutex;
};

static const unsigned char NXU8010_RGBW_INIT_DATA[] = {
	0x01, 0x08,
	0x00, 0x02,
	0x02, 0x00,
	0x03, 0x70,
	0x04, 0x28,
	0x05, 0x00,
	0x06, 0x40,
	0x07, 0x0A,
	0x30, 0x0C,
	0x31, 0x00,
	0x32, 0x00,
	0x33, 0x00,
	0x08, 0x08,
	0x09, 0x18,
	0x0A, 0x00,
	0x0B, 0x00,
	0x0C, 0x00,
	0x0D, 0x00,
	0x0E, 0x08,
	0x0F, 0x18,
	0x10, 0x00,
	0x11, 0x00,
	0x12, 0x00,
	0x13, 0x00,
	0x14, 0x08,
	0x15, 0x18,
	0x16, 0x00,
	0x17, 0x00,
	0x18, 0x00,
	0x19, 0x00,
	0x1A, 0x08,
	0x1B, 0x18,
	0x1C, 0x00,
	0x1D, 0x00,
	0x1E, 0x00,
	0x1F, 0x00,
	0x35, 0x15,
	0x50, 0x01,
	0x51, 0x23,
	0x52, 0x45,
	0x53, 0x67,
	0x54, 0x89,
	0x55, 0xAB,
	0x56, 0xCD,
	0x57, 0xEF,
	0x41, 0x00,
	0x40, 0x01,

	0x01, 0x04, /* reset */
	0x34, 0x00, /* gain control begin */
	0x00, 0x22,
	0x00, 0x02,
	0x07, 0x0A,
	0x08, 0x08,
	0x0E, 0x08,
	0x14, 0x08,
	0x1A, 0x08, /* gain control end */

	0x34, 0x03
};

#define R_LOW_DATA		0x20
#define R_HIGH_DATA		0x21
#define G_LOW_DATA		0x22
#define G_HIGH_DATA		0x23
#define B_LOW_DATA		0x24
#define B_HIGH_DATA		0x25
#define W_LOW_DATA		0x26
#define W_HIGH_DATA		0x27
#define CORRELATION_CODE	0x2A
#define MODULE_ID		0x35
#define SLEEP_ADDR		0x00
#define SLEEP_VALUE		0x22

#define NXU8010_DRVNAME		"nxu8010"
#define NXU8010_SLAVE_ADDR	0x44

#define NXU8010_DEBUG
#ifdef NXU8010_DEBUG
#define NXU8010DB(fmt, args...) pr_err(fmt, ##args)
#else
#define NXU8010DB(x, ...)
#endif

extern struct platform_driver g_nxu8010_driver;

static struct i2c_board_info __initdata nxu8010_board_info = {
	I2C_BOARD_INFO("nxu8010", NXU8010_SLAVE_ADDR)
};
static struct mutex nxu8010_mutex;

static int nxu8010_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id);
static int nxu8010_i2c_remove(struct i2c_client *client);

static const struct i2c_device_id nxu8010_i2c_id[] = {
	{NXU8010_DRVNAME, 0},
	{"", 0}
};
MODULE_DEVICE_TABLE(i2c, nxu8010_i2c_id);

#ifdef CONFIG_OF
static const struct of_device_id nxu8010_of_match[] = {
	{ .compatible = "nxu,nxu8010" },
	{ .compatible = "letv,nxu8010" },
	{ .compatible = "nxu8010" },
	{},
};
MODULE_DEVICE_TABLE(of, nxu8010_of_match);
#endif

static struct i2c_client *g_nxu8010_I2Cclient;

static int nxu8010_read(u8 addr, u8 *value)
{
	u8 pBuff[2];
	int ret = 0;

	if (!g_nxu8010_I2Cclient)
		return -ENODEV;

	mutex_lock(&nxu8010_mutex);
	g_nxu8010_I2Cclient->addr = NXU8010_SLAVE_ADDR;

	pBuff[0] = addr;
	ret = i2c_master_send(g_nxu8010_I2Cclient, pBuff, 1);
	if (ret < 0) {
		NXU8010DB("[nxu8010] i2c send failed!!\n");
		mutex_unlock(&nxu8010_mutex);
		return -1;
	}

	ret = i2c_master_recv(g_nxu8010_I2Cclient, &pBuff[1], 1);
	if (ret < 0) {
		mutex_unlock(&nxu8010_mutex);
		NXU8010DB("[nxu8010] i2c recv failed!!\n");
		return -1;
	}

	*value = pBuff[1];
	mutex_unlock(&nxu8010_mutex);

	return 0;
}

static int nxu8010_write(u8 addr, u8 value)
{
	int ret = 0;
	u8 pBuff[2] = {0};

	if (!g_nxu8010_I2Cclient)
		return -ENODEV;

	mutex_lock(&nxu8010_mutex);
	g_nxu8010_I2Cclient->addr = NXU8010_SLAVE_ADDR;

	pBuff[0] = addr;
	pBuff[1] = value;

	ret = i2c_master_send(g_nxu8010_I2Cclient, pBuff, 2);
	if (ret < 0) {
		mutex_unlock(&nxu8010_mutex);
		NXU8010DB("[nxu8010] i2c send failed!!\n");
		return -1;
	}
	mutex_unlock(&nxu8010_mutex);

	return 0;
}

static void nxu8010_init(void)
{
	int i;
	int count = sizeof(NXU8010_RGBW_INIT_DATA) / sizeof(u8) / 2;
	u8 reg_addr = 0, reg_val = 0;

	for (i = 0; i < count; ++i) {
		reg_addr = NXU8010_RGBW_INIT_DATA[i * 2];
		reg_val = NXU8010_RGBW_INIT_DATA[i * 2 + 1];

		if (nxu8010_write(reg_addr, reg_val)) {
			pr_err("[nxu8010] Error RGBW init...\n");
			return;
		}
	}
}

static ssize_t store_reg(struct device_driver *drv, const char *buf, size_t count)
{
	int reg_addr = 0x0;
	int reg_val = 0x0;

	NXU8010DB("[nxu8010] buf=%s\n", buf);
	if (sscanf(buf, "0x%x 0x%x", &reg_addr, &reg_val) == 2)
		nxu8010_write((u8)reg_addr, (u8)reg_val);

	return count;
}

static ssize_t show_reg(struct device_driver *drv, char *buf)
{
	NXU8010DB("[nxu8010] show_reg\n");
	nxu8010_init();

	return 0;
}

static ssize_t show_values(struct device_driver *drv, char *buf)
{
	u8 read_buf[2] = {0,};
	int len = 0;
	int values[4] = {0,};

	nxu8010_read(R_LOW_DATA, &read_buf[0]);
	nxu8010_read(R_HIGH_DATA, &read_buf[1]);
	values[0] = read_buf[0] | (read_buf[1] << 8);

	nxu8010_read(G_LOW_DATA, &read_buf[0]);
	nxu8010_read(G_HIGH_DATA, &read_buf[1]);
	values[1] = read_buf[0] | (read_buf[1] << 8);

	nxu8010_read(B_LOW_DATA, &read_buf[0]);
	nxu8010_read(B_HIGH_DATA, &read_buf[1]);
	values[2] = read_buf[0] | (read_buf[1] << 8);

	nxu8010_read(W_LOW_DATA, &read_buf[0]);
	nxu8010_read(W_HIGH_DATA, &read_buf[1]);
	values[3] = read_buf[0] | (read_buf[1] << 8);

	NXU8010DB("[nxu8010] show_values values=%d %d %d %d\n",
		  values[0], values[1], values[2], values[3]);

	len = snprintf(buf, 32, "%d %d %d %d\n",
		       values[0], values[1], values[2], values[3]);

	return len;
}

static ssize_t show_code(struct device_driver *drv, char *buf)
{
	u8 read_buf = 0;
	int len = 0;

	nxu8010_read(CORRELATION_CODE, &read_buf);
	NXU8010DB("[nxu8010] show_code 0x%02x\n", (int)read_buf);
	len = snprintf(buf, 16, "0x%02x\n", (int)read_buf);

	return len;
}

static ssize_t show_modelid(struct device_driver *drv, char *buf)
{
	u8 read_buf = 0;
	int len = 0;

	nxu8010_read(MODULE_ID, &read_buf);
	NXU8010DB("[nxu8010] show_modelid 0x%02x\n", (int)read_buf);
	len = snprintf(buf, 16, "0x%02x\n", (int)read_buf);

	return len;
}

static DRIVER_ATTR(reg, S_IWUSR | S_IRUGO, show_reg, store_reg);
static DRIVER_ATTR(values, S_IRUGO, show_values, NULL);
static DRIVER_ATTR(code, S_IRUGO, show_code, NULL);
static DRIVER_ATTR(modelid, S_IRUGO, show_modelid, NULL);

#define NXU8010_IOCTL_INIT		_IO('n', 0x01)
#define NXU8010_IOCTL_G_DATA		_IOR('n', 0x02, unsigned char)
#define NXU8010_IOCTL_G_CHIPID		_IOR('n', 0x03, GET_NEXU8010_DATA)

static unsigned int nxu8010_read_chipid(void)
{
	u8 read_buf = 0;

	nxu8010_read(CORRELATION_CODE, &read_buf);
	NXU8010DB("[nxu8010] get chipid 0x%02x\n", (int)read_buf);

	return (unsigned int)read_buf;
}

static void nxu8010_read_data(GET_NEXU8010_DATA *get_data)
{
	u8 read_buf[2] = {0,};
	int values[4] = {0,};

	nxu8010_read(R_LOW_DATA, &read_buf[0]);
	nxu8010_read(R_HIGH_DATA, &read_buf[1]);
	values[0] = read_buf[0] | (read_buf[1] << 8);

	nxu8010_read(G_LOW_DATA, &read_buf[0]);
	nxu8010_read(G_HIGH_DATA, &read_buf[1]);
	values[1] = read_buf[0] | (read_buf[1] << 8);

	nxu8010_read(B_LOW_DATA, &read_buf[0]);
	nxu8010_read(B_HIGH_DATA, &read_buf[1]);
	values[2] = read_buf[0] | (read_buf[1] << 8);

	nxu8010_read(W_LOW_DATA, &read_buf[0]);
	nxu8010_read(W_HIGH_DATA, &read_buf[1]);
	values[3] = read_buf[0] | (read_buf[1] << 8);

	NXU8010DB("[nxu8010] values= R:%d G: %d B: %d W:%d\n",
		  values[0], values[1], values[2], values[3]);

	get_data->r_data = values[0];
	get_data->g_data = values[1];
	get_data->b_data = values[2];
	get_data->w_data = values[3];
}

static int nxu8010_ioctl_handler(struct file *file,
				 unsigned int cmd, unsigned long arg,
				 void __user *p)
{
	unsigned char chip_id;
	GET_NEXU8010_DATA get_data;

	switch (cmd) {
	case NXU8010_IOCTL_INIT:
		NXU8010DB("NXU8010_IOCTL_INIT\n");
		nxu8010_init();
		break;

	case NXU8010_IOCTL_G_DATA:
		NXU8010DB("NXU8010_IOCTL_G_DATA\n");
		nxu8010_read_data(&get_data);
		NXU8010DB("R: %d, G: %d, B: %d W: %d\n",
			  get_data.r_data, get_data.g_data,
			  get_data.b_data, get_data.w_data);
		if (copy_to_user((GET_NEXU8010_DATA __user *)p, &get_data,
				 sizeof(GET_NEXU8010_DATA)))
			return -EFAULT;
		break;

	case NXU8010_IOCTL_G_CHIPID:
		NXU8010DB("NXU8010_IOCTL_G_CHIPID\n");
		chip_id = (unsigned char)nxu8010_read_chipid();
		NXU8010DB("NXU8010 chip_id:%d\n", chip_id);
		if (copy_to_user((unsigned char __user *)p, &chip_id,
				 sizeof(unsigned char)))
			return -EFAULT;
		break;

	default:
		break;
	}

	return 0;
}

static long nxu8010_ioctl(struct file *file,
			  unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct nxu8010_data *data =
		container_of(file->private_data,
			     struct nxu8010_data, miscdev);

	if (data)
		mutex_lock(&data->work_mutex);
	else
		pr_err("nxu8010_ioctl NULL\n");

	ret = nxu8010_ioctl_handler(file, cmd, arg, (void __user *)arg);

	if (data)
		mutex_unlock(&data->work_mutex);
	else
		pr_err("nxu8010_ioctl NULL\n");

	return ret;
}

static int nxu8010_open(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations nxu8010_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= nxu8010_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= nxu8010_ioctl,
#endif
	.open		= nxu8010_open,
};

static int nxu8010_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret;
	struct nxu8010_data *nxu_data = NULL;

	NXU8010DB("[nxu8010] nxu8010_i2c_probe\n");

	g_nxu8010_I2Cclient = client;

	mutex_init(&nxu8010_mutex);

	ret = driver_create_file(&(g_nxu8010_driver.driver), &driver_attr_reg);
	ret |= driver_create_file(&(g_nxu8010_driver.driver), &driver_attr_values);
	ret |= driver_create_file(&(g_nxu8010_driver.driver), &driver_attr_code);
	ret |= driver_create_file(&(g_nxu8010_driver.driver), &driver_attr_modelid);
	if (ret)
		pr_err("[nxu8010] failed to create driver sysfs files\n");

	nxu_data = devm_kzalloc(&client->dev, sizeof(struct nxu8010_data), GFP_KERNEL);
	if (!nxu_data)
		return -ENOMEM;

	mutex_init(&nxu_data->work_mutex);
	nxu_data->miscdev.minor = MISC_DYNAMIC_MINOR;
	nxu_data->miscdev.name = "nxu8010_dev";
	nxu_data->miscdev.fops = &nxu8010_fops;

	if (misc_register(&nxu_data->miscdev) != 0)
		pr_err("cannot register misc devices\n");

	i2c_set_clientdata(client, nxu_data);

	NXU8010DB("[nxu8010] Attached!!\n");

	return 0;
}

static int nxu8010_i2c_remove(struct i2c_client *client)
{
	struct nxu8010_data *nxu_data = i2c_get_clientdata(client);

	driver_remove_file(&(g_nxu8010_driver.driver), &driver_attr_reg);
	driver_remove_file(&(g_nxu8010_driver.driver), &driver_attr_values);
	driver_remove_file(&(g_nxu8010_driver.driver), &driver_attr_code);
	driver_remove_file(&(g_nxu8010_driver.driver), &driver_attr_modelid);

	if (nxu_data)
		misc_deregister(&nxu_data->miscdev);

	return 0;
}

struct i2c_driver nxu8010_i2c_driver = {
	.probe = nxu8010_i2c_probe,
	.remove = nxu8010_i2c_remove,
	.driver = {
		.name = NXU8010_DRVNAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(nxu8010_of_match),
	},
	.id_table = nxu8010_i2c_id,
};

static int nxu8010_probe(struct platform_device *pdev)
{
	int rc;

	NXU8010DB("nxu8010_probe\n");
	rc = i2c_add_driver(&nxu8010_i2c_driver);
	if (rc)
		pr_err("%d erro ret:%d\n", __LINE__, rc);
	return 0;
}

static int nxu8010_remove(struct platform_device *pdev)
{
	i2c_del_driver(&nxu8010_i2c_driver);
	return 0;
}

static int nxu8010_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	return 0;
}

static int nxu8010_resume(struct platform_device *pdev)
{
	return 0;
}

struct platform_driver g_nxu8010_driver = {
	.probe = nxu8010_probe,
	.remove = nxu8010_remove,
	.suspend = nxu8010_suspend,
	.resume = nxu8010_resume,
	.driver = {
		.name = "nxu8010",
		.owner = THIS_MODULE,