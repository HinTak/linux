#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/mfd/sdp_micom.h>
#include <linux/rtc.h>
#include <linux/of.h>

#define DRIVER_NAME		"rtc-wt61p807"

#define SDP_MICOM_DATA_LEN	5

struct wt61p807_rtc {
	struct rtc_device *rtc_dev;
	struct rtc_time *rtc_tm;
};

static void wt61p807_rtc_gettime_calc(char *data, struct rtc_time *rtc_tm)
{
	if (data) {
		rtc_tm->tm_year = ((data[1] & 0xFE) >> 1);
		rtc_tm->tm_mon = ((data[1] & 0x01) << 3) |
				((data[2] & 0xE0) >> 5);
		rtc_tm->tm_mday = (data[2] & 0x1F);
		rtc_tm->tm_hour = (data[3] & 0x1F);
		rtc_tm->tm_min = (data[4] & 0x3F);
		rtc_tm->tm_sec = (data[5] & 0x3F);

		rtc_tm->tm_year += 100;
		rtc_tm->tm_mon -= 1;
	} else {
		rtc_tm->tm_year = 0;
		rtc_tm->tm_mon = 0;
		rtc_tm->tm_mday = 0;
		rtc_tm->tm_hour = 0;
		rtc_tm->tm_min = 0;
		rtc_tm->tm_sec = 0;
	}
}

static void wt61p807_rtc_gettime_cb(struct sdp_micom_msg *msg, void *dev_id)
{
	struct wt61p807_rtc *rtc = dev_id;

	if (!rtc)
		return;

	wt61p807_rtc_gettime_calc(msg->msg, rtc->rtc_tm);
}

static int wt61p807_rtc_settime(struct device *dev, struct rtc_time *tm)
{
	char data[SDP_MICOM_DATA_LEN];
	int year;

	year = tm->tm_year - 100;

	if (year < 0 || year >= 128) {
		dev_err(dev, "[RTC]rtc only supports 127 years(year:%d)\n", tm->tm_year);
		return -EINVAL;
	}

	data[0] = (year << 1) | (((tm->tm_mon + 1) & 0x08) >> 3);
	data[1] = (((tm->tm_mon + 1) & 0x07) << 5) | tm->tm_mday;
	data[2] = tm->tm_hour;
	data[3] = tm->tm_min;
	data[4] = tm->tm_sec;
	printk("[RTC]Set Current Time[%04d(%d):%02d:%02d %02d:%02d:%02d]\n", tm->tm_year, year, tm->tm_mon, tm->tm_mday,
			tm->tm_hour, tm->tm_min, tm->tm_sec);

	sdp_micom_send_cmd_sync(SDP_MICOM_CMD_SET_TIME, SDP_MICOM_ACK_SET_TIME,
		data, SDP_MICOM_DATA_LEN);

	return 0;
}

static int wt61p807_rtc_gettime(struct device *dev, struct rtc_time *rtc_tm)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct wt61p807_rtc *rtc = platform_get_drvdata(pdev);
	struct rtc_time *rtc_micom = NULL;

	if (!pdev || !rtc || !rtc_tm)
		return -EINVAL;

	sdp_micom_send_cmd_sync(SDP_MICOM_CMD_GET_TIME, SDP_MICOM_ACK_GET_TIME, NULL, 0);

	rtc_micom = rtc->rtc_tm;
	printk("[RTC]Get Current Time[%04d:%02d:%02d %02d:%02d:%02d]\n", rtc_micom->tm_year, rtc_micom->tm_mon, rtc_micom->tm_mday,
			rtc_micom->tm_hour, rtc_micom->tm_min, rtc_micom->tm_sec);

	if (rtc_valid_tm(rtc_micom) < 0) {
		/* initialize rtc when got invalid rtc. */
		printk("[RTC]Invalid time.!!\n");
		rtc_micom->tm_year     = 100;	//Base Year : 2000year
		rtc_micom->tm_mon      = 0;
		rtc_micom->tm_mday     = 1;
		rtc_micom->tm_hour     = 0;
		rtc_micom->tm_min      = 0;
		rtc_micom->tm_sec      = 0;

		wt61p807_rtc_settime(&pdev->dev, rtc_micom);
	}

	memcpy(rtc_tm, rtc_micom, sizeof(struct rtc_time));

	return rtc_valid_tm(rtc_tm);
}

static const struct rtc_class_ops wt61p807_rtcops = {
	.read_time	= wt61p807_rtc_gettime,
	.set_time	= wt61p807_rtc_settime,
};

static int wt61p807_rtc_probe(struct platform_device *pdev)
{
	struct wt61p807_rtc *rtc;
	struct rtc_time *rtc_tm;
	struct sdp_micom_cb *micom_cb;
	int ret;

	rtc = devm_kzalloc(&pdev->dev, sizeof(struct wt61p807_rtc), GFP_KERNEL);
	if (!rtc) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}

	rtc_tm = devm_kzalloc(&pdev->dev, sizeof(struct rtc_time), GFP_KERNEL);
	if (!rtc_tm) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}
	rtc->rtc_tm = rtc_tm;
	platform_set_drvdata(pdev, rtc);

	micom_cb = devm_kzalloc(&pdev->dev,
			sizeof(struct sdp_micom_cb), GFP_KERNEL);
	if (!micom_cb) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		return -ENOMEM;
	}

	micom_cb->id		= SDP_MICOM_DEV_RTC;
	micom_cb->name		= DRIVER_NAME;
	micom_cb->cb		= wt61p807_rtc_gettime_cb;
	micom_cb->dev_id	= rtc;

	ret = sdp_micom_register_cb(micom_cb);
	if (ret < 0) {
		dev_err(&pdev->dev, "micom callback registration failed\n");
		return ret;
	}

	rtc->rtc_dev = rtc_device_register("wt61p807", &pdev->dev,
				&wt61p807_rtcops, THIS_MODULE);
	if (IS_ERR(rtc->rtc_dev)) {
		dev_err(&pdev->dev, "cannot attach rtc\n");
		ret = PTR_ERR(rtc->rtc_dev);
		return ret;
	}

	return 0;
}

static int wt61p807_rtc_remove(struct platform_device *pdev)
{
	struct wt61p807_rtc *rtc = platform_get_drvdata(pdev);

	rtc_device_unregister(rtc->rtc_dev);

	return 0;
}

static struct platform_driver wt61p807_rtc_driver = {
	.probe		= wt61p807_rtc_probe,
	.remove		= wt61p807_rtc_remove,
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(wt61p807_rtc_driver);
