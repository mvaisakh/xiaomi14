/*
 * Copyright (c) 2016-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) 2021-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include "qdf_mem.h"
#include <qdf_module.h>
#include "wlan_lmac_if_def.h"
#include "wlan_lmac_if_api.h"
#include "wlan_mgmt_txrx_tgt_api.h"
#include "wlan_scan_tgt_api.h"
#include <wlan_reg_services_api.h>
#include <wlan_reg_ucfg_api.h>
#ifdef WLAN_ATF_ENABLE
#include "wlan_atf_tgt_api.h"
#endif
#ifdef WLAN_SA_API_ENABLE
#include "wlan_sa_api_tgt_api.h"
#endif
#ifdef WIFI_POS_CONVERGED
#include "target_if_wifi_pos.h"
#include "target_if_wifi_pos_rx_ops.h"
#endif /* WIFI_POS_CONVERGED */
#include "wlan_reg_tgt_api.h"
#ifdef CONVERGED_P2P_ENABLE
#include "wlan_p2p_tgt_api.h"
#endif
#ifdef FEATURE_WLAN_TDLS
#include "wlan_tdls_tgt_api.h"
#endif

#include "wlan_crypto_global_api.h"
#ifdef DFS_COMPONENT_ENABLE
#include <wlan_dfs_tgt_api.h>
#include <wlan_objmgr_vdev_obj.h>
#include <wlan_dfs_utils_api.h>
#if defined(QCA_SUPPORT_DFS_CHAN_POSTNOL) || defined(QCA_DFS_BW_EXPAND)
#include <dfs_postnol_ucfg.h>
#endif
#endif

#ifdef WLAN_SUPPORT_GREEN_AP
#include <wlan_green_ap_api.h>
#include <wlan_green_ap_ucfg_api.h>
#endif
#include <wlan_ftm_ucfg_api.h>

#ifdef WLAN_SUPPORT_FILS
#include <wlan_fd_tgt_api.h>
#endif

#ifdef QCA_SUPPORT_CP_STATS
#include <wlan_cp_stats_tgt_api.h>
#include <wlan_cp_stats_utils_api.h>
#endif /* QCA_SUPPORT_CP_STATS */
#include <wlan_vdev_mgr_tgt_if_rx_api.h>

#ifdef WLAN_CFR_ENABLE
#include "wlan_cfr_tgt_api.h"
#endif

#ifdef WIFI_POS_CONVERGED
#include "wifi_pos_api.h"
#endif

#include "wlan_mgmt_txrx_rx_reo_tgt_api.h"

#ifdef WLAN_FEATURE_11BE_MLO
#include "wlan_mlo_mgr_cmn.h"
#include <wlan_mlo_t2lm.h>
#endif

#include <wlan_twt_tgt_if_rx_api.h>
#ifdef WLAN_FEATURE_MCC_QUOTA
#include <wlan_p2p_mcc_quota_tgt_api.h>
#endif

#ifdef WLAN_FEATURE_DBAM_CONFIG
#include "target_if_coex.h"
#endif
#if defined(WIFI_POS_CONVERGED) && defined(WLAN_FEATURE_RTT_11AZ_SUPPORT)
#include <wifi_pos_pasn_api.h>
#endif

#include "target_if.h"

/* Function pointer for OL/WMA specific UMAC tx_ops
 * registration.
 */
QDF_STATUS (*wlan_lmac_if_umac_tx_ops_register)
				(struct wlan_lmac_if_tx_ops *tx_ops);
qdf_export_symbol(wlan_lmac_if_umac_tx_ops_register);

/* Function pointer to call legacy crypto rxpn registration in OL */
QDF_STATUS (*wlan_lmac_if_umac_crypto_rxpn_ops_register)
				(struct wlan_lmac_if_rx_ops *rx_ops);
qdf_export_symbol(wlan_lmac_if_umac_crypto_rxpn_ops_register);

static void
tgt_vdev_mgr_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
	tgt_vdev_mgr_register_rx_ops(rx_ops);
}

#ifdef QCA_SUPPORT_CP_STATS
#if defined(WLAN_SUPPORT_TWT) && defined(WLAN_TWT_CONV_SUPPORTED)
/**
 * wlan_target_if_cp_stats_rx_ops_register() - register cp_stats rx ops
 * @rx_ops: lmac rx_ops
 *
 * Return: none
 */
static void
wlan_target_if_cp_stats_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
	rx_ops->cp_stats_rx_ops.twt_get_session_param_resp =
			tgt_cp_stats_twt_get_session_evt_handler;
}
#else
static void
wlan_target_if_cp_stats_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
}
#endif

/**
 * wlan_lmac_if_cp_stats_rx_ops_register() - API to register cp stats Rx Ops
 * @rx_ops:	pointer to lmac rx ops
 *
 * This API will be used to register function pointers for FW events
 *
 * Return: void
 */
static void
wlan_lmac_if_cp_stats_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
	wlan_target_if_cp_stats_rx_ops_register(rx_ops);
	tgt_cp_stats_register_rx_ops(rx_ops);
}
#else
static void
wlan_lmac_if_cp_stats_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
}
#endif /* QCA_SUPPORT_CP_STATS */

#ifdef DCS_INTERFERENCE_DETECTION
/**
 * wlan_target_if_dcs_rx_ops_register() - API to register dcs Rx Ops
 * @rx_ops:	pointer to lmac rx ops
 *
 * This API will be used to register function pointers for FW events
 *
 * Return: void
 */
static void
wlan_target_if_dcs_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
	rx_ops->dcs_rx_ops.process_dcs_event = tgt_dcs_process_event;
}
#else
static void
wlan_target_if_dcs_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
}
#endif /* DCS_INTERFERENCE_DETECTION */

#ifdef WLAN_ATF_ENABLE
#ifdef WLAN_ATF_INCREASED_STA
static void
wlan_lmac_if_atf_rx_ops_max_clients(struct wlan_lmac_if_atf_rx_ops *atf_rx_ops)
{
	atf_rx_ops->atf_set_fw_max_client_512_support =
				tgt_atf_set_fw_cap_max_client_512_support;
}
#else
static void
wlan_lmac_if_atf_rx_ops_max_clients(struct wlan_lmac_if_atf_rx_ops *atf_rx_ops)
{
}
#endif /* WLAN_ATF_INCREASED_STA */

/**
 * wlan_lmac_if_atf_rx_ops_register() - Function to register ATF RX ops.
 * @rx_ops: Pointer to wlan_lmac_if_rx_ops
 */
static void
wlan_lmac_if_atf_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
	struct wlan_lmac_if_atf_rx_ops *atf_rx_ops = &rx_ops->atf_rx_ops;

	/* ATF rx ops */
	atf_rx_ops->atf_get_fmcap = tgt_atf_get_fmcap;
	atf_rx_ops->atf_get_mode = tgt_atf_get_mode;
	atf_rx_ops->atf_get_msdu_desc = tgt_atf_get_msdu_desc;
	atf_rx_ops->atf_get_max_vdevs = tgt_atf_get_max_vdevs;
	atf_rx_ops->atf_get_peers = tgt_atf_get_peers;
	atf_rx_ops->atf_get_tput_based = tgt_atf_get_tput_based;
	atf_rx_ops->atf_get_logging = tgt_atf_get_logging;
	atf_rx_ops->atf_get_ssidgroup = tgt_atf_get_ssidgroup;
	atf_rx_ops->atf_get_vdev_ac_blk_cnt = tgt_atf_get_vdev_ac_blk_cnt;
	atf_rx_ops->atf_get_peer_blk_txbitmap = tgt_atf_get_peer_blk_txbitmap;
	atf_rx_ops->atf_get_vdev_blk_txtraffic = tgt_atf_get_vdev_blk_txtraffic;
	atf_rx_ops->atf_get_sched = tgt_atf_get_sched;
	atf_rx_ops->atf_get_peer_stats = tgt_atf_get_peer_stats;
	atf_rx_ops->atf_get_token_allocated = tgt_atf_get_token_allocated;
	atf_rx_ops->atf_get_token_utilized = tgt_atf_get_token_utilized;

	atf_rx_ops->atf_set_sched = tgt_atf_set_sched;
	atf_rx_ops->atf_set_fmcap = tgt_atf_set_fmcap;
	atf_rx_ops->atf_set_msdu_desc = tgt_atf_set_msdu_desc;
	atf_rx_ops->atf_set_max_vdevs = tgt_atf_set_max_vdevs;
	atf_rx_ops->atf_set_peers = tgt_atf_set_peers;
	atf_rx_ops->atf_set_peer_stats = tgt_atf_set_peer_stats;
	atf_rx_ops->atf_set_vdev_blk_txtraffic = tgt_atf_set_vdev_blk_txtraffic;
	atf_rx_ops->atf_peer_blk_txtraffic = tgt_atf_peer_blk_txtraffic;
	atf_rx_ops->atf_peer_unblk_txtraffic = tgt_atf_peer_unblk_txtraffic;
	atf_rx_ops->atf_set_token_allocated = tgt_atf_set_token_allocated;
	atf_rx_ops->atf_set_token_utilized = tgt_atf_set_token_utilized;
	atf_rx_ops->atf_process_tx_ppdu_stats = tgt_atf_process_tx_ppdu_stats;
	atf_rx_ops->atf_process_rx_ppdu_stats = tgt_atf_process_rx_ppdu_stats;
	atf_rx_ops->atf_is_stats_enabled = tgt_atf_is_stats_enabled;
	wlan_lmac_if_atf_rx_ops_max_clients(atf_rx_ops);
}
#else
static void
wlan_lmac_if_atf_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
}
#endif

#ifdef WLAN_SUPPORT_FILS
static void
wlan_lmac_if_fd_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
	struct wlan_lmac_if_fd_rx_ops *fd_rx_ops = &rx_ops->fd_rx_ops;

	fd_rx_ops->fd_is_fils_enable = tgt_fd_is_fils_enable;
	fd_rx_ops->fd_alloc = tgt_fd_alloc;
	fd_rx_ops->fd_stop = tgt_fd_stop;
	fd_rx_ops->fd_free = tgt_fd_free;
	fd_rx_ops->fd_get_valid_fd_period = tgt_fd_get_valid_fd_period;
	fd_rx_ops->fd_swfda_handler = tgt_fd_swfda_handler;
	fd_rx_ops->fd_offload = tgt_fd_offload;
	fd_rx_ops->fd_tmpl_update = tgt_fd_tmpl_update;
}
#else
static void
wlan_lmac_if_fd_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
}
#endif

#ifdef WLAN_SA_API_ENABLE
/**
 * wlan_lmac_if_sa_api_rx_ops_register() - Function to register SA_API RX ops.
 * @rx_ops: Pointer to wlan_lmac_if_rx_ops
 */
static void
wlan_lmac_if_sa_api_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
	struct wlan_lmac_if_sa_api_rx_ops *sa_api_rx_ops = &rx_ops->sa_api_rx_ops;

	/* SA API rx ops */
	sa_api_rx_ops->sa_api_get_sa_supported = tgt_sa_api_get_sa_supported;
	sa_api_rx_ops->sa_api_get_validate_sw  = tgt_sa_api_get_validate_sw;
	sa_api_rx_ops->sa_api_enable_sa        = tgt_sa_api_enable_sa;
	sa_api_rx_ops->sa_api_get_sa_enable    = tgt_sa_api_get_sa_enable;

	sa_api_rx_ops->sa_api_peer_assoc_hanldler = tgt_sa_api_peer_assoc_hanldler;
	sa_api_rx_ops->sa_api_update_tx_feedback = tgt_sa_api_update_tx_feedback;
	sa_api_rx_ops->sa_api_update_rx_feedback = tgt_sa_api_update_rx_feedback;

	sa_api_rx_ops->sa_api_ucfg_set_param = tgt_sa_api_ucfg_set_param;
	sa_api_rx_ops->sa_api_ucfg_get_param = tgt_sa_api_ucfg_get_param;

	sa_api_rx_ops->sa_api_is_tx_feedback_enabled = tgt_sa_api_is_tx_feedback_enabled;
	sa_api_rx_ops->sa_api_is_rx_feedback_enabled = tgt_sa_api_is_rx_feedback_enabled;

	sa_api_rx_ops->sa_api_convert_rate_2g = tgt_sa_api_convert_rate_2g;
	sa_api_rx_ops->sa_api_convert_rate_5g = tgt_sa_api_convert_rate_5g;
	sa_api_rx_ops->sa_api_get_sa_mode = tgt_sa_api_get_sa_mode;

	sa_api_rx_ops->sa_api_get_beacon_txantenna = tgt_sa_api_get_beacon_txantenna;
	sa_api_rx_ops->sa_api_cwm_action = tgt_sa_api_cwm_action;
}
#else
static void
wlan_lmac_if_sa_api_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
}
#endif

#ifdef WLAN_CFR_ENABLE
/**
 * wlan_lmac_if_cfr_rx_ops_register() - Function to register CFR RX ops
 * @rx_ops: Pointer to wlan_lmac_if_rx_ops
 */
static void
wlan_lmac_if_cfr_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
	struct wlan_lmac_if_cfr_rx_ops *cfr_rx_ops = &rx_ops->cfr_rx_ops;

	/* CFR rx ops */
	cfr_rx_ops->cfr_support_set = tgt_cfr_support_set;
	cfr_rx_ops->cfr_info_send  = tgt_cfr_info_send;
	cfr_rx_ops->cfr_capture_count_support_set =
		tgt_cfr_capture_count_support_set;
	cfr_rx_ops->cfr_mo_marking_support_set = tgt_cfr_mo_marking_support_set;
	cfr_rx_ops->cfr_aoa_for_rcc_support_set =
		tgt_cfr_aoa_for_rcc_support_set;
}
#else
static void
wlan_lmac_if_cfr_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
}
#endif

static void
wlan_lmac_if_crypto_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
	wlan_crypto_register_crypto_rx_ops(&rx_ops->crypto_rx_ops);
	if (wlan_lmac_if_umac_crypto_rxpn_ops_register)
		wlan_lmac_if_umac_crypto_rxpn_ops_register(rx_ops);
}

#if defined(WIFI_POS_CONVERGED) && defined(WLAN_FEATURE_RTT_11AZ_SUPPORT)
static void
wlan_lmac_if_wifi_pos_rx_ops(struct wlan_lmac_if_rx_ops *rx_ops)
{
	struct wlan_lmac_if_wifi_pos_rx_ops *wifi_pos_rx_ops =
		&rx_ops->wifi_pos_rx_ops;

	wifi_pos_rx_ops->wifi_pos_ranging_peer_create_cb =
			wifi_pos_handle_ranging_peer_create;
	wifi_pos_rx_ops->wifi_pos_ranging_peer_create_rsp_cb =
			wifi_pos_handle_ranging_peer_create_rsp;
	wifi_pos_rx_ops->wifi_pos_ranging_peer_delete_cb =
			wifi_pos_handle_ranging_peer_delete;
	wifi_pos_rx_ops->wifi_pos_vdev_delete_all_ranging_peers_rsp_cb =
			wifi_pos_vdev_delete_all_ranging_peers_rsp;
}
#else
static inline void
wlan_lmac_if_wifi_pos_rx_ops(struct wlan_lmac_if_rx_ops *rx_ops)
{}
#endif

#ifdef WIFI_POS_CONVERGED
static void
wlan_lmac_if_umac_rx_ops_register_wifi_pos(struct wlan_lmac_if_rx_ops *rx_ops)
{
	wifi_pos_register_rx_ops(rx_ops);
	wlan_lmac_if_wifi_pos_rx_ops(rx_ops);
}
#else
static void wlan_lmac_if_umac_rx_ops_register_wifi_pos(
				struct wlan_lmac_if_rx_ops *rx_ops)
{
}
#endif /* WIFI_POS_CONVERGED */

#ifdef CONFIG_BAND_6GHZ
static void wlan_lmac_if_register_master_list_ext_handler(
					struct wlan_lmac_if_rx_ops *rx_ops)
{
	rx_ops->reg_rx_ops.master_list_ext_handler =
		tgt_reg_process_master_chan_list_ext;
}

static void wlan_lmac_if_register_super_chan_display(
					struct wlan_lmac_if_rx_ops *rx_ops)
{
	rx_ops->reg_rx_ops.reg_display_super_chan_list =
		wlan_reg_display_super_chan_list;
}

#ifdef CONFIG_AFC_SUPPORT
static void wlan_lmac_if_register_afc_handlers(
					struct wlan_lmac_if_rx_ops *rx_ops)
{
	rx_ops->reg_rx_ops.afc_event_handler = tgt_reg_process_afc_event;
	rx_ops->reg_rx_ops.reg_set_afc_dev_type = tgt_reg_set_afc_dev_type;
	rx_ops->reg_rx_ops.reg_get_afc_dev_type = tgt_reg_get_afc_dev_type;
	rx_ops->reg_rx_ops.reg_set_eirp_preferred_support =
				tgt_reg_set_eirp_preferred_support;
	rx_ops->reg_rx_ops.reg_get_eirp_preferred_support =
				tgt_reg_get_eirp_preferred_support;
}
#else
static inline void wlan_lmac_if_register_afc_handlers(
					struct wlan_lmac_if_rx_ops *rx_ops)
{
}
#endif

#else
static inline void wlan_lmac_if_register_master_list_ext_handler(
					struct wlan_lmac_if_rx_ops *rx_ops)
{
}

static inline void wlan_lmac_if_register_afc_handlers(
					struct wlan_lmac_if_rx_ops *rx_ops)
{
}

static inline void wlan_lmac_if_register_super_chan_display(
					struct wlan_lmac_if_rx_ops *rx_ops)
{
}
#endif

#if defined(CONFIG_BAND_6GHZ)
static void wlan_lmac_if_register_6g_edge_chan_supp(
					struct wlan_lmac_if_rx_ops *rx_ops)
{
	rx_ops->reg_rx_ops.reg_set_lower_6g_edge_ch_supp =
		tgt_reg_set_lower_6g_edge_ch_supp;

	rx_ops->reg_rx_ops.reg_set_disable_upper_6g_edge_ch_supp =
		tgt_reg_set_disable_upper_6g_edge_ch_supp;
}
#else
static inline void wlan_lmac_if_register_6g_edge_chan_supp(
					struct wlan_lmac_if_rx_ops *rx_ops)
{
}
#endif

#ifdef WLAN_REG_PARTIAL_OFFLOAD
/**
 * wlan_lmac_if_umac_reg_rx_ops_register_po() - Function to register Reg RX ops
 * for Partial Offload
 * @rx_ops: Pointer to wlan_lmac_if_rx_ops
 *
 * Return: void
 */
static void wlan_lmac_if_umac_reg_rx_ops_register_po(
					struct wlan_lmac_if_rx_ops *rx_ops)
{
	rx_ops->reg_rx_ops.reg_program_default_cc =
		ucfg_reg_program_default_cc;

	rx_ops->reg_rx_ops.reg_get_current_regdomain =
		wlan_reg_get_curr_regdomain;
}
#else
static void wlan_lmac_if_umac_reg_rx_ops_register_po(
					struct wlan_lmac_if_rx_ops *rx_ops)
{
}
#endif

static void wlan_lmac_if_umac_reg_rx_ops_register(
	struct wlan_lmac_if_rx_ops *rx_ops)
{
	rx_ops->reg_rx_ops.master_list_handler =
		tgt_reg_process_master_chan_list;

	wlan_lmac_if_register_master_list_ext_handler(rx_ops);

	rx_ops->reg_rx_ops.reg_11d_new_cc_handler =
		tgt_reg_process_11d_new_country;

	rx_ops->reg_rx_ops.reg_set_regdb_offloaded =
		tgt_reg_set_regdb_offloaded;

	rx_ops->reg_rx_ops.reg_set_11d_offloaded =
		tgt_reg_set_11d_offloaded;

	rx_ops->reg_rx_ops.reg_set_6ghz_supported =
		tgt_reg_set_6ghz_supported;

	rx_ops->reg_rx_ops.reg_set_5dot9_ghz_supported =
		tgt_reg_set_5dot9_ghz_supported;

	rx_ops->reg_rx_ops.get_dfs_region =
		wlan_reg_get_dfs_region;

	rx_ops->reg_rx_ops.reg_ch_avoid_event_handler =
		tgt_reg_process_ch_avoid_event;

	rx_ops->reg_rx_ops.reg_freq_to_chan =
		wlan_reg_freq_to_chan;

	rx_ops->reg_rx_ops.reg_set_chan_144 =
		ucfg_reg_modify_chan_144;

	rx_ops->reg_rx_ops.reg_get_chan_144 =
		ucfg_reg_get_en_chan_144;

	wlan_lmac_if_umac_reg_rx_ops_register_po(rx_ops);

	rx_ops->reg_rx_ops.reg_enable_dfs_channels =
		ucfg_reg_enable_dfs_channels;

	rx_ops->reg_rx_ops.reg_modify_pdev_chan_range =
		wlan_reg_modify_pdev_chan_range;

	rx_ops->reg_rx_ops.reg_update_pdev_wireless_modes =
		wlan_reg_update_pdev_wireless_modes;

	rx_ops->reg_rx_ops.reg_is_range_only6g =
		wlan_reg_is_range_only6g;

	rx_ops->reg_rx_ops.reg_is_range_overlap_6g =
		wlan_reg_is_range_overlap_6g;

	rx_ops->reg_rx_ops.reg_ignore_fw_reg_offload_ind =
		tgt_reg_ignore_fw_reg_offload_ind;

	rx_ops->reg_rx_ops.reg_disable_chan_coex =
		wlan_reg_disable_chan_coex;

	rx_ops->reg_rx_ops.reg_get_unii_5g_bitmap =
		ucfg_reg_get_unii_5g_bitmap;

	rx_ops->reg_rx_ops.reg_set_ext_tpc_supported =
		tgt_reg_set_ext_tpc_supported;

	wlan_lmac_if_register_6g_edge_chan_supp(rx_ops);

	wlan_lmac_if_register_afc_handlers(rx_ops);

	wlan_lmac_if_register_super_chan_display(rx_ops);

	rx_ops->reg_rx_ops.reg_r2p_table_update_response_handler =
		tgt_reg_process_r2p_table_update_response;
}

#ifdef CONVERGED_P2P_ENABLE
#ifdef WLAN_FEATURE_MCC_QUOTA
static inline void
wlan_lmac_if_umac_rx_ops_register_p2p_mcc_quota(struct wlan_lmac_if_rx_ops *rx_ops)
{
	rx_ops->p2p.mcc_quota_ev_handler = tgt_p2p_mcc_quota_event_cb;
}
#else
static inline void wlan_lmac_if_umac_rx_ops_register_p2p_mcc_quota(
				struct wlan_lmac_if_rx_ops *rx_ops)
{
}
#endif

#ifdef FEATURE_P2P_LISTEN_OFFLOAD
static inline void
wlan_lmac_if_umac_rx_ops_register_p2p_listen_offload(struct wlan_lmac_if_rx_ops *rx_ops)
{
	rx_ops->p2p.lo_ev_handler = tgt_p2p_lo_event_cb;
}
#else
static inline void
wlan_lmac_if_umac_rx_ops_register_p2p_listen_offload(struct wlan_lmac_if_rx_ops *rx_ops)
{
}
#endif

static void wlan_lmac_if_umac_rx_ops_register_p2p(
				struct wlan_lmac_if_rx_ops *rx_ops)
{
	wlan_lmac_if_umac_rx_ops_register_p2p_listen_offload(rx_ops);
	rx_ops->p2p.noa_ev_handler = tgt_p2p_noa_event_cb;
	rx_ops->p2p.add_mac_addr_filter_evt_handler =
		tgt_p2p_add_mac_addr_status_event_cb;
	wlan_lmac_if_umac_rx_ops_register_p2p_mcc_quota(rx_ops);
}
#else
static void wlan_lmac_if_umac_rx_ops_register_p2p(
				struct wlan_lmac_if_rx_ops *rx_ops)
{
}
#endif

/*
 * register_precac_auto_chan_rx_ops_freq() - Register auto chan switch rx ops
 * for frequency based channel APIs.
 * rx_ops: Pointer to wlan_lmac_if_dfs_rx_ops
 */
#ifdef DFS_COMPONENT_ENABLE
#if defined(WLAN_DFS_PRECAC_AUTO_CHAN_SUPPORT) && defined(CONFIG_CHAN_FREQ_API)
static inline void
register_precac_auto_chan_rx_ops_freq(struct wlan_lmac_if_dfs_rx_ops *rx_ops)
{
	if (!rx_ops)
		return;
	rx_ops->dfs_get_precac_chan_state_for_freq =
		ucfg_dfs_get_precac_chan_state_for_freq;
}
#else
static inline void
register_precac_auto_chan_rx_ops_freq(struct wlan_lmac_if_dfs_rx_ops *rx_ops)
{
}
#endif
#endif

#ifdef DFS_COMPONENT_ENABLE
#ifdef WLAN_DFS_PRECAC_AUTO_CHAN_SUPPORT
static inline void
register_precac_auto_chan_rx_ops(struct wlan_lmac_if_dfs_rx_ops *rx_ops)
{
	if (!rx_ops)
		return;
	rx_ops->dfs_set_precac_intermediate_chan =
		ucfg_dfs_set_precac_intermediate_chan;
	rx_ops->dfs_get_precac_intermediate_chan =
		ucfg_dfs_get_precac_intermediate_chan;
}
#else
static inline void
register_precac_auto_chan_rx_ops(struct wlan_lmac_if_dfs_rx_ops *rx_ops)
{
}
#endif

/*
 * register_dfs_rx_ops_for_freq() - Register DFS rx ops for frequency based
 * channel APIs.
 * rx_ops: Pointer to wlan_lmac_if_dfs_rx_ops.
 */
#ifdef CONFIG_CHAN_FREQ_API
static void register_dfs_rx_ops_for_freq(struct wlan_lmac_if_dfs_rx_ops *rx_ops)
{
	if (!rx_ops)
		return;
	rx_ops->dfs_set_current_channel_for_freq =
		tgt_dfs_set_current_channel_for_freq;
}
#endif

/*
 * register_rcac_dfs_rx_ops() - Register DFS RX-Ops for RCAC specific
 * APIs.
 * @rx_ops: Pointer to wlan_lmac_if_dfs_rx_ops.
 */
#ifdef QCA_SUPPORT_ADFS_RCAC
static void register_rcac_dfs_rx_ops(struct wlan_lmac_if_dfs_rx_ops *rx_ops)
{
	if (!rx_ops)
		return;
	rx_ops->dfs_set_rcac_enable = ucfg_dfs_set_rcac_enable;
	rx_ops->dfs_get_rcac_enable = ucfg_dfs_get_rcac_enable;
	rx_ops->dfs_set_rcac_freq = ucfg_dfs_set_rcac_freq;
	rx_ops->dfs_get_rcac_freq = ucfg_dfs_get_rcac_freq;
	rx_ops->dfs_is_agile_rcac_enabled = ucfg_dfs_is_agile_rcac_enabled;
}
#else
static inline void
register_rcac_dfs_rx_ops(struct wlan_lmac_if_dfs_rx_ops *rx_ops)
{
}
#endif

/*
 * register_agile_dfs_rx_ops() - Register Rx-Ops for Agile Specific APIs
 * @rx_ops: Pointer to wlan_lmac_if_dfs_rx_ops.
 */
#ifdef QCA_SUPPORT_AGILE_DFS
static void register_agile_dfs_rx_ops(struct wlan_lmac_if_dfs_rx_ops *rx_ops)
{
	if (!rx_ops)
		return;
	rx_ops->dfs_agile_sm_deliver_evt = utils_dfs_agile_sm_deliver_evt;
}
#else
static inline void
register_agile_dfs_rx_ops(struct wlan_lmac_if_dfs_rx_ops *rx_ops)
{
}
#endif

#ifdef QCA_SUPPORT_DFS_CHAN_POSTNOL
/* register_dfs_chan_postnol_rx_ops() - Register DFS Rx-Ops for postNOL
 * channel change APIs.
 * @rx_ops: Pointer to wlan_lmac_if_dfs_rx_ops.
 */
static void
register_dfs_chan_postnol_rx_ops(struct wlan_lmac_if_dfs_rx_ops *rx_ops)
{
	if (!rx_ops)
		return;

	rx_ops->dfs_set_postnol_freq = ucfg_dfs_set_postnol_freq;
	rx_ops->dfs_set_postnol_mode = ucfg_dfs_set_postnol_mode;
	rx_ops->dfs_set_postnol_cfreq2 = ucfg_dfs_set_postnol_cfreq2;
	rx_ops->dfs_get_postnol_freq = ucfg_dfs_get_postnol_freq;
	rx_ops->dfs_get_postnol_mode = ucfg_dfs_get_postnol_mode;
	rx_ops->dfs_get_postnol_cfreq2 = ucfg_dfs_get_postnol_cfreq2;
}
#else
static inline void
register_dfs_chan_postnol_rx_ops(struct wlan_lmac_if_dfs_rx_ops *rx_ops)
{
}
#endif

#ifdef QCA_DFS_BW_EXPAND
/* register_dfs_bw_expand_rx_ops() - Register DFS Rx-Ops for BW Expand
 * @rx_ops: Pointer to wlan_lmac_if_dfs_rx_ops.
 */
static void
register_dfs_bw_expand_rx_ops(struct wlan_lmac_if_dfs_rx_ops *rx_ops)
{
	if (!rx_ops)
		return;

	rx_ops->dfs_set_bw_expand = ucfg_dfs_set_bw_expand;
	rx_ops->dfs_get_bw_expand = ucfg_dfs_get_bw_expand;
}
#else
static inline void
register_dfs_bw_expand_rx_ops(struct wlan_lmac_if_dfs_rx_ops *rx_ops)
{
}
#endif

#if defined(QCA_DFS_BW_PUNCTURE) && !defined(CONFIG_REG_CLIENT)
/* register_dfs_puncture_rx_ops() - Register DFS Rx-Ops for DFS puncture.
 * @rx_ops: Pointer to wlan_lmac_if_dfs_rx_ops.
 */
static void
register_dfs_puncture_rx_ops(struct wlan_lmac_if_dfs_rx_ops *rx_ops)
{
	if (!rx_ops)
		return;

	rx_ops->dfs_set_dfs_puncture = ucfg_dfs_set_dfs_puncture;
	rx_ops->dfs_get_dfs_puncture = ucfg_dfs_get_dfs_puncture;
}
#else
static inline void
register_dfs_puncture_rx_ops(struct wlan_lmac_if_dfs_rx_ops *rx_ops)
{
}
#endif

#ifdef WLAN_MGMT_RX_REO_SUPPORT
static QDF_STATUS
wlan_lmac_if_mgmt_rx_reo_rx_ops_register(
	struct wlan_lmac_if_mgmt_txrx_rx_ops *mgmt_txrx_rx_ops)
{
	struct wlan_lmac_if_mgmt_rx_reo_rx_ops *mgmt_rx_reo_rx_ops;

	mgmt_rx_reo_rx_ops = &mgmt_txrx_rx_ops->mgmt_rx_reo_rx_ops;
	mgmt_rx_reo_rx_ops->fw_consumed_event_handler =
			tgt_mgmt_rx_reo_fw_consumed_event_handler;
	mgmt_rx_reo_rx_ops->host_drop_handler =
			tgt_mgmt_rx_reo_host_drop_handler;
	mgmt_rx_reo_rx_ops->release_frames = tgt_mgmt_rx_reo_release_frames;

	return QDF_STATUS_SUCCESS;
}
#else
static QDF_STATUS
wlan_lmac_if_mgmt_rx_reo_rx_ops_register(
	struct wlan_lmac_if_mgmt_txrx_rx_ops *mgmt_txrx_rx_ops)
{
	return QDF_STATUS_SUCCESS;
}
#endif

static QDF_STATUS
wlan_lmac_if_mgmt_txrx_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
	struct wlan_lmac_if_mgmt_txrx_rx_ops *mgmt_txrx_rx_ops;

	if (!rx_ops) {
		qdf_print("lmac if rx ops pointer is NULL");
		return QDF_STATUS_E_NULL_VALUE;
	}

	/* mgmt txrx rx ops */
	mgmt_txrx_rx_ops = &rx_ops->mgmt_txrx_rx_ops;

	mgmt_txrx_rx_ops->mgmt_tx_completion_handler =
			tgt_mgmt_txrx_tx_completion_handler;
	mgmt_txrx_rx_ops->mgmt_rx_frame_handler =
			tgt_mgmt_txrx_rx_frame_handler;
	mgmt_txrx_rx_ops->mgmt_txrx_get_nbuf_from_desc_id =
			tgt_mgmt_txrx_get_nbuf_from_desc_id;
	mgmt_txrx_rx_ops->mgmt_txrx_get_peer_from_desc_id =
			tgt_mgmt_txrx_get_peer_from_desc_id;
	mgmt_txrx_rx_ops->mgmt_txrx_get_vdev_id_from_desc_id =
			tgt_mgmt_txrx_get_vdev_id_from_desc_id;
	mgmt_txrx_rx_ops->mgmt_txrx_get_free_desc_pool_count =
			tgt_mgmt_txrx_get_free_desc_pool_count;
	mgmt_txrx_rx_ops->mgmt_rx_frame_entry =
			tgt_mgmt_txrx_rx_frame_entry;

	return wlan_lmac_if_mgmt_rx_reo_rx_ops_register(mgmt_txrx_rx_ops);
}

#if defined(WLAN_DFS_PARTIAL_OFFLOAD) && defined(HOST_DFS_SPOOF_TEST)
static void dfs_action_on_status_assign(
		struct wlan_lmac_if_dfs_rx_ops *dfs_rx_ops)
{
	dfs_rx_ops->dfs_action_on_status = tgt_dfs_action_on_status_from_fw;
}
#else
static inline void dfs_action_on_status_assign(
		struct wlan_lmac_if_dfs_rx_ops *dfs_rx_ops)
{
}
#endif

static QDF_STATUS
wlan_lmac_if_umac_dfs_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
	struct wlan_lmac_if_dfs_rx_ops *dfs_rx_ops;

	dfs_rx_ops = &rx_ops->dfs_rx_ops;

	dfs_rx_ops->dfs_get_radars = tgt_dfs_get_radars;
	dfs_rx_ops->dfs_process_phyerr = tgt_dfs_process_phyerr;
	dfs_rx_ops->dfs_destroy_object = tgt_dfs_destroy_object;
	dfs_rx_ops->dfs_radar_enable = tgt_dfs_radar_enable;
	dfs_rx_ops->dfs_is_radar_enabled = tgt_dfs_is_radar_enabled;
	dfs_rx_ops->dfs_control = tgt_dfs_control;
	dfs_rx_ops->dfs_is_precac_timer_running =
		tgt_dfs_is_precac_timer_running;
	dfs_rx_ops->dfs_agile_precac_start =
		tgt_dfs_agile_precac_start;
	dfs_rx_ops->dfs_set_agile_precac_state =
		tgt_dfs_set_agile_precac_state;
	dfs_rx_ops->dfs_start_precac_timer = utils_dfs_start_precac_timer;
	dfs_rx_ops->dfs_cancel_precac_timer = utils_dfs_cancel_precac_timer;
	dfs_rx_ops->dfs_reset_adfs_config = ucfg_dfs_reset_agile_config;
	dfs_rx_ops->dfs_override_precac_timeout =
		ucfg_dfs_override_precac_timeout;
	dfs_rx_ops->dfs_set_precac_enable = ucfg_dfs_set_precac_enable;
	dfs_rx_ops->dfs_get_agile_precac_enable =
		ucfg_dfs_get_agile_precac_enable;
	dfs_rx_ops->dfs_get_override_precac_timeout =
		ucfg_dfs_get_override_precac_timeout;
	dfs_rx_ops->dfs_process_radar_ind = tgt_dfs_process_radar_ind;
	dfs_rx_ops->dfs_dfs_cac_complete_ind = tgt_dfs_cac_complete;
	dfs_rx_ops->dfs_dfs_ocac_complete_ind = tgt_dfs_ocac_complete;
	dfs_rx_ops->dfs_stop = tgt_dfs_stop;
	dfs_rx_ops->dfs_reinit_timers = ucfg_dfs_reinit_timers;
	dfs_rx_ops->dfs_enable_stadfs = tgt_dfs_enable_stadfs;
	dfs_rx_ops->dfs_is_stadfs_enabled = tgt_dfs_is_stadfs_enabled;
	dfs_rx_ops->dfs_process_phyerr_filter_offload =
		tgt_dfs_process_phyerr_filter_offload;
	dfs_rx_ops->dfs_is_phyerr_filter_offload =
		tgt_dfs_is_phyerr_filter_offload;

	dfs_action_on_status_assign(dfs_rx_ops);

	dfs_rx_ops->dfs_override_status_timeout =
		ucfg_dfs_set_override_status_timeout;
	dfs_rx_ops->dfs_get_override_status_timeout =
		ucfg_dfs_get_override_status_timeout;
	dfs_rx_ops->dfs_reset_spoof_test =
		tgt_dfs_reset_spoof_test;
	dfs_rx_ops->dfs_is_disable_radar_marking_set =
		utils_dfs_get_disable_radar_marking;
	dfs_rx_ops->dfs_set_nol_subchannel_marking =
		ucfg_dfs_set_nol_subchannel_marking;
	dfs_rx_ops->dfs_get_nol_subchannel_marking =
		ucfg_dfs_get_nol_subchannel_marking;
	dfs_rx_ops->dfs_set_bw_reduction =
		utils_dfs_bw_reduce;
	dfs_rx_ops->dfs_is_bw_reduction_needed =
		utils_dfs_is_bw_reduce;
	dfs_rx_ops->dfs_allow_hw_pulses =
		ucfg_dfs_allow_hw_pulses;
	dfs_rx_ops->dfs_is_hw_pulses_allowed =
		ucfg_dfs_is_hw_pulses_allowed;
	dfs_rx_ops->dfs_set_fw_adfs_support =
		tgt_dfs_set_fw_adfs_support;
	dfs_rx_ops->dfs_reset_dfs_prevchan =
		utils_dfs_reset_dfs_prevchan;
	dfs_rx_ops->dfs_init_tmp_psoc_nol =
		tgt_dfs_init_tmp_psoc_nol;
	dfs_rx_ops->dfs_deinit_tmp_psoc_nol =
		tgt_dfs_deinit_tmp_psoc_nol;
	dfs_rx_ops->dfs_save_dfs_nol_in_psoc =
		tgt_dfs_save_dfs_nol_in_psoc;
	dfs_rx_ops->dfs_reinit_nol_from_psoc_copy =
		tgt_dfs_reinit_nol_from_psoc_copy;
	dfs_rx_ops->dfs_reinit_precac_lists =
		tgt_dfs_reinit_precac_lists;
	dfs_rx_ops->dfs_complete_deferred_tasks =
		tgt_dfs_complete_deferred_tasks;
	register_precac_auto_chan_rx_ops(dfs_rx_ops);
	register_precac_auto_chan_rx_ops_freq(dfs_rx_ops);
	register_dfs_rx_ops_for_freq(dfs_rx_ops);
	register_rcac_dfs_rx_ops(dfs_rx_ops);
	register_agile_dfs_rx_ops(dfs_rx_ops);
	register_dfs_chan_postnol_rx_ops(dfs_rx_ops);
	register_dfs_bw_expand_rx_ops(dfs_rx_ops);
	register_dfs_puncture_rx_ops(dfs_rx_ops);

	return QDF_STATUS_SUCCESS;
}
#else
static QDF_STATUS
wlan_lmac_if_umac_dfs_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
	return QDF_STATUS_SUCCESS;
}
#endif

#ifdef FEATURE_WLAN_TDLS
static QDF_STATUS
wlan_lmac_if_umac_tdls_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
	rx_ops->tdls_rx_ops.tdls_ev_handler = tgt_tdls_event_handler;

	return QDF_STATUS_SUCCESS;
}
#else
static QDF_STATUS
wlan_lmac_if_umac_tdls_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
	return QDF_STATUS_SUCCESS;
}
#endif

#ifdef WLAN_SUPPORT_GREEN_AP
static QDF_STATUS
wlan_lmac_if_umac_green_ap_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
	rx_ops->green_ap_rx_ops.is_ps_enabled = wlan_green_ap_is_ps_enabled;
	rx_ops->green_ap_rx_ops.is_dbg_print_enabled =
					ucfg_green_ap_get_debug_prints;
	rx_ops->green_ap_rx_ops.ps_set = ucfg_green_ap_set_ps_config;
	rx_ops->green_ap_rx_ops.ps_get = ucfg_green_ap_get_ps_config;
	rx_ops->green_ap_rx_ops.suspend_handle = wlan_green_ap_suspend_handle;

	return QDF_STATUS_SUCCESS;
}
#else
static QDF_STATUS
wlan_lmac_if_umac_green_ap_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
	return QDF_STATUS_SUCCESS;
}
#endif

#ifdef QCA_WIFI_FTM
static QDF_STATUS
wlan_lmac_if_umac_ftm_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
	struct wlan_lmac_if_ftm_rx_ops *ftm_rx_ops;

	ftm_rx_ops = &rx_ops->ftm_rx_ops;

	ftm_rx_ops->ftm_ev_handler = wlan_ftm_process_utf_event;

	return QDF_STATUS_SUCCESS;
}
#else
static QDF_STATUS
wlan_lmac_if_umac_ftm_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
	return QDF_STATUS_SUCCESS;
}
#endif

#ifdef WLAN_FEATURE_11BE_MLO
#ifdef WLAN_FEATURE_11BE_MLO_ADV_FEATURE
static inline void
wlan_lmac_if_mlo_rx_link_switch_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
	rx_ops->mlo_rx_ops.mlo_link_switch_request_handler =
					mlo_mgr_link_switch_request_params;
}
#else
static inline void
wlan_lmac_if_mlo_rx_link_switch_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
}
#endif /* WLAN_FEATURE_11BE_MLO_ADV_FEATURE */

/**
 * wlan_lmac_if_mlo_mgr_rx_ops_register() - API to register mlo mgr Rx Ops
 * @rx_ops: pointer to lmac rx ops
 *
 * This API will be used to register function pointers for FW events
 *
 * Return: void
 */
static void
wlan_lmac_if_mlo_mgr_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
	/* register handler for received mlo related events */
	rx_ops->mlo_rx_ops.process_link_set_active_resp =
		mlo_process_link_set_active_resp;
	rx_ops->mlo_rx_ops.process_mlo_vdev_tid_to_link_map_event =
		wlan_mlo_vdev_tid_to_link_map_event;
	rx_ops->mlo_rx_ops.process_mlo_link_state_info_event =
		wlan_handle_ml_link_state_info_event;
	rx_ops->mlo_rx_ops.mlo_link_disable_request_handler =
		wlan_mlo_link_disable_request_handler;

	wlan_lmac_if_mlo_rx_link_switch_ops_register(rx_ops);
}
#else
static void
wlan_lmac_if_mlo_mgr_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
}
#endif /* WLAN_FEATURE_11BE_MLO */

#if defined(WLAN_SUPPORT_TWT) && defined(WLAN_TWT_CONV_SUPPORTED)
static
void wlan_lmac_if_twt_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
	tgt_twt_register_rx_ops(rx_ops);
}
#else
static
void wlan_lmac_if_twt_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
}
#endif /* WLAN_SUPPORT_TWT && WLAN_TWT_CONV_SUPPORTED */

#ifdef WLAN_FEATURE_DBAM_CONFIG
static void
wlan_lmac_if_dbam_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
	rx_ops->dbam_rx_ops.dbam_resp_event = target_if_dbam_process_event;
}
#else
static void
wlan_lmac_if_dbam_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
}
#endif /* WLAN_FEATURE_DBAM_CONFIG */

/**
 * wlan_lmac_if_umac_rx_ops_register() - UMAC rx handler register
 * @rx_ops: Pointer to rx_ops structure to be populated
 *
 * Register umac RX callabacks which will be called by DA/OL/WMA/WMI
 *
 * Return: QDF_STATUS_SUCCESS - in case of success
 */
QDF_STATUS
wlan_lmac_if_umac_rx_ops_register(struct wlan_lmac_if_rx_ops *rx_ops)
{
	/* Component specific public api's to be called to register
	 * respective callbacks
	 * Ex: rx_ops->fp = function;
	 */

	if (!rx_ops) {
		qdf_print("lmac if rx ops pointer is NULL");
		return QDF_STATUS_E_INVAL;
	}

	wlan_lmac_if_mgmt_txrx_rx_ops_register(rx_ops);

	/* scan rx ops */
	rx_ops->scan.scan_ev_handler = tgt_scan_event_handler;
	rx_ops->scan.scan_set_max_active_scans = tgt_scan_set_max_active_scans;

	wlan_lmac_if_atf_rx_ops_register(rx_ops);

	wlan_lmac_if_cp_stats_rx_ops_register(rx_ops);

	wlan_target_if_dcs_rx_ops_register(rx_ops);

	wlan_lmac_if_sa_api_rx_ops_register(rx_ops);

	wlan_lmac_if_cfr_rx_ops_register(rx_ops);

	wlan_lmac_if_crypto_rx_ops_register(rx_ops);
	/* wifi_pos rx ops */
	wlan_lmac_if_umac_rx_ops_register_wifi_pos(rx_ops);

	/* tdls rx ops */
	wlan_lmac_if_umac_tdls_rx_ops_register(rx_ops);

	wlan_lmac_if_umac_reg_rx_ops_register(rx_ops);

	/* p2p rx ops */
	wlan_lmac_if_umac_rx_ops_register_p2p(rx_ops);

	/* DFS rx_ops */
	wlan_lmac_if_umac_dfs_rx_ops_register(rx_ops);

	wlan_lmac_if_umac_green_ap_rx_ops_register(rx_ops);

	/* FTM rx_ops */
	wlan_lmac_if_umac_ftm_rx_ops_register(rx_ops);

	/* FILS Discovery */
	wlan_lmac_if_fd_rx_ops_register(rx_ops);

	/* MLME rx_ops */
	tgt_vdev_mgr_rx_ops_register(rx_ops);

	wlan_lmac_if_mlo_mgr_rx_ops_register(rx_ops);

	wlan_lmac_if_twt_rx_ops_register(rx_ops);

	wlan_lmac_if_dbam_rx_ops_register(rx_ops);

	return QDF_STATUS_SUCCESS;
}

QDF_STATUS wlan_lmac_if_set_umac_txops_registration_cb(QDF_STATUS (*handler)
				(struct wlan_lmac_if_tx_ops *))
{
	wlan_lmac_if_umac_tx_ops_register = handler;
	return QDF_STATUS_SUCCESS;
}
qdf_export_symbol(wlan_lmac_if_set_umac_txops_registration_cb);

QDF_STATUS wlan_lmac_if_set_umac_crypto_rxpn_ops_registration_cb(
		QDF_STATUS (*handler)(struct wlan_lmac_if_rx_ops *))
{
	wlan_lmac_if_umac_crypto_rxpn_ops_register = handler;
	return QDF_STATUS_SUCCESS;
}
qdf_export_symbol(wlan_lmac_if_set_umac_crypto_rxpn_ops_registration_cb);
