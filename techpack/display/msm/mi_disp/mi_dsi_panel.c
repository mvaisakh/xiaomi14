/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020 XiaoMi, Inc. All rights reserved.
 */

#define pr_fmt(fmt)	"mi-dsi-panel:[%s:%d] " fmt, __func__, __LINE__
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/rtc.h>
#include <linux/pm_wakeup.h>
#include <video/mipi_display.h>

#include "dsi_panel.h"
#include "dsi_display.h"
#include "dsi_ctrl_hw.h"
#include "dsi_parser.h"
#include "internals.h"
#include "sde_connector.h"
#include "sde_trace.h"
#include "mi_dsi_panel.h"
#include "mi_disp_feature.h"
#include "mi_disp_print.h"
#include "mi_disp_parser.h"
#include "mi_dsi_display.h"
#include "mi_disp_lhbm.h"
#include "mi_panel_id.h"
#include "mi_disp_nvt_alpha_data.h"

#define to_dsi_display(x) container_of(x, struct dsi_display, host)

static int mi_dsi_update_51_mipi_cmd(struct dsi_panel *panel,
			enum dsi_cmd_set_type type, int bl_lvl);
void mi_dsi_update_backlight_in_aod(struct dsi_panel *panel,
			bool restore_backlight);

static u64 g_panel_id[MI_DISP_MAX];

typedef int (*mi_display_pwrkey_callback)(int);
extern void mi_display_pwrkey_callback_set(mi_display_pwrkey_callback);

static int mi_panel_id_init(struct dsi_panel *panel)
{
	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	g_panel_id[mi_get_disp_id(panel->type)] = panel->mi_cfg.mi_panel_id;

	return 0;
}

enum mi_project_panel_id mi_get_panel_id_by_dsi_panel(struct dsi_panel *panel)
{
	if (!panel) {
		DISP_ERROR("invalid params\n");
		return PANEL_ID_INVALID;
	}

	return mi_get_panel_id(panel->mi_cfg.mi_panel_id);
}

enum mi_project_panel_id mi_get_panel_id_by_disp_id(int disp_id)
{
	if (!is_support_disp_id(disp_id)) {
		DISP_ERROR("Unsupported display id\n");
		return PANEL_ID_INVALID;
	}

	return mi_get_panel_id(g_panel_id[disp_id]);
}

int mi_dsi_panel_init(struct dsi_panel *panel)
{
	struct mi_dsi_panel_cfg *mi_cfg = NULL;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;

	mi_cfg->dsi_panel = panel;
	mutex_init(&mi_cfg->doze_lock);

	mi_cfg->disp_wakelock = wakeup_source_register(NULL, "disp_wakelock");
	if (!mi_cfg->disp_wakelock) {
		DISP_ERROR("doze_wakelock wake_source register failed");
		return -ENOMEM;
	}

	mi_dsi_panel_parse_config(panel);
	mi_panel_id_init(panel);
	atomic_set(&mi_cfg->brightness_clone, 0);
	if ((mi_get_panel_id_by_dsi_panel(panel) == N3_PANEL_PA) ||
		(mi_get_panel_id_by_dsi_panel(panel) == N2_PANEL_PA)){
		mi_display_pwrkey_callback_set(mi_display_powerkey_callback);
	}
	return 0;
}

int mi_dsi_panel_deinit(struct dsi_panel *panel)
{
	struct mi_dsi_panel_cfg *mi_cfg = NULL;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;

	if (mi_cfg->disp_wakelock) {
		wakeup_source_unregister(mi_cfg->disp_wakelock);
	}
	return 0;
}

int mi_dsi_acquire_wakelock(struct dsi_panel *panel)
{
	struct mi_dsi_panel_cfg *mi_cfg = NULL;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;

	if (mi_cfg->disp_wakelock) {
		__pm_stay_awake(mi_cfg->disp_wakelock);
	}

	return 0;
}

int mi_dsi_release_wakelock(struct dsi_panel *panel)
{
	struct mi_dsi_panel_cfg *mi_cfg = NULL;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;

	if (mi_cfg->disp_wakelock) {
		__pm_relax(mi_cfg->disp_wakelock);
	}

	return 0;

}

bool is_aod_and_panel_initialized(struct dsi_panel *panel)
{
	if ((panel->power_mode == SDE_MODE_DPMS_LP1 ||
		panel->power_mode == SDE_MODE_DPMS_LP2) &&
		dsi_panel_initialized(panel)){
		return true;
	} else {
		return false;
	}
}

int mi_dsi_panel_esd_irq_ctrl(struct dsi_panel *panel,
				bool enable)
{
	int ret  = 0;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	ret = mi_dsi_panel_esd_irq_ctrl_locked(panel, enable);
	mutex_unlock(&panel->panel_lock);

	return ret;
}

int mi_dsi_panel_esd_irq_ctrl_locked(struct dsi_panel *panel,
				bool enable)
{
	struct mi_dsi_panel_cfg *mi_cfg;
	struct irq_desc *desc;

	if (!panel || !panel->panel_initialized) {
		DISP_ERROR("Panel not ready!\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;
	if (gpio_is_valid(mi_cfg->esd_err_irq_gpio)) {
		if (mi_cfg->esd_err_irq) {
			if (enable) {
				if (!mi_cfg->esd_err_enabled) {
					desc = irq_to_desc(mi_cfg->esd_err_irq);
					if (!irq_settings_is_level(desc))
						desc->istate &= ~IRQS_PENDING;
					enable_irq_wake(mi_cfg->esd_err_irq);
					enable_irq(mi_cfg->esd_err_irq);
					mi_cfg->esd_err_enabled = true;
					DISP_INFO("[%s] esd irq is enable\n", panel->type);
				}
			} else {
				if (mi_cfg->esd_err_enabled) {
					disable_irq_wake(mi_cfg->esd_err_irq);
					disable_irq_nosync(mi_cfg->esd_err_irq);
					mi_cfg->esd_err_enabled = false;
					DISP_INFO("[%s] esd irq is disable\n", panel->type);
				}
			}
		}
	} else {
		DISP_INFO("[%s] esd irq gpio invalid\n", panel->type);
	}

	return 0;
}

static void mi_disp_set_dimming_delayed_work_handler(struct kthread_work *work)
{
	struct disp_delayed_work *delayed_work = container_of(work,
					struct disp_delayed_work, delayed_work.work);
	struct dsi_panel *panel = (struct dsi_panel *)(delayed_work->data);
	struct disp_feature_ctl ctl;

	memset(&ctl, 0, sizeof(struct disp_feature_ctl));
	ctl.feature_id = DISP_FEATURE_DIMMING;
	ctl.feature_val = FEATURE_ON;

	DISP_INFO("[%s] panel set backlight dimming on\n", panel->type);
	mi_dsi_acquire_wakelock(panel);
	mi_dsi_panel_set_disp_param(panel, &ctl);
	mi_dsi_release_wakelock(panel);

	kfree(delayed_work);
}

int mi_dsi_panel_tigger_dimming_delayed_work(struct dsi_panel *panel)
{
	int disp_id = 0;
	struct disp_feature *df = mi_get_disp_feature();
	struct disp_display *dd_ptr;
	struct disp_delayed_work *delayed_work;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	delayed_work = kzalloc(sizeof(*delayed_work), GFP_KERNEL);
	if (!delayed_work) {
		DISP_ERROR("failed to allocate delayed_work buffer\n");
		return -ENOMEM;
	}

	disp_id = mi_get_disp_id(panel->type);
	dd_ptr = &df->d_display[disp_id];

	kthread_init_delayed_work(&delayed_work->delayed_work,
			mi_disp_set_dimming_delayed_work_handler);
	delayed_work->dd_ptr = dd_ptr;
	delayed_work->wq = &dd_ptr->pending_wq;
	delayed_work->data = panel;

	return kthread_queue_delayed_work(dd_ptr->worker, &delayed_work->delayed_work,
				msecs_to_jiffies(panel->mi_cfg.panel_on_dimming_delay));
}


static void mi_disp_timming_switch_delayed_work_handler(struct kthread_work *work)
{
	struct disp_delayed_work *delayed_work = container_of(work,
					struct disp_delayed_work, delayed_work.work);
	struct dsi_panel *dsi_panel = (struct dsi_panel *)(delayed_work->data);

	mi_dsi_acquire_wakelock(dsi_panel);

	dsi_panel_acquire_panel_lock(dsi_panel);
	if (dsi_panel->panel_initialized) {
		dsi_panel_tx_cmd_set(dsi_panel, DSI_CMD_SET_MI_FRAME_SWITCH_MODE_SEC);
		DISP_INFO("DSI_CMD_SET_MI_FRAME_SWITCH_MODE_SEC\n");
	} else {
		DISP_ERROR("Panel not initialized, don't send DSI_CMD_SET_MI_FRAME_SWITCH_MODE_SEC\n");
	}
	dsi_panel_release_panel_lock(dsi_panel);

	mi_dsi_release_wakelock(dsi_panel);

	kfree(delayed_work);
}

int mi_dsi_panel_tigger_timming_switch_delayed_work(struct dsi_panel *panel)
{
	int disp_id = 0;
	struct disp_feature *df = mi_get_disp_feature();
	struct disp_display *dd_ptr;
	struct disp_delayed_work *delayed_work;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	delayed_work = kzalloc(sizeof(*delayed_work), GFP_KERNEL);
	if (!delayed_work) {
		DISP_ERROR("failed to allocate delayed_work buffer\n");
		return -ENOMEM;
	}

	disp_id = mi_get_disp_id(panel->type);
	dd_ptr = &df->d_display[disp_id];

	kthread_init_delayed_work(&delayed_work->delayed_work,
			mi_disp_timming_switch_delayed_work_handler);
	delayed_work->dd_ptr = dd_ptr;
	delayed_work->wq = &dd_ptr->pending_wq;
	delayed_work->data = panel;
	return kthread_queue_delayed_work(dd_ptr->worker, &delayed_work->delayed_work,
				msecs_to_jiffies(20));
}

int mi_dsi_update_switch_cmd_N3(struct dsi_panel *panel, u32 cmd_update_index, u32 index)
{
	struct dsi_cmd_update_info *info = NULL;
	struct dsi_display_mode *cur_mode = NULL;
	int i = 0;
	u8 dc_6C_cfg = 0;
	u8  cmd_update_count = 0;

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	cur_mode = panel->cur_mode;

	if (panel->mi_cfg.real_dc_state == FEATURE_ON) {
		dc_6C_cfg = cur_mode->priv_info->dc_on_6Creg;
	} else {
		dc_6C_cfg = cur_mode->priv_info->dc_off_6Creg;
	}

	info = cur_mode->priv_info->cmd_update[cmd_update_index];
	cmd_update_count = cur_mode->priv_info->cmd_update_count[cmd_update_index];

	for (i = 0; i < cmd_update_count; i++ ) {
		DISP_DEBUG("[%s] update [%s] mipi_address(0x%02X) index(%d) lenght(%d)\n",
			panel->type, cmd_set_prop_map[info->type],
			info->mipi_address, info->index, info->length);
		if (info && info->mipi_address == 0x6C) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode,
				index, info, &dc_6C_cfg, sizeof(dc_6C_cfg));
		}
		info++;
	}

	return 0;
}

int mi_dsi_set_switch_cmd_before(struct dsi_panel *panel, int fps_mode)
{
	int rc = 0;
	int last_fps = 0;
	int last_fps_mode = 0;
	int cur_time_h_skew = 0;
	struct mi_dsi_panel_cfg *mi_cfg  = NULL;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}
	DISP_DEBUG("[%s] DSI_CMD_SET_MI_TIMING_SWITCH BEFOR, fps=%d\n",
		       panel->name, last_fps);
	mi_cfg = &panel->mi_cfg;
	last_fps = mi_cfg->last_refresh_rate;
	last_fps_mode = mi_cfg->last_fps_mode;
	cur_time_h_skew = panel->cur_mode->timing.h_skew;

	if ((last_fps == 10 && (fps_mode & FPS_MODE_AUTO))
		|| ((fps_mode & FPS_MODE_IDLE) && ((cur_time_h_skew & FPS_VALUE_MASK) == 10)
		&& (last_fps_mode & FPS_MODE_AUTO))) {
		DSI_DEBUG("[%s] N3 switch 10hz to auto 120hz remove delay cmd, rc=%d\n",
		       panel->name, rc);
		return rc;
	}

	if (last_fps == 120 && ((last_fps_mode & FPS_MODE_AUTO) || (last_fps_mode & FPS_MODE_QSYNC))) {
		mi_dsi_update_switch_cmd_N3(panel, DSI_CMD_SET_TIMING_SWITCH_FROM_AUTO_UPDATA,
					DSI_CMD_SET_MI_TIMING_SWITCH_FROM_AUTO);
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_TIMING_SWITCH_FROM_AUTO);
	} else if (last_fps >= 90) {
		mi_dsi_update_switch_cmd_N3(panel, DSI_CMD_SET_TIMING_SWITCH_FROM_NORMAL_UPDATA,
					DSI_CMD_SET_MI_TIMING_SWITCH_FROM_NORMAL);
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_TIMING_SWITCH_FROM_NORMAL);
	} else if (last_fps >= 24) {
		mi_dsi_update_switch_cmd_N3(panel, DSI_CMD_SET_TIMING_SWITCH_FROM_SKIP_UPDATA,
					DSI_CMD_SET_MI_TIMING_SWITCH_FROM_SKIP);
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_TIMING_SWITCH_FROM_SKIP);
	} else if (last_fps >= 1) {
		mi_dsi_update_switch_cmd_N3(panel, DSI_CMD_SET_TIMING_SWITCH_FROM_AUTO_UPDATA,
					DSI_CMD_SET_MI_TIMING_SWITCH_FROM_AUTO);
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_TIMING_SWITCH_FROM_AUTO);
	}
	if (rc)
		DSI_INFO("[%s] N3 failed to send DSI_CMD_SET_MI_TIMING_SWITCH BEFOR cmds, rc=%d\n",
		       panel->name, rc);

	return rc;
}

static inline bool mi_dsi_update_BA_reg(struct dsi_panel *panel, u8 *val, u8 size)
{
	u32 last_framerate = 0;
	u32 cur_framerate = 0;
	u32 fps_mode = 0;
	struct mi_dsi_panel_cfg *mi_cfg  = NULL;
	bool ret = false;
	u8 *framerate_val_ptr = NULL;

	u8 switch_framerate_40HZ_cfg[2][9] = {
		/*120HZ switch to 40HZ*/
		{0x91,0x01,0x01,0x00,0x01,0x02,0x02,0x00,0x00},
		/*60/30/24/1HZ switch to 40HZ*/
		{0x91,0x02,0x02,0x00,0x01,0x02,0x02,0x00,0x00}
	};
	u8 switch_framerate_30HZ_cfg[2][9] = {
		/*120HZ switch to 30HZ*/
		{0x91,0x01,0x01,0x00,0x01,0x03,0x03,0x00,0x00},
		/*60/30/24/1HZ switch to 30HZ*/
		{0x91,0x03,0x03,0x00,0x01,0x03,0x03,0x00,0x00}
	};
	u8 switch_framerate_24HZ_cfg[3][9] = {
		/*120HZ switch to 24HZ*/
		{0x91,0x03,0x01,0x00,0x11,0x04,0x04,0x00,0x05},
		/*60HZ switch to 24HZ*/
		{0x91,0x03,0x03,0x00,0x11,0x04,0x04,0x00,0x05},
		/*40/30/1HZ switch to 24HZ*/
		{0x91,0x04,0x04,0x00,0x01,0x04,0x04,0x00,0x00}
	};
	u8 switch_framerate_1HZ_cfg[3][9] = {
		/*120HZ switch to 1HZ*/
		{0x91,0x0B,0x01,0x00,0x11,0x77,0x77,0x00,0x05},
		/*60/40HZ switch to 1HZ*/
		{0x91,0x0B,0x03,0x00,0x11,0x77,0x77,0x00,0x05},
		/*30/24HZ switch to 1HZ*/
		{0x91,0x0B,0x0B,0x00,0x11,0x77,0x77,0x00,0x05}
	};

	if (!panel || !panel->cur_mode) {
		DISP_ERROR("invalid params\n");
		ret = false;
		goto exit;
	}

	mi_cfg = &panel->mi_cfg;
	last_framerate = mi_cfg->last_refresh_rate;
	fps_mode = (panel->cur_mode->timing.h_skew >> FPS_MODE_OFFSET) & FPS_MODE_VALUE_MASK;

	if(panel->cur_mode->timing.h_skew == FPS_NORMAL)
		cur_framerate = panel->cur_mode->timing.refresh_rate;
	else if (fps_mode & FPS_MODE_IDLE)
		cur_framerate = panel->cur_mode->timing.h_skew & FPS_VALUE_MASK;
	else if ((fps_mode & FPS_MODE_AUTO) || (fps_mode & FPS_MODE_QSYNC))
		cur_framerate = (panel->cur_mode->timing.h_skew >> FPS_SF_FPS_OFFSET) & FPS_VALUE_MASK;

	DISP_DEBUG("fps mode:%d, last fps:%d, cur_fps:%d\n", fps_mode, last_framerate, cur_framerate);

	switch (cur_framerate) {
		case 120:
		case 90:
		case 60:
			framerate_val_ptr = NULL;
			ret = false;
			break;
		case 40:
			if(last_framerate == 120 || last_framerate == 90)
				framerate_val_ptr = switch_framerate_40HZ_cfg[0];
			else
				framerate_val_ptr = switch_framerate_40HZ_cfg[1];
			break;
		case 30:
			if(last_framerate == 120 || last_framerate == 90)
				framerate_val_ptr = switch_framerate_30HZ_cfg[0];
			else
				framerate_val_ptr = switch_framerate_30HZ_cfg[1];
			break;
		case 24:
			if(last_framerate == 120 || last_framerate == 90)
				framerate_val_ptr = switch_framerate_24HZ_cfg[0];
			else if (last_framerate == 60)
				framerate_val_ptr = switch_framerate_24HZ_cfg[1];
			else
				framerate_val_ptr = switch_framerate_24HZ_cfg[2];
			break;
		case 1:
			if(last_framerate == 120 || last_framerate == 90)
				framerate_val_ptr = switch_framerate_1HZ_cfg[0];
			else if (last_framerate == 60 || last_framerate == 40)
				framerate_val_ptr = switch_framerate_1HZ_cfg[1];
			else
				framerate_val_ptr = switch_framerate_1HZ_cfg[2];
			break;
		default:
			framerate_val_ptr = NULL;
			break;
	}

	if(framerate_val_ptr != NULL) {
		memcpy(val, framerate_val_ptr, size);
		ret = true;
	}

exit:
	return ret;
}

int mi_dsi_update_flat_mode_on_cmd(struct dsi_panel *panel,
		enum dsi_cmd_set_type type)
{
	struct dsi_cmd_update_info *info = NULL;
	struct dsi_display_mode *cur_mode = NULL;
	u8 update_D5_cfg = 0;
	u32 cmd_update_index = 0;
	u8  cmd_update_count = 0;
	int i = 0;

	if (type != DSI_CMD_SET_MI_FLAT_MODE_ON) {
		DISP_ERROR("invalid type parameter\n");
		return -EINVAL;
	}

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	cur_mode = panel->cur_mode;

	cmd_update_index = DSI_CMD_SET_MI_FLAT_MODE_ON_UPDATE;
	info = cur_mode->priv_info->cmd_update[cmd_update_index];
	cmd_update_count = cur_mode->priv_info->cmd_update_count[cmd_update_index];
	for (i = 0; i < cmd_update_count; i++ ) {
		if (info && info->mipi_address == 0xD5) {
			update_D5_cfg = 0x73;
			DISP_INFO("panel id is 0x%x, update_D5_cfg is 0x73", panel->id_config.build_id);
			mi_dsi_panel_update_cmd_set(panel, cur_mode,
				type, info,
				&update_D5_cfg, sizeof(update_D5_cfg));
		}

		info++;
	}

	return 0;
}


int mi_dsi_update_timing_switch_and_flat_mode_cmd(struct dsi_panel *panel,
		enum dsi_cmd_set_type type)
{
	struct dsi_cmd_update_info *info = NULL;
	struct dsi_display_mode *cur_mode = NULL;
	int i = 0;
	u8 gamma_2F_cfg = 0;
	u8 gamma_26_cfg = 0;
	u32 cmd_update_index = 0;
	u8  cmd_update_count = 0;
	bool is_update_BA_reg = false;
	u8 update_BA_reg_val[9] = {0};
	bool flat_mode_enable = false;

	if (mi_get_panel_id_by_dsi_panel(panel) != N2_PANEL_PA) {
		DISP_DEBUG("this project not need to dynamically update these parameters\n");
		return 0;
	}

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	cur_mode = panel->cur_mode;

	switch (type) {
		case DSI_CMD_SET_TIMING_SWITCH:
			cmd_update_index = DSI_CMD_SET_TIMING_SWITCH_UPDATA;
			flat_mode_enable = panel->mi_cfg.feature_val[DISP_FEATURE_FLAT_MODE];
			is_update_BA_reg = mi_dsi_update_BA_reg(panel, update_BA_reg_val, sizeof(update_BA_reg_val));
			break;
		case DSI_CMD_SET_MI_FLAT_MODE_ON:
			cmd_update_index = DSI_CMD_SET_MI_FLAT_MODE_ON_UPDATE;
			flat_mode_enable = true;
			break;
		case DSI_CMD_SET_MI_FLAT_MODE_OFF:
			cmd_update_index = DSI_CMD_SET_MI_FLAT_MODE_OFF_UPDATE;
			flat_mode_enable = false;
			break;
		case DSI_CMD_SET_MI_DOZE_HBM:
			cmd_update_index = DSI_CMD_SET_MI_DOZE_HBM_UPDATE;
			flat_mode_enable = false;
			break;
		case DSI_CMD_SET_MI_DOZE_LBM:
			cmd_update_index = DSI_CMD_SET_MI_DOZE_LBM_UPDATE;
			flat_mode_enable = false;
			break;
		default:
			DISP_ERROR("[%s] unsupport cmd %s\n",
				panel->type, cmd_set_prop_map[type]);
			return -EINVAL;
	}

	if (flat_mode_enable) {
		gamma_2F_cfg = cur_mode->priv_info->flat_on_2Freg_gamma;
		gamma_26_cfg = cur_mode->priv_info->flat_on_26reg_gamma;
	} else {
		gamma_2F_cfg = cur_mode->priv_info->flat_off_2Freg_gamma;
		gamma_26_cfg = cur_mode->priv_info->flat_off_26reg_gamma;
	}

	DISP_DEBUG("panel id is 0x%x, gamma_2Freg_cfg is 0x%x, gamma_26reg_cfg is 0x%x",
			panel->id_config.build_id, gamma_2F_cfg, gamma_26_cfg);

	info = cur_mode->priv_info->cmd_update[cmd_update_index];
	cmd_update_count = cur_mode->priv_info->cmd_update_count[cmd_update_index];
	for (i = 0; i < cmd_update_count; i++ ) {

		if (info && info->mipi_address == 0x2F) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode,
				type, info,
				&gamma_2F_cfg, sizeof(gamma_2F_cfg));
		}

		if (info && info->mipi_address == 0x26) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode,
				type, info,
				&gamma_26_cfg, sizeof(gamma_26_cfg));
		}

		if (info && info->mipi_address == 0xBA && is_update_BA_reg) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode,
				type, info,
				update_BA_reg_val, sizeof(update_BA_reg_val));
		}

		info++;
	}

	return 0;
}

int mi_dsi_panel_set_gamma_update_reg(struct dsi_panel *panel, u32 bl_lvl)
{
	int rc = 0;
	struct mi_dsi_panel_cfg *mi_cfg  = NULL;
	u32 last_refresh_rate;
	u32 vsync_period_us;
	ktime_t cur_time;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;

	if (mi_cfg->panel_state != PANEL_STATE_ON) {
		DISP_INFO("%s exit when panel state(%d)\n", __func__, mi_cfg->panel_state);
		return 0;
	}

	last_refresh_rate = mi_cfg->last_refresh_rate;
	vsync_period_us = 1000000 / last_refresh_rate;

	if (mi_cfg->nedd_auto_update_gamma) {
		cur_time = ktime_get();
		if (last_refresh_rate && (ktime_to_us(cur_time - mi_cfg->last_mode_switch_time)
				<= vsync_period_us)) {
			DISP_INFO("sleep before update gamma, vsync_period = %d, diff_time = %llu",
					vsync_period_us,
					ktime_to_us(cur_time - mi_cfg->last_mode_switch_time));
			usleep_range(vsync_period_us, vsync_period_us + 10);
		}
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_AUTO_UPDATE_GAMMA);
		DISP_INFO("send auto update gamma cmd\n");
		if (rc)
			DISP_ERROR("failed to set DSI_CMD_SET_MI_AUTO_UPDATE_GAMMA\n");
		else
			mi_cfg->nedd_auto_update_gamma = false;
	}

	return rc;
}

int mi_dsi_panel_set_gamma_update_state(struct dsi_panel *panel)
{
	int rc = 0;
	struct mi_dsi_panel_cfg *mi_cfg  = NULL;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;

	if (mi_cfg->panel_state != PANEL_STATE_ON) {
		DISP_INFO("%s exit when panel state(%d)\n", __func__, mi_cfg->panel_state);
		return 0;
	}

	mutex_lock(&panel->panel_lock);

	mi_cfg->nedd_auto_update_gamma = true;
	DISP_DEBUG("set auto update gamma state to true\n");

	mutex_unlock(&panel->panel_lock);

	return rc;
}

bool is_hbm_fod_on(struct dsi_panel *panel)
{
	struct mi_dsi_panel_cfg *mi_cfg = &panel->mi_cfg;
	int feature_val;
	bool is_fod_on = false;

	feature_val = mi_cfg->feature_val[DISP_FEATURE_LOCAL_HBM];
	switch (feature_val) {
	case LOCAL_HBM_NORMAL_WHITE_1000NIT:
	case LOCAL_HBM_NORMAL_WHITE_750NIT:
	case LOCAL_HBM_NORMAL_WHITE_500NIT:
	case LOCAL_HBM_NORMAL_WHITE_110NIT:
	case LOCAL_HBM_NORMAL_GREEN_500NIT:
	case LOCAL_HBM_HLPM_WHITE_1000NIT:
	case LOCAL_HBM_HLPM_WHITE_110NIT:
		is_fod_on = true;
		break;
	default:
		break;
	}

	if (mi_cfg->feature_val[DISP_FEATURE_HBM_FOD] == FEATURE_ON) {
		is_fod_on = true;
	}

	return is_fod_on;
}

bool is_dc_on_skip_backlight(struct dsi_panel *panel, u32 bl_lvl)
{
	struct mi_dsi_panel_cfg *mi_cfg = &panel->mi_cfg;

	if (!mi_cfg->dc_feature_enable)
		return false;
	if (mi_cfg->feature_val[DISP_FEATURE_DC] == FEATURE_OFF)
		return false;
	if (mi_cfg->dc_type == TYPE_CRC_SKIP_BL && bl_lvl < mi_cfg->dc_threshold)
		return true;
	else
		return false;
}

bool is_backlight_set_skip(struct dsi_panel *panel, u32 bl_lvl)
{
	struct mi_dsi_panel_cfg *mi_cfg = &panel->mi_cfg;

	if (mi_cfg->in_fod_calibration) {
		DISP_INFO("[%s] skip set backlight %d due to fod calibration\n", panel->type, bl_lvl);
		return true;
	} else if (is_hbm_fod_on(panel)) {
		if (bl_lvl != 0) {
			if (mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N3_PANEL_PA && is_hbm_fod_on(panel)) {
				DISP_DEBUG("[%s] skip set backlight %d due to LHBM is on", panel->type, bl_lvl);
				return true;
			}
			DISP_INFO("[%s] update 51 reg to %d even LHBM is on,",
				panel->type, bl_lvl);
			return false;
		} else {
			DISP_INFO("[%s] skip set backlight %d due to fod hbm\n", panel->type, bl_lvl);
		}
		return true;
	} else if (bl_lvl != 0 && is_dc_on_skip_backlight(panel, bl_lvl)) {
		DISP_DEBUG("[%s] skip set backlight %d due to DC on\n", panel->type, bl_lvl);
		return true;
	} else if (panel->power_mode == SDE_MODE_DPMS_LP1) {
		if (bl_lvl == 0) {
			DISP_INFO("[%s] skip set backlight %d due to LP1 on\n", panel->type, bl_lvl);
			return true;
		}
		if (mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N3_PANEL_PA ||
			mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N2_PANEL_PA) {
			DISP_INFO("[%s] skip set backlight %d due to LP1 on\n", panel->type, bl_lvl);
			return true;
		}
		return false;
	} else {
		return false;
	}
}

void mi_dsi_panel_update_last_bl_level(struct dsi_panel *panel, int brightness)
{
	struct mi_dsi_panel_cfg *mi_cfg;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return;
	}

	mi_cfg = &panel->mi_cfg;

	if ((mi_cfg->last_bl_level == 0 || mi_cfg->dimming_state == STATE_DIM_RESTORE) &&
		brightness > 0 && !is_hbm_fod_on(panel) && !mi_cfg->in_fod_calibration) {
		mi_dsi_panel_tigger_dimming_delayed_work(panel);

		if (mi_cfg->dimming_state == STATE_DIM_RESTORE)
			mi_cfg->dimming_state = STATE_NONE;
	}

	mi_cfg->last_bl_level = brightness;
	if (brightness != 0)
		mi_cfg->last_no_zero_bl_level = brightness;

	return;
}

int mi_dsi_print_51_backlight_log(struct dsi_panel *panel,
		struct dsi_cmd_desc *cmd)
{
	u8 *buf = NULL;
	u32 bl_lvl = 0;
	int i = 0;
	struct mi_dsi_panel_cfg *mi_cfg;
	static int use_count = 20;

	if (!panel || !cmd) {
		DISP_ERROR("Invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;
	buf = (u8 *)cmd->msg.tx_buf;
	if (buf && buf[0] == MIPI_DCS_SET_DISPLAY_BRIGHTNESS) {
		if (cmd->msg.tx_len >= 3) {
			if (panel->bl_config.bl_inverted_dbv)
				bl_lvl = (buf[1] << 8) | buf[2];
			else
				bl_lvl = buf[1] | (buf[2] << 8);

			if (use_count-- > 0)
				DISP_TIME_INFO("[%s] set 51 backlight %d\n", panel->type, bl_lvl);

			if (!mi_cfg->last_bl_level || !bl_lvl) {
					use_count = 20;
			}
		}

		if (mi_get_backlight_log_mask() & BACKLIGHT_LOG_ENABLE) {
			DISP_INFO("[%s] [0x51 backlight debug] tx_len = %d\n",
					panel->type, cmd->msg.tx_len);
			for (i = 0; i < cmd->msg.tx_len; i++) {
				DISP_INFO("[%s] [0x51 backlight debug] tx_buf[%d] = 0x%02X\n",
					panel->type, i, buf[i]);
			}

			if (mi_get_backlight_log_mask() & BACKLIGHT_LOG_DUMP_STACK)
				dump_stack();
		}
	}

	return 0;
}

static int mi_dsi_panel_parse_cmd_sets_update_sub(struct dsi_panel *panel,
		struct dsi_display_mode *mode, enum dsi_cmd_set_type type)
{
	int rc = 0;
	int j = 0, k = 0;
	u32 length = 0;
	u32 count = 0;
	u32 size = 0;
	u32 *arr_32 = NULL;
	const u32 *arr;
	struct dsi_parser_utils *utils = &panel->utils;
	struct dsi_cmd_update_info *info;
	int type1 = -1;

	if (!mode || !mode->priv_info) {
		DISP_ERROR("invalid arguments\n");
		return -EINVAL;
	}

	arr = utils->get_property(utils->data, cmd_set_update_map[type],
		&length);

	if (!arr) {
		DISP_DEBUG("[%s] commands not defined\n", cmd_set_update_map[type]);
		return rc;
	}

	if (length & 0x1) {
		DISP_ERROR("[%s] syntax error\n", cmd_set_update_map[type]);
		rc = -EINVAL;
		goto error;
	}

	DISP_INFO("%s length = %d\n", cmd_set_update_map[type], length);

	for (j = DSI_CMD_SET_PRE_ON; j < DSI_CMD_SET_MAX; j++) {
		if (strstr(cmd_set_update_map[type], cmd_set_prop_map[j])) {
			type1 = j;
			DISP_INFO("find type(%d) is [%s] commands\n", type1,
				cmd_set_prop_map[type1]);
			break;
		}
	}
	if (type1 < 0 || j == DSI_CMD_SET_MAX) {
		rc = -EINVAL;
		goto error;
	}

	length = length / sizeof(u32);
	size = length * sizeof(u32);

	arr_32 = kzalloc(size, GFP_KERNEL);
	if (!arr_32) {
		rc = -ENOMEM;
		goto error;
	}

	rc = utils->read_u32_array(utils->data, cmd_set_update_map[type],
						arr_32, length);
	if (rc) {
		DISP_ERROR("[%s] read failed\n", cmd_set_update_map[type]);
		goto error_free_arr_32;
	}

	count = length / 3;
	size = count * sizeof(*info);
	info = kzalloc(size, GFP_KERNEL);
	if (!info) {
		rc = -ENOMEM;
		goto error_free_arr_32;
	}

	mode->priv_info->cmd_update[type] = info;
	mode->priv_info->cmd_update_count[type] = count;

	for (k = 0; k < length; k += 3) {
		info->type = type1;
		info->mipi_address= arr_32[k];
		info->index= arr_32[k + 1];
		info->length= arr_32[k + 2];
		DISP_INFO("update [%s] mipi_address(0x%02X) index(%d) lenght(%d)\n",
			cmd_set_update_map[type], info->mipi_address,
			info->index, info->length);
		info++;
	}
error_free_arr_32:
	kfree(arr_32);
error:
	return rc;
}


int mi_dsi_panel_parse_cmd_sets_update(struct dsi_panel *panel,
		struct dsi_display_mode *mode)
{
	int rc = 0;
	int i = 0;


	if (!mode || !mode->priv_info) {
		DISP_ERROR("invalid arguments\n");
		return -EINVAL;
	}

	DISP_INFO("WxH: %dx%d, FPS: %d\n", mode->timing.h_active,
			mode->timing.v_active, mode->timing.refresh_rate);

	for (i = 0; i < DSI_CMD_UPDATE_MAX; i++) {
		rc = mi_dsi_panel_parse_cmd_sets_update_sub(panel, mode, i);
	}

	return rc;
}

int mi_dsi_panel_update_cmd_set(struct dsi_panel *panel,
			struct dsi_display_mode *mode, enum dsi_cmd_set_type type,
			struct dsi_cmd_update_info *info, u8 *payload, u32 size)
{
	int rc = 0;
	int i = 0;
	struct dsi_cmd_desc *cmds = NULL;
	u32 count;
	u8 *tx_buf = NULL;
	size_t tx_len;

	if (!panel || !mode || !mode->priv_info)
	{
		DISP_ERROR("invalid panel or cur_mode params\n");
		return -EINVAL;
	}

	if (!info || !payload) {
		DISP_ERROR("invalid info or payload params\n");
		return -EINVAL;
	}

	if (type != info->type || size != info->length) {
		DISP_ERROR("please check type(%d, %d) or update size(%d, %d)\n",
			type, info->type, info->length, size);
		return -EINVAL;
	}

	cmds = mode->priv_info->cmd_sets[type].cmds;
	count = mode->priv_info->cmd_sets[type].count;
	if (count == 0) {
		DISP_ERROR("[%s] No commands to be sent\n", cmd_set_prop_map[type]);
		return -EINVAL;
	}

	DISP_DEBUG("WxH: %dx%d, FPS: %d\n", mode->timing.h_active,
			mode->timing.v_active, mode->timing.refresh_rate);

	DISP_DEBUG("[%s] update [%s] mipi_address(0x%02X) index(%d) lenght(%d)\n",
			panel->type, cmd_set_prop_map[info->type],
			info->mipi_address, info->index, info->length);
	for (i = 0; i < size; i++) {
		DISP_DEBUG("[%s] payload[%d] = 0x%02X\n", panel->type, i, payload[i]);
	}

	if (cmds && count >= info->index) {
		tx_buf = (u8 *)cmds[info->index].msg.tx_buf;
		tx_len = cmds[info->index].msg.tx_len;
		if (tx_buf && tx_buf[0] == info->mipi_address && tx_len >= info->length) {
			memcpy(&tx_buf[1], payload, info->length);
			for (i = 0; i < tx_len; i++) {
				DISP_DEBUG("[%s] tx_buf[%d] = 0x%02X\n",
					panel->type, i, tx_buf[i]);
			}
		} else {
			if (tx_buf) {
				DISP_ERROR("[%s] %s mipi address(0x%02X != 0x%02X)\n",
					panel->type, cmd_set_prop_map[type],
					tx_buf[0], info->mipi_address);
			} else {
				DISP_ERROR("[%s] panel tx_buf is NULL pointer\n",
					panel->type);
			}
			rc = -EINVAL;
		}
	} else {
		DISP_ERROR("[%s] panel cmd[%s] index error\n",
			panel->type, cmd_set_prop_map[type]);
		rc = -EINVAL;
	}

	return rc;
}

int mi_dsi_panel_parse_gamma_config(struct dsi_panel *panel,
		struct dsi_display_mode *mode)
{
	int rc;
	struct dsi_display_mode_priv_info *priv_info;
	struct dsi_parser_utils *utils = &panel->utils;
	u32 gamma_26_cfg[2] = {0, 0};
	u32 gamma_c0_cfg[2] = {0, 0};
	u32 gamma_df_cfg[2] = {0, 0};

	priv_info = mode->priv_info;

	rc = utils->read_u32_array(utils->data, "mi,mdss-flat-status-control-gamma-26-cfg",
			gamma_26_cfg, 2);
	if (rc) {
		DISP_DEBUG("mi,mdss-flat-status-control-gamma-26-cfg not defined rc=%d\n", rc);
	} else {
		DISP_INFO("FPS: %d, gamma 26 cfg: 0x%02X, 0x%02X\n",
			mode->timing.refresh_rate, gamma_26_cfg[0], gamma_26_cfg[1]);
		priv_info->flat_on_26reg_gamma = gamma_26_cfg[0];
		priv_info->flat_off_26reg_gamma = gamma_26_cfg[1];
	}

	rc = utils->read_u32_array(utils->data, "mi,mdss-flat-status-control-gamma-C0-cfg",
			gamma_c0_cfg, 2);
	if (rc) {
		DISP_DEBUG("mi,mdss-flat-status-control-gamma-C0-cfg not defined rc=%d\n", rc);
	} else {
		DISP_INFO("FPS: %d, gamma c0 cfg: 0x%02X, 0x%02X\n",
			mode->timing.refresh_rate, gamma_c0_cfg[0], gamma_c0_cfg[1]);
		priv_info->flat_on_C0reg_gamma = gamma_c0_cfg[0];
		priv_info->flat_off_C0reg_gamma = gamma_c0_cfg[1];
	}

	rc = utils->read_u32_array(utils->data, "mi,mdss-flat-status-control-gamma-DF-cfg",
			gamma_df_cfg, 2);
	if (rc) {
		DISP_DEBUG("mi,mdss-flat-status-control-gamma-DF-cfg not defined rc=%d\n", rc);
	} else {
		DISP_INFO("FPS: %d, gamma df cfg: 0x%02X, 0x%02X\n",
			mode->timing.refresh_rate, gamma_df_cfg[0], gamma_df_cfg[1]);
		priv_info->flat_on_DFreg_gamma = gamma_df_cfg[0];
		priv_info->flat_off_DFreg_gamma = gamma_df_cfg[1];
	}

	return 0;
}

int mi_dsi_panel_parse_dc_fps_config(struct dsi_panel *panel,
		struct dsi_display_mode *mode)
{
	int rc;
	struct dsi_display_mode_priv_info *priv_info;
	struct dsi_parser_utils *utils = &panel->utils;
	u32 dc_6c_cfg[2] = {0, 0};

	priv_info = mode->priv_info;

	rc = utils->read_u32_array(utils->data, "mi,mdss-dc-status-control-dc-6C-cfg",
			dc_6c_cfg, 2);
	if (rc) {
		DISP_DEBUG("mi,mdss-dc-status-control-dc-6C-cfg not defined rc=%d\n", rc);
	} else {
		DISP_INFO("FPS: %d, dc 6C cfg: 0x%02X, 0x%02X\n",
			mode->timing.refresh_rate, dc_6c_cfg[0], dc_6c_cfg[1]);
		priv_info->dc_on_6Creg = dc_6c_cfg[0];
		priv_info->dc_off_6Creg = dc_6c_cfg[1];
	}

	return 0;
}


int mi_dsi_panel_parse_2F26reg_gamma_config(struct dsi_panel *panel,
		struct dsi_display_mode *mode)
{
	int rc;
	struct dsi_display_mode_priv_info *priv_info;
	struct dsi_parser_utils *utils = &panel->utils;
	u32 gamma_2f_cfg[2] = {0, 0};
	u32 gamma_26_cfg[2] = {0, 0};

	priv_info = mode->priv_info;

	rc = utils->read_u32_array(utils->data, "mi,mdss-flat-status-control-gamma-2F-cfg",
			gamma_2f_cfg, 2);
	if (rc) {
		DISP_INFO("mi,mdss-flat-status-control-gamma-2F-cfg not defined rc=%d\n", rc);
	} else {
		DISP_INFO("FPS: %d, gamma 2F cfg: 0x%02X, 0x%02X\n",
			mode->timing.refresh_rate, gamma_2f_cfg[0], gamma_2f_cfg[1]);
		priv_info->flat_on_2Freg_gamma = gamma_2f_cfg[0];
		priv_info->flat_off_2Freg_gamma = gamma_2f_cfg[1];
	}

	rc = utils->read_u32_array(utils->data, "mi,mdss-flat-status-control-gamma-26-cfg",
			gamma_26_cfg, 2);
	if (rc) {
		DISP_INFO("mi,mdss-flat-status-control-gamma-26-cfg not defined rc=%d\n", rc);
	} else {
		DISP_INFO("FPS: %d, gamma 26 cfg: 0x%02X, 0x%02X\n",
			mode->timing.refresh_rate, gamma_26_cfg[0], gamma_26_cfg[1]);
		priv_info->flat_on_26reg_gamma = gamma_26_cfg[0];
		priv_info->flat_off_26reg_gamma = gamma_26_cfg[1];
	}

	return 0;
}

int mi_dsi_panel_write_cmd_set(struct dsi_panel *panel,
				struct dsi_panel_cmd_set *cmd_sets)
{
	int rc = 0, i = 0;
	ssize_t len;
	struct dsi_cmd_desc *cmds;
	u32 count;
	enum dsi_cmd_set_state state;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	cmds = cmd_sets->cmds;
	count = cmd_sets->count;
	state = cmd_sets->state;

	if (count == 0) {
		DISP_DEBUG("[%s] No commands to be sent for state\n", panel->type);
		goto error;
	}

	for (i = 0; i < count; i++) {
		cmds->ctrl_flags = 0;

		if (state == DSI_CMD_SET_STATE_LP)
			cmds->msg.flags |= MIPI_DSI_MSG_USE_LPM;

		len = dsi_host_transfer_sub(panel->host, cmds);
		if (len < 0) {
			rc = len;
			DISP_ERROR("failed to set cmds, rc=%d\n", rc);
			goto error;
		}
		if (cmds->post_wait_ms)
			usleep_range(cmds->post_wait_ms * 1000,
					((cmds->post_wait_ms * 1000) + 10));
		cmds++;
	}
error:
	return rc;
}

int mi_dsi_panel_read_batch_number(struct dsi_panel *panel)
{
	int rc = 0;
	unsigned long mode_flags_backup = 0;
	u8 rdbuf[8];
	ssize_t read_len = 0;
	u8 read_batch_number = 0;

	int i = 0;
	struct panel_batch_info info[] = {
		{0x00, "P0.0"},
		{0x01, "P0.1"},
		{0x10, "P1.0"},
		{0x11, "P1.1"},
		{0x12, "P1.2"},
		{0x13, "P1.2"},
		{0x20, "P2.0"},
		{0x21, "P2.1"},
		{0x30, "MP"},
	};

	if (!panel || !panel->host) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}
	mutex_lock(&panel->panel_lock);

	mode_flags_backup = panel->mipi_device.mode_flags;
	panel->mipi_device.mode_flags |= MIPI_DSI_MODE_LPM;
	read_len = mipi_dsi_dcs_read(&panel->mipi_device, 0xDA, rdbuf, 1);
	panel->mipi_device.mode_flags = mode_flags_backup;
	if (read_len > 0) {
		read_batch_number = rdbuf[0];
		panel->mi_cfg.panel_batch_number = read_batch_number;
		for (i = 0; i < ARRAY_SIZE(info); i++) {
			if (read_batch_number == info[i].batch_number) {
				DISP_INFO("panel batch is %s\n", info[i].batch_name);
				break;
			}
		}
		rc = 0;
 		panel->mi_cfg.panel_batch_number_read_done = true;
	} else {
		DISP_ERROR("failed to read panel batch number\n");
		panel->mi_cfg.panel_batch_number = 0;
		rc = -EAGAIN;
		panel->mi_cfg.panel_batch_number_read_done = false;
	}

	mutex_unlock(&panel->panel_lock);
	return rc;
}

int mi_dsi_panel_write_dsi_cmd_set(struct dsi_panel *panel,
			int type)
{
	int rc = 0;
	int i = 0, j = 0;
	u8 *tx_buf = NULL;
	u8 *buffer = NULL;
	int buf_size = 1024;
	u32 cmd_count = 0;
	int buf_count = 1024;
	struct dsi_cmd_desc *cmds;
	enum dsi_cmd_set_state state;
	struct dsi_display_mode *mode;

	if (!panel || !panel->cur_mode || type < 0 || type >= DSI_CMD_SET_MAX) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	buffer = kzalloc(buf_size, GFP_KERNEL);
	if (!buffer) {
		return -ENOMEM;
	}

	mutex_lock(&panel->panel_lock);

	mode = panel->cur_mode;
	cmds = mode->priv_info->cmd_sets[type].cmds;
	cmd_count = mode->priv_info->cmd_sets[type].count;
	state = mode->priv_info->cmd_sets[type].state;

	if (cmd_count == 0) {
		DISP_ERROR("[%s] No commands to be sent\n", cmd_set_prop_map[type]);
		rc = -EAGAIN;
		goto error;
	}

	DISP_INFO("set cmds [%s], count (%d), state(%s)\n",
		cmd_set_prop_map[type], cmd_count,
		(state == DSI_CMD_SET_STATE_LP) ? "dsi_lp_mode" : "dsi_hs_mode");

	for (i = 0; i < cmd_count; i++) {
		memset(buffer, 0, buf_size);
		buf_count = snprintf(buffer, buf_size, "%02X", cmds->msg.tx_len);
		tx_buf = (u8 *)cmds->msg.tx_buf;
		for (j = 0; j < cmds->msg.tx_len ; j++) {
			buf_count += snprintf(buffer + buf_count,
					buf_size - buf_count, " %02X", tx_buf[j]);
		}
		DISP_DEBUG("[%d] %s\n", i, buffer);
		cmds++;
	}

	rc = dsi_panel_tx_cmd_set(panel, type);

error:
	mutex_unlock(&panel->panel_lock);
	kfree(buffer);
	return rc;
}

ssize_t mi_dsi_panel_show_dsi_cmd_set_type(struct dsi_panel *panel,
			char *buf, size_t size)
{
	ssize_t count = 0;
	int type = 0;

	if (!panel || !buf) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	count = snprintf(buf, size, "%s: dsi cmd_set name\n", "id");

	for (type = DSI_CMD_SET_PRE_ON; type < DSI_CMD_SET_MAX; type++) {
		count += snprintf(buf + count, size - count, "%02d: %s\n",
				     type, cmd_set_prop_map[type]);
	}

	return count;
}

int mi_dsi_panel_update_doze_cmd_locked(struct dsi_panel *panel, u8 value)
{
	int rc = 0;
	struct dsi_display_mode *cur_mode = NULL;
	struct dsi_cmd_update_info *info = NULL;
	u32 cmd_update_index = 0;
	u32 cmd_update_count = 0;
	int i = 0;

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	cur_mode = panel->cur_mode;

	cmd_update_index = DSI_CMD_SET_MI_DOZE_LBM_UPDATE;
	info = cur_mode->priv_info->cmd_update[cmd_update_index];
	cmd_update_count = cur_mode->priv_info->cmd_update_count[cmd_update_index];
	for (i = 0; i < cmd_update_count; i++){
		DISP_INFO("[%s] update [%s] mipi_address(0x%02X) index(%d) lenght(%d)\n",
			panel->type, cmd_set_prop_map[info->type],
			info->mipi_address, info->index, info->length);
		mi_dsi_panel_update_cmd_set(panel, cur_mode,
			DSI_CMD_SET_MI_DOZE_LBM, info,
			&value, sizeof(value));
		info++;
	}

	return rc;
}

int mi_dsi_panel_set_doze_brightness(struct dsi_panel *panel,
			u32 doze_brightness)
{
	int rc = 0;
	struct mi_dsi_panel_cfg *mi_cfg;

	mutex_lock(&panel->panel_lock);

	mi_cfg = &panel->mi_cfg;

	if (is_hbm_fod_on(panel)) {
		mi_cfg->last_doze_brightness = mi_cfg->doze_brightness;
		mi_cfg->doze_brightness = doze_brightness;
		DISP_INFO("Skip! [%s] set doze brightness %d due to FOD_HBM_ON\n",
			panel->type, doze_brightness);
	} else if ((panel->mi_cfg.lhbm_gxzw && panel->mi_cfg.aod_to_normal_pending == true)
		||panel->mi_cfg.aod_to_normal_statue == true) {
		mi_cfg->last_doze_brightness = mi_cfg->doze_brightness;
		mi_cfg->doze_brightness = doze_brightness;
		mi_dsi_update_backlight_in_aod(panel, false);
		panel->mi_cfg.aod_to_normal_pending = false;
	} else if (panel->mi_cfg.panel_state == PANEL_STATE_ON
		|| mi_cfg->doze_brightness != doze_brightness) {
		if (doze_brightness == DOZE_BRIGHTNESS_HBM) {
			panel->mi_cfg.panel_state = PANEL_STATE_DOZE_HIGH;
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_HBM);
			if (rc) {
				DISP_ERROR("[%s] failed to send DOZE_HBM cmd, rc=%d\n",
					panel->type, rc);
			}
		} else if (doze_brightness == DOZE_BRIGHTNESS_LBM) {
			panel->mi_cfg.panel_state = PANEL_STATE_DOZE_LOW;
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_LBM);
			if (rc) {
				DISP_ERROR("[%s] failed to send DOZE_LBM cmd, rc=%d\n",
					panel->type, rc);
			}
		}
		mi_cfg->last_doze_brightness = mi_cfg->doze_brightness;
		mi_cfg->doze_brightness = doze_brightness;
		DISP_TIME_INFO("[%s] set doze brightness to %s\n",
			panel->type, get_doze_brightness_name(doze_brightness));
	} else {
		DISP_INFO("[%s] %s has been set, skip\n", panel->type,
			get_doze_brightness_name(doze_brightness));
	}

	mutex_unlock(&panel->panel_lock);

	return rc;
}

int mi_dsi_panel_get_doze_brightness(struct dsi_panel *panel,
			u32 *doze_brightness)
{
	struct mi_dsi_panel_cfg *mi_cfg;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	mi_cfg = &panel->mi_cfg;
	*doze_brightness =  mi_cfg->doze_brightness;

	mutex_unlock(&panel->panel_lock);

	return 0;
}

int mi_dsi_panel_get_brightness(struct dsi_panel *panel,
			u32 *brightness)
{
	struct mi_dsi_panel_cfg *mi_cfg;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	mi_cfg = &panel->mi_cfg;
	*brightness =  mi_cfg->last_bl_level;

	mutex_unlock(&panel->panel_lock);

	return 0;
}

int mi_dsi_panel_write_dsi_cmd(struct dsi_panel *panel,
			struct dsi_cmd_rw_ctl *ctl)
{
	struct dsi_panel_cmd_set cmd_sets = {0};
	u32 packet_count = 0;
	u32 dlen = 0;
	int rc = 0;

	mutex_lock(&panel->panel_lock);

	if (!panel || !panel->panel_initialized) {
		DISP_ERROR("Panel not initialized!\n");
		rc = -EAGAIN;
		goto exit_unlock;
	}

	if (!ctl->tx_len || !ctl->tx_ptr) {
		DISP_ERROR("[%s] invalid params\n", panel->type);
		rc = -EINVAL;
		goto exit_unlock;
	}

	rc = dsi_panel_get_cmd_pkt_count(ctl->tx_ptr, ctl->tx_len, &packet_count);
	if (rc) {
		DISP_ERROR("[%s] write dsi commands failed, rc=%d\n",
			panel->type, rc);
		goto exit_unlock;
	}

	DISP_DEBUG("[%s] packet-count=%d\n", panel->type, packet_count);

	rc = dsi_panel_alloc_cmd_packets(&cmd_sets, packet_count);
	if (rc) {
		DISP_ERROR("[%s] failed to allocate cmd packets, rc=%d\n",
			panel->type, rc);
		goto exit_unlock;
	}

	rc = dsi_panel_create_cmd_packets(ctl->tx_ptr, dlen, packet_count,
				cmd_sets.cmds);
	if (rc) {
		DISP_ERROR("[%s] failed to create cmd packets, rc=%d\n",
			panel->type, rc);
		goto exit_free1;
	}

	if (ctl->tx_state == MI_DSI_CMD_LP_STATE) {
		cmd_sets.state = DSI_CMD_SET_STATE_LP;
	} else if (ctl->tx_state == MI_DSI_CMD_HS_STATE) {
		cmd_sets.state = DSI_CMD_SET_STATE_HS;
	} else {
		DISP_ERROR("[%s] command state unrecognized-%s\n",
			panel->type, cmd_sets.state);
		goto exit_free1;
	}

	rc = mi_dsi_panel_write_cmd_set(panel, &cmd_sets);
	if (rc) {
		DISP_ERROR("[%s] failed to send cmds, rc=%d\n", panel->type, rc);
		goto exit_free2;
	}

exit_free2:
	if (ctl->tx_len && ctl->tx_ptr)
		dsi_panel_destroy_cmd_packets(&cmd_sets);
exit_free1:
	if (ctl->tx_len && ctl->tx_ptr)
		dsi_panel_dealloc_cmd_packets(&cmd_sets);
exit_unlock:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int mi_dsi_panel_set_brightness_clone(struct dsi_panel *panel,
			u32 brightness_clone)
{
	int rc = 0;
	struct mi_dsi_panel_cfg *mi_cfg;
	int disp_id = MI_DISP_PRIMARY;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;

	if (brightness_clone > mi_cfg->max_brightness_clone)
		brightness_clone = mi_cfg->max_brightness_clone;

	atomic_set(&mi_cfg->brightness_clone, brightness_clone);

	disp_id = mi_get_disp_id(panel->type);
	mi_disp_feature_event_notify_by_type(disp_id,
			MI_DISP_EVENT_BRIGHTNESS_CLONE,
			sizeof(u32), atomic_read(&mi_cfg->brightness_clone));

	return rc;
}

int mi_dsi_panel_get_brightness_clone(struct dsi_panel *panel,
			u32 *brightness_clone)
{
	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	*brightness_clone = atomic_read(&panel->mi_cfg.brightness_clone);

	return 0;
}

int mi_dsi_panel_get_max_brightness_clone(struct dsi_panel *panel,
			u32 *max_brightness_clone)
{
	struct mi_dsi_panel_cfg *mi_cfg;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;
	*max_brightness_clone =  mi_cfg->max_brightness_clone;

	return 0;
}

void mi_dsi_update_backlight_in_aod(struct dsi_panel *panel, bool restore_backlight)
{
	int bl_lvl = 0;
	struct mi_dsi_panel_cfg *mi_cfg = &panel->mi_cfg;
	struct mipi_dsi_device *dsi = &panel->mipi_device;

	if (restore_backlight) {
		bl_lvl = mi_cfg->last_bl_level;
	} else {
		switch (mi_cfg->doze_brightness) {
			case DOZE_BRIGHTNESS_HBM:
				bl_lvl = mi_cfg->doze_hbm_dbv_level;
				break;
			case DOZE_BRIGHTNESS_LBM:
				bl_lvl = mi_cfg->doze_lbm_dbv_level;
				break;
			default:
				return;
		}
	}
	DISP_INFO("[%s] mi_dsi_update_backlight_in_aod bl_lvl=%d\n",
			panel->type, bl_lvl);
	if (panel->bl_config.bl_inverted_dbv)
		bl_lvl = (((bl_lvl & 0xff) << 8) | (bl_lvl >> 8));
	mipi_dsi_dcs_set_display_brightness(dsi, bl_lvl);

	return;
}

static int mi_dsi_update_51_mipi_cmd(struct dsi_panel *panel,
			enum dsi_cmd_set_type type, int bl_lvl)
{
	struct dsi_display_mode *cur_mode = NULL;
	struct dsi_cmd_update_info *info = NULL;
	u32 cmd_update_index = 0;
	u32 cmd_update_count = 0;
	u8 bl_buf[6] = {0};
	int j = 0;
	int size = 2;

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	cur_mode = panel->cur_mode;

	DISP_INFO("[%s] bl_lvl = %d\n", panel->type, bl_lvl);

	bl_buf[0] = (bl_lvl >> 8) & 0xff;
	bl_buf[1] = bl_lvl & 0xff;
	size = 2;

	switch (type) {
	case DSI_CMD_SET_NOLP:
		cmd_update_index = DSI_CMD_SET_NOLP_UPDATE;
		break;
	case DSI_CMD_SET_MI_HBM_ON:
		cmd_update_index = DSI_CMD_SET_MI_HBM_ON_UPDATA;
		break;
	case DSI_CMD_SET_MI_HBM_OFF:
		cmd_update_index = DSI_CMD_SET_MI_HBM_OFF_UPDATA;
		break;
	case DSI_CMD_SET_MI_HBM_FOD_ON:
		cmd_update_index = DSI_CMD_SET_MI_HBM_FOD_ON_UPDATA;
		break;
	case DSI_CMD_SET_MI_HBM_FOD_OFF:
		cmd_update_index = DSI_CMD_SET_MI_HBM_FOD_OFF_UPDATA;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_PRE:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_PRE_UPDATE;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_UPDATE;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT_UPDATE;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT_UPDATE;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_HBM:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_HBM_UPDATE;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_NORMAL:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_NORMAL_UPDATE;
		break;
	case DSI_CMD_SET_MI_DOZE_HBM:
		cmd_update_index = DSI_CMD_SET_MI_DOZE_HBM_UPDATE;
		break;
	case DSI_CMD_SET_MI_DOZE_LBM:
		cmd_update_index = DSI_CMD_SET_MI_DOZE_LBM_UPDATE;
		break;
	case DSI_CMD_SET_MI_IP_ON:
		cmd_update_index = DSI_CMD_SET_MI_IP_ON_UPDATE;
		break;
	case DSI_CMD_SET_MI_IP_OFF:
		cmd_update_index = DSI_CMD_SET_MI_IP_OFF_UPDATE;
		break;
	default:
		DISP_ERROR("[%s] unsupport cmd %s\n",
				panel->type, cmd_set_prop_map[type]);
		return -EINVAL;
	}

	info = cur_mode->priv_info->cmd_update[cmd_update_index];
	cmd_update_count = cur_mode->priv_info->cmd_update_count[cmd_update_index];
	for (j = 0; j < cmd_update_count; j++) {
		DISP_DEBUG("[%s] update [%s] mipi_address(0x%02X) index(%d) lenght(%d)\n",
			panel->type, cmd_set_prop_map[info->type],
			info->mipi_address, info->index, info->length);
		if (info && info->mipi_address != 0x51) {
			DISP_DEBUG("[%s] error mipi address (0x%02X)\n", panel->type, info->mipi_address);
			info++;
			continue;
		} else {
			mi_dsi_panel_update_cmd_set(panel, cur_mode,
					type, info, bl_buf, size);
			break;
		}
	}

	return 0;
}

/* Note: Factory version need flat cmd send out immediately,
 * do not care it may lead panel flash.
 * Dev version need flat cmd send out send with te
 */
static int mi_dsi_send_flat_sync_with_te_locked(struct dsi_panel *panel,
			bool enable)
{
	int rc = 0;

#ifdef CONFIG_FACTORY_BUILD
	if (enable) {
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_ON);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_SEC_ON);
	} else {
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_OFF);
		rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_SEC_OFF);
	}
	DISP_INFO("send flat %s cmd immediately", enable ? "ON" : "OFF");
#else
	/*flat cmd will be send out at mi_sde_connector_flat_fence*/
	DISP_DEBUG("flat cmd should send out sync with te");
#endif
	return rc;
}

int mi_dsi_panel_read_manufacturer_info(struct dsi_panel *panel,
		u32 manufacturer_info_addr, char *rdbuf, int rdlen)
{
	int ret = 0;
	if (!panel || !panel->host) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}
	mutex_lock(&panel->panel_lock);
	ret = mi_dsi_panel_read_manufacturer_info_locked(panel, manufacturer_info_addr, rdbuf, rdlen);
	mutex_unlock(&panel->panel_lock);
	return ret;
}

int mi_dsi_panel_read_manufacturer_info_locked(struct dsi_panel *panel,
		u32 manufacturer_info_addr, char *rdbuf, int rdlen)
{
	int rc = 0;

	if (!panel || !panel->host) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	rc = mipi_dsi_dcs_read(&panel->mipi_device, manufacturer_info_addr, rdbuf, rdlen);
	if (rc < 0) {
		DISP_ERROR("[%s] read manufacturer_info param failed, rc=%d\n", panel->type, rc);
		goto exit;
	}
	if (rc != rdlen) {
		DISP_INFO("read manufacturer_info failed (%d)\n", rc);
		goto exit;
	}

exit:
	return rc;
}

#define N3_LHBM1000NITS 6
#define N3_LHBM110NITS 0
#define N3_LHBMGREEN500NITS 12

static inline void mi_dsi_panel_cal_lhbm_param_N3(int type,
		u8 *update_val, u8 size, int bl_lvl)
{
	int i = 0;
	u64 tmp;
	u16 w1000_dev[8] = {3590, 3590, 3610, 3625, 3635, 3645, 3650, 3660};
	u16 g500_dev[8] = {4240, 4210, 4190, 4170, 4150, 4140, 4130, 4120};
	u8 dev_index = 0;

	if (bl_lvl <= 0x1C2)
		dev_index = 0;
	else if (bl_lvl > 0x1C2 && bl_lvl <= 0x333)
		dev_index = 1;
	else if (bl_lvl > 0x333 && bl_lvl <= 0x484)
		dev_index = 2;
	else if (bl_lvl > 0x484 && bl_lvl <= 0x61A)
		dev_index = 3;
	else if (bl_lvl > 0x61A && bl_lvl <= 0x7FF)
		dev_index = 4;
	else if (bl_lvl > 0x7FF && bl_lvl <= 0x903)
		dev_index = 5;
	else if (bl_lvl > 0x903 && bl_lvl <= 0xA10)
		dev_index = 6;
	else if (bl_lvl > 0xA10 && bl_lvl <= 0xB32)
		dev_index = 7;

	for (i = 0; i < size / 2; i++) {
		tmp = (update_val[i*2] << 8) | update_val[i*2+1];

		if (type == N3_LHBMGREEN500NITS) {
			DISP_DEBUG("G500 gamma update tmp[%d] * 1000 / %d\n", tmp, g500_dev[dev_index]);
			tmp = tmp * 1000 / g500_dev[dev_index];
		} else if (type == N3_LHBM1000NITS) {
			DISP_DEBUG("W1000 gamma update tmp[%d] * 100 / %d\n", tmp, w1000_dev[dev_index]);
			tmp = tmp * 1000 / w1000_dev[dev_index];
		}

		update_val[i*2] = (tmp >> 8) & 0xFF;
		update_val[i*2 + 1] = tmp & 0xFF;
		DISP_DEBUG("gamma update tmp %d [%d] %d 0x%x  %x\n", i, bl_lvl, dev_index, update_val[i*2], update_val[i*2+ 1]);
	}
}

static int mi_dsi_panel_update_lhbm_param_N3(struct dsi_panel * panel,
		enum dsi_cmd_set_type type, int bl_lvl, bool lhbm_is_0size)
{
	int rc=0;
	struct dsi_display_mode *cur_mode = NULL;
	struct dsi_cmd_update_info *info = NULL;
	struct mi_dsi_panel_cfg *mi_cfg = NULL;
	u32 cmd_update_index = 0;
	u32 cmd_update_count = 0;
	u8 *update_rgb_reg_ptr = NULL;
	u8 update_rgb_reg[24] = {0};
	u8 alpha_buf[4] = {0};
	int i = 0;
	int lhbm_data_index = 0;
	u8 length = 24;
	u8 size_set = 0;
	bool is_cal_lhbm_param = false;

	if (!panel->mi_cfg.read_lhbm_gamma_success ||
		mi_get_panel_id_by_dsi_panel(panel) != N3_PANEL_PA ) {
		DISP_DEBUG("don't need update white rgb config\n");
		return 0;
	}

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	cur_mode = panel->cur_mode;
	mi_cfg = &panel->mi_cfg;

	if(bl_lvl <= panel->mi_cfg.lhbm_lbl_mode_threshold
		&& panel->id_config.build_id <= N3_PANEL_PA_P11) {
		alpha_buf[0] = (aa_alpha_N3_PANEL_PA_P11[bl_lvl] >> 8) & 0xFF;
		alpha_buf[1] = aa_alpha_N3_PANEL_PA_P11[bl_lvl] & 0xFF;
	} else if(bl_lvl <= panel->mi_cfg.lhbm_lbl_mode_threshold
		&& panel->id_config.build_id > N3_PANEL_PA_P11) {
		alpha_buf[0] = (aa_alpha_N3_PANEL_PA_P12[bl_lvl] >> 8) & 0xFF;
		alpha_buf[1] = aa_alpha_N3_PANEL_PA_P12[bl_lvl] & 0xFF;
	}

	if(bl_lvl <= mi_cfg->lhbm_lbl_mode_threshold) {

		alpha_buf[2] = 0x01;
		alpha_buf[3] = 0xC2;
	} else {
		alpha_buf[0] = 0x10;
		alpha_buf[1] = 0x00;
		alpha_buf[2] = (bl_lvl >> 8) & 0xff;
		alpha_buf[3] = bl_lvl & 0xff;
	}
	DISP_DEBUG("[%s] bl_lvl = %d, alpha = 0x%x%x type=%d\n",
			panel->type, bl_lvl, alpha_buf[0], alpha_buf[1], type);
	if(lhbm_is_0size)
		size_set = 0x01;
	else
		size_set = 0x03;

	switch (type) {
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_UPDATE;
		lhbm_data_index = N3_LHBM1000NITS;
		is_cal_lhbm_param = true;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT_UPDATE;
		lhbm_data_index = N3_LHBM1000NITS;
		is_cal_lhbm_param = true;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT_UPDATE;
		lhbm_data_index = N3_LHBM110NITS;
		is_cal_lhbm_param = false;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT_UPDATE;
		lhbm_data_index = N3_LHBM110NITS;
		is_cal_lhbm_param = false;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT_UPDATE;
		lhbm_data_index = N3_LHBMGREEN500NITS;
		is_cal_lhbm_param = true;
		break;
	default:
		DISP_ERROR("[%s] unsupport cmd %s\n", panel->type, cmd_set_prop_map[type]);
		return -EINVAL;
	}

	for ( i= 0; i< length; i++) {
		update_rgb_reg[i] = mi_cfg->lhbm_rgb_param[i % 6 + lhbm_data_index];
		DISP_DEBUG("[%s] update_rgb_reg[%d] = %d \n", panel->type, i, update_rgb_reg[i]);
	}
	update_rgb_reg_ptr = update_rgb_reg;
	if (is_cal_lhbm_param)
		mi_dsi_panel_cal_lhbm_param_N3(lhbm_data_index, update_rgb_reg_ptr, length, bl_lvl);

	DISP_DEBUG("[%s] lhbm_data_index = %d build_id=0x%02X\n", panel->type, lhbm_data_index, panel->id_config.build_id);

	info = cur_mode->priv_info->cmd_update[cmd_update_index];
	cmd_update_count = cur_mode->priv_info->cmd_update_count[cmd_update_index];
	for ( i= 0; i< cmd_update_count; i++) {
		DISP_DEBUG("[%s] update [%s] mipi_address(0x%02X) index(%d) lenght(%d)\n",
			panel->type, cmd_set_prop_map[info->type],
			info->mipi_address, info->index, info->length);
		if (info && info->mipi_address == 0x63 && info->length == sizeof(alpha_buf)) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info,
				alpha_buf, sizeof(alpha_buf));
		} else if (info && info->mipi_address == 0xC5 && info->length == length) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg, length);
		} else if (info && info->mipi_address == 0x62) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, &size_set, sizeof(size_set));
		}
		info++;
	}

	return rc;
}

int mi_dsi_panel_update_gamma_param(struct dsi_panel * panel, u32 cmd_update_index,
		enum dsi_cmd_set_type type)
{
	int rc=0;
	struct dsi_display_mode *cur_mode = NULL;
	struct dsi_cmd_update_info *info = NULL;
	struct mi_dsi_panel_cfg *mi_cfg = NULL;
	u32 cmd_update_count = 0;
	u8 *update_rgb_reg_ptr = NULL;
	u8 *update_rgb_reg_ptr2 = NULL;
	u8 update_rgb_reg[16] = {0};
	u8 update_rgb_reg2[2] = {0};
	int i = 0, j = 0;
	u8 length = 16;

	if (!panel->mi_cfg.read_gamma_success ||
		mi_get_panel_id_by_dsi_panel(panel) != N3_PANEL_PA ) {
		DISP_DEBUG("don't need update gamma config\n");
		return 0;
	}

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	cur_mode = panel->cur_mode;
	mi_cfg = &panel->mi_cfg;
	info = cur_mode->priv_info->cmd_update[cmd_update_index];
	cmd_update_count = cur_mode->priv_info->cmd_update_count[cmd_update_index];
	for ( i= 0; i< cmd_update_count; i++) {
		DISP_DEBUG("[%s] update [%s] mipi_address(0x%02X) index(%d) lenght(%d)\n",
			panel->type, cmd_set_prop_map[info->type],
			info->mipi_address, info->index, info->length);
		if (info && info->mipi_address == mi_cfg->gamma_rgb_param[0] && info->length == length) {
			for (j = 0;j < length;j++) {
				update_rgb_reg[j] = mi_cfg->gamma_rgb_param[j + 1];
				DISP_DEBUG("[%s] update_rgb_reg[%d] = %d \n", panel->type, j, update_rgb_reg[j]);
			}
			update_rgb_reg_ptr = update_rgb_reg;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg, length);
		} else if (info && info->mipi_address == mi_cfg->gamma_rgb_param[17] && info->length == length) {
			for (j = 0;j < length;j++) {
				update_rgb_reg[j] = mi_cfg->gamma_rgb_param[j + 18];
				DISP_DEBUG("[%s] update_rgb_reg[%d] = %d \n", panel->type, j, update_rgb_reg[j]);
			}
			update_rgb_reg_ptr = update_rgb_reg;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg, length);
		} else if (info && info->mipi_address == mi_cfg->gamma_rgb_param[34] && info->length == length) {
			for (j = 0;j < length;j++) {
				update_rgb_reg[j] = mi_cfg->gamma_rgb_param[j + 35];
				DISP_DEBUG("[%s] update_rgb_reg[%d] = %d \n", panel->type, j, update_rgb_reg[j]);
			}
			update_rgb_reg_ptr = update_rgb_reg;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg, length);
		} else if (info && info->mipi_address == mi_cfg->gamma_rgb_param[51] && info->length == 2) {
			for (j = 0; j < 2; j++) {
				update_rgb_reg2[j] = mi_cfg->gamma_rgb_param[j + 52];
				DISP_DEBUG("[%s] update_rgb_reg2[%d] = %d \n", panel->type, j, update_rgb_reg2[j]);
			}
			update_rgb_reg_ptr2 = update_rgb_reg2;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg2, 2);
		} else if (info && info->mipi_address == mi_cfg->gamma_rgb_param[54] && info->length == length) {
			for (j = 0; j < length; j++) {
				update_rgb_reg[j] = mi_cfg->gamma_rgb_param[j + 55];
				DISP_DEBUG("[%s] update_rgb_reg[%d] = %d \n", panel->type, j, update_rgb_reg[j]);
			}
			update_rgb_reg_ptr = update_rgb_reg;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg, length);
		} else if (info && info->mipi_address == mi_cfg->gamma_rgb_param[71] && info->length == length) {
			for (j = 0; j < length; j++) {
				update_rgb_reg[j] = mi_cfg->gamma_rgb_param[j + 72];
				DISP_DEBUG("[%s] update_rgb_reg[%d] = %d \n", panel->type, j, update_rgb_reg[j]);
			}
			update_rgb_reg_ptr = update_rgb_reg;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg, length);
		} else if (info && info->mipi_address == mi_cfg->gamma_rgb_param[88] && info->length == length) {
			for (j = 0; j < length; j++) {
				update_rgb_reg[j] = mi_cfg->gamma_rgb_param[j + 89];
				DISP_DEBUG("[%s] update_rgb_reg[%d] = %d \n", panel->type, j, update_rgb_reg[j]);
			}
			update_rgb_reg_ptr = update_rgb_reg;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg, length);
		} else if (info && info->mipi_address == mi_cfg->gamma_rgb_param[105] && info->length == 2) {
			for (j = 0; j < 2; j++) {
				update_rgb_reg2[j] = mi_cfg->gamma_rgb_param[j + 106];
				DISP_DEBUG("[%s] update_rgb_reg2[%d] = %d \n", panel->type, j, update_rgb_reg2[j]);
			}
			update_rgb_reg_ptr2 = update_rgb_reg2;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg2, 2);
		} else if (info && info->mipi_address == mi_cfg->gamma_rgb_param[108] && info->length == length) {
			for (j = 0; j < length; j++) {
				update_rgb_reg[j] = mi_cfg->gamma_rgb_param[j + 109];
				DISP_DEBUG("[%s] update_rgb_reg[%d] = %d \n", panel->type, j, update_rgb_reg[j]);
			}
			update_rgb_reg_ptr = update_rgb_reg;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg, length);
		} else if (info && info->mipi_address == mi_cfg->gamma_rgb_param[125] && info->length == length) {
			for (j = 0; j < length; j++) {
				update_rgb_reg[j] = mi_cfg->gamma_rgb_param[j + 126];
				DISP_DEBUG("[%s] update_rgb_reg[%d] = %d \n", panel->type, j, update_rgb_reg[j]);
			}
			update_rgb_reg_ptr = update_rgb_reg;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg, length);
		} else if (info && info->mipi_address == mi_cfg->gamma_rgb_param[142] && info->length == length) {
			for (j = 0; j < length; j++) {
				update_rgb_reg[j] = mi_cfg->gamma_rgb_param[j + 143];
				DISP_DEBUG("[%s] update_rgb_reg[%d] = %d \n", panel->type, j, update_rgb_reg[j]);
			}
			update_rgb_reg_ptr = update_rgb_reg;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg, length);
		} else if (info && info->mipi_address == mi_cfg->gamma_rgb_param[159] && info->length == 2) {
			for (j = 0; j < 2; j++) {
				update_rgb_reg2[j] = mi_cfg->gamma_rgb_param[j + 160];
				DISP_DEBUG("[%s] update_rgb_reg[%d] = %d \n", panel->type, j, update_rgb_reg2[j]);
			}
			update_rgb_reg_ptr2 = update_rgb_reg2;
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info, update_rgb_reg2, 2);
		}
		info++;
	}

	return rc;
}

#define LHBM1000NITS 0
#define LHBM750NITS 6
#define LHBM500NITS 12
#define LHBM110NITS 18
#define LHBMGREEN500NITS 24
#define LHBMGIRONNITSOFFSET 26

static inline void mi_dsi_panel_cal_lhbm_param(struct dsi_panel * panel, enum dsi_cmd_set_type type,
		u8 *update_val, u8 size , int bl_lvl, int type_index)
{
	int i = 0;
	int bl_index = 0;
	int coefficient_index = 0;
	u16 tmp;

	if (panel->id_config.build_id < N2_PANEL_PA_P11_02) {
		/*
	 * gamma update, R,G,B except 110nit & 550nit(Green:R/B)
	 * 1000nit/750nit/500nit 5.3/4.2
	 * 500nit(Green:G) 5/4.2
	*/
		for (i = 0; i < size / 2; i++) {
			tmp = (update_val[i*2] << 8) | update_val[i*2+1];

			if (type == DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT) {
				DISP_DEBUG("gamma update：tmp[%d] * 500 / 420\n",tmp);
				tmp = tmp * 500 / 420;
			} else {
				DISP_DEBUG("gamma update：tmp[%d] * 530 / 420\n",tmp);
				tmp = tmp * 530 / 420;
			}

			update_val[i*2] = (tmp >> 8) & 0xFF;
			update_val[i*2 + 1] = tmp & 0xFF;
		}
	} else {
		if (bl_lvl <= 327) bl_index = 0;
		else if (327 < bl_lvl && bl_lvl <= 573) bl_index = 1;
		else if (573 < bl_lvl && bl_lvl <= 819) bl_index = 2;
		else if (819 < bl_lvl && bl_lvl <= 1024) bl_index = 3;
		else if (1024 < bl_lvl && bl_lvl <= 1228) bl_index = 4;
		else if (1228 < bl_lvl && bl_lvl <= 1433) bl_index = 5;
		else if (1433 < bl_lvl && bl_lvl <= 1638) bl_index = 6;
		else if (1638 < bl_lvl && bl_lvl <= 1842) bl_index = 7;
		else if (1842 < bl_lvl && bl_lvl <= 2047) bl_index = 8;
		else if (2047 < bl_lvl && bl_lvl <= 2252) bl_index = 9;
		else if (2252 < bl_lvl && bl_lvl <= 2457) bl_index = 10;
		else if (2457 < bl_lvl && bl_lvl <= 2661) bl_index = 11;
		else if (2661 < bl_lvl && bl_lvl <= 2866) bl_index = 12;
		else if (2866 < bl_lvl && bl_lvl <= 2923) bl_index = 13;
		else if (2923 < bl_lvl && bl_lvl <= 2981) bl_index = 14;
		else if (2981 < bl_lvl && bl_lvl <= 3096) bl_index = 15;
		else if (3096 < bl_lvl && bl_lvl <= 3211) bl_index = 16;
		else if (3211 < bl_lvl && bl_lvl <= 3326) bl_index = 17;
		else if (3326 < bl_lvl && bl_lvl <= 3440) bl_index = 18;
		else if (3440 < bl_lvl && bl_lvl <= 3555) bl_index = 19;
		else if (3555 < bl_lvl && bl_lvl <= 3670) bl_index = 20;
		else if (3670 < bl_lvl && bl_lvl <= 3785) bl_index = 21;
		else if (3785 < bl_lvl && bl_lvl <= 3900) bl_index = 22;

		coefficient_index = type_index*23 + bl_index;
		DISP_DEBUG("type_index = %d,bl_index = %d,coefficient_index = %d\n",type_index,bl_index,coefficient_index);

		if (coefficient_index < 0 || coefficient_index > 114) {
			DISP_ERROR("invalid lhbm coefficient_index\n");
			return ;
		}
		for (i = 0; i < size / 2; i++) {
			tmp = (update_val[i*2] << 8) | update_val[i*2+1];
			if (panel->id_config.build_id == N2_PANEL_PA_P11_02) {
				DISP_DEBUG("gamma update：tmp[%d] * lhbm_coefficient_N2_PANEL_PA_P11_02[%d] / 420\n",tmp,coefficient_index);
				tmp = tmp * lhbm_coefficient_N2_PANEL_PA_P11_02[coefficient_index] / 420;
			} else if (panel->id_config.build_id >= N2_PANEL_PA_P2_01) {
				DISP_DEBUG("gamma update：tmp[%d] * lhbm_coefficient_N2_PANEL_PA_P2[%d] / 420\n",tmp,coefficient_index);
				tmp = tmp * lhbm_coefficient_N2_PANEL_PA_P2[coefficient_index] / 420;
			}

			update_val[i*2] = (tmp >> 8) & 0xFF;
			update_val[i*2 + 1] = tmp & 0xFF;
		}
	}
}

static int mi_dsi_panel_update_lhbm_param_N2(struct dsi_panel * panel,
		enum dsi_cmd_set_type type, int flat_mode, int bl_lvl)
{
	int rc = 0;
	struct dsi_display_mode *cur_mode = NULL;
	struct dsi_cmd_update_info *info = NULL;
	struct mi_dsi_panel_cfg *mi_cfg = NULL;
	u32 cmd_update_index = 0;
	u32 cmd_update_count = 0;
	int i= 0;
	u8 DF_gir_reg_val[2] = {0x22, 0x20};
	u8 DF_bl_reg_val[2] = {0x05, 0x1C};
	u8 alpha_buf[2] = {0};
	u8 *update_D0_reg_ptr = NULL;
	u8 update_D0_reg[6] = {0};
	u32 tmp_bl;
	u8 length = 6;
	u8 lhbm_data_index = 0;
	bool is_cal_lhbm_param = false;
	int type_index = 0;

	if (mi_get_panel_id_by_dsi_panel(panel) != N2_PANEL_PA) {
		DISP_DEBUG("don't need update lhbm command\n");
		return 0;
	}

	if (!panel || !panel->cur_mode || !panel->cur_mode->priv_info) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	cur_mode = panel->cur_mode;
	mi_cfg = &panel->mi_cfg;

	if(bl_lvl < 327) {
		if (panel->id_config.build_id >= N2_PANEL_PA_P11_02) {
			alpha_buf[0] = (aa_alpha_N2_PANEL_PA_P11_02[bl_lvl] >> 8) & 0xFF;
			alpha_buf[1] = aa_alpha_N2_PANEL_PA_P11_02[bl_lvl] & 0xFF;
		} else {
			alpha_buf[0] = (aa_alpha_N2_PANEL_PA[bl_lvl] >> 8) & 0xFF;
			alpha_buf[1] = aa_alpha_N2_PANEL_PA[bl_lvl] & 0xFF;
		}
	} else {
		alpha_buf[0] = 0x0F;
		alpha_buf[1] = 0xFF;
		tmp_bl = bl_lvl * 4;
		DF_bl_reg_val[0] = (tmp_bl>> 8) & 0xff;
		DF_bl_reg_val[1] = tmp_bl & 0xff;
	}
	DISP_DEBUG("[%s] bl_lvl = %d, alpha = 0x%x%x\n",
		panel->type, bl_lvl, alpha_buf[0], alpha_buf[1]);

	switch (type) {
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_UPDATE;
		lhbm_data_index = LHBM1000NITS;
		is_cal_lhbm_param = true;
		type_index = 0;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_750NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_750NIT_UPDATE;
		lhbm_data_index = LHBM750NITS;
		is_cal_lhbm_param = true;
		type_index = 1;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_500NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_500NIT_UPDATE;
		lhbm_data_index = LHBM500NITS;
		is_cal_lhbm_param = true;
		type_index = 2;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT_UPDATE;
		lhbm_data_index = LHBM110NITS;
		type_index = 3;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT_UPDATE;
		lhbm_data_index = LHBM1000NITS;
		is_cal_lhbm_param = true;
		type_index = 0;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT_UPDATE;
		lhbm_data_index = LHBM110NITS;
		type_index = 3;
		break;
	case DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT:
		cmd_update_index = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT_UPDATE;
		lhbm_data_index = LHBMGREEN500NITS;
		is_cal_lhbm_param = true;
		type_index = 4;
		break;
	default:
		DISP_ERROR("[%s] unsupport cmd %s\n", panel->type, cmd_set_prop_map[type]);
		return -EINVAL;
	}

	if(mi_cfg->read_lhbm_gamma_success) {
		if(lhbm_data_index == LHBMGREEN500NITS) {
			length = 2;
			update_D0_reg_ptr = &update_D0_reg[2];
		} else {
			length = 6;
			update_D0_reg_ptr = update_D0_reg;
		}

		if (flat_mode)
			memcpy(update_D0_reg_ptr, &mi_cfg->lhbm_param[lhbm_data_index+LHBMGIRONNITSOFFSET], length);
		else
			memcpy(update_D0_reg_ptr, &mi_cfg->lhbm_param[lhbm_data_index], length);

		if (is_cal_lhbm_param || panel->id_config.build_id >= N2_PANEL_PA_P11_02) {
			mi_dsi_panel_cal_lhbm_param(panel,type, update_D0_reg_ptr, length, bl_lvl, type_index);
		}
		DISP_DEBUG("update lbhm data index %d, calculate data %d, length %d",
				lhbm_data_index, is_cal_lhbm_param, length);
	}

	cur_mode = panel->cur_mode;
	info = cur_mode->priv_info->cmd_update[cmd_update_index];
	cmd_update_count = cur_mode->priv_info->cmd_update_count[cmd_update_index];
	for (i = 0; i < cmd_update_count; i++) {
		if (info && info->mipi_address == 0xDF && info->length == 1) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info,
				&DF_gir_reg_val[flat_mode], 1);
		} else if (info && info->mipi_address == 0xDF && info->length == 2) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info,
				DF_bl_reg_val, sizeof(DF_bl_reg_val));
		}  else if (info && info->mipi_address == 0xD0 && info->length == length &&
			mi_cfg->read_lhbm_gamma_success){
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info,
				update_D0_reg, sizeof(update_D0_reg));
		} else if (info && info->mipi_address == 0x87) {
			mi_dsi_panel_update_cmd_set(panel, cur_mode, type, info,
				alpha_buf, sizeof(alpha_buf));
		}
		info++;
	}

	return rc;
}

int mi_dsi_panel_set_dbi_by_temp_locked(struct dsi_panel *panel,
			int temp_val)
{
	int rc = 0;
	int real_temp_val = temp_val;
	struct mi_dsi_panel_cfg *mi_cfg  = NULL;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}
	mi_cfg = &panel->mi_cfg;
	if (!(mi_get_panel_id_by_dsi_panel(panel) == N3_PANEL_PA &&
		(panel->id_config.build_id >= N3_PANEL_PA_P11))) {
		DISP_DEBUG("not support this dbi feature id:%d temp_val %d\n",
			panel->id_config.build_id, temp_val);
		return rc;
	}
	DISP_DEBUG("[%s] set temp_val %d \n", panel->type, temp_val);

	switch (temp_val) {
	case TEMP_INDEX_25:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		break;
	case TEMP_INDEX_28:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_28_MODE);
		break;
	case TEMP_INDEX_32:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_32_MODE);
		break;
	case TEMP_INDEX_36:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_36_MODE);
		break;
	case TEMP_INDEX_40:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_40_MODE);
		break;
	default:
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DBI_BWG_25_MODE);
		real_temp_val = TEMP_INDEX_25;
		DISP_INFO("[%s] set temp_val default 25 \n", panel->type);
		break;
	}
	mi_cfg->real_dbi_state = real_temp_val;
	return rc;
}

int mi_dsi_panel_set_dbi_by_temp(struct dsi_panel *panel, int temp_val)
{
	int rc = 0;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	rc = mi_dsi_panel_set_dbi_by_temp_locked(panel, temp_val);

	mutex_unlock(&panel->panel_lock);

	return rc;
}

int mi_dsi_panel_set_round_corner_locked(struct dsi_panel *panel,
			bool enable)
{
	int rc = 0;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	if (panel->mi_cfg.ddic_round_corner_enabled) {
		if (enable)
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ROUND_CORNER_ON);
		else
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_ROUND_CORNER_OFF);

		if (rc)
			DISP_ERROR("[%s] failed to send ROUND_CORNER(%s) cmds, rc=%d\n",
				panel->type, enable ? "On" : "Off", rc);
	} else {
		DISP_INFO("[%s] ddic round corner feature not enabled\n", panel->type);
	}

	return rc;
}

int mi_dsi_panel_set_round_corner(struct dsi_panel *panel, bool enable)
{
	int rc = 0;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	rc = mi_dsi_panel_set_round_corner_locked(panel, enable);

	mutex_unlock(&panel->panel_lock);

	return rc;
}

int mi_dsi_panel_set_dc_mode(struct dsi_panel *panel, bool enable)
{
	int rc = 0;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	rc = mi_dsi_panel_set_dc_mode_locked(panel, enable);

	mutex_unlock(&panel->panel_lock);

	return rc;
}


int mi_dsi_panel_set_dc_mode_locked(struct dsi_panel *panel, bool enable)
{
	int rc = 0;
	struct mi_dsi_panel_cfg *mi_cfg  = NULL;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;
	if (mi_get_panel_id(mi_cfg->mi_panel_id) == N3_PANEL_PA &&
		panel->id_config.build_id < N3_PANEL_PA_P20){
		DISP_INFO("N3 not support DC for  build_id %d \n",panel->id_config.build_id);
		return rc ;
	}

	if (mi_cfg->dc_feature_enable) {
		if (enable) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DC_ON);
			mi_cfg->real_dc_state = FEATURE_ON;
		} else {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DC_OFF);
			mi_cfg->real_dc_state = FEATURE_OFF;
		}

		if (rc)
			DISP_ERROR("failed to set DC mode: %d\n", enable);
		else
			DISP_INFO("DC mode: %s\n", enable ? "On" : "Off");
	} else {
		DISP_INFO("DC mode: TODO\n");
	}

	return rc;
}

int mi_dsi_panel_set_lhbm_0size_locked(struct dsi_panel *panel)
{

	int rc = 0;
	int update_bl_lvl = 0;
	bool lhbm_is_0size = false;
	enum dsi_cmd_set_type lhbm_type = DSI_CMD_SET_MAX;
	struct mi_dsi_panel_cfg *mi_cfg = NULL;

	if (!panel) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;
	DISP_DEBUG("LOCAL_HBM_OFF_TO_0SIZE_LHBM\n");

	mi_cfg->dimming_state = STATE_DIM_BLOCK;
	lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT;
	lhbm_is_0size = true;
	if (mi_cfg->last_bl_level > mi_cfg->lhbm_hbm_mode_threshold) {
		DISP_DEBUG("LOCAL_HBM_OFF_TO_0SIZE_LHBM in HBM\n");
		update_bl_lvl = mi_cfg->lhbm_hbm_mode_threshold;
		mi_dsi_update_51_mipi_cmd(panel,
				DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_PRE, update_bl_lvl);
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_PRE);
	} else {
		DISP_DEBUG("LOCAL_HBM_OFF_TO_0SIZE_LHBM\n");
		if (is_aod_and_panel_initialized(panel)) {
			switch (mi_cfg->doze_brightness) {
			case DOZE_BRIGHTNESS_HBM:
				update_bl_lvl = mi_cfg->doze_hbm_dbv_level;
				DISP_DEBUG("LOCAL_HBM_OFF_TO_0SIZE_LHBM in doze_hbm_dbv_level\n");
				break;
			case DOZE_BRIGHTNESS_LBM:
				update_bl_lvl = mi_cfg->doze_lbm_dbv_level;
				DISP_DEBUG("LOCAL_HBM_OFF_TO_0SIZE_LHBM in doze_lbm_dbv_level\n");
				break;
			default:
				update_bl_lvl = mi_cfg->doze_hbm_dbv_level;
				DISP_DEBUG("LOCAL_HBM_OFF_TO_0SIZE_LHBM use doze_hbm_dbv_level as defaults\n");
				break;
			}
		} else {
			update_bl_lvl = mi_cfg->last_bl_level;
		}
	}
	mi_dsi_update_51_mipi_cmd(panel, lhbm_type, update_bl_lvl);
	mi_dsi_panel_update_lhbm_param_N3(panel, lhbm_type, update_bl_lvl, lhbm_is_0size);
	rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
	mi_cfg->lhbm_0size_on = true;

	return rc;
}

int mi_dsi_panel_set_lhbm_fod_locked(struct dsi_panel *panel,
		struct disp_feature_ctl *ctl)
{
	int rc = 0;
	int update_bl_lvl = 0;
	bool lhbm_is_0size = false;
	u32 last_bl_level_store = 0;
	enum dsi_cmd_set_type lhbm_type = DSI_CMD_SET_MAX;
	struct mi_dsi_panel_cfg *mi_cfg = NULL;

	if (!panel || !ctl || ctl->feature_id != DISP_FEATURE_LOCAL_HBM) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;

	if (mi_cfg->feature_val[DISP_FEATURE_FOD_CALIBRATION_BRIGHTNESS] > 0) {
		DISP_TIME_INFO("fod calibration brightness, update alpha,"
				"last bl (%d)-->fod calibration bl(%d)",
				mi_cfg->last_bl_level,
				mi_cfg->feature_val[DISP_FEATURE_FOD_CALIBRATION_BRIGHTNESS]);
		last_bl_level_store = mi_cfg->last_bl_level;
		mi_cfg->last_bl_level = mi_cfg->feature_val[DISP_FEATURE_FOD_CALIBRATION_BRIGHTNESS];
	}

	DISP_TIME_INFO("[%s] Local HBM: %s\n", panel->type,
			get_local_hbm_state_name(ctl->feature_val));

	switch (ctl->feature_val) {
	case LOCAL_HBM_OFF_TO_NORMAL:
		mi_dsi_update_backlight_in_aod(panel, true);
		if (mi_cfg->last_bl_level > mi_cfg->lhbm_hbm_mode_threshold) {
			DISP_INFO("LOCAL_HBM_OFF_TO_HBM\n");
			mi_cfg->dimming_state = STATE_DIM_BLOCK;
			lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_HBM;
			mi_dsi_update_51_mipi_cmd(panel, lhbm_type,
					mi_cfg->last_bl_level);
			rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
		} else if (mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N3_PANEL_PA &&
			mi_cfg->lhbm_gxzw &&
			mi_cfg->feature_val[DISP_FEATURE_FP_STATUS] != AUTH_STOP) {
			rc = mi_dsi_panel_set_lhbm_0size_locked(panel);
			DISP_DEBUG("LOCAL_HBM_TO_0SIZE_LHBM in off\n");
		} else {
			DISP_INFO("LOCAL_HBM_OFF_TO_NORMAL\n");
			panel->mi_cfg.lhbm_0size_on = false;
			lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_NORMAL;
			mi_dsi_update_51_mipi_cmd(panel, lhbm_type,
				mi_cfg->last_bl_level);
			rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
			mi_cfg->lhbm_0size_on = false;

			mi_cfg->dimming_state = STATE_DIM_RESTORE;
			mi_dsi_panel_update_last_bl_level(panel, mi_cfg->last_bl_level);
		}
		/*normal fod, restore dbi by dbv*/
		if (mi_get_panel_id(mi_cfg->mi_panel_id) == N2_PANEL_PA) {
  			mi_cfg->dbi_bwg_type = DSI_CMD_SET_MAX;
			dsi_panel_update_backlight(panel, mi_cfg->last_bl_level);
  		}
		break;
	case LOCAL_HBM_OFF_TO_NORMAL_BACKLIGHT:
		DISP_INFO("LOCAL_HBM_OFF_TO_NORMAL_BACKLIGHT\n");
		mi_dsi_update_backlight_in_aod(panel, false);
		lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_NORMAL;
		/* display backlight value should equal AOD brightness */
		if (is_aod_and_panel_initialized(panel)) {
			switch (mi_cfg->doze_brightness) {
			case DOZE_BRIGHTNESS_HBM:
				if (mi_cfg->last_no_zero_bl_level < mi_cfg->doze_hbm_dbv_level
					&& mi_cfg->feature_val[DISP_FEATURE_FP_STATUS] == AUTH_STOP) {
					mi_dsi_update_51_mipi_cmd(panel, lhbm_type,
						mi_cfg->last_no_zero_bl_level);
				} else {
					mi_dsi_update_51_mipi_cmd(panel, lhbm_type,
						mi_cfg->doze_hbm_dbv_level);
				}
				DISP_INFO("LOCAL_HBM_OFF_TO_NORMAL_BACKLIGHT in doze_hbm_dbv_level\n");
				break;
			case DOZE_BRIGHTNESS_LBM:
				if (mi_cfg->last_no_zero_bl_level < mi_cfg->doze_lbm_dbv_level) {
					mi_dsi_update_51_mipi_cmd(panel, lhbm_type,
						mi_cfg->last_no_zero_bl_level);
				} else {
					mi_dsi_update_51_mipi_cmd(panel, lhbm_type,
						mi_cfg->doze_lbm_dbv_level);
				}
				DISP_INFO("LOCAL_HBM_OFF_TO_NORMAL_BACKLIGHT in doze_lbm_dbv_level\n");
				break;
			default:
				DISP_INFO("LOCAL_HBM_OFF_TO_NORMAL_BACKLIGHT defaults\n");
				break;
			}
		} else {
			mi_dsi_update_51_mipi_cmd(panel, lhbm_type, mi_cfg->last_bl_level);
		}

		DISP_INFO("LOCAL_HBM_OFF_TO_NORMAL\n");
		rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
		mi_cfg->lhbm_0size_on = false;
		if (mi_cfg->need_fod_animal_in_normal)
			panel->mi_cfg.aod_to_normal_statue = true;
		mi_cfg->dimming_state = STATE_DIM_RESTORE;
		break;
	case LOCAL_HBM_OFF_TO_NORMAL_BACKLIGHT_RESTORE:
		/* display backlight value should equal unlock brightness */
		DISP_INFO("LOCAL_HBM_OFF_TO_NORMAL_BACKLIGHT_RESTORE\n");
		mi_dsi_update_backlight_in_aod(panel, true);
		mi_dsi_update_51_mipi_cmd(panel, DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_NORMAL,
					mi_cfg->last_bl_level);

		DISP_INFO("LOCAL_HBM_OFF_TO_NORMAL\n");
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_NORMAL);
		panel->mi_cfg.lhbm_0size_on = false;

		mi_cfg->dimming_state = STATE_DIM_RESTORE;
		break;
	case LOCAL_HBM_NORMAL_WHITE_1000NIT:
		mi_cfg->dimming_state = STATE_DIM_BLOCK;
		lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT;
		if (mi_cfg->last_bl_level > mi_cfg->lhbm_hbm_mode_threshold) {
			DISP_INFO("LOCAL_HBM_NORMAL_WHITE_1000NIT in HBM\n");
			if (mi_get_panel_id(mi_cfg->mi_panel_id) == N2_PANEL_PA)
				update_bl_lvl = mi_cfg->last_bl_level;
			else if (mi_get_panel_id(mi_cfg->mi_panel_id) == N3_PANEL_PA) {
				update_bl_lvl = mi_cfg->lhbm_hbm_mode_threshold;
				mi_dsi_update_51_mipi_cmd(panel,
					DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_PRE, update_bl_lvl);
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_1000NIT_PRE);
			} else
				update_bl_lvl = panel->bl_config.bl_max_level;
		} else {
			DISP_DEBUG("LOCAL_HBM_NORMAL_WHITE_1000NIT\n");
			if (is_aod_and_panel_initialized(panel)) {
				switch (mi_cfg->doze_brightness) {
				case DOZE_BRIGHTNESS_HBM:
					update_bl_lvl = mi_cfg->doze_hbm_dbv_level;
					DISP_INFO("LOCAL_HBM_NORMAL_WHITE_1000NIT in doze_hbm_dbv_level\n");
					break;
				case DOZE_BRIGHTNESS_LBM:
					update_bl_lvl = mi_cfg->doze_lbm_dbv_level;
					DISP_INFO("LOCAL_HBM_NORMAL_WHITE_1000NIT in doze_lbm_dbv_level\n");
					break;
				default:
					update_bl_lvl = mi_cfg->doze_hbm_dbv_level;
					DISP_INFO("LOCAL_HBM_NORMAL_WHITE_1000NIT use doze_hbm_dbv_level as defaults\n");
					break;
				}
			} else {
				update_bl_lvl = mi_cfg->last_bl_level;
			}
		}
		mi_dsi_update_51_mipi_cmd(panel, lhbm_type, update_bl_lvl);
		switch (mi_get_panel_id_by_dsi_panel(panel)) {
		case N2_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N2(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N3_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N3(panel, lhbm_type, update_bl_lvl, lhbm_is_0size);
			break;
		default:
			DISP_ERROR("unknown panel name\n");
			break;
		}
		rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
		panel->mi_cfg.lhbm_0size_on = false;
		break;
	case LOCAL_HBM_NORMAL_WHITE_750NIT:
		lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_750NIT;
		mi_dsi_panel_update_lhbm_param_N2(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], mi_cfg->last_bl_level);

		DISP_INFO("LOCAL_HBM_NORMAL_WHITE_750NIT\n");
		mi_cfg->dimming_state = STATE_DIM_BLOCK;
		rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
		break;
	case LOCAL_HBM_NORMAL_WHITE_500NIT:
		lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_500NIT;
		mi_dsi_panel_update_lhbm_param_N2(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], mi_cfg->last_bl_level);

		DISP_INFO("LOCAL_HBM_NORMAL_WHITE_500NIT\n");
		mi_cfg->dimming_state = STATE_DIM_BLOCK;
		rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
		break;
	case LOCAL_HBM_NORMAL_WHITE_110NIT:
		DISP_DEBUG("LOCAL_HBM_NORMAL_WHITE_110NIT\n");
		mi_cfg->dimming_state = STATE_DIM_BLOCK;
		lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_WHITE_110NIT;

		update_bl_lvl = mi_cfg->last_bl_level;

		if (is_aod_and_panel_initialized(panel)) {
			switch (mi_cfg->doze_brightness) {
			case DOZE_BRIGHTNESS_HBM:
				update_bl_lvl = mi_cfg->doze_hbm_dbv_level;
				DISP_INFO("LOCAL_HBM_NORMAL_WHITE_110NIT in doze_hbm_dbv_level\n");
				break;
			case DOZE_BRIGHTNESS_LBM:
				update_bl_lvl = mi_cfg->doze_lbm_dbv_level;
				DISP_INFO("LOCAL_HBM_NORMAL_WHITE_110NIT in doze_lbm_dbv_level\n");
				break;
			default:
				update_bl_lvl = mi_cfg->doze_lbm_dbv_level;
				DISP_INFO("LOCAL_HBM_NORMAL_WHITE_110NIT use doze_lbm_dbv_level as defaults\n");
				break;
			}
		}
		switch (mi_get_panel_id_by_dsi_panel(panel)) {
		case N2_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N2(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N3_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N3(panel, lhbm_type, update_bl_lvl, lhbm_is_0size);
			break;
		default:
			DISP_ERROR("unknown panel name\n");
			break;
		}
		rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
		panel->mi_cfg.lhbm_0size_on = false;
		break;
	case LOCAL_HBM_NORMAL_GREEN_500NIT:
		DISP_INFO("LOCAL_HBM_NORMAL_GREEN_500NIT\n");
		mi_cfg->dimming_state = STATE_DIM_BLOCK;
		lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_NORMAL_GREEN_500NIT;
		update_bl_lvl = mi_cfg->last_bl_level;
		switch (mi_get_panel_id_by_dsi_panel(panel)) {
		case N2_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N2(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N3_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N3(panel, lhbm_type, update_bl_lvl, lhbm_is_0size);
			break;
		default:
			DISP_ERROR("unknown panel name\n");
			break;
		}
		rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
		panel->mi_cfg.lhbm_0size_on = false;
		break;
	case LOCAL_HBM_HLPM_WHITE_1000NIT:
		DISP_DEBUG("LOCAL_HBM_HLPM_WHITE_1000NIT\n");
		mi_cfg->dimming_state = STATE_DIM_BLOCK;
		lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_1000NIT;
		update_bl_lvl = mi_cfg->last_bl_level;

		if (is_aod_and_panel_initialized(panel)) {
			switch (mi_cfg->doze_brightness) {
			case DOZE_BRIGHTNESS_HBM:
				update_bl_lvl = mi_cfg->doze_hbm_dbv_level;
				DISP_INFO("LOCAL_HBM_HLPM_WHITE_1000NIT in doze_hbm_dbv_level\n");
				break;
			case DOZE_BRIGHTNESS_LBM:
				update_bl_lvl = mi_cfg->doze_lbm_dbv_level;
				DISP_INFO("LOCAL_HBM_HLPM_WHITE_1000NIT in doze_lbm_dbv_level\n");
				break;
			default:
				update_bl_lvl = mi_cfg->doze_hbm_dbv_level;
				DISP_INFO("LOCAL_HBM_HLPM_WHITE_1000NIT use doze_hbm_dbv_level as defaults\n");
				break;
			}
		}
		mi_dsi_update_51_mipi_cmd(panel, lhbm_type, update_bl_lvl);
		mi_dsi_update_backlight_in_aod(panel, false);
		switch (mi_get_panel_id_by_dsi_panel(panel)) {
		case N2_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N2(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N3_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N3(panel, lhbm_type, update_bl_lvl, lhbm_is_0size);
			break;
		default:
			DISP_ERROR("unknown panel name\n");
			break;
		}
		rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
		panel->mi_cfg.panel_state = PANEL_STATE_ON;
		break;
	case LOCAL_HBM_HLPM_WHITE_110NIT:
		DISP_DEBUG("LOCAL_HBM_HLPM_WHITE_110NIT\n");
		mi_cfg->dimming_state = STATE_DIM_BLOCK;
		lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_HLPM_WHITE_110NIT;
		update_bl_lvl = mi_cfg->last_bl_level;

		if (is_aod_and_panel_initialized(panel)) {
			switch (mi_cfg->doze_brightness) {
			case DOZE_BRIGHTNESS_HBM:
				update_bl_lvl = mi_cfg->doze_hbm_dbv_level;
				DISP_INFO("LOCAL_HBM_HLPM_WHITE_110NIT in doze_hbm_dbv_level\n");
				break;
			case DOZE_BRIGHTNESS_LBM:
				update_bl_lvl = mi_cfg->doze_lbm_dbv_level;
				DISP_INFO("LOCAL_HBM_HLPM_WHITE_110NIT in doze_lbm_dbv_level\n");
				break;
			default:
				update_bl_lvl = mi_cfg->doze_lbm_dbv_level;
				DISP_INFO("LOCAL_HBM_HLPM_WHITE_1000NIT use doze_lbm_dbv_level as defaults\n");
				break;
			}
		}
		mi_dsi_update_51_mipi_cmd(panel,lhbm_type, update_bl_lvl);

		mi_dsi_update_backlight_in_aod(panel, false);
		switch (mi_get_panel_id_by_dsi_panel(panel)) {
		case N2_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N2(panel, lhbm_type,
				mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE], update_bl_lvl);
			break;
		case N3_PANEL_PA:
			mi_dsi_panel_update_lhbm_param_N3(panel, lhbm_type, update_bl_lvl, lhbm_is_0size);
			break;
		default:
			DISP_ERROR("unknown panel name\n");
			break;
		}
		rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
		panel->mi_cfg.panel_state = PANEL_STATE_ON;
		break;
	case LOCAL_HBM_OFF_TO_HLPM:
		DISP_INFO("LOCAL_HBM_OFF_TO_HLPM\n");
		mi_cfg->dimming_state = STATE_DIM_BLOCK;
		panel->mi_cfg.panel_state = PANEL_STATE_DOZE_HIGH;
		lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_HLPM;
		rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
		break;
	case LOCAL_HBM_OFF_TO_LLPM:
		DISP_INFO("LOCAL_HBM_OFF_TO_LLPM\n");
		mi_cfg->dimming_state = STATE_DIM_BLOCK;
		panel->mi_cfg.panel_state = PANEL_STATE_DOZE_LOW;
		lhbm_type = DSI_CMD_SET_MI_LOCAL_HBM_OFF_TO_LLPM;
		rc = dsi_panel_tx_cmd_set(panel, lhbm_type);
		break;
	default:
		DISP_ERROR("invalid local hbm value\n");
		break;
	}

	if (mi_cfg->feature_val[DISP_FEATURE_FOD_CALIBRATION_BRIGHTNESS] > 0)
		mi_cfg->last_bl_level = last_bl_level_store;

	return rc;
}

static int mi_dsi_panel_aod_to_normal_optimize_locked(struct dsi_panel *panel,
		bool enable)
{
	int rc = 0;
	struct mi_dsi_panel_cfg *mi_cfg = NULL;

	if (!panel || !panel->panel_initialized) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mi_cfg = &panel->mi_cfg;

	if (!mi_cfg->need_fod_animal_in_normal)
		return 0;

	DISP_TIME_DEBUG("[%s] fod aod_to_normal: %d\n", panel->type, enable);

	if (is_hbm_fod_on(panel)) {
		DSI_INFO("fod hbm on, skip fod aod_to_normal: %d\n", enable);
		return 0;
	}

	if (enable && mi_cfg->panel_state != PANEL_STATE_ON) {
		switch (mi_cfg->doze_brightness) {
		case DOZE_BRIGHTNESS_HBM:
			DISP_INFO("enter DOZE HBM NOLP\n");
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_HBM_NOLP);
			if (rc) {
				DISP_ERROR("[%s] failed to send MI_DOZE_HBM_NOLP cmd, rc=%d\n",
					panel->type, rc);
			} else {
				panel->mi_cfg.panel_state = PANEL_STATE_ON;
				mi_cfg->aod_to_normal_statue = true;
			}
			break;
		case DOZE_BRIGHTNESS_LBM:
			DISP_INFO("enter DOZE LBM NOLP\n");
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_LBM_NOLP);
			if (rc) {
				DISP_ERROR("[%s] failed to send MI_DOZE_LBM_NOLP cmd, rc=%d\n",
					panel->type, rc);
			} else {
				panel->mi_cfg.panel_state = PANEL_STATE_ON;
				mi_cfg->aod_to_normal_statue = true;
			}
			break;
		default:
			break;
		}
	} else if (!enable && mi_cfg->panel_state == PANEL_STATE_ON &&
		panel->power_mode != SDE_MODE_DPMS_ON &&
		mi_cfg->feature_val[DISP_FEATURE_FP_STATUS] != AUTH_STOP) {
		switch (mi_cfg->doze_brightness) {
		case DOZE_BRIGHTNESS_HBM:
			DISP_INFO("enter DOZE HBM\n");
			mi_dsi_update_51_mipi_cmd(panel, DSI_CMD_SET_MI_DOZE_HBM,
				mi_cfg->doze_hbm_dbv_level);
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_HBM);
			if (rc) {
				DISP_ERROR("[%s] failed to send MI_DOZE_HBM_NOLP cmd, rc=%d\n",
					panel->type, rc);
			}
			panel->mi_cfg.panel_state = PANEL_STATE_DOZE_HIGH;
			mi_cfg->aod_to_normal_statue = false;
			break;
		case DOZE_BRIGHTNESS_LBM:
			DISP_INFO("enter DOZE LBM\n");
			mi_dsi_update_51_mipi_cmd(panel, DSI_CMD_SET_MI_DOZE_LBM,
					mi_cfg->doze_lbm_dbv_level);
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_LBM);
			if (rc) {
				DISP_ERROR("[%s] failed to send MI_DOZE_LBM_NOLP cmd, rc=%d\n",
					panel->type, rc);
			}
			panel->mi_cfg.panel_state = PANEL_STATE_DOZE_LOW;
			mi_cfg->aod_to_normal_statue = false;
			break;
		default:
			break;
		}
	} else {
		rc = -EAGAIN;
	}

	return rc;
}

bool mi_dsi_panel_is_need_tx_cmd(u32 feature_id)
{
	switch (feature_id) {
	case DISP_FEATURE_SENSOR_LUX:
	case DISP_FEATURE_LOW_BRIGHTNESS_FOD:
	case DISP_FEATURE_FP_STATUS:
	case DISP_FEATURE_FOLD_STATUS:
	case DISP_FEATURE_POWERSTATUS:
	case DISP_FEATURE_SYSTEM_BUILD_VERSION:
	case DISP_FEATURE_FRAME_DROP_COUNT:
		return false;
	default:
		return true;
	}
}

int mi_dsi_panel_set_disp_param(struct dsi_panel *panel, struct disp_feature_ctl *ctl)
{
	int rc = 0;
	struct mi_dsi_panel_cfg *mi_cfg = NULL;

	if (!panel || !ctl) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);

	DISP_TIME_INFO("[%s] feature: %s, value: %d\n", panel->type,
			get_disp_feature_id_name(ctl->feature_id), ctl->feature_val);


	mi_cfg = &panel->mi_cfg;

	if (!panel->panel_initialized &&
		mi_dsi_panel_is_need_tx_cmd(ctl->feature_id)) {
		if (ctl->feature_id == DISP_FEATURE_DC)
			mi_cfg->feature_val[DISP_FEATURE_DC] = ctl->feature_val;
		if (ctl->feature_id == DISP_FEATURE_DBI)
  			mi_cfg->feature_val[DISP_FEATURE_DBI] = ctl->feature_val;
		DISP_WARN("[%s] panel not initialized!\n", panel->type);
		rc = -ENODEV;
		goto exit;
	}


	switch (ctl->feature_id) {
	case DISP_FEATURE_DIMMING:
		if (!mi_cfg->disable_ic_dimming){
			if (mi_cfg->dimming_state != STATE_DIM_BLOCK) {
				if (ctl->feature_val == FEATURE_ON )
					rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DIMMINGON);
				else
					rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DIMMINGOFF);
				mi_cfg->feature_val[DISP_FEATURE_DIMMING] = ctl->feature_val;
			} else {
				DISP_INFO("skip dimming %s\n", ctl->feature_val ? "on" : "off");
			}
		} else{
			DISP_INFO("disable_ic_dimming is %d\n", mi_cfg->disable_ic_dimming);
		}
		break;
	case DISP_FEATURE_HBM:
		mi_cfg->feature_val[DISP_FEATURE_HBM] = ctl->feature_val;
#ifdef CONFIG_FACTORY_BUILD
		if (ctl->feature_val == FEATURE_ON) {
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_ON);
			mi_cfg->dimming_state = STATE_DIM_BLOCK;
		} else {
			mi_dsi_update_51_mipi_cmd(panel, DSI_CMD_SET_MI_HBM_OFF,
					mi_cfg->last_bl_level);
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_OFF);
			mi_cfg->dimming_state = STATE_DIM_RESTORE;
		}
#endif
		mi_disp_feature_event_notify_by_type(mi_get_disp_id(panel->type),
				MI_DISP_EVENT_HBM, sizeof(ctl->feature_val), ctl->feature_val);
		break;
	case DISP_FEATURE_HBM_FOD:
		DISP_INFO("TODO\n");
		break;
	case DISP_FEATURE_DOZE_BRIGHTNESS:
#ifdef CONFIG_FACTORY_BUILD
		if (dsi_panel_initialized(panel) &&
			is_aod_brightness(ctl->feature_val)) {
			if (ctl->feature_val == DOZE_BRIGHTNESS_HBM)
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_HBM);
			else
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_LBM);
		} else {
			if (mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N3_PANEL_PA) {
				mi_dsi_update_switch_cmd_N3(panel, DSI_CMD_SET_NOLP_UPDATE,
							DSI_CMD_SET_NOLP);
			}
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_NOLP);
			dsi_panel_update_backlight(panel, mi_cfg->last_bl_level);
		}
#else
		if (is_aod_and_panel_initialized(panel) &&
			is_aod_brightness(ctl->feature_val)) {
			if (ctl->feature_val == DOZE_BRIGHTNESS_HBM) {
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_HBM);
			} else {
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DOZE_LBM);
			}
		} else {
			if (mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N3_PANEL_PA) {
				mi_dsi_update_switch_cmd_N3(panel, DSI_CMD_SET_NOLP_UPDATE,
							DSI_CMD_SET_NOLP);
			}
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_NOLP);
		}
#endif
		mi_cfg->feature_val[DISP_FEATURE_DOZE_BRIGHTNESS] = ctl->feature_val;
		break;
	case DISP_FEATURE_FOD_CALIBRATION_BRIGHTNESS:
		if (ctl->feature_val == -1) {
			DISP_INFO("FOD calibration brightness restore last_bl_level=%d\n",
				mi_cfg->last_bl_level);
			dsi_panel_update_backlight(panel, mi_cfg->last_bl_level);
			mi_cfg->in_fod_calibration = false;
		} else {
			if (ctl->feature_val >= 0 &&
				ctl->feature_val <= panel->bl_config.bl_max_level) {
				mi_cfg->in_fod_calibration = true;
				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_DIMMINGOFF);
				dsi_panel_update_backlight(panel, ctl->feature_val);
				mi_cfg->dimming_state = STATE_NONE;
			} else {
				mi_cfg->in_fod_calibration = false;
				DISP_ERROR("FOD calibration invalid brightness level:%d\n",
						ctl->feature_val);
			}
		}
		mi_cfg->feature_val[DISP_FEATURE_FOD_CALIBRATION_BRIGHTNESS] = ctl->feature_val;
		break;
	case DISP_FEATURE_FOD_CALIBRATION_HBM:
		if (ctl->feature_val == -1) {
			DISP_INFO("FOD calibration HBM restore last_bl_level=%d\n",
					mi_cfg->last_bl_level);
			mi_dsi_update_51_mipi_cmd(panel, DSI_CMD_SET_MI_HBM_FOD_OFF,
					mi_cfg->last_bl_level);
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_FOD_OFF);
			mi_cfg->dimming_state = STATE_DIM_RESTORE;
			mi_cfg->in_fod_calibration = false;
		} else {
			mi_cfg->in_fod_calibration = true;
			mi_cfg->dimming_state = STATE_DIM_BLOCK;
			rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_HBM_FOD_ON);
		}
		mi_cfg->feature_val[DISP_FEATURE_FOD_CALIBRATION_HBM] = ctl->feature_val;
		break;
	case DISP_FEATURE_FLAT_MODE:
		if (!mi_cfg->flat_sync_te) {
			if (ctl->feature_val == FEATURE_ON) {
				DISP_INFO("flat mode on\n");
				if (mi_get_panel_id_by_dsi_panel(panel) == N2_PANEL_PA)
					mi_dsi_update_timing_switch_and_flat_mode_cmd(panel, DSI_CMD_SET_MI_FLAT_MODE_ON);
				else if (mi_get_panel_id_by_dsi_panel(panel) == N3_PANEL_PA &&
					panel->id_config.build_id >= N3_PANEL_PA_P11)
					mi_dsi_update_flat_mode_on_cmd(panel, DSI_CMD_SET_MI_FLAT_MODE_ON);

				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_ON);
				rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_SEC_ON);
			}
			else {
				DISP_INFO("flat mode off\n");
				if (mi_get_panel_id_by_dsi_panel(panel) == N2_PANEL_PA)
					mi_dsi_update_timing_switch_and_flat_mode_cmd(panel, DSI_CMD_SET_MI_FLAT_MODE_OFF);

				rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_OFF);
				rc |= dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_FLAT_MODE_SEC_OFF);
			}
			mi_disp_feature_event_notify_by_type(mi_get_disp_id(panel->type),
				MI_DISP_EVENT_FLAT_MODE, sizeof(ctl->feature_val), ctl->feature_val);
		} else {
			rc = mi_dsi_send_flat_sync_with_te_locked(panel,
					ctl->feature_val == FEATURE_ON);
		}
		mi_cfg->feature_val[DISP_FEATURE_FLAT_MODE] = ctl->feature_val;
		break;
	case DISP_FEATURE_CRC:
		DISP_INFO("TODO\n");
		break;
	case DISP_FEATURE_DC:
		DISP_INFO("DC mode state:%d\n", ctl->feature_val);
		if (mi_cfg->dc_feature_enable) {
			rc = mi_dsi_panel_set_dc_mode_locked(panel, ctl->feature_val == FEATURE_ON);
		}
		mi_cfg->feature_val[DISP_FEATURE_DC] = ctl->feature_val;
		mi_disp_feature_event_notify_by_type(mi_get_disp_id(panel->type),
				MI_DISP_EVENT_DC, sizeof(ctl->feature_val), ctl->feature_val);
		break;
	case DISP_FEATURE_LOCAL_HBM:
		rc = mi_dsi_panel_set_lhbm_fod_locked(panel, ctl);
		mi_cfg->feature_val[DISP_FEATURE_LOCAL_HBM] = ctl->feature_val;
		break;
	case DISP_FEATURE_SENSOR_LUX:
		DISP_DEBUG("DISP_FEATURE_SENSOR_LUX=%d\n", ctl->feature_val);
		mi_cfg->feature_val[DISP_FEATURE_SENSOR_LUX] = ctl->feature_val;
		break;
	case DISP_FEATURE_FP_STATUS:
		mi_cfg->feature_val[DISP_FEATURE_FP_STATUS] = ctl->feature_val;
		break;
	case DISP_FEATURE_FOLD_STATUS:
		DISP_INFO("DISP_FEATURE_FOLD_STATUS=%d\n", ctl->feature_val);
		mi_cfg->feature_val[DISP_FEATURE_FOLD_STATUS] = ctl->feature_val;
		break;
	case DISP_FEATURE_NATURE_FLAT_MODE:
		DISP_INFO("TODO\n");
		break;
	case DISP_FEATURE_SPR_RENDER:
		DISP_INFO("TODO\n");
		break;
	case DISP_FEATURE_AOD_TO_NORMAL:
		rc = mi_dsi_panel_aod_to_normal_optimize_locked(panel,
				ctl->feature_val == FEATURE_ON);
		mi_cfg->feature_val[DISP_FEATURE_AOD_TO_NORMAL] = ctl->feature_val;
		break;
	case DISP_FEATURE_COLOR_INVERT:
		DISP_INFO("TODO\n");
		break;
	case DISP_FEATURE_DC_BACKLIGHT:
		DISP_INFO("TODO\n");
		break;
	case DISP_FEATURE_GIR:
		DISP_INFO("TODO\n");
		break;
	case DISP_FEATURE_DBI:
		if (mi_get_panel_id_by_dsi_panel(panel) == N2_PANEL_PA) {
			DISP_INFO("by temp: %d, bl_lvl: %x\n", ctl->feature_val,
						panel->mi_cfg.last_bl_level);
			mi_cfg->feature_val[DISP_FEATURE_DBI] = ctl->feature_val;
			if (panel->mi_cfg.last_bl_level < 0x84 && panel->mi_cfg.last_bl_level
			&& !is_aod_and_panel_initialized(panel)) {
				dsi_panel_update_backlight(panel, mi_cfg->last_bl_level);
			}
		} else {
			DISP_INFO("by temp gray_level:%x\n", ctl->feature_val);
			if (mi_cfg->feature_val[DISP_FEATURE_DBI] == ctl->feature_val) {
				DISP_INFO("gray_level is the same, return\n");
				break;
			}
			mi_dsi_panel_set_dbi_by_temp_locked(panel, ctl->feature_val);
			mi_cfg->feature_val[DISP_FEATURE_DBI] = ctl->feature_val;
		}
		break;
	case DISP_FEATURE_DDIC_ROUND_CORNER:
		DISP_INFO("DDIC round corner state:%d\n", ctl->feature_val);
		rc = mi_dsi_panel_set_round_corner_locked(panel, ctl->feature_val == FEATURE_ON);
		mi_cfg->feature_val[DISP_FEATURE_DDIC_ROUND_CORNER] = ctl->feature_val;
		break;
	case DISP_FEATURE_HBM_BACKLIGHT:
		DISP_INFO("hbm backlight:%d\n", ctl->feature_val);
		panel->mi_cfg.last_bl_level = ctl->feature_val;
		dsi_panel_update_backlight(panel, panel->mi_cfg.last_bl_level);
		break;
	case DISP_FEATURE_BACKLIGHT:
		DISP_INFO("backlight:%d\n", ctl->feature_val);
		panel->mi_cfg.last_bl_level = ctl->feature_val;
		dsi_panel_update_backlight(panel, panel->mi_cfg.last_bl_level);
		break;
	case DISP_FEATURE_POWERSTATUS:
		DISP_DEBUG("power get:%d, last power mode = %d\n", ctl->feature_val, panel->power_mode);
		break;
	case DISP_FEATURE_SYSTEM_BUILD_VERSION:
		DISP_INFO("system build version:%s\n", ctl->tx_ptr);
		break;
	case DISP_FEATURE_FRAME_DROP_COUNT:
		DISP_INFO("frame drop count:%d\n", ctl->feature_val);
		break;
	default:
		DISP_ERROR("invalid feature argument: %d\n", ctl->feature_id);
		break;
	}
exit:
	mutex_unlock(&panel->panel_lock);
	return rc;
}

int mi_dsi_panel_get_disp_param(struct dsi_panel *panel,
			struct disp_feature_ctl *ctl)
{
	struct mi_dsi_panel_cfg *mi_cfg;
	int i = 0;

	if (!panel || !ctl) {
		DISP_ERROR("invalid params\n");
		return -EINVAL;
	}

	if (!is_support_disp_feature_id(ctl->feature_id)) {
		DISP_ERROR("unsupported disp feature id\n");
		return -EINVAL;
	}

	mutex_lock(&panel->panel_lock);
	mi_cfg = &panel->mi_cfg;
	for (i = DISP_FEATURE_DIMMING; i < DISP_FEATURE_MAX; i++) {
		if (i == ctl->feature_id) {
			ctl->feature_val =  mi_cfg->feature_val[i];
			DISP_INFO("%s: %d\n", get_disp_feature_id_name(ctl->feature_id),
				ctl->feature_val);
		}
	}
	mutex_unlock(&panel->panel_lock);

	return 0;
}

ssize_t mi_dsi_panel_show_disp_param(struct dsi_panel *panel,
			char *buf, size_t size)
{
	struct mi_dsi_panel_cfg *mi_cfg;
	ssize_t count = 0;
	int i = 0;

	if (!panel || !buf || !size) {
		DISP_ERROR("invalid params\n");
		return -EAGAIN;
	}

	count = snprintf(buf, size, "%040s: feature vaule\n", "feature name[feature id]");

	mutex_lock(&panel->panel_lock);
	mi_cfg = &panel->mi_cfg;
	for (i = DISP_FEATURE_DIMMING; i < DISP_FEATURE_MAX; i++) {
		count += snprintf(buf + count, size - count, "%036s[%02d]: %d\n",
				get_disp_feature_id_name(i), i, mi_cfg->feature_val[i]);

	}
	mutex_unlock(&panel->panel_lock);

	return count;
}

int dsi_panel_parse_build_id_read_config(struct dsi_panel *panel)
{
	struct drm_panel_build_id_config *id_config;
	struct dsi_parser_utils *utils = &panel->utils;
	int rc = 0;

	if (!panel) {
		DISP_ERROR("Invalid Params\n");
		return -EINVAL;
	}

	id_config = &panel->id_config;
	id_config->build_id = 0;
	if (!id_config)
		return -EINVAL;

	rc = utils->read_u32(utils->data, "mi,panel-build-id-read-length",
                                &id_config->id_cmds_rlen);
	if (rc) {
		id_config->id_cmds_rlen = 0;
		return -EINVAL;
	}

	dsi_panel_parse_cmd_sets_sub(&id_config->id_cmd,
			DSI_CMD_SET_MI_PANEL_BUILD_ID, utils);
	if (!(id_config->id_cmd.count)) {
		DISP_ERROR("panel build id read command parsing failed\n");
		return -EINVAL;
	}
	return 0;
}

int dsi_panel_parse_wp_reg_read_config(struct dsi_panel *panel)
{
	struct drm_panel_wp_config *wp_config;
	struct dsi_parser_utils *utils = &panel->utils;
	int rc = 0;

	if (!panel) {
		DISP_ERROR("Invalid Params\n");
		return -EINVAL;
	}

	wp_config = &panel->wp_config;
	if (!wp_config)
		return -EINVAL;

	dsi_panel_parse_cmd_sets_sub(&wp_config->wp_cmd,
			DSI_CMD_SET_MI_PANEL_WP_READ, utils);
	if (!wp_config->wp_cmd.count) {
		DISP_ERROR("wp_info read command parsing failed\n");
		return -EINVAL;
	}

	dsi_panel_parse_cmd_sets_sub(&wp_config->pre_tx_cmd,
			DSI_CMD_SET_MI_PANEL_WP_READ_PRE_TX, utils);
	if (!wp_config->pre_tx_cmd.count)
		DISP_INFO("wp_info pre command parsing failed\n");

	rc = utils->read_u32(utils->data, "mi,mdss-dsi-panel-wp-read-length",
				&wp_config->wp_cmds_rlen);
	if (rc || !wp_config->wp_cmds_rlen) {
		wp_config->wp_cmds_rlen = 0;
		return -EINVAL;
	}

	rc = utils->read_u32(utils->data, "mi,mdss-dsi-panel-wp-read-index",
				&wp_config->wp_read_info_index);
	if (rc || !wp_config->wp_read_info_index) {
		wp_config->wp_read_info_index = 0;
	}

	wp_config->return_buf = kcalloc(wp_config->wp_cmds_rlen,
		sizeof(unsigned char), GFP_KERNEL);
	if (!wp_config->return_buf) {
		return -ENOMEM;
	}
	return 0;
}

int dsi_panel_parse_cell_id_read_config(struct dsi_panel *panel)
{
	struct drm_panel_cell_id_config *cell_id_config;
	struct dsi_parser_utils *utils = &panel->utils;
	int rc = 0;

	if (!panel) {
		DISP_ERROR("Invalid Params\n");
		return -EINVAL;
	}

	cell_id_config = &panel->cell_id_config;
	if (!cell_id_config)
		return -EINVAL;

	dsi_panel_parse_cmd_sets_sub(&cell_id_config->cell_id_cmd,
			DSI_CMD_SET_MI_PANEL_CELL_ID_READ, utils);
	if (!cell_id_config->cell_id_cmd.count) {
		DISP_ERROR("cell_id_info read command parsing failed\n");
		return -EINVAL;
	}

	dsi_panel_parse_cmd_sets_sub(&cell_id_config->pre_tx_cmd,
			DSI_CMD_SET_MI_PANEL_CELL_ID_READ_PRE_TX, utils);
	if (!cell_id_config->pre_tx_cmd.count)
		DISP_INFO("cell_id_info pre command parsing failed\n");

	dsi_panel_parse_cmd_sets_sub(&cell_id_config->after_tx_cmd,
			DSI_CMD_SET_MI_PANEL_CELL_ID_READ_AFTER_TX, utils);
	if (!cell_id_config->after_tx_cmd.count)
		DISP_INFO("cell_id_info after command parsing failed\n");

	rc = utils->read_u32(utils->data, "mi,mdss-dsi-panel-cell-id-read-length",
				&cell_id_config->cell_id_cmds_rlen);
	if (rc || !cell_id_config->cell_id_cmds_rlen) {
		cell_id_config->cell_id_cmds_rlen = 0;
		return -EINVAL;
	}

	rc = utils->read_u32(utils->data, "mi,mdss-dsi-panel-cell-id-read-index",
				&cell_id_config->cell_id_read_info_index);
	if (rc || !cell_id_config->cell_id_read_info_index) {
		cell_id_config->cell_id_read_info_index = 0;
	}

	cell_id_config->return_buf = kcalloc(cell_id_config->cell_id_cmds_rlen,
		sizeof(unsigned char), GFP_KERNEL);
	if (!cell_id_config->return_buf) {
		return -ENOMEM;
	}
	return 0;
}

int mi_dsi_panel_ip_set_by_dbv(struct dsi_panel *panel, u32 bl_lvl)
{
	int rc = 0;
	struct mi_dsi_panel_cfg *mi_cfg = NULL;

	if (!panel)
		return -EINVAL;

	if (mi_get_panel_id_by_dsi_panel(panel) != N2_PANEL_PA)
		return 0;

	mi_cfg = &panel->mi_cfg;
	if (bl_lvl  >= 0x160 && !mi_cfg->ip_state) {
		mi_dsi_update_51_mipi_cmd(panel, DSI_CMD_SET_MI_IP_ON, bl_lvl);
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_IP_ON);
		if (rc) {
			DISP_ERROR("(%s) send DSI_CMD_SET_MI_IP_ON failed! bl_lvl(%d),rc(%d)\n",
					panel->name, bl_lvl, rc);
			return rc;
		} else {
			mi_cfg->ip_state = true;
			DISP_INFO("(%s) send DSI_CMD_SET_MI_IP_ON! bl_lvl(%d),rc(%d)\n",
					panel->name, bl_lvl, rc);
		}
	} else if (bl_lvl != 0 && bl_lvl < 0x160 && mi_cfg->ip_state) {
		mi_dsi_update_51_mipi_cmd(panel, DSI_CMD_SET_MI_IP_OFF, bl_lvl);
		rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_MI_IP_OFF);
		if (rc) {
			DISP_ERROR("(%s) send DSI_CMD_SET_MI_IP_OFF failed! bl_lvl(%d),rc(%d)\n",
					panel->name, bl_lvl, rc);
			return rc;
		} else {
			mi_cfg->ip_state = false;
			DISP_INFO("(%s) send DSI_CMD_SET_MI_IP_OFF! bl_lvl(%d),rc(%d)\n",
					panel->name, bl_lvl, rc);
		}
	}

	return rc;
}

static inline void mi_dsi_panel_lgs_cie_by_temperature(enum dsi_cmd_set_type *dbi_bwg_type, int bl_lvl, int temp)
{
	if (temp >= 36100) {
		if (0x84 > bl_lvl && bl_lvl >= 0x4C) {
			*dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_41_MODE;
		} else if (0x4C > bl_lvl && bl_lvl >= 0x28) {
			*dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_42_MODE;
		} else if (0x28 > bl_lvl && bl_lvl >= 0x19) {
			*dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_43_MODE;
		}
	}else if ( 36100 > temp && temp >= 32100) {
		if (0x84 > bl_lvl && bl_lvl >= 0x4C) {
			*dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_44_MODE;
		} else if (0x4C > bl_lvl && bl_lvl >= 0x28) {
			*dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_45_MODE;
		} else if (0x28 > bl_lvl && bl_lvl >= 0x19) {
			*dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_46_MODE;
		}
	} else if (32100 > temp && temp >= 28100) {
		if (0x84 > bl_lvl && bl_lvl >= 0x4C) {
			*dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_47_MODE;
		} else if (0x4C > bl_lvl && bl_lvl >= 0x28) {
			*dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_48_MODE;
		} else if (0x28 > bl_lvl && bl_lvl >= 0x19) {
			*dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_49_MODE;
		}
	}
}

int mi_dsi_panel_vrr_set_by_dbv(struct dsi_panel *panel, u32 bl_lvl)
{
	int rc = 0;
	struct mi_dsi_panel_cfg *mi_cfg;
	enum dsi_cmd_set_type skip_source_type, dbi_bwg_type;
	int temp = 0;

	if (!panel)
		return -EINVAL;

	if (mi_get_panel_id_by_dsi_panel(panel) != N2_PANEL_PA)
		return 0;

	mi_cfg = &panel->mi_cfg;

	skip_source_type = dbi_bwg_type = DSI_CMD_SET_MAX;
	if (bl_lvl == 0) {
		mi_cfg->skip_source_type = DSI_CMD_SET_MAX;
		mi_cfg->dbi_bwg_type = DSI_CMD_SET_MAX;
		return -EINVAL;
	}

	if (bl_lvl  >= 0x7FF) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_500_1400NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_500_1400NIT_MODE;
	} else if (0x7FF > bl_lvl && bl_lvl >= 0x333) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_200_500NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_200_500NIT_MODE;
	} else if (0x333 > bl_lvl && bl_lvl >= 0x1C2) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_110_200NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_110_200NIT_MODE;
	} else if (0x1C2 > bl_lvl && bl_lvl >= 0x147) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_80_110NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_80_110NIT_MODE;
	} else if (0x147 > bl_lvl && bl_lvl >= 0x12B) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_72_79NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_72_79NIT_MODE;
	} else if (0x12B > bl_lvl && bl_lvl >= 0x110) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_65_72NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_65_72NIT_MODE;
	} else if (0x110 > bl_lvl && bl_lvl >= 0xF5) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_60_65NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_60_65NIT_MODE;
	} else if (0xF5 > bl_lvl && bl_lvl >= 0xDC) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_53_60NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_53_60NIT_MODE;
	} else if (0xDC > bl_lvl && bl_lvl >= 0x84) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_30_53NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_30_53NIT_MODE;
	} else if ( 0x84 > bl_lvl && bl_lvl && panel->id_config.build_id == N2_PANEL_PA_P11_02 ) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_30_53NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_0_30NIT_MODE;
	} else if (0x84 > bl_lvl && bl_lvl >= 0x4C && panel->id_config.build_id > N2_PANEL_PA_P11_02) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_19_30NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_19_30NIT_MODE;
		temp = mi_cfg->feature_val[DISP_FEATURE_DBI];
		mi_dsi_panel_lgs_cie_by_temperature(&dbi_bwg_type, bl_lvl, temp);
	} else if (0x4C > bl_lvl && bl_lvl >= 0x28&& panel->id_config.build_id > N2_PANEL_PA_P11_02) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_10_19NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_10_19NIT_MODE;
		temp = mi_cfg->feature_val[DISP_FEATURE_DBI];
		mi_dsi_panel_lgs_cie_by_temperature(&dbi_bwg_type, bl_lvl, temp);
	} else if (0x28 > bl_lvl && bl_lvl >= 0x19&& panel->id_config.build_id > N2_PANEL_PA_P11_02) {
		skip_source_type = DSI_CMD_SET_MI_SKIP_SOURCE_6_10NIT_MODE;
		dbi_bwg_type = DSI_CMD_SET_MI_DBI_BWG_6_10NIT_MODE;
		temp = mi_cfg->feature_val[DISP_FEATURE_DBI];
		mi_dsi_panel_lgs_cie_by_temperature(&dbi_bwg_type, bl_lvl, temp);
	}

	if ((mi_cfg->skip_source_type != skip_source_type) && (skip_source_type != DSI_CMD_SET_MAX)) {
		rc = dsi_panel_tx_cmd_set(panel, skip_source_type);
		if (rc) {
			DISP_ERROR("(%s) send  cmds(%s) failed! bl_lvl(%d),rc(%d)\n",
					panel->name, cmd_set_prop_map[skip_source_type], bl_lvl, rc);
			return rc;
		}
		mi_cfg->skip_source_type = skip_source_type;
		DISP_DEBUG("Send SKip Source(%s), temp = %d, bl_lvl = 0x%x\n",
				cmd_set_prop_map[skip_source_type], temp, bl_lvl);
	}

	if ((mi_cfg->dbi_bwg_type != dbi_bwg_type) && (dbi_bwg_type != DSI_CMD_SET_MAX)) {
		rc = dsi_panel_tx_cmd_set(panel, dbi_bwg_type);
		if (rc) {
			DISP_ERROR("(%s) send  cmds(%s) failed! bl_lvl(%d),rc(%d)\n",
					panel->name, cmd_set_prop_map[dbi_bwg_type], bl_lvl, rc);
			return rc;
		}
		mi_cfg->dbi_bwg_type = dbi_bwg_type;
		DISP_DEBUG("Send DBI BWG(%s), temp = %d, bl_lvl = 0x%x\n",
				cmd_set_prop_map[dbi_bwg_type], temp, bl_lvl);
	}

	return rc;
}

int mi_dsi_panel_set_nolp_locked(struct dsi_panel *panel)
{
	int rc = 0;
	u32 doze_brightness = panel->mi_cfg.doze_brightness;
	int update_bl = 0;

	if (panel->mi_cfg.panel_state == PANEL_STATE_ON) {
		DISP_INFO("panel already PANEL_STATE_ON, skip nolp");
		return rc;
	}

	if (doze_brightness == DOZE_TO_NORMAL)
		doze_brightness = panel->mi_cfg.last_doze_brightness;

	switch (doze_brightness) {
		case DOZE_BRIGHTNESS_HBM:
			DISP_INFO("set doze_hbm_dbv_level in nolp");
			update_bl = panel->mi_cfg.doze_hbm_dbv_level;
			break;
		case DOZE_BRIGHTNESS_LBM:
			DISP_INFO("set doze_lbm_dbv_level in nolp");
			update_bl = panel->mi_cfg.doze_lbm_dbv_level;
			break;
		default:
			break;
	}
	mi_dsi_update_51_mipi_cmd(panel, DSI_CMD_SET_NOLP, update_bl);

	if (mi_get_panel_id(panel->mi_cfg.mi_panel_id) == N3_PANEL_PA)
		mi_dsi_update_switch_cmd_N3(panel, DSI_CMD_SET_NOLP_UPDATE, DSI_CMD_SET_NOLP);

	rc = dsi_panel_tx_cmd_set(panel, DSI_CMD_SET_NOLP);

	panel->power_mode = SDE_MODE_DPMS_ON;

	return rc;
}
