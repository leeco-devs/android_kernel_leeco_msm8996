/*
 * TI LM3697 Backlight Driver
 *
 * Copyright 2014 Texas Instruments
 *
 * Author: Milo Kim <milo.kim@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_data/lm3697_bl.h>
#include <linux/regmap.h>
#include <linux/slab.h>


/* Registers */
#define LM3697_REG_OUTPUT_CFG		0x10

#define LM3697_REG_BOOST_CTRL		0x16

#define LM3697_REG_IMAX_A		0x17
#define LM3697_REG_IMAX_B		0x18

#define LM3697_REG_BRIGHTNESS_CTRL	0x1A

#define LM3697_REG_PWM_CFG		0x1C

#define LM3697_REG_BRT_A_LSB		0x20
#define LM3697_REG_BRT_A_MSB		0x21
#define LM3697_REG_BRT_B_LSB		0x22
#define LM3697_REG_BRT_B_MSB		0x23
#define LM3697_BRT_LSB_MASK		(BIT(0) | BIT(1) | BIT(2))
#define LM3697_BRT_MSB_SHIFT		3

#define LM3697_REG_ENABLE		0x24

/* Other definitions */
#define LM3697_MAX_REGISTERS		0xB4
#define LM3697_MAX_STRINGS		3
#define LM3697_MAX_BRIGHTNESS		2047
#define LM3697_IMAX_OFFSET		6

/*
 * struct lm3697_bl_chip
 * @dev: Parent device structure
 * @regmap: Used for I2C register access
 * @pdata: LM3697 platform data
 */
struct lm3697_bl_chip {
	struct device *dev;
	struct regmap *regmap;
	struct lm3697_platform_data *pdata;
};

/*
 * struct lm3697_bl
 * @bank_id: Control bank ID. BANK A or BANK A and B
 * @chip: LM3697 backlight chip structure for low level access
 * @bl_pdata: LM3697 backlight platform data
 */
struct lm3697_bl {
	int bank_id;
	struct lm3697_bl_chip *chip;
	struct lm3697_backlight_platform_data *bl_pdata;
};

/*
 * The display driver owns the brightness path and calls lm3697_bl_set() for
 * every backlight level, so no backlight device is registered here. The whole
 * backlight class half of the vendor driver is dead code and stays out.
 */
static struct lm3697_bl *lm3697_backlight;

static int lm3697_bl_write_byte(struct lm3697_bl_chip *chip, u8 reg, u8 data)
{
	return regmap_write(chip->regmap, reg, data);
}

static int lm3697_bl_update_bits(struct lm3697_bl_chip *chip, u8 reg, u8 mask,
				 u8 data)
{
	return regmap_update_bits(chip->regmap, reg, mask, data);
}

static int lm3697_bl_enable(struct lm3697_bl *lm3697_bl, int enable)
{
	return lm3697_bl_update_bits(lm3697_bl->chip, LM3697_REG_ENABLE,
				     BIT(lm3697_bl->bank_id),
				     enable << lm3697_bl->bank_id);
}

static int lm3697_bl_set_brightness(struct lm3697_bl *lm3697_bl, int brightness)
{
	int ret;
	u8 data;
	u8 reg_lsb[] = { LM3697_REG_BRT_A_LSB, LM3697_REG_BRT_B_LSB, };
	u8 reg_msb[] = { LM3697_REG_BRT_A_MSB, LM3697_REG_BRT_B_MSB, };

	if (brightness > LM3697_MAX_BRIGHTNESS)
		brightness = LM3697_MAX_BRIGHTNESS;

	/* Two registers(LSB and MSB) are updated for 11-bit dimming */

	data = brightness & LM3697_BRT_LSB_MASK;
	ret = lm3697_bl_update_bits(lm3697_bl->chip,
				    reg_lsb[lm3697_bl->bank_id],
				    LM3697_BRT_LSB_MASK, data);
	if (ret)
		return ret;

	data = (brightness >> LM3697_BRT_MSB_SHIFT) & 0xFF;

	return lm3697_bl_write_byte(lm3697_bl->chip,
				    reg_msb[lm3697_bl->bank_id], data);
}

int lm3697_bl_set(int bl_level)
{
	struct lm3697_bl *lm3697_bl = lm3697_backlight;
	int ret;

	/*
	 * Probe may not have run yet, or the chip may be absent from the board
	 * file, so never dereference a missing backlight here.
	 */
	if (!lm3697_bl)
		return -ENODEV;

	ret = lm3697_bl_enable(lm3697_bl, bl_level > 0);
	if (ret)
		return ret;

	/*
	 * Brightness register should always be written
	 * not only register based mode but also in PWM mode.
	 */
	return lm3697_bl_set_brightness(lm3697_bl, bl_level);
}

static int lm3697_bl_set_ctrl_mode(struct lm3697_bl *lm3697_bl)
{
	struct lm3697_backlight_platform_data *pdata = lm3697_bl->bl_pdata;
	int bank_id = lm3697_bl->bank_id;

	/*
	 * A pwm-period in DT selects PWM brightness control. The PWM line is
	 * driven outside this driver, so only the bank's PWM bit is set here.
	 */
	if (!pdata->pwm_period)
		return 0;

	return lm3697_bl_update_bits(lm3697_bl->chip, LM3697_REG_PWM_CFG,
				     BIT(bank_id), 1 << bank_id);
}

static int lm3697_bl_string_configure(struct lm3697_bl *lm3697_bl)
{
	int bank_id = lm3697_bl->bank_id;
	int is_detected = 0;
	int i, ret;

	/* Control bank assignment for backlight string configuration */
	for (i = 0; i < LM3697_MAX_STRINGS; i++) {
		if (test_bit(i, &lm3697_bl->bl_pdata->bl_string)) {
			ret = lm3697_bl_update_bits(lm3697_bl->chip,
						    LM3697_REG_OUTPUT_CFG,
						    BIT(i), bank_id << i);
			if (ret)
				return ret;

			is_detected = 1;
		}
	}

	if (!is_detected) {
		dev_err(lm3697_bl->chip->dev, "No backlight string found\n");
		return -EINVAL;
	}

	return 0;
}

static int lm3697_bl_set_current(struct lm3697_bl *lm3697_bl)
{
	u8 reg[] = { LM3697_REG_IMAX_A, LM3697_REG_IMAX_B, };

	return lm3697_bl_write_byte(lm3697_bl->chip, reg[lm3697_bl->bank_id],
				    lm3697_bl->bl_pdata->imax);
}

static int lm3697_bl_configure(struct lm3697_bl *lm3697_bl)
{
	int ret;

	ret = lm3697_bl_set_ctrl_mode(lm3697_bl);
	if (ret)
		return ret;

	ret = lm3697_bl_string_configure(lm3697_bl);
	if (ret)
		return ret;

	return lm3697_bl_set_current(lm3697_bl);
}

static enum lm3697_max_current lm3697_get_current_code(u8 imax_mA)
{
	const enum lm3697_max_current imax_table[] = {
		LM3697_IMAX_6mA,  LM3697_IMAX_7mA,  LM3697_IMAX_8mA,
		LM3697_IMAX_9mA,  LM3697_IMAX_10mA, LM3697_IMAX_11mA,
		LM3697_IMAX_12mA, LM3697_IMAX_13mA, LM3697_IMAX_14mA,
		LM3697_IMAX_15mA, LM3697_IMAX_16mA, LM3697_IMAX_17mA,
		LM3697_IMAX_18mA, LM3697_IMAX_19mA, LM3697_IMAX_20mA,
		LM3697_IMAX_21mA, LM3697_IMAX_22mA, LM3697_IMAX_23mA,
		LM3697_IMAX_24mA, LM3697_IMAX_25mA, LM3697_IMAX_26mA,
		LM3697_IMAX_27mA, LM3697_IMAX_28mA, LM3697_IMAX_29mA,
	};

	/*
	 * Convert milliampere to appropriate enum code value.
	 * Input range : 5 ~ 30mA
	 */

	if (imax_mA <= 5)
		return LM3697_IMAX_5mA;

	if (imax_mA >= 30)
		return LM3697_IMAX_30mA;

	return imax_table[imax_mA - LM3697_IMAX_OFFSET];
}

static int lm3697_bl_parse_dt(struct device *dev, struct lm3697_bl_chip *chip)
{
	struct lm3697_platform_data *pdata;
	struct lm3697_backlight_platform_data *bl_pdata;
	struct device_node *node = dev->of_node;
	struct device_node *child;
	int num_backlights;
	int i = 0;
	unsigned int imax_mA;

	if (!node) {
		dev_err(dev, "No device node exists\n");
		return -ENODEV;
	}

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	pdata->en_gpio = of_get_named_gpio(node, "ti,enable-gpio", 0);
	if (pdata->en_gpio < 0)
		return pdata->en_gpio;

	num_backlights = of_get_child_count(node);
	if (num_backlights == 0) {
		dev_err(dev, "No backlight strings\n");
		return -EINVAL;
	}

	bl_pdata = devm_kzalloc(dev, sizeof(*bl_pdata) * num_backlights,
				GFP_KERNEL);
	if (!bl_pdata)
		return -ENOMEM;

	for_each_child_of_node(node, child) {
		of_property_read_string(child, "backlight-name",
					&bl_pdata[i].name);

		/* Make backlight strings */
		bl_pdata[i].bl_string = 0;
		if (of_property_read_bool(child, "hvled1-used"))
			bl_pdata[i].bl_string |= LM3697_HVLED1;
		if (of_property_read_bool(child, "hvled2-used"))
			bl_pdata[i].bl_string |= LM3697_HVLED2;
		if (of_property_read_bool(child, "hvled3-used"))
			bl_pdata[i].bl_string |= LM3697_HVLED3;

		imax_mA = 0;
		of_property_read_u32(child, "max-current-milliamp", &imax_mA);
		bl_pdata[i].imax = lm3697_get_current_code(imax_mA);

		of_property_read_u32(child, "initial-brightness",
				     &bl_pdata[i].init_brightness);

		/* PWM mode */
		of_property_read_u32(child, "pwm-period",
				     &bl_pdata[i].pwm_period);

		i++;
	}

	pdata->bl_pdata = bl_pdata;
	pdata->num_backlights = num_backlights;
	chip->pdata = pdata;

	return 0;
}

static int lm3697_bl_enable_hw(struct lm3697_bl_chip *chip)
{
	return devm_gpio_request_one(chip->dev, chip->pdata->en_gpio,
				     GPIOF_OUT_INIT_HIGH, "lm3697_hwen");
}

static struct lm3697_bl *lm3697_bl_register(struct lm3697_bl_chip *chip)
{
	struct lm3697_backlight_platform_data *pdata = chip->pdata->bl_pdata;
	struct lm3697_bl *lm3697_bl, *each;
	int num_backlights = chip->pdata->num_backlights;
	int i, ret;

	lm3697_bl = devm_kzalloc(chip->dev, sizeof(*lm3697_bl) * num_backlights,
				 GFP_KERNEL);
	if (!lm3697_bl)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < num_backlights; i++) {
		each = lm3697_bl + i;
		each->bank_id = i;
		each->chip = chip;
		each->bl_pdata = pdata + i;

		ret = lm3697_bl_configure(each);
		if (ret) {
			dev_err(chip->dev, "Backlight config err: %d\n", ret);
			return ERR_PTR(ret);
		}
	}

	return lm3697_bl;
}

static struct regmap_config lm3697_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = LM3697_MAX_REGISTERS,
};

static int lm3697_bl_probe(struct i2c_client *cl,
			   const struct i2c_device_id *id)
{
	struct device *dev = &cl->dev;
	struct lm3697_platform_data *pdata = dev_get_platdata(dev);
	struct lm3697_bl_chip *chip;
	struct lm3697_bl *lm3697_bl;
	int ret;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = dev;
	chip->pdata = pdata;

	chip->regmap = devm_regmap_init_i2c(cl, &lm3697_regmap_config);
	if (IS_ERR(chip->regmap))
		return PTR_ERR(chip->regmap);

	if (!chip->pdata) {
		if (IS_ENABLED(CONFIG_OF))
			ret = lm3697_bl_parse_dt(dev, chip);
		else
			return -ENODEV;

		if (ret)
			return ret;
	}

	/* Vendor chip setup, written before the HWEN line is raised. */
	ret = lm3697_bl_write_byte(chip, LM3697_REG_BRIGHTNESS_CTRL, 0x06);
	if (ret)
		return ret;

	ret = lm3697_bl_write_byte(chip, LM3697_REG_BOOST_CTRL, 0x01);
	if (ret)
		return ret;

	ret = lm3697_bl_enable_hw(chip);
	if (ret)
		return ret;

	lm3697_bl = lm3697_bl_register(chip);
	if (IS_ERR(lm3697_bl))
		return PTR_ERR(lm3697_bl);

	i2c_set_clientdata(cl, lm3697_bl);

	lm3697_backlight = lm3697_bl;

	return 0;
}

static int lm3697_bl_remove(struct i2c_client *cl)
{
	struct lm3697_bl *lm3697_bl = i2c_get_clientdata(cl);

	if (lm3697_backlight == lm3697_bl)
		lm3697_backlight = NULL;

	gpio_set_value(lm3697_bl->chip->pdata->en_gpio, 0);

	return 0;
}

static const struct i2c_device_id lm3697_bl_ids[] = {
	{ "lm3697", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm3697_bl_ids);

#ifdef CONFIG_OF
static const struct of_device_id lm3697_bl_of_match[] = {
	{ .compatible = "ti,lm3697", },
	{ }
};
MODULE_DEVICE_TABLE(of, lm3697_bl_of_match);
#endif

static struct i2c_driver lm3697_bl_driver = {
	.probe = lm3697_bl_probe,
	.remove = lm3697_bl_remove,
	.driver = {
		.name = "lm3697",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(lm3697_bl_of_match),
	},
	.id_table = lm3697_bl_ids,
};

module_i2c_driver(lm3697_bl_driver);

MODULE_DESCRIPTION("TI LM3697 Backlight Driver");
MODULE_AUTHOR("Milo Kim");
MODULE_LICENSE("GPL");
