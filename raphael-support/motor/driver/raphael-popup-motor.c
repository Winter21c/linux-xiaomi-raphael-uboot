// SPDX-License-Identifier: GPL-2.0
/*
 * Pop-up camera stepper motor for the Xiaomi Redmi K20 Pro (raphael).
 *
 * The mechanism is a TI DRV8846 stepper driver:
 *   STEP   <- qcom LPG PWM channel (pm8150l pwm-lpg@bc00, channel 1)
 *   DIR    <- tlmm 9
 *   M0/M1  <- tlmm 21 / tlmm 33   (microstep mode)
 *   nSLEEP <- tlmm 35
 *   PWREN  <- tlmm 130
 *
 * The vendor moves the carriage with 720 ms of 19.2 kHz steps after a 50 ms
 * ramp-up at 1.6 kHz, and retracts with the same profile in the other
 * direction.  Timing parameters come from the stock device tree
 * (motor,high-pwm-period-ns = 52083, motor,high-duration-ms = 720,
 *  motor,rampup-pwm-period-ns = 625000, motor,rampup-duration-ms = 50).
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/pwm.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>


struct popup_motor {
	struct device *dev;
	struct pwm_device *pwm;
	struct gpio_desc *mode0, *mode1, *sleep, *dir, *pwren;
	u32 high_period_ns, high_duration_ms;
	u32 ramp_period_ns, ramp_duration_ms;
	struct mutex lock;
	int position;		/* 1 = extended, 0 = retracted */
};

static void motor_stop(struct popup_motor *m)
{
	pwm_disable(m->pwm);
	gpiod_set_value_cansleep(m->sleep, 0);
	gpiod_set_value_cansleep(m->pwren, 0);
}

static int motor_move(struct popup_motor *m, int extend)
{
	struct pwm_state st;
	int ret;

	if (m->position == extend && extend >= 0)
		return 0;

	mutex_lock(&m->lock);

	gpiod_set_value_cansleep(m->pwren, 1);
	udelay(1000);
	gpiod_set_value_cansleep(m->sleep, 1);
	gpiod_set_value_cansleep(m->mode0, 1);
	gpiod_set_value_cansleep(m->mode1, 0);
	gpiod_set_value_cansleep(m->dir, extend ? 1 : 0);
	udelay(1000);

	/* ramp up: slow steps */
	pwm_init_state(m->pwm, &st);
	st.period = m->ramp_period_ns;
	st.duty_cycle = m->ramp_period_ns / 2;
	st.enabled = true;
	ret = pwm_apply_state(m->pwm, &st);
	if (ret)
		goto out;
	msleep(m->ramp_duration_ms);

	/* cruise */
	st.period = m->high_period_ns;
	st.duty_cycle = m->high_period_ns / 2;
	st.enabled = true;
	ret = pwm_apply_state(m->pwm, &st);
	if (ret)
		goto out;
	msleep(m->high_duration_ms);

	/* ramp down */
	st.period = m->ramp_period_ns;
	st.duty_cycle = m->ramp_period_ns / 2;
	pwm_apply_state(m->pwm, &st);
	msleep(m->ramp_duration_ms);

	m->position = extend;
	dev_info(m->dev, "moved to %s\n", extend ? "extended" : "retracted");
out:
	motor_stop(m);
	mutex_unlock(&m->lock);
	return ret;
}

static ssize_t position_show(struct device *dev, struct device_attribute *a, char *buf)
{
	struct popup_motor *m = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", m->position ? "extended" : "retracted");
}

static ssize_t position_store(struct device *dev, struct device_attribute *a,
			      const char *buf, size_t count)
{
	struct popup_motor *m = dev_get_drvdata(dev);
	int val, ret;

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;
	ret = motor_move(m, !!val);
	return ret ? ret : count;
}
static DEVICE_ATTR_RW(position);

static int popup_motor_probe(struct platform_device *pdev)
{
	struct popup_motor *m;
	int ret;

	m = devm_kzalloc(&pdev->dev, sizeof(*m), GFP_KERNEL);
	if (!m)
		return -ENOMEM;
	m->dev = &pdev->dev;
	mutex_init(&m->lock);

	m->pwm = devm_pwm_get(&pdev->dev, NULL);
	if (IS_ERR(m->pwm))
		return dev_err_probe(&pdev->dev, PTR_ERR(m->pwm), "no pwm\n");

	m->mode0 = devm_gpiod_get(&pdev->dev, "mode0", GPIOD_OUT_LOW);
	m->mode1 = devm_gpiod_get(&pdev->dev, "mode1", GPIOD_OUT_LOW);
	m->sleep = devm_gpiod_get(&pdev->dev, "sleep", GPIOD_OUT_LOW);
	m->dir   = devm_gpiod_get(&pdev->dev, "dir", GPIOD_OUT_LOW);
	m->pwren = devm_gpiod_get(&pdev->dev, "pwren", GPIOD_OUT_LOW);
	if (IS_ERR(m->mode0) || IS_ERR(m->mode1) || IS_ERR(m->sleep) ||
	    IS_ERR(m->dir) || IS_ERR(m->pwren))
		return dev_err_probe(&pdev->dev, -EPROBE_DEFER, "gpios\n");

	if (device_property_read_u32(&pdev->dev, "high-period-ns", &m->high_period_ns))
		m->high_period_ns = 52083;
	if (device_property_read_u32(&pdev->dev, "high-duration-ms", &m->high_duration_ms))
		m->high_duration_ms = 720;
	if (device_property_read_u32(&pdev->dev, "ramp-period-ns", &m->ramp_period_ns))
		m->ramp_period_ns = 625000;
	if (device_property_read_u32(&pdev->dev, "ramp-duration-ms", &m->ramp_duration_ms))
		m->ramp_duration_ms = 50;

	platform_set_drvdata(pdev, m);
	ret = device_create_file(&pdev->dev, &dev_attr_position);
	if (ret)
		return ret;

	dev_info(&pdev->dev, "popup motor ready (%u ns cruise, %u ms)\n",
		 m->high_period_ns, m->high_duration_ms);
	return 0;
}

static const struct of_device_id popup_motor_of_match[] = {
	{ .compatible = "xiaomi,raphael-popup-motor" },
	{ }
};
MODULE_DEVICE_TABLE(of, popup_motor_of_match);

static struct platform_driver popup_motor_driver = {
	.probe = popup_motor_probe,
	.driver = {
		.name = "raphael-popup-motor",
		.of_match_table = popup_motor_of_match,
	},
};
module_platform_driver(popup_motor_driver);
MODULE_LICENSE("GPL");
