/*
 * Copyright (c) 2012 Synaptics Incorporated
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/kernel.h>
#include <linux/rmi.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/vivo_touchscreen_common.h>
#include "rmi_driver.h"

#define QUERY_BASE_INDEX 1
#define MAX_NAME_LEN 256
#define MAX_BUFFER_LEN 80

#define FILTER_MODE_MIN				0
#define FILTER_MODE_MAX				3
#define MULTI_BUTTON_REPORT_MIN			0
#define MULTI_BUTTON_REPORT_MAX			3
#define TX_RX_BUTTON_MIN			0
#define TX_RX_BUTTON_MAX			255
#define THREADHOLD_BUTTON_MIN			0
#define THREADHOLD_BUTTON_MAX			255
#define RELEASE_THREADHOLD_BUTTON_MIN		0
#define RELEASE_THREADHOLD_BUTTON_MAX		255
#define STRONGEST_BUTTON_HYSTERESIS_MIN		0
#define STRONGEST_BUTTON_HYSTERESIS_MAX		255
#define FILTER_STRENGTH_MIN			0
#define FILTER_STRENGTH_MAX			255

#define BUTTON_NUMBERS	3
#define RELEASE_DELAY_TIME	50

union f1a_0d_query {
	struct {
		u8 max_button_count:3;
		u8 reserved:5;

		u8 has_general_control:1;
		u8 has_interrupt_enable:1;
		u8 has_multibutton_select:1;
		u8 has_tx_rx_map:1;
		u8 has_perbutton_threshold:1;
		u8 has_release_threshold:1;
		u8 has_strongestbtn_hysteresis:1;
		u8 has_filter_strength:1;
	};
	u8 regs[2];
};

union f1a_0d_control_0 {
	struct {
		u8 multibutton_report:2;
		u8 filter_mode:2;
	};
	u8 regs[1];
};

struct f1a_0d_control_3_4 {
	u8 transmitterbutton;
	u8 receiverbutton;
};

struct f1a_0d_control {
	union f1a_0d_control_0 general_control;
	u8 *button_int_enable;
	u8 *multi_button;
	struct f1a_0d_control_3_4 *electrode_map;
	u8 *button_threshold;
	u8 button_release_threshold;
	u8 strongest_button_hysteresis;
	u8 filter_strength;
};

/* data specific to fn $1a that needs to be kept around */
struct f1a_data {
	struct f1a_0d_control button_control;
	union f1a_0d_query button_query;
	u8 button_count;
	int button_bitmask_size;
	u8 *button_data_buffer;
	u8 *button_map;
	char input_name[MAX_NAME_LEN];
	char input_phys[MAX_NAME_LEN];
	struct input_dev *input;
	u16 general_control_address;
	u16 button_int_enable_address;
	u16 multi_button_address;
	u16 electrode_map_address;
	u16 button_threshold_address;
	u16 button_release_threshold_address;
	u16 strongest_button_hysteresis_address;
	u16 filter_strength_address;
	int	old_button_status[BUTTON_NUMBERS];
	int new_button_status[BUTTON_NUMBERS]; /* add for one button dclick while one press */
	struct timer_list release_delay;
	struct work_struct delay_work;
	struct rmi_driver_data *ddata;
#if defined(BBK_TS_TRACE_BASELINE)
	struct timer_list trace_timer;
	struct work_struct trace_work;
#endif
};

#if defined(BBK_TS_TRACE_BASELINE)
#define TRACE_TIMEOUT 1500  /*ms*/
#endif

static ssize_t rmi_f1a_button_count_show(struct device *dev,
				struct device_attribute *attr,
				char *buf);
static ssize_t rmi_f1a_button_map_show(struct device *dev,
				struct device_attribute *attr, char *buf);
static ssize_t rmi_f1a_button_map_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count);
static ssize_t rmi_f1a_has_general_control_show(struct device *dev,
				struct device_attribute *attr, char *buf);
static ssize_t rmi_f1a_has_interrupt_enable_show(struct device *dev,
				struct device_attribute *attr, char *buf);
static ssize_t rmi_f1a_has_multibutton_select_show(struct device *dev,
				struct device_attribute *attr, char *buf);
static ssize_t rmi_f1a_has_tx_rx_map_show(struct device *dev,
				struct device_attribute *attr, char *buf);
static ssize_t rmi_f1a_has_perbutton_threshold_show(struct device *dev,
				struct device_attribute *attr, char *buf);
static ssize_t rmi_f1a_has_release_threshold_show(struct device *dev,
				struct device_attribute *attr, char *buf);
static ssize_t rmi_f1a_has_strongestbtn_hysteresis_show(struct device *dev,
				struct device_attribute *attr, char *buf);
static ssize_t rmi_f1a_has_filter_strength_show(struct device *dev,
				struct device_attribute *attr, char *buf);
static ssize_t rmi_f1a_multibutton_report_show(struct device *dev,
				struct device_attribute *attr, char *buf);
static ssize_t rmi_f1a_multibutton_report_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count);
static ssize_t rmi_f1a_filter_mode_show(struct device *dev,
				struct device_attribute *attr,
				char *buf);
static ssize_t rmi_f1a_filter_mode_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count);
static ssize_t rmi_f1a_button_int_enable_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf);
static ssize_t rmi_f1a_button_int_enable_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count);
static ssize_t rmi_f1a_multibutton_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf);
static ssize_t rmi_f1a_multibutton_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count);
static ssize_t rmi_f1a_electrode_map_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf);
static ssize_t rmi_f1a_electrode_map_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count);
static ssize_t rmi_f1a_threshold_button_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf);
static ssize_t rmi_f1a_threshold_button_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count);
static ssize_t rmi_f1a_button_release_threshold_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf);
static ssize_t rmi_f1a_button_release_threshold_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count);
static ssize_t rmi_f1a_strongest_button_hysteresis_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf);
static ssize_t rmi_f1a_strongest_button_hysteresis_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count);
static ssize_t rmi_f1a_filter_strength_show(struct device *dev,
				 struct device_attribute *attr,
				 char *buf);
static ssize_t rmi_f1a_filter_strength_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count);

static int rmi_f1a_alloc_memory(struct rmi_function_container *fc);

static void rmi_f1a_free_memory(struct rmi_function_container *fc);

static int rmi_f1a_initialize(struct rmi_function_container *fc);

static int rmi_f1a_register_device(struct rmi_function_container *fc);

static int rmi_f1a_create_sysfs(struct rmi_function_container *fc);

static int rmi_f1a_config(struct rmi_function_container *fc);

static int rmi_f1a_reset(struct rmi_function_container *fc);

static void rmi_f1a_release_delay_timer_func(unsigned long data);

static void rmi_f1a_release_delay_timer_work(struct work_struct *work);

#if defined(BBK_TS_TRACE_BASELINE)
static void rmi_f1a_trace_timer_func(unsigned long data);

static void rmi_f1a_trace_work_func(struct work_struct *work);

int rmi_f11_check_baseline(void);
#endif


static struct device_attribute attrs[] = {
	__ATTR(button_count, RMI_RO_ATTR,
		rmi_f1a_button_count_show, rmi_store_error),
	__ATTR(button_map, RMI_RW_ATTR,
		rmi_f1a_button_map_show, rmi_f1a_button_map_store),
	__ATTR(has_general_control, RMI_RO_ATTR,
		rmi_f1a_has_general_control_show, rmi_store_error),
	__ATTR(has_interrupt_enable, RMI_RO_ATTR,
		rmi_f1a_has_interrupt_enable_show, rmi_store_error),
	__ATTR(has_multibutton_select, RMI_RO_ATTR,
		rmi_f1a_has_multibutton_select_show, rmi_store_error),
	__ATTR(has_tx_rx_map, RMI_RO_ATTR,
		rmi_f1a_has_tx_rx_map_show, rmi_store_error),
	__ATTR(has_perbutton_threshold, RMI_RO_ATTR,
		rmi_f1a_has_perbutton_threshold_show, rmi_store_error),
	__ATTR(has_release_threshold, RMI_RO_ATTR,
		rmi_f1a_has_release_threshold_show, rmi_store_error),
	__ATTR(has_strongestbtn_hysteresis, RMI_RO_ATTR,
		rmi_f1a_has_strongestbtn_hysteresis_show, rmi_store_error),
	__ATTR(has_filter_strength, RMI_RO_ATTR,
		rmi_f1a_has_filter_strength_show, rmi_store_error),
	__ATTR(multibutton_report, RMI_RW_ATTR,
		rmi_f1a_multibutton_report_show,
		rmi_f1a_multibutton_report_store),
	__ATTR(filter_mode, RMI_RW_ATTR,
		rmi_f1a_filter_mode_show, rmi_f1a_filter_mode_store),
	__ATTR(button_int_enable, RMI_RW_ATTR,
		rmi_f1a_button_int_enable_show,
		rmi_f1a_button_int_enable_store),
	__ATTR(multibutton, RMI_RW_ATTR,
		rmi_f1a_multibutton_show, rmi_f1a_multibutton_store),
	__ATTR(electrode_map, RMI_RW_ATTR,
		rmi_f1a_electrode_map_show,
		rmi_f1a_electrode_map_store),
	__ATTR(threshold_button, RMI_RW_ATTR,
		rmi_f1a_threshold_button_show,
		rmi_f1a_threshold_button_store),
	__ATTR(button_release_threshold, RMI_RW_ATTR,
		rmi_f1a_button_release_threshold_show,
		rmi_f1a_button_release_threshold_store),
	__ATTR(strongest_button_hysteresis, RMI_RW_ATTR,
		rmi_f1a_strongest_button_hysteresis_show,
		rmi_f1a_strongest_button_hysteresis_store),
	__ATTR(filter_strength, RMI_RW_ATTR,
		rmi_f1a_filter_strength_show,
		rmi_f1a_filter_strength_store)
};

static int rmi_f1a_read_control_parameters(struct rmi_device *rmi_dev,
	struct f1a_data *f1a)
{
	int error = 0;
	struct f1a_0d_control *button_control = &f1a->button_control;
	union f1a_0d_query *button_query = &f1a->button_query;
	u16 ctrl_base_addr = f1a->general_control_address;
	int button_bitmask_size = f1a->button_bitmask_size;

	if (button_query->has_general_control) {
		error = rmi_read_block(rmi_dev, ctrl_base_addr,
				(u8 *)&button_control->general_control,
				sizeof(union f1a_0d_control_0));
		if (error < 0) {
			VIVO_TS_LOG_ERR("[%s]:Failed to read f1a_0d_control_0, code: %d.\n", __func__, error);
			return error;
		}
		ctrl_base_addr = ctrl_base_addr +
				sizeof(union f1a_0d_control_0);
	}

	if (button_query->has_interrupt_enable &&
		button_control->button_int_enable) {
		f1a->button_int_enable_address = ctrl_base_addr;
		error = rmi_read_block(rmi_dev, ctrl_base_addr,
			button_control->button_int_enable,
			button_bitmask_size);
		if (error < 0) {
			VIVO_TS_LOG_ERR("[%s]:Failed to read f1a_0d_control_1, code: %d.\n", __func__, error);
			return error;
		}
		ctrl_base_addr = ctrl_base_addr + button_bitmask_size;
	}

	if (button_query->has_multibutton_select &&
		button_control->multi_button) {
		f1a->multi_button_address = ctrl_base_addr;
		error = rmi_read_block(rmi_dev, ctrl_base_addr,
				button_control->multi_button,
				button_bitmask_size);
		if (error < 0) {
			VIVO_TS_LOG_ERR("[%s]:Failed to read f1a_0d_control_2, code: %d.\n", __func__, error);
			return error;
		}
		ctrl_base_addr = ctrl_base_addr + button_bitmask_size;
	}

	if (button_query->has_tx_rx_map &&
		button_control->electrode_map) {
		f1a->electrode_map_address = ctrl_base_addr;
		error = rmi_read_block(rmi_dev, ctrl_base_addr,
			(u8 *)(button_control->electrode_map),
			sizeof(struct f1a_0d_control_3_4)*f1a->button_count);
		if (error < 0) {
			VIVO_TS_LOG_ERR("[%s]:Failed to read f1a_0d_control_3_4, code: %d.\n", __func__, error);
			return error;
		}
		ctrl_base_addr = ctrl_base_addr +
			(sizeof(struct f1a_0d_control_3_4)*f1a->button_count);
	}
	if (button_query->has_perbutton_threshold &&
		button_control->button_threshold) {
		f1a->button_threshold_address = ctrl_base_addr;
		error = rmi_read_block(rmi_dev, ctrl_base_addr,
			button_control->button_threshold,
			f1a->button_count);
		if (error < 0) {
			VIVO_TS_LOG_ERR("[%s]:Failed to read f1a_0d_control_5, code: %d.\n", __func__, error);
			return error;
		}
		ctrl_base_addr = ctrl_base_addr +
			(sizeof(u8)*f1a->button_count);
	}

	if (button_query->has_release_threshold) {
		f1a->button_release_threshold_address = ctrl_base_addr;
		error = rmi_read_block(rmi_dev, ctrl_base_addr,
				&button_control->button_release_threshold,
				sizeof(u8));
		if (error < 0) {
			VIVO_TS_LOG_ERR("[%s]:Failed to read f1a_0d_control_6, code: %d.\n", __func__, error);
			return error;
		}
		ctrl_base_addr = ctrl_base_addr + sizeof(u8);
	}

	if (button_query->has_strongestbtn_hysteresis) {
		f1a->strongest_button_hysteresis_address = ctrl_base_addr;
		error = rmi_read_block(rmi_dev, ctrl_base_addr,
				&button_control->
					strongest_button_hysteresis,
				sizeof(u8));
		if (error < 0) {
			VIVO_TS_LOG_ERR("[%s]:Failed to read f1a_0d_control_7, code: %d.\n", __func__, error);
			return error;
		}
		ctrl_base_addr = ctrl_base_addr + sizeof(u8);
	}

	if (button_query->has_filter_strength) {
		f1a->filter_strength_address = ctrl_base_addr;
		error = rmi_read_block(rmi_dev, ctrl_base_addr,
				(u8 *)&button_control->filter_strength,
				sizeof(u8));
		if (error < 0) {
			VIVO_TS_LOG_ERR("[%s]:Failed to read f1a_0d_control_8, code: %d.\n", __func__, error);
			return error;
		}
	}
	return 0;
}


static int rmi_f1a_init(struct rmi_function_container *fc)
{
	int rc;

	rc = rmi_f1a_alloc_memory(fc);
	if (rc < 0)
		goto err_free_data;

	rc = rmi_f1a_initialize(fc);
	if (rc < 0)
		goto err_free_data;

	rc = rmi_f1a_register_device(fc);
	if (rc < 0)
		goto err_free_data;

	rc = rmi_f1a_create_sysfs(fc);
	if (rc < 0)
		goto err_free_data;

	return 0;

err_free_data:
	rmi_f1a_free_memory(fc);

	return rc;
}


static int rmi_f1a_alloc_memory(struct rmi_function_container *fc)
{
	struct f1a_data *f1a;
	int rc;

	f1a = kzalloc(sizeof(struct f1a_data), GFP_KERNEL);
	if (!f1a) {
		VIVO_TS_LOG_ERR("[%s]:Failed to allocate function data.\n", __func__);
		return -ENOMEM;
	}
	fc->data = f1a;

	rc = rmi_read_block(fc->rmi_dev, fc->fd.query_base_addr,
			(u8 *)&f1a->button_query,
			sizeof(union f1a_0d_query));
	if (rc < 0) {
		VIVO_TS_LOG_ERR("[%s]:Failed to read query register.\n", __func__);
		return rc;
	}

	f1a->button_count = f1a->button_query.max_button_count+1;

	f1a->button_bitmask_size = sizeof(u8)*(f1a->button_count + 7) / 8;
	f1a->button_data_buffer = kcalloc(f1a->button_bitmask_size,
				sizeof(u8), GFP_KERNEL);
	if (!f1a->button_data_buffer) {
		VIVO_TS_LOG_ERR("[%s]:Failed to allocate button data buffer.\n", __func__);
		return -ENOMEM;
	}

	f1a->button_map = kcalloc(f1a->button_count,
				sizeof(unsigned char), GFP_KERNEL);
	if (!f1a->button_map) {
		VIVO_TS_LOG_ERR("[%s]:Failed to allocate button map.\n", __func__);
		return -ENOMEM;
	}
	if (f1a->button_query.has_interrupt_enable) {
		f1a->button_control.button_int_enable =
				kzalloc(f1a->button_bitmask_size, GFP_KERNEL);
		if (!f1a->button_control.button_int_enable) {
			VIVO_TS_LOG_ERR("[%s]:Failed to allocate interrupt button.\n", __func__);
			return -ENOMEM;
		}
	}
	if (f1a->button_query.has_multibutton_select) {
		f1a->button_control.multi_button =
			kzalloc(f1a->button_bitmask_size, GFP_KERNEL);
		if (!f1a->button_control.multi_button) {
			VIVO_TS_LOG_ERR("[%s]:Failed to allocate multi button group select.\n", __func__);
			return -ENOMEM;
		}
	}
	if (f1a->button_query.has_tx_rx_map) {
		f1a->button_control.electrode_map =
			kzalloc(f1a->button_count *
				sizeof(struct f1a_0d_control_3_4), GFP_KERNEL);
		if (!f1a->button_control.electrode_map) {
			VIVO_TS_LOG_ERR("[%s]:Failed to allocate f1a_0d_control_3_4.\n", __func__);
			return -ENOMEM;
		}
	}
	if (f1a->button_query.has_perbutton_threshold) {
		f1a->button_control.button_threshold =
			kzalloc(f1a->button_count, GFP_KERNEL);
		if (!f1a->button_control.button_threshold) {
			VIVO_TS_LOG_ERR("[%s]:Failed to allocate button threshold.\n", __func__);
			return -ENOMEM;
		}
	}
	return 0;
}



static void rmi_f1a_free_memory(struct rmi_function_container *fc)
{
	struct f1a_data *f1a = fc->data;

	if (f1a) {
		kfree(f1a->button_data_buffer);
		kfree(f1a->button_map);
		kfree(f1a->button_control.button_int_enable);
		kfree(f1a->button_control.multi_button);
		kfree(f1a->button_control.electrode_map);
		kfree(f1a->button_control.button_threshold);
		kfree(f1a);
		fc->data = NULL;
	}
}


static int rmi_f1a_initialize(struct rmi_function_container *fc)
{
	struct rmi_device *rmi_dev = fc->rmi_dev;
	struct rmi_device_platform_data *pdata;
	struct f1a_data *f1a = fc->data;
	int i;
	int rc;
	int copy_count;

	VIVO_TS_LOG_DBG("[%s]:Intializing F1A values.\n", __func__);

	/* initial all default values for f1a data here */
	pdata = to_rmi_platform_data(rmi_dev);
	if (pdata) {
		if (!pdata->f1a_button_map)
			VIVO_TS_LOG_ERR("[%s]: - button_map is NULL", __func__);
		else if (!pdata->f1a_button_map->map)
			VIVO_TS_LOG_ERR("[%s]:Platformdata button map is missing!\n", __func__);
		else {
			if (pdata->f1a_button_map->nbuttons !=
					f1a->button_count)
				VIVO_TS_LOG_ERR("[%s]:Platformdata button map size (%d) != number "
					"of buttons on device (%d).\n", __func__,
					pdata->f1a_button_map->nbuttons,
					f1a->button_count);
			copy_count = min(f1a->button_count,
					 pdata->f1a_button_map->nbuttons);
			for (i = 0; i < copy_count; i++)
				f1a->button_map[i] =
					pdata->f1a_button_map->map[i];
		}
	}

	f1a->general_control_address = fc->fd.control_base_addr;
	rc = rmi_f1a_read_control_parameters(rmi_dev, f1a);
	if (rc < 0) {
		VIVO_TS_LOG_ERR("[%s]:Failed to initialize F1a control params.\n", __func__);
		return rc;
	}
	for (i = 0; i < BUTTON_NUMBERS; i++) {
		f1a->old_button_status[i] = 0;
	}

	f1a->ddata = rmi_get_driverdata(rmi_dev);
	setup_timer(&f1a->release_delay, rmi_f1a_release_delay_timer_func, (unsigned long)f1a);
	INIT_WORK(&f1a->delay_work, rmi_f1a_release_delay_timer_work);
#if defined(BBK_TS_TRACE_BASELINE)
	setup_timer(&f1a->trace_timer, rmi_f1a_trace_timer_func, f1a);
	INIT_WORK(&f1a->trace_work, rmi_f1a_trace_work_func);
#endif

	return 0;
}



static int rmi_f1a_register_device(struct rmi_function_container *fc)
{
	struct rmi_device *rmi_dev = fc->rmi_dev;
	struct input_dev *input_dev;
	struct f1a_data *f1a = fc->data;
	int i;
	int rc;

	input_dev = input_allocate_device();
	if (!input_dev) {
		VIVO_TS_LOG_ERR("[%s]:Failed to allocate input device.\n", __func__);
		return -ENOMEM;
	}

	f1a->input = input_dev;
	snprintf(f1a->input_name, MAX_NAME_LEN, "%sfn%02x",
			dev_name(&rmi_dev->dev),
			fc->fd.function_number);
	input_dev->name = f1a->input_name;
	snprintf(f1a->input_phys, MAX_NAME_LEN, "%s/input0", input_dev->name);
	input_dev->phys = f1a->input_phys;
	input_dev->dev.parent = &rmi_dev->dev;
	input_set_drvdata(input_dev, f1a);

	/* Set up any input events. */
	set_bit(EV_SYN, input_dev->evbit);
	set_bit(EV_KEY, input_dev->evbit);
	/* set bits for each button... */
	for (i = 0; i < f1a->button_count; i++)
	{
		set_bit(f1a->button_map[i], input_dev->keybit);
		input_set_capability(input_dev, EV_KEY, f1a->button_map[i]); // set capability
	}

	rc = input_register_device(input_dev);
	if (rc < 0) {
		VIVO_TS_LOG_ERR("[%s]:Failed to register input device.\n", __func__);
		goto error_free_device;
	}

	return 0;

error_free_device:
	input_free_device(input_dev);

	return rc;
}


static int rmi_f1a_create_sysfs(struct rmi_function_container *fc)
{
	int attr_count = 0;
	int rc;
	struct f1a_data *data;
	union f1a_0d_query *button_query;
	char *name;
	data = fc->data;
	button_query = &data->button_query;

	VIVO_TS_LOG_DBG("[%s]:Creating sysfs files.\n", __func__);
	/* Set up sysfs device attributes. */
	for (attr_count = 0; attr_count < ARRAY_SIZE(attrs); attr_count++) {
		name = (char *) attrs[attr_count].attr.name;
		if (!button_query->has_general_control &&
			(!strcmp(name, "multibutton_report") ||
			!strcmp(name, "filter_mode")))
			continue;
		if (!button_query->has_interrupt_enable &&
			!strcmp(name, "button_int_enable"))
			continue;
		if (!button_query->has_multibutton_select &&
			!strcmp(name, "multibutton"))
			continue;
		if (!button_query->has_tx_rx_map &&
			!strcmp(name, "electrode_map"))
			continue;
		if (!button_query->has_perbutton_threshold &&
			!strcmp(name, "threshold_button"))
			continue;
		if (!button_query->has_release_threshold &&
			!strcmp(name, "button_release_threshold"))
			continue;
		if (!button_query->has_strongestbtn_hysteresis &&
			!strcmp(name, "strongest_button_hysteresis"))
			continue;
		if (!button_query->has_filter_strength &&
			!strcmp(name, "filter_strength"))
			continue;
		if (sysfs_create_file
		    (&fc->dev.kobj, &attrs[attr_count].attr) < 0) {
			VIVO_TS_LOG_ERR("[%s]:Failed to create sysfs file for %s.", __func__,
				attrs[attr_count].attr.name);
			rc = -ENODEV;
			goto err_remove_sysfs;
		}
	}

	return 0;

err_remove_sysfs:
	for (attr_count--; attr_count >= 0; attr_count--)
		sysfs_remove_file(&fc->dev.kobj,
						  &attrs[attr_count].attr);
	return rc;

}



static int rmi_f1a_config(struct rmi_function_container *fc)
{
	struct f1a_data *data;
	int retval;
	union f1a_0d_query *button_query;

	data = fc->data;
	button_query = &data->button_query;
	if (button_query->has_general_control) {
		retval = rmi_write_block(fc->rmi_dev,
				data->general_control_address,
				(u8 *)&data->button_control.general_control,
				sizeof(union f1a_0d_control_0));
		if (retval < 0) {
			VIVO_TS_LOG_ERR("[%s]: Could not write general_control to 0x%x\n",
					__func__, fc->fd.control_base_addr);
			return retval;
		}
	}
	if (button_query->has_interrupt_enable) {
		retval = rmi_write_block(fc->rmi_dev,
				data->button_int_enable_address,
				data->button_control.button_int_enable,
				data->button_bitmask_size);
		if (retval < 0) {
			VIVO_TS_LOG_ERR("[%s]: Could not write interrupt_enable_store to 0x%x\n",
				__func__, data->button_int_enable_address);
			return retval;
		}
	}
	if (button_query->has_multibutton_select) {
		retval = rmi_write_block(fc->rmi_dev,
				data->multi_button_address,
				data->button_control.multi_button,
				data->button_bitmask_size);
		if (retval < 0) {
			VIVO_TS_LOG_ERR("[%s]: Could not write multi_button_store to 0x%x\n",
				__func__, data->multi_button_address);
			return -EINVAL;
		}
	}
	if (button_query->has_tx_rx_map) {
		retval = rmi_write_block(fc->rmi_dev,
				data->electrode_map_address,
				(u8 *)data->button_control.electrode_map,
				sizeof(struct f1a_0d_control_3_4)*
				data->button_count);
		if (retval < 0) {
			VIVO_TS_LOG_ERR("[%s]: Could not electrode_map_store to 0x%x\n",
					__func__, data->electrode_map_address);
			return -EINVAL;
		}
	}
	if (button_query->has_perbutton_threshold) {
		retval = rmi_write_block(fc->rmi_dev,
				data->button_threshold_address,
				data->button_control.button_threshold,
				data->button_count);
		if (retval < 0) {
			VIVO_TS_LOG_ERR("[%s]: Could not button_threshold_store to 0x%x\n",
				__func__, data->button_threshold_address);
			return retval;
		}
	}
	if (button_query->has_release_threshold) {
		retval = rmi_write_block(fc->rmi_dev,
			data->button_release_threshold_address,
			&data->button_control.button_release_threshold,
			sizeof(u8));
		if (retval < 0) {
			VIVO_TS_LOG_ERR("[%s]: Could not write  button_release_threshold store to 0x%x\n",
				__func__,
				data->button_release_threshold_address);
			return -EINVAL;
		}
	}
	if (button_query->has_strongestbtn_hysteresis) {
		retval = rmi_write_block(fc->rmi_dev,
			data->strongest_button_hysteresis_address,
			&data->button_control.strongest_button_hysteresis,
			sizeof(u8));
		if (retval < 0) {
			VIVO_TS_LOG_ERR("[%s]: Could not write strongest_button_hysteresis store"
				" to 0x%x\n",
				__func__,
				data->strongest_button_hysteresis_address);
			return -EINVAL;
		}
	}
	if (button_query->has_filter_strength) {
		retval = rmi_write_block(fc->rmi_dev,
				data->filter_strength_address,
				&data->button_control.filter_strength,
				sizeof(u8));
		if (retval < 0) {
			VIVO_TS_LOG_ERR("[%s]: Could not write filter_strength store to 0x%x\n", __func__,
				data->filter_strength_address);
			return -EINVAL;
		}
	}
	return 0;
}


static int rmi_f1a_reset(struct rmi_function_container *fc)
{
	/* we do nnothing here */
	return 0;
}


static void rmi_f1a_remove(struct rmi_function_container *fc)
{
	struct f1a_data *f1a = fc->data;
	int attr_count = 0;

	for (attr_count = 0; attr_count < ARRAY_SIZE(attrs); attr_count++)
		sysfs_remove_file(&fc->dev.kobj,
				  &attrs[attr_count].attr);

	input_unregister_device(f1a->input);

	rmi_f1a_free_memory(fc);
}
static void rmi_f1a_release_delay_timer_work(struct work_struct *work)
{
	struct f1a_data *f1a = container_of(work, struct f1a_data, delay_work);
	int i = 0;
	int button_status = 0;

	/* no need to check whether there is a release event in delay time
	   for input subsystem will fix it 
	*/

	VIVO_TS_LOG_DBG("[%s]:f1a->ddata->AA_area_point_pressed is %d\n", __func__,
				f1a->ddata->AA_area_point_pressed);

	for (i = 0; i < BUTTON_NUMBERS; i++) {
		VIVO_TS_LOG_DBG("[%s]:f1a->old_button_status[%d] = %d\n", __func__,
					i, f1a->old_button_status[i]);
		if (f1a->old_button_status[i] == 1) {
			/* there is a pressed event the up should be cancled */
			if (f1a->new_button_status[i] != f1a->old_button_status[i]) {
				f1a->old_button_status[i] = 0;
				if (f1a->ddata->AA_area_point_pressed == 1) {
					button_status = -1;
				}else{
					button_status = 0;
				}
				input_event(f1a->input, EV_KEY, f1a->button_map[i],
							button_status);
			}
		}
	}
	input_sync(f1a->input); /* sync after groups of events */
}

static void rmi_f1a_release_delay_timer_func(unsigned long data)
{
	struct f1a_data *f1a = (struct f1a_data *)data;

	schedule_work(&f1a->delay_work);
}

#if defined(BBK_TS_TRACE_BASELINE)
static void rmi_f1a_trace_timer_func(unsigned long data)
{
	struct f1a_data *f1a = (struct f1a_data *)data;

	schedule_work(&f1a->trace_work);
}

static void rmi_f1a_release_all_key(struct f1a_data *f1a)
{
	int button;

	for (button = 0; button < BUTTON_NUMBERS; button++) {
		if(f1a->old_button_status[button] != 0) {
			f1a->old_button_status[button] = 0;
			input_event(f1a->input, EV_KEY, f1a->button_map[button],
						0);
			input_sync(f1a->input);
		}
	}
}

static void rmi_f1a_trace_work_func(struct work_struct *work)
{
	struct f1a_data *f1a = container_of(work, struct f1a_data, trace_work);
	int button;

	for (button = 0; button < BUTTON_NUMBERS; button++) {
		if(f1a->old_button_status[button] != 0) {
			VIVO_TS_LOG_INF("[%s]: There is a button(%d) pressed when trace timer time out\n",
					__func__, f1a->button_map[button]);
			if (rmi_f11_check_baseline()) {
				rmi_f1a_release_all_key(f1a);
				return;
			}
		}
	}
}
#endif

int rmi_f1a_attention(struct rmi_function_container *fc, u8 *irq_bits)
{
	struct rmi_device *rmi_dev = fc->rmi_dev;
	struct rmi_driver_data *ddata = rmi_get_driverdata(rmi_dev);
	struct f1a_data *f1a = fc->data;
	u16 data_base_addr = fc->fd.data_base_addr;
	int error;
	int button;

	VIVO_TS_LOG_DBG("[%s]: ======== is called!=========== \n", __func__);
	/* Read the button data. */
	error = rmi_read_block(rmi_dev, data_base_addr, f1a->button_data_buffer,
			f1a->button_bitmask_size);
	if (error < 0) {
		VIVO_TS_LOG_ERR("[%s]: Failed to read button data registers.\n",
			__func__);
		return error;
	}

	if (ddata->has_lcd_shutoff) {
		VIVO_TS_LOG_ERR("[%s]: LCD has shut off\n", __func__);
		return 0;
	}

	/* Generate events for buttons that change state. */
	for (button = 0; button < BUTTON_NUMBERS; button++) {
		int button_reg;
		int button_shift;
		int button_status;
		unsigned long now;
		long diff;

		/* determine which data byte the button status is in */
		button_reg = button / 8;
		/* bit shift to get button's status */
		button_shift = button % 8;
		button_status =
		    ((f1a->button_data_buffer[button_reg] >> button_shift)
			& 0x01) != 0;
		VIVO_TS_LOG_DBG("[%s]: Button %d (code %d) -> %d====\n",
			__func__, button, f1a->button_map[button],
				button_status);

		VIVO_TS_LOG_INF("[%s]:AA_area_point_pressed is %d\n", __func__, ddata->AA_area_point_pressed);

		f1a->new_button_status[button] = button_status;

#if defined(BBK_TS_TRACE_BASELINE)
		if (button_status) {
			mod_timer(&f1a->trace_timer,
					jiffies + msecs_to_jiffies(TRACE_TIMEOUT));
		}
#endif
		if (ddata->AA_area_point_pressed == 1) {
			if (button_status != f1a->old_button_status[button]) {
				if (button_status == 1) {
					button_status = 0;
				}else{
					button_status = -1;
				}
			}

			VIVO_TS_LOG_DBG("[%s]:button_status = %d f1a->old_button_status[button] = %d\n", __func__,
							button_status, f1a->old_button_status[button]);
	
			if (button_status != f1a->old_button_status[button]) {
				if (button_status == -1) {
					f1a->old_button_status[button] = 0;
				}else{
					f1a->old_button_status[button] = button_status;
				}
				input_event(f1a->input, EV_KEY, f1a->button_map[button],
							button_status);
				input_sync(f1a->input); /* sync after groups of events */
			}
		}else if (button_status == 1){
			if (button_status != f1a->old_button_status[button]) {
				now = jiffies;
				diff = now - ddata->AA_area_point_release_time; 

#if 0
				VIVO_TS_LOG_INF("[%s]:button_status = %d now =%u AA_area_point_release_time = %ld\n", __func__,
								button_status, now, ddata->AA_area_point_release_time);
#endif

				f1a->old_button_status[button] = button_status;
				/* if diff < 200ms should not report the down event */
				if (diff > HZ/5) {
					input_event(f1a->input, EV_KEY, f1a->button_map[button],
								button_status);
					input_sync(f1a->input); /* sync after groups of events */
				}
			}
		}else if (button_status == 0) {
			/* release event with no finger pressed on tp 
			   should delay to judge whether the release event is valide
			*/
			VIVO_TS_LOG_DBG("[%s]:set up delay timer\n", __func__);

			if (button_status != f1a->old_button_status[button]) {
				mod_timer(&f1a->release_delay,
						jiffies + msecs_to_jiffies(RELEASE_DELAY_TIME));
			}
		}
	}

	//input_sync(f1a->input); /* sync after groups of events */
	return 0;
}

static struct rmi_function_handler function_handler = {
	.func = 0x1a,
	.init = rmi_f1a_init,
	.config = rmi_f1a_config,
	.reset = rmi_f1a_reset,
	.attention = rmi_f1a_attention,
	.remove = rmi_f1a_remove
};

static int __init rmi_f1a_module_init(void)
{
	int error;

	error = rmi_register_function_driver(&function_handler);
	if (error < 0) {
		VIVO_TS_LOG_ERR("[%s]: register failed!\n", __func__);
		return error;
	}

	return 0;
}

static void rmi_f1a_module_exit(void)
{
	rmi_unregister_function_driver(&function_handler);
}

static ssize_t rmi_f1a_filter_mode_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;

	return snprintf(buf, PAGE_SIZE, "%u\n",
			data->button_control.general_control.filter_mode);

}

static ssize_t rmi_f1a_filter_mode_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;
	unsigned int new_value;
	unsigned int old_value;
	int result;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	if (sscanf(buf, "%u", &new_value) < 1) {
		VIVO_TS_LOG_ERR("[%s]: Error - filter_mode_store has an invalid len.\n", __func__);
		return -EINVAL;
	}

	if (new_value < FILTER_MODE_MIN || new_value > FILTER_MODE_MAX) {
		VIVO_TS_LOG_ERR("[%s]: Error - filter_mode_store has an "
		"invalid value %d.\n",
		__func__, new_value);
		return -EINVAL;
	}
	old_value = data->button_control.general_control.filter_mode;
	data->button_control.general_control.filter_mode = new_value;
	result = rmi_write_block(fc->rmi_dev, data->general_control_address,
		(u8 *)&(data->button_control.general_control),
			sizeof(union f1a_0d_control_0));
	if (result < 0) {
		data->button_control.general_control.filter_mode = old_value;
		VIVO_TS_LOG_ERR("[%s]: Could not write filter_mode_store to 0x%x\n",
				__func__, fc->fd.control_base_addr);
		return result;
	}

	return count;
}

static ssize_t rmi_f1a_multibutton_report_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;

	return snprintf(buf, PAGE_SIZE, "%u\n",
		data->button_control.general_control.multibutton_report);

}

static ssize_t rmi_f1a_multibutton_report_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;
	unsigned int new_value;
	unsigned int old_value;
	int result;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	if (sscanf(buf, "%u", &new_value) < 1) {
		VIVO_TS_LOG_ERR("[%s]: Error - multibutton_report_store has an "
		"invalid len.\n",
		__func__);
		return -EINVAL;
	}

	if (new_value < MULTI_BUTTON_REPORT_MIN ||
		new_value > MULTI_BUTTON_REPORT_MAX) {
		VIVO_TS_LOG_ERR("[%s]: Error - multibutton_report_store has an "
		"invalid value %d.\n",
		__func__, new_value);
		return -EINVAL;
	}
	old_value = data->button_control.general_control.multibutton_report;
	data->button_control.general_control.multibutton_report = new_value;
	result = rmi_write_block(fc->rmi_dev, data->general_control_address,
		(u8 *)&(data->button_control.general_control),
			sizeof(union f1a_0d_control_0));
	if (result < 0) {
		data->button_control.general_control.multibutton_report =
				old_value;
		VIVO_TS_LOG_ERR("[%s]: Could not write multibutton_report_store to 0x%x\n",
				__func__, fc->fd.control_base_addr);
		return result;
	}

	return count;

}

static ssize_t rmi_f1a_button_int_enable_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;
	int i, len, total_len = 0;
	char *current_buf = buf;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	/* loop through each button map value and copy its
	 * string representation into buf */
	for (i = 0; i < data->button_count; i++) {
		int button_reg;
		int button_shift;
		int interrupt_button;

		button_reg = i / 8;
		button_shift = i % 8;
		interrupt_button =
		    ((data->button_control.button_int_enable[button_reg] >>
				button_shift) & 0x01);

		/* get next button mapping value and write it to buf */
		len = snprintf(current_buf, PAGE_SIZE - total_len,
			"%u ", interrupt_button);
		/* bump up ptr to next location in buf if the
		 * snprintf was valid.  Otherwise issue an error
		 * and return. */
		if (len > 0) {
			current_buf += len;
			total_len += len;
		} else {
			VIVO_TS_LOG_ERR("[%s]: Failed to build interrupt button"
				" buffer, code = %d.\n", __func__, len);
			return snprintf(buf, PAGE_SIZE, "unknown\n");
		}
	}
	len = snprintf(current_buf, PAGE_SIZE - total_len, "\n");
	if (len > 0)
		total_len += len;
	else
		VIVO_TS_LOG_ERR("[%s]: Failed to append carriage return.\n",
			 __func__);
	return total_len;

}

static ssize_t rmi_f1a_button_int_enable_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;
	int i;
	int button_count = 0;
	int retval = count;
	int button_reg = 0;
	u8 *new_value;
	fc = to_rmi_function_container(dev);
	data = fc->data;
	new_value = kzalloc(data->button_bitmask_size, GFP_KERNEL);
	if (!new_value) {
		VIVO_TS_LOG_ERR("[%s]: Error - failed to allocate button interrupt enable.\n", __func__);
		return -ENOMEM;
	}
	for (i = 0; i < data->button_count && *buf != 0;
	     i++, buf += 2) {
		int button_shift;
		int button;
		int result;

		button_reg = i / 8;
		button_shift = i % 8;
		/* get next button mapping value and store and bump up to
		 * point to next item in buf */
		result = sscanf(buf, "%u", &button);

		if ((result != 1) || (button != 0 && button != 1)) {
			VIVO_TS_LOG_ERR("[%s]: Error - interrupt enable button for"
				"button %d is not a valid value 0x%x.\n",
				__func__, i, button);
			kfree(new_value);
			return -EINVAL;
		}
		if (button)
			new_value[button_reg] |= (1 << button_shift);

		button_count++;
	}

	/* Make sure the button count matches */
	if (button_count != data->button_count) {
		VIVO_TS_LOG_ERR("[%s]: Error - interrupt enable button count of %d"
			" doesn't match device button count of %d.\n",
			 __func__, button_count, data->button_count);
		kfree(new_value);
		return -EINVAL;
	}
	/* write back to the control register */
	retval = rmi_write_block(fc->rmi_dev,
			data->button_int_enable_address,
			new_value, data->button_bitmask_size);
	if (retval < 0) {
		VIVO_TS_LOG_ERR("[%s]: Could not write interrupt_enable_store to 0x%x\n",
			__func__, data->button_int_enable_address);
		return retval;
	}
	memcpy(data->button_control.button_int_enable,
		new_value, data->button_bitmask_size);

	kfree(new_value);
	return count;
}

static ssize_t rmi_f1a_multibutton_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;
	int i, len, total_len = 0;
	char *current_buf = buf;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	/* loop through each button map value and copy its
	 * string representation into buf */
	for (i = 0; i < data->button_count; i++) {
		int button_reg;
		int button_shift;
		int multibutton;

		button_reg = i / 8;
		button_shift = i % 8;
		multibutton = ((data->button_control.
			multi_button[button_reg] >> button_shift) & 0x01);

		/* get next button mapping value and write it to buf */
		len = snprintf(current_buf, PAGE_SIZE - total_len,
			"%u ", multibutton);
		/* bump up ptr to next location in buf if the
		 * snprintf was valid.  Otherwise issue an error
		 * and return. */
		if (len > 0) {
			current_buf += len;
			total_len += len;
		} else {
			VIVO_TS_LOG_ERR("[%s]: Failed to build multibutton buffer"
				", code = %d.\n", __func__, len);
			return snprintf(buf, PAGE_SIZE, "unknown\n");
		}
	}
	len = snprintf(current_buf, PAGE_SIZE - total_len, "\n");
	if (len > 0)
		total_len += len;
	else
		VIVO_TS_LOG_ERR("[%s]: Failed to append carriage return.\n",
			 __func__);

	return total_len;

}

static ssize_t rmi_f1a_multibutton_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;
	int i;
	int button_count = 0;
	int retval = count;
	int button_reg = 0;
	u8 *new_value;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	new_value = kzalloc(data->button_bitmask_size, GFP_KERNEL);
	if (!new_value) {
		VIVO_TS_LOG_ERR("[%s]: Error - failed to allocate multi button.\n", __func__);
		return -ENOMEM;
	}
	for (i = 0; i < data->button_count && *buf != 0;
	     i++, buf += 2) {
		int button_shift;
		int button;
		int result;

		button_reg = i / 8;
		button_shift = i % 8;
		/* get next button mapping value and store and bump up to
		 * point to next item in buf */
		result = sscanf(buf, "%u", &button);

		if ((result < 1) || (button < 0 || button > 1)) {
			VIVO_TS_LOG_ERR("[%s]: Error - multibutton for button %d"
				" is not a valid value 0x%x.\n",
				__func__, i, button);
			kfree(new_value);
			return -EINVAL;
		}

		if (button)
			new_value[button_reg] |= (1 << button_shift);

		button_count++;
	}

	/* Make sure the button count matches */
	if (button_count != data->button_count) {
		VIVO_TS_LOG_ERR("[%s]: Error - multibutton count of %d doesn't match"
		     " device button count of %d.\n", __func__, button_count,
		     data->button_count);
		kfree(new_value);
		return -EINVAL;
	}
	/* write back to the control register */
	retval = rmi_write_block(fc->rmi_dev, data->multi_button_address,
			new_value,
			data->button_bitmask_size);
	if (retval < 0) {
		VIVO_TS_LOG_ERR("[%s]: Could not write multibutton_store to"
			" 0x%x\n", __func__, data->multi_button_address);
		kfree(new_value);
		return -EINVAL;
	}
	memcpy(data->button_control.multi_button, new_value,
		data->button_bitmask_size);
	kfree(new_value);
	return count;
}

static ssize_t rmi_f1a_electrode_map_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;
	int i, len, total_len = 0;
	char *current_buf = buf;
	struct f1a_0d_control *button_control;
	fc = to_rmi_function_container(dev);
	data = fc->data;
	button_control = &data->button_control;
	for (i = 0; i < data->button_count; i++) {
		len = snprintf(current_buf, PAGE_SIZE - total_len, "%u:%u ",
			button_control->electrode_map[i].transmitterbutton,
			button_control->electrode_map[i].receiverbutton);
		/* bump up ptr to next location in buf if the
		 * snprintf was valid.  Otherwise issue an error
		 * and return. */
		if (len > 0) {
			current_buf += len;
			total_len += len;
		} else {
			VIVO_TS_LOG_ERR("[%s]: Failed to build electrode map buffer, code = %d.\n",
				__func__, len);
			return snprintf(buf, PAGE_SIZE, "unknown\n");
		}
	}
	len = snprintf(current_buf, PAGE_SIZE - total_len, "\n");
	if (len > 0)
		total_len += len;
	else
		VIVO_TS_LOG_ERR("[%s]: Failed to append carriage return.\n",
			 __func__);
	return total_len;


}

static ssize_t rmi_f1a_electrode_map_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;
	char colon;
	int i;
	int result;
	int retval = count;
	int button_count = 0;
	char tx_rx[MAX_BUFFER_LEN];
	struct f1a_0d_control_3_4  *new_value;
	fc = to_rmi_function_container(dev);
	data = fc->data;

	new_value = kzalloc(data->button_count *
			sizeof(struct f1a_0d_control_3_4), GFP_KERNEL);
	if (!new_value) {
		VIVO_TS_LOG_ERR("[%s]: Error - failed to allocate eletrode map.\n", __func__);
		return -ENOMEM;
	}
	for (i = 0; i < data->button_count && *buf != 0; i++) {
		/* get next button mapping value and store and bump up to
		 * point to next item in buf */
		result = sscanf(buf, "%s ", tx_rx);
		if (result < 1) {
			VIVO_TS_LOG_ERR("[%s]: Error - scanf threshold button\n", __func__);
			kfree(new_value);
			return -EINVAL;
		}
		result = sscanf(tx_rx, "%u%c%u",
			(unsigned int *)&new_value[i].transmitterbutton,
			&colon,
			(unsigned int *)&new_value[i].receiverbutton);
		buf += strlen(tx_rx)+1;
		/* Make sure the key is a valid key */
		if ((result < 3) || (colon != ':') ||
			(new_value[i].transmitterbutton < TX_RX_BUTTON_MIN ||
			new_value[i].transmitterbutton > TX_RX_BUTTON_MAX) ||
			(new_value[i].receiverbutton < TX_RX_BUTTON_MIN ||
			new_value[i].receiverbutton > TX_RX_BUTTON_MAX)) {
			VIVO_TS_LOG_ERR("[%s]: Error - electrode map for button %d "
				"is not a valid value 0x%x:0x%x.\n",
				__func__, i, new_value[i].transmitterbutton,
				new_value[i].receiverbutton);
			kfree(new_value);
			return -EINVAL;
		}
		button_count++;
	}

	if (button_count != data->button_count) {
		VIVO_TS_LOG_ERR("[%s]: Error - button map count of %d doesn't match device "
		     "button count of %d.\n", __func__, button_count,
		     data->button_count);
		kfree(new_value);
		return -EINVAL;
	}
	/* write back to the control register */
	retval = rmi_write_block(fc->rmi_dev, data->electrode_map_address,
			(u8 *)new_value,
			sizeof(struct f1a_0d_control_3_4)*button_count);
	if (retval < 0) {
		VIVO_TS_LOG_ERR("[%s]: Could not transmitter_btn_store to 0x%x\n",
				__func__,  data->electrode_map_address);
		return -EINVAL;
		kfree(new_value);
	}
	for (i = 0; i < data->button_count; i++) {
		data->button_control.electrode_map[i].transmitterbutton =
			new_value[i].transmitterbutton;
		data->button_control.electrode_map[i].receiverbutton =
			new_value[i].receiverbutton;
	}
	kfree(new_value);
	return count;
}

static ssize_t rmi_f1a_threshold_button_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;
	int i, len, total_len = 0;
	char *current_buf = buf;

	fc = to_rmi_function_container(dev);
	data = fc->data;

	for (i = 0; i < data->button_count; i++) {
		len = snprintf(current_buf, PAGE_SIZE - total_len, "%u ",
			data->button_control.button_threshold[i]);
		/* bump up ptr to next location in buf if the
		 * snprintf was valid.  Otherwise issue an error
		 * and return. */
		if (len > 0) {
			current_buf += len;
			total_len += len;
		} else {
			VIVO_TS_LOG_ERR("[%s]: Failed to build threshold_button, "
				"code = %d.\n", __func__, len);
			return snprintf(buf, PAGE_SIZE, "unknown\n");
		}
	}
	len = snprintf(current_buf, PAGE_SIZE - total_len, "\n");
	if (len > 0)
		total_len += len;
	else
		VIVO_TS_LOG_ERR("[%s]: Failed to append carriage return.\n",
			 __func__);
	return total_len;


}

static ssize_t rmi_f1a_threshold_button_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;
	int i;
	int result;
	int retval = count;
	int button_count = 0;
	char threshold_button[MAX_BUFFER_LEN];
	u8 *new_value;
	fc = to_rmi_function_container(dev);
	data = fc->data;
	new_value = kzalloc(data->button_count, GFP_KERNEL);
	if (!new_value) {
		VIVO_TS_LOG_ERR("[%s]: Error - failed to allocate threshold button.\n", __func__);
		return -ENOMEM;
	}

	for (i = 0; i < data->button_count && *buf != 0; i++) {
		/* get next button mapping value and store and bump up to
		 * point to next item in buf */
		result = sscanf(buf, "%s ", threshold_button);
		if (result < 1) {
			VIVO_TS_LOG_ERR("[%s]: Error - scanf threshold button\n", __func__);
			kfree(new_value);
			return -EINVAL;
		}
		result = sscanf(threshold_button, "%u",
				(unsigned int *)&new_value[i]);
		buf += strlen(threshold_button)+1;
		/* Make sure the key is a valid key */
		if ((result < 1) ||
			(new_value[i] < THREADHOLD_BUTTON_MIN ||
			new_value[i] > THREADHOLD_BUTTON_MAX)) {
			VIVO_TS_LOG_ERR("[%s]: Error - threshold_button for button %d "
				"is not a valid value 0x%x.\n",
				__func__, i, new_value[i]);
			kfree(new_value);
			return -EINVAL;
		}
		button_count++;
	}

	if (button_count != data->button_count) {
		VIVO_TS_LOG_ERR("[%s]: Error - button map count of %d doesn't match device "
		     "button count of %d.\n", __func__, button_count,
		     data->button_count);
		kfree(new_value);
		return -EINVAL;
	}

	/* write back to the control register */
	retval = rmi_write_block(fc->rmi_dev, data->button_threshold_address,
		new_value, data->button_count);
	if (retval < 0) {
		VIVO_TS_LOG_ERR("[%s]: Could not threshold_button_store to 0x%x\n",
				__func__,  data->button_threshold_address);
		kfree(new_value);
		return -EINVAL;
	}
	memcpy(data->button_control.button_threshold, new_value,
		data->button_count);
	kfree(new_value);
	return count;
}


static ssize_t rmi_f1a_button_release_threshold_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;

	return snprintf(buf, PAGE_SIZE, "%u\n", data->button_control.
		button_release_threshold);

}
static ssize_t rmi_f1a_button_release_threshold_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;
	u8 new_value;
	u8 old_value;
	int len;

	fc = to_rmi_function_container(dev);

	data = fc->data;

	len = sscanf(buf, "%u", (unsigned int *)&new_value);
	if ((len < 1) ||
		(new_value < RELEASE_THREADHOLD_BUTTON_MIN ||
		new_value > RELEASE_THREADHOLD_BUTTON_MAX)) {
		VIVO_TS_LOG_ERR("[%s]: Error - release threshold_button "
			"is not a valid value 0x%x.\n",
			__func__, new_value);
		return -EINVAL;
	}
	old_value = data->button_control.button_release_threshold;
	data->button_control.button_release_threshold = new_value;
	/* write back to the control register */
	len = rmi_write_block(fc->rmi_dev,
		data->button_release_threshold_address,
		(u8 *)&(data->button_control.button_release_threshold),
			sizeof(u8));
	if (len < 0) {
		data->button_control.button_release_threshold = old_value;
		VIVO_TS_LOG_ERR("[%s]: Could not button_release_threshold_store to"
			" 0x%x\n", __func__,
			data->button_release_threshold_address);
		return len;
	}

	return count;
}

static ssize_t rmi_f1a_strongest_button_hysteresis_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;

	return snprintf(buf, PAGE_SIZE, "%u\n", data->button_control.
		strongest_button_hysteresis);

}
static ssize_t rmi_f1a_strongest_button_hysteresis_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;
	u8 new_value;
	u8 old_value;
	int len;

	fc = to_rmi_function_container(dev);

	data = fc->data;

	len = sscanf(buf, "%u", (unsigned int *)&new_value);
	if ((len < 1) ||
		(new_value < STRONGEST_BUTTON_HYSTERESIS_MIN ||
		new_value > STRONGEST_BUTTON_HYSTERESIS_MAX)) {
		VIVO_TS_LOG_ERR("[%s]: Error - strongest button hysteresis is not a valid value 0x%x.\n",
			__func__, new_value);
		return -EINVAL;
	}
	old_value = data->button_control.strongest_button_hysteresis;
	data->button_control.strongest_button_hysteresis = new_value;
	/* write back to the control register */
	len = rmi_write_block(fc->rmi_dev,
		data->strongest_button_hysteresis_address,
		(u8 *)&(data->button_control.strongest_button_hysteresis),
			sizeof(u8));
	if (len < 0) {
		data->button_control.strongest_button_hysteresis = old_value;
		VIVO_TS_LOG_ERR("[%s]: Could not strongest_button_hysteresis_store"
			" to 0x%x\n", __func__,
			data->strongest_button_hysteresis_address);
		return len;
	}

	return count;
}

static ssize_t rmi_f1a_filter_strength_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;

	return snprintf(buf, PAGE_SIZE, "%u\n", data->button_control.
		filter_strength);

}

static ssize_t rmi_f1a_filter_strength_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;
	int new_value;
	int old_value;
	int len;

	fc = to_rmi_function_container(dev);

	data = fc->data;

	len = sscanf(buf, "%u", &new_value);
	if ((len < 1) ||
		(new_value < FILTER_STRENGTH_MIN ||
		new_value > FILTER_STRENGTH_MAX)) {
		VIVO_TS_LOG_ERR("[%s]: Error - filter strength button is not a valid value 0x%x.\n",
			__func__, new_value);
		return -EINVAL;
	}
	old_value = data->button_control.filter_strength;
	data->button_control.filter_strength = new_value;
	/* write back to the control register */
	len = rmi_write_block(fc->rmi_dev,
		data->filter_strength_address,
		(u8 *)&(data->button_control.filter_strength),
			sizeof(u8));
	if (len < 0) {
		data->button_control.filter_strength = old_value;
		VIVO_TS_LOG_ERR("[%s]: Could not filter_strength_store to 0x%x\n", __func__,
			data->filter_strength_address);
		return len;
	}

	return count;
}


static ssize_t rmi_f1a_button_count_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	return snprintf(buf, PAGE_SIZE, "%u\n",
			data->button_query.max_button_count);
}

static ssize_t rmi_f1a_has_general_control_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	return snprintf(buf, PAGE_SIZE, "%u\n",
			data->button_query.has_general_control);
}

static ssize_t rmi_f1a_has_interrupt_enable_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	return snprintf(buf, PAGE_SIZE, "%u\n",
			data->button_query.has_interrupt_enable);
}

static ssize_t rmi_f1a_has_multibutton_select_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	return snprintf(buf, PAGE_SIZE, "%u\n",
			data->button_query.has_multibutton_select);
}

static ssize_t rmi_f1a_has_tx_rx_map_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	return snprintf(buf, PAGE_SIZE, "%u\n",
			data->button_query.has_tx_rx_map);
}

static ssize_t rmi_f1a_has_perbutton_threshold_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	return snprintf(buf, PAGE_SIZE, "%u\n",
			data->button_query.has_perbutton_threshold);
}

static ssize_t rmi_f1a_has_release_threshold_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	return snprintf(buf, PAGE_SIZE, "%u\n",
			data->button_query.has_release_threshold);
}

static ssize_t rmi_f1a_has_strongestbtn_hysteresis_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	return snprintf(buf, PAGE_SIZE, "%u\n",
			data->button_query.has_strongestbtn_hysteresis);
}

static ssize_t rmi_f1a_has_filter_strength_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	return snprintf(buf, PAGE_SIZE, "%u\n",
			data->button_query.has_filter_strength);
}

static ssize_t rmi_f1a_button_map_show(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{

	struct rmi_function_container *fc;
	struct f1a_data *data;
	int i, len, total_len = 0;
	char *current_buf = buf;

	fc = to_rmi_function_container(dev);
	data = fc->data;
	/* loop through each button map value and copy its
	 * string representation into buf */
	for (i = 0; i < data->button_count; i++) {
		/* get next button mapping value and write it to buf */
		len = snprintf(current_buf, PAGE_SIZE - total_len,
			"%u ", data->button_map[i]);
		/* bump up ptr to next location in buf if the
		 * snprintf was valid.  Otherwise issue an error
		 * and return. */
		if (len > 0) {
			current_buf += len;
			total_len += len;
		} else {
			VIVO_TS_LOG_ERR("[%s]: Failed to build button map buffer, "
				"code = %d.\n", __func__, len);
			return snprintf(buf, PAGE_SIZE, "unknown\n");
		}
	}
	len = snprintf(current_buf, PAGE_SIZE - total_len, "\n");
	if (len > 0)
		total_len += len;
	else
		VIVO_TS_LOG_ERR("[%s]: Failed to append carriage return.\n",
			 __func__);
	return total_len;
}

static ssize_t rmi_f1a_button_map_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf,
				size_t count)
{
	struct rmi_function_container *fc;
	struct f1a_data *data;
	unsigned int button;
	int i;
	int retval = count;
	int button_count = 0;
	unsigned char temp_button_map[KEY_MAX];

	fc = to_rmi_function_container(dev);
	data = fc->data;

	/* Do validation on the button map data passed in.  Store button
	 * mappings into a temp buffer and then verify button count and
	 * data prior to clearing out old button mappings and storing the
	 * new ones. */
	for (i = 0; i < data->button_count && *buf != 0;
	     i++) {
		/* get next button mapping value and store and bump up to
		 * point to next item in buf */
		sscanf(buf, "%u", &button);

		/* Make sure the key is a valid key */
		if (button > KEY_MAX) {
			VIVO_TS_LOG_ERR("[%s]: Error - button map for button %d is not a"
				" valid value 0x%x.\n", __func__, i, button);
			retval = -EINVAL;
			goto err_ret;
		}

		temp_button_map[i] = button;
		button_count++;

		/* bump up buf to point to next item to read */
		while (*buf != 0) {
			buf++;
			if (*(buf - 1) == ' ')
				break;
		}
	}

	/* Make sure the button count matches */
	if (button_count != data->button_count) {
		VIVO_TS_LOG_ERR("[%s]: Error - button map count of %d doesn't match device "
		     "button count of %d.\n", __func__, button_count,
		     data->button_count);
		retval = -EINVAL;
		goto err_ret;
	}

	/* Clear the key bits for the old button map. */
	for (i = 0; i < button_count; i++)
		clear_bit(data->button_map[i], data->input->keybit);

	/* Switch to the new map. */
	memcpy(data->button_map, temp_button_map,
	       data->button_count);

	/* Loop through the key map and set the key bit for the new mapping. */
	for (i = 0; i < button_count; i++)
		set_bit(data->button_map[i], data->input->keybit);

err_ret:
	return retval;
}

module_init(rmi_f1a_module_init);
module_exit(rmi_f1a_module_exit);

MODULE_AUTHOR("Vivian Ly <vly@synaptics.com>");
MODULE_DESCRIPTION("RMI F1a module");
MODULE_LICENSE("GPL");
MODULE_VERSION(RMI_DRIVER_VERSION);
