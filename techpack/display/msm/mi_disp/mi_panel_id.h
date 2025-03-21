/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2020 XiaoMi, Inc. All rights reserved.
 */

#ifndef _MI_PANEL_ID_H_
#define _MI_PANEL_ID_H_

#include <linux/types.h>
#include "dsi_panel.h"

/*
   Naming Rules,Wiki Dec .
   4Byte : Project ASCII Value .Exemple "L18" ASCII Is 004C3138
   1Byte : Prim Panel Is 'P' ASCII Value , Sec Panel Is 'S' Value
   1Byte : Panel Vendor
   1Byte : DDIC Vendor ,Samsung 0x0C Novatek 0x02
   1Byte : Production Batch Num
*/
#define N2_42_02_0A_PANEL_ID   0x00004E3200420200
#define N3_42_0D_0A_PANEL_ID   0x00004E3300420D00

/* PA: Primary display, First selection screen
 * PB: Primary display, Second selection screen
 * SA: Secondary display, First selection screen
 * SB: Secondary display, Second selection screen
 */
enum mi_project_panel_id {
	PANEL_ID_INVALID = 0,
	N2_PANEL_PA,
	N3_PANEL_PA,
	PANEL_ID_MAX
};

#define N2_PANEL_PA_P00 0x00
#define N2_PANEL_PA_P01 0x03
#define N2_PANEL_PA_P11_01 0x50
#define N2_PANEL_PA_P11_02 0x52
#define N2_PANEL_PA_P2_01 0x80
#define N2_PANEL_PA_P2_02 0x81

#define N3_PANEL_PA_P00 0x00
#define N3_PANEL_PA_P10 0x20
#define N3_PANEL_PA_P11 0x50
#define N3_PANEL_PA_P11_2_0 0x51
#define N3_PANEL_PA_P11_2_1 0x52
#define N3_PANEL_PA_P11_2_3 0x53
#define N3_PANEL_PA_P20 0x80
#define N3_PANEL_PA_MP 0xC0

static inline enum mi_project_panel_id mi_get_panel_id(u64 mi_panel_id)
{
	switch(mi_panel_id) {
	case N2_42_02_0A_PANEL_ID:
		return N2_PANEL_PA;
	case N3_42_0D_0A_PANEL_ID:
		return N3_PANEL_PA;
	default:
		return PANEL_ID_INVALID;
	}
}

static inline const char *mi_get_panel_id_name(u64 mi_panel_id)
{
	switch (mi_get_panel_id(mi_panel_id)) {
	case N2_PANEL_PA:
		return "N2_PANEL_PA";
	case N3_PANEL_PA:
		return "N3_PANEL_PA";
	default:
		return "unknown";
	}
}

static inline bool is_use_nvt_dsc_config(u64 mi_panel_id)
{
	switch(mi_panel_id) {
	case N2_42_02_0A_PANEL_ID:
		return true;
	default:
		return false;
	}
}

enum mi_project_panel_id mi_get_panel_id_by_dsi_panel(struct dsi_panel *panel);
enum mi_project_panel_id mi_get_panel_id_by_disp_id(int disp_id);

#endif /* _MI_PANEL_ID_H_ */
