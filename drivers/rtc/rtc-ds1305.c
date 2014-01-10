/*
 * rtc-ds1305.c -- driver for DS1305 and DS1306 SPI RTC chips
 *
 * Copyright (C) 2008 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/bcd.h>
#include <linux/slab.h>
#include <linux/rtc.h>
#include <linux/workqueue.h>

#include <linux/spi/spi.h>
#include <linux/spi/ds1305.h>
#include <linux/module.h>


#define DS1305_WRITE		0x80


#define DS1305_RTC_LEN		7		

#define DS1305_SEC		0x00		
#define DS1305_MIN		0x01
#define DS1305_HOUR		0x02
#	define DS1305_HR_12		0x40	
#	define DS1305_HR_PM		0x20	
#define DS1305_WDAY		0x03
#define DS1305_MDAY		0x04
#define DS1305_MON		0x05
#define DS1305_YEAR		0x06


#define DS1305_ALM_LEN		4		
#define DS1305_ALM_DISABLE	0x80

#define DS1305_ALM0(r)		(0x07 + (r))	
#define DS1305_ALM1(r)		(0x0b + (r))


#define DS1305_CONTROL_LEN	3		

#define DS1305_CONTROL		0x0f		
#	define DS1305_nEOSC		0x80	
#	define DS1305_WP		0x40	
#	define DS1305_INTCN		0x04	
#	define DS1306_1HZ		0x04	
#	define DS1305_AEI1		0x02	
#	define DS1305_AEI0		0x01	
#define DS1305_STATUS		0x10
#define DS1305_TRICKLE		0x11

#define DS1305_NVRAM_LEN	96		

#define DS1305_NVRAM		0x20		


struct ds1305 {
	struct spi_device	*spi;
	struct rtc_device	*rtc;

	struct work_struct	work;

	unsigned long		flags;
#define FLAG_EXITING	0

	bool			hr12;
	u8			ctrl[DS1305_CONTROL_LEN];
};




static unsigned bcd2hour(u8 bcd)
{
	if (bcd & DS1305_HR_12) {
		unsigned	hour = 0;

		bcd &= ~DS1305_HR_12;
		if (bcd & DS1305_HR_PM) {
			hour = 12;
			bcd &= ~DS1305_HR_PM;
		}
		hour += bcd2bin(bcd);
		return hour - 1;
	}
	return bcd2bin(bcd);
}

static u8 hour2bcd(bool hr12, int hour)
{
	if (hr12) {
		hour++;
		if (hour <= 12)
			return DS1305_HR_12 | bin2bcd(hour);
		hour -= 12;
		return DS1305_HR_12 | DS1305_HR_PM | bin2bcd(hour);
	}
	return bin2bcd(hour);
}



static int ds1305_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct ds1305	*ds1305 = dev_get_drvdata(dev);
	u8		buf[2];
	long		err = -EINVAL;

	buf[0] = DS1305_WRITE | DS1305_CONTROL;
	buf[1] = ds1305->ctrl[0];

	if (enabled) {
		if (ds1305->ctrl[0] & DS1305_AEI0)
			goto done;
		buf[1] |= DS1305_AEI0;
	} else {
		if (!(buf[1] & DS1305_AEI0))
			goto done;
		buf[1] &= ~DS1305_AEI0;
	}
	err = spi_write_then_read(ds1305->spi, buf, sizeof buf, NULL, 0);
	if (err >= 0)
		ds1305->ctrl[0] = buf[1];
done:
	return err;

}



static int ds1305_get_time(struct device *dev, struct rtc_time *time)
{
	struct ds1305	*ds1305 = dev_get_drvdata(dev);
	u8		addr = DS1305_SEC;
	u8		buf[DS1305_RTC_LEN];
	int		status;

	status = spi_write_then_read(ds1305->spi, &addr, sizeof addr,
			buf, sizeof buf);
	if (status < 0)
		return status;

	dev_vdbg(dev, "%s: %02x %02x %02x, %02x %02x %02x %02x\n",
		"read", buf[0], buf[1], buf[2], buf[3],
		buf[4], buf[5], buf[6]);

	
	time->tm_sec = bcd2bin(buf[DS1305_SEC]);
	time->tm_min = bcd2bin(buf[DS1305_MIN]);
	time->tm_hour = bcd2hour(buf[DS1305_HOUR]);
	time->tm_wday = buf[DS1305_WDAY] - 1;
	time->tm_mday = bcd2bin(buf[DS1305_MDAY]);
	time->tm_mon = bcd2bin(buf[DS1305_MON]) - 1;
	time->tm_year = bcd2bin(buf[DS1305_YEAR]) + 100;

	dev_vdbg(dev, "%s secs=%d, mins=%d, "
		"hours=%d, mday=%d, mon=%d, year=%d, wday=%d\n",
		"read", time->tm_sec, time->tm_min,
		time->tm_hour, time->tm_mday,
		time->tm_mon, time->tm_year, time->tm_wday);

	
	return rtc_valid_tm(time);
}

static int ds1305_set_time(struct device *dev, struct rtc_time *time)
{
	struct ds1305	*ds1305 = dev_get_drvdata(dev);
	u8		buf[1 + DS1305_RTC_LEN];
	u8		*bp = buf;

	dev_vdbg(dev, "%s secs=%d, mins=%d, "
		"hours=%d, mday=%d, mon=%d, year=%d, wday=%d\n",
		"write", time->tm_sec, time->tm_min,
		time->tm_hour, time->tm_mday,
		time->tm_mon, time->tm_year, time->tm_wday);

	
	*bp++ = DS1305_WRITE | DS1305_SEC;

	*bp++ = bin2bcd(time->tm_sec);
	*bp++ = bin2bcd(time->tm_min);
	*bp++ = hour2bcd(ds1305->hr12, time->tm_hour);
	*bp++ = (time->tm_wday < 7) ? (time->tm_wday + 1) : 1;
	*bp++ = bin2bcd(time->tm_mday);
	*bp++ = bin2bcd(time->tm_mon + 1);
	*bp++ = bin2bcd(time->tm_year - 100);

	dev_dbg(dev, "%s: %02x %02x %02x, %02x %02x %02x %02x\n",
		"write", buf[1], buf[2], buf[3],
		buf[4], buf[5], buf[6], buf[7]);

	
	return spi_write_then_read(ds1305->spi, buf, sizeof buf,
			NULL, 0);
}


static int ds1305_get_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct ds1305	*ds1305 = dev_get_drvdata(dev);
	struct spi_device *spi = ds1305->spi;
	u8		addr;
	int		status;
	u8		buf[DS1305_ALM_LEN];

	addr = DS1305_CONTROL;
	status = spi_write_then_read(spi, &addr, sizeof addr,
			ds1305->ctrl, sizeof ds1305->ctrl);
	if (status < 0)
		return status;

	alm->enabled = !!(ds1305->ctrl[0] & DS1305_AEI0);
	alm->pending = !!(ds1305->ctrl[1] & DS1305_AEI0);

	
	addr = DS1305_ALM0(DS1305_SEC);
	status = spi_write_then_read(spi, &addr, sizeof addr,
			buf, sizeof buf);
	if (status < 0)
		return status;

	dev_vdbg(dev, "%s: %02x %02x %02x %02x\n",
		"alm0 read", buf[DS1305_SEC], buf[DS1305_MIN],
		buf[DS1305_HOUR], buf[DS1305_WDAY]);

	if ((DS1305_ALM_DISABLE & buf[DS1305_SEC])
			|| (DS1305_ALM_DISABLE & buf[DS1305_MIN])
			|| (DS1305_ALM_DISABLE & buf[DS1305_HOUR]))
		return -EIO;

	alm->time.tm_sec = bcd2bin(buf[DS1305_SEC]);
	alm->time.tm_min = bcd2bin(buf[DS1305_MIN]);
	alm->time.tm_hour = bcd2hour(buf[DS1305_HOUR]);
	alm->time.tm_mday = -1;
	alm->time.tm_mon = -1;
	alm->time.tm_year = -1;
	
	alm->time.tm_wday = -1;
	alm->time.tm_mday = -1;
	alm->time.tm_isdst = -1;

	return 0;
}

static int ds1305_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct ds1305	*ds1305 = dev_get_drvdata(dev);
	struct spi_device *spi = ds1305->spi;
	unsigned long	now, later;
	struct rtc_time	tm;
	int		status;
	u8		buf[1 + DS1305_ALM_LEN];

	
	status = rtc_tm_to_time(&alm->time, &later);
	if (status < 0)
		return status;

	
	status = ds1305_get_time(dev, &tm);
	if (status < 0)
		return status;
	status = rtc_tm_to_time(&tm, &now);
	if (status < 0)
		return status;

	
	if (later <= now)
		return -EINVAL;
	if ((later - now) > 24 * 60 * 60)
		return -EDOM;

	
	if (ds1305->ctrl[0] & DS1305_AEI0) {
		ds1305->ctrl[0] &= ~DS1305_AEI0;

		buf[0] = DS1305_WRITE | DS1305_CONTROL;
		buf[1] = ds1305->ctrl[0];
		status = spi_write_then_read(ds1305->spi, buf, 2, NULL, 0);
		if (status < 0)
			return status;
	}

	
	buf[0] = DS1305_WRITE | DS1305_ALM0(DS1305_SEC);
	buf[1 + DS1305_SEC] = bin2bcd(alm->time.tm_sec);
	buf[1 + DS1305_MIN] = bin2bcd(alm->time.tm_min);
	buf[1 + DS1305_HOUR] = hour2bcd(ds1305->hr12, alm->time.tm_hour);
	buf[1 + DS1305_WDAY] = DS1305_ALM_DISABLE;

	dev_dbg(dev, "%s: %02x %02x %02x %02x\n",
		"alm0 write", buf[1 + DS1305_SEC], buf[1 + DS1305_MIN],
		buf[1 + DS1305_HOUR], buf[1 + DS1305_WDAY]);

	status = spi_write_then_read(spi, buf, sizeof buf, NULL, 0);
	if (status < 0)
		return status;

	
	if (alm->enabled) {
		ds1305->ctrl[0] |= DS1305_AEI0;

		buf[0] = DS1305_WRITE | DS1305_CONTROL;
		buf[1] = ds1305->ctrl[0];
		status = spi_write_then_read(ds1305->spi, buf, 2, NULL, 0);
	}

	return status;
}

#ifdef CONFIG_PROC_FS

static int ds1305_proc(struct device *dev, struct seq_file *seq)
{
	struct ds1305	*ds1305 = dev_get_drvdata(dev);
	char		*diodes = "no";
	char		*resistors = "";

	
	if ((ds1305->ctrl[2] & 0xf0) == DS1305_TRICKLE_MAGIC) {
		switch (ds1305->ctrl[2] & 0x0c) {
		case DS1305_TRICKLE_DS2:
			diodes = "2 diodes, ";
			break;
		case DS1305_TRICKLE_DS1:
			diodes = "1 diode, ";
			break;
		default:
			goto done;
		}
		switch (ds1305->ctrl[2] & 0x03) {
		case DS1305_TRICKLE_2K:
			resistors = "2k Ohm";
			break;
		case DS1305_TRICKLE_4K:
			resistors = "4k Ohm";
			break;
		case DS1305_TRICKLE_8K:
			resistors = "8k Ohm";
			break;
		default:
			diodes = "no";
			break;
		}
	}

done:
	return seq_printf(seq,
			"trickle_charge\t: %s%s\n",
			diodes, resistors);
}

#else
#define ds1305_proc	NULL
#endif

static const struct rtc_class_ops ds1305_ops = {
	.read_time	= ds1305_get_time,
	.set_time	= ds1305_set_time,
	.read_alarm	= ds1305_get_alarm,
	.set_alarm	= ds1305_set_alarm,
	.proc		= ds1305_proc,
	.alarm_irq_enable = ds1305_alarm_irq_enable,
};

static void ds1305_work(struct work_struct *work)
{
	struct ds1305	*ds1305 = container_of(work, struct ds1305, work);
	struct mutex	*lock = &ds1305->rtc->ops_lock;
	struct spi_device *spi = ds1305->spi;
	u8		buf[3];
	int		status;

	
	mutex_lock(lock);

	ds1305->ctrl[0] &= ~(DS1305_AEI1 | DS1305_AEI0);
	ds1305->ctrl[1] = 0;

	buf[0] = DS1305_WRITE | DS1305_CONTROL;
	buf[1] = ds1305->ctrl[0];
	buf[2] = 0;

	status = spi_write_then_read(spi, buf, sizeof buf,
			NULL, 0);
	if (status < 0)
		dev_dbg(&spi->dev, "clear irq --> %d\n", status);

	mutex_unlock(lock);

	if (!test_bit(FLAG_EXITING, &ds1305->flags))
		enable_irq(spi->irq);

	rtc_update_irq(ds1305->rtc, 1, RTC_AF | RTC_IRQF);
}

static irqreturn_t ds1305_irq(int irq, void *p)
{
	struct ds1305		*ds1305 = p;

	disable_irq(irq);
	schedule_work(&ds1305->work);
	return IRQ_HANDLED;
}



static void msg_init(struct spi_message *m, struct spi_transfer *x,
		u8 *addr, size_t count, char *tx, char *rx)
{
	spi_message_init(m);
	memset(x, 0, 2 * sizeof(*x));

	x->tx_buf = addr;
	x->len = 1;
	spi_message_add_tail(x, m);

	x++;

	x->tx_buf = tx;
	x->rx_buf = rx;
	x->len = count;
	spi_message_add_tail(x, m);
}

static ssize_t
ds1305_nvram_read(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr,
		char *buf, loff_t off, size_t count)
{
	struct spi_device	*spi;
	u8			addr;
	struct spi_message	m;
	struct spi_transfer	x[2];
	int			status;

	spi = container_of(kobj, struct spi_device, dev.kobj);

	if (unlikely(off >= DS1305_NVRAM_LEN))
		return 0;
	if (count >= DS1305_NVRAM_LEN)
		count = DS1305_NVRAM_LEN;
	if ((off + count) > DS1305_NVRAM_LEN)
		count = DS1305_NVRAM_LEN - off;
	if (unlikely(!count))
		return count;

	addr = DS1305_NVRAM + off;
	msg_init(&m, x, &addr, count, NULL, buf);

	status = spi_sync(spi, &m);
	if (status < 0)
		dev_err(&spi->dev, "nvram %s error %d\n", "read", status);
	return (status < 0) ? status : count;
}

static ssize_t
ds1305_nvram_write(struct file *filp, struct kobject *kobj,
		struct bin_attribute *attr,
		char *buf, loff_t off, size_t count)
{
	struct spi_device	*spi;
	u8			addr;
	struct spi_message	m;
	struct spi_transfer	x[2];
	int			status;

	spi = container_of(kobj, struct spi_device, dev.kobj);

	if (unlikely(off >= DS1305_NVRAM_LEN))
		return -EFBIG;
	if (count >= DS1305_NVRAM_LEN)
		count = DS1305_NVRAM_LEN;
	if ((off + count) > DS1305_NVRAM_LEN)
		count = DS1305_NVRAM_LEN - off;
	if (unlikely(!count))
		return count;

	addr = (DS1305_WRITE | DS1305_NVRAM) + off;
	msg_init(&m, x, &addr, count, buf, NULL);

	status = spi_sync(spi, &m);
	if (status < 0)
		dev_err(&spi->dev, "nvram %s error %d\n", "write", status);
	return (status < 0) ? status : count;
}

static struct bin_attribute nvram = {
	.attr.name	= "nvram",
	.attr.mode	= S_IRUGO | S_IWUSR,
	.read		= ds1305_nvram_read,
	.write		= ds1305_nvram_write,
	.size		= DS1305_NVRAM_LEN,
};



static int __devinit ds1305_probe(struct spi_device *spi)
{
	struct ds1305			*ds1305;
	int				status;
	u8				addr, value;
	struct ds1305_platform_data	*pdata = spi->dev.platform_data;
	bool				write_ctrl = false;

	if ((spi->bits_per_word && spi->bits_per_word != 8)
			|| (spi->max_speed_hz > 2000000)
			|| !(spi->mode & SPI_CPHA))
		return -EINVAL;

	
	ds1305 = kzalloc(sizeof *ds1305, GFP_KERNEL);
	if (!ds1305)
		return -ENOMEM;
	ds1305->spi = spi;
	spi_set_drvdata(spi, ds1305);

	
	addr = DS1305_CONTROL;
	status = spi_write_then_read(spi, &addr, sizeof addr,
			ds1305->ctrl, sizeof ds1305->ctrl);
	if (status < 0) {
		dev_dbg(&spi->dev, "can't %s, %d\n",
				"read", status);
		goto fail0;
	}

	dev_dbg(&spi->dev, "ctrl %s: %02x %02x %02x\n",
			"read", ds1305->ctrl[0],
			ds1305->ctrl[1], ds1305->ctrl[2]);

	if ((ds1305->ctrl[0] & 0x38) != 0 || (ds1305->ctrl[1] & 0xfc) != 0) {
		dev_dbg(&spi->dev, "RTC chip is not present\n");
		status = -ENODEV;
		goto fail0;
	}
	if (ds1305->ctrl[2] == 0)
		dev_dbg(&spi->dev, "chip may not be present\n");

	if (ds1305->ctrl[0] & DS1305_WP) {
		u8		buf[2];

		ds1305->ctrl[0] &= ~DS1305_WP;

		buf[0] = DS1305_WRITE | DS1305_CONTROL;
		buf[1] = ds1305->ctrl[0];
		status = spi_write_then_read(spi, buf, sizeof buf, NULL, 0);

		dev_dbg(&spi->dev, "clear WP --> %d\n", status);
		if (status < 0)
			goto fail0;
	}

	if (ds1305->ctrl[0] & DS1305_nEOSC) {
		ds1305->ctrl[0] &= ~DS1305_nEOSC;
		write_ctrl = true;
		dev_warn(&spi->dev, "SET TIME!\n");
	}

	
	if (ds1305->ctrl[1]) {
		ds1305->ctrl[1] = 0;
		write_ctrl = true;
	}

	
	if (pdata) {
		
		if (((ds1305->ctrl[2] & 0xf0) != DS1305_TRICKLE_MAGIC)) {
			ds1305->ctrl[2] = DS1305_TRICKLE_MAGIC
						| pdata->trickle;
			write_ctrl = true;
		}

		
		if (pdata->is_ds1306) {
			if (pdata->en_1hz) {
				if (!(ds1305->ctrl[0] & DS1306_1HZ)) {
					ds1305->ctrl[0] |= DS1306_1HZ;
					write_ctrl = true;
				}
			} else {
				if (ds1305->ctrl[0] & DS1306_1HZ) {
					ds1305->ctrl[0] &= ~DS1306_1HZ;
					write_ctrl = true;
				}
			}
		}
	}

	if (write_ctrl) {
		u8		buf[4];

		buf[0] = DS1305_WRITE | DS1305_CONTROL;
		buf[1] = ds1305->ctrl[0];
		buf[2] = ds1305->ctrl[1];
		buf[3] = ds1305->ctrl[2];
		status = spi_write_then_read(spi, buf, sizeof buf, NULL, 0);
		if (status < 0) {
			dev_dbg(&spi->dev, "can't %s, %d\n",
					"write", status);
			goto fail0;
		}

		dev_dbg(&spi->dev, "ctrl %s: %02x %02x %02x\n",
				"write", ds1305->ctrl[0],
				ds1305->ctrl[1], ds1305->ctrl[2]);
	}

	
	addr = DS1305_HOUR;
	status = spi_write_then_read(spi, &addr, sizeof addr,
				&value, sizeof value);
	if (status < 0) {
		dev_dbg(&spi->dev, "read HOUR --> %d\n", status);
		goto fail0;
	}

	ds1305->hr12 = (DS1305_HR_12 & value) != 0;
	if (ds1305->hr12)
		dev_dbg(&spi->dev, "AM/PM\n");

	
	ds1305->rtc = rtc_device_register("ds1305", &spi->dev,
			&ds1305_ops, THIS_MODULE);
	if (IS_ERR(ds1305->rtc)) {
		status = PTR_ERR(ds1305->rtc);
		dev_dbg(&spi->dev, "register rtc --> %d\n", status);
		goto fail0;
	}

	if (spi->irq) {
		INIT_WORK(&ds1305->work, ds1305_work);
		status = request_irq(spi->irq, ds1305_irq,
				0, dev_name(&ds1305->rtc->dev), ds1305);
		if (status < 0) {
			dev_dbg(&spi->dev, "request_irq %d --> %d\n",
					spi->irq, status);
			goto fail1;
		}

		device_set_wakeup_capable(&spi->dev, 1);
	}

	
	status = sysfs_create_bin_file(&spi->dev.kobj, &nvram);
	if (status < 0) {
		dev_dbg(&spi->dev, "register nvram --> %d\n", status);
		goto fail2;
	}

	return 0;

fail2:
	free_irq(spi->irq, ds1305);
fail1:
	rtc_device_unregister(ds1305->rtc);
fail0:
	kfree(ds1305);
	return status;
}

static int __devexit ds1305_remove(struct spi_device *spi)
{
	struct ds1305 *ds1305 = spi_get_drvdata(spi);

	sysfs_remove_bin_file(&spi->dev.kobj, &nvram);

	
	if (spi->irq) {
		set_bit(FLAG_EXITING, &ds1305->flags);
		free_irq(spi->irq, ds1305);
		cancel_work_sync(&ds1305->work);
	}

	rtc_device_unregister(ds1305->rtc);
	spi_set_drvdata(spi, NULL);
	kfree(ds1305);
	return 0;
}

static struct spi_driver ds1305_driver = {
	.driver.name	= "rtc-ds1305",
	.driver.owner	= THIS_MODULE,
	.probe		= ds1305_probe,
	.remove		= __devexit_p(ds1305_remove),
	
};

module_spi_driver(ds1305_driver);

MODULE_DESCRIPTION("RTC driver for DS1305 and DS1306 chips");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:rtc-ds1305");