# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2022 The Android Open Source Project
# Copyright (C) 2026 Vaisakh Murali

"""
This module contains a full list of qcom ext module
"""
load(
    "//build/kernel/kleaf:kernel.bzl",
    "kernel_module_group",
    "kernel_modules_install",
)
load(":target_variants.bzl", "get_all_variants")

def get_msm_ext_modules(target, variant):
    tv = "{}_{}".format(target, variant)
    group_name = "{}_qcom_ddk_modules".format(tv)

    kernel_module_group(
        name = group_name,
        srcs = [
            "//vendor/qcom/opensource/mmrm-driver:{}_mmrm_driver".format(tv),
            "//vendor/qcom/opensource/mm-drivers/hw_fence:{}_msm_hw_fence".format(tv),
            "//vendor/qcom/opensource/mm-drivers/msm_ext_display:{}_msm_ext_display".format(tv),
            "//vendor/qcom/opensource/mm-drivers/sync_fence:{}_sync_fence".format(tv),
            "//vendor/qcom/opensource/securemsm-kernel:{}_modules".format(tv),
            "//vendor/qcom/opensource/synx-kernel:{}_modules".format(tv),
            "//vendor/qcom/opensource/mm-sys-kernel/ubwcp:{}_ubwcp".format(tv),
            "//vendor/qcom/opensource/spu-kernel:{}_spu-drivers".format(tv),
            "//vendor/qcom/opensource/audio-kernel:{}_audio".format(tv),
            "//vendor/qcom/opensource/dsp-kernel:{}_modules".format(tv),
            "//vendor/qcom/opensource/datarmnet-ext/mem:{}_rmnet_mem".format(tv),
            "//vendor/qcom/opensource/dataipa:{}_gsim".format(tv),
            "//vendor/qcom/opensource/dataipa:{}_ipam".format(tv),
            "//vendor/qcom/opensource/datarmnet:{}_rmnet_core".format(tv),
            "//vendor/qcom/opensource/datarmnet:{}_rmnet_ctl".format(tv),
            "//vendor/qcom/opensource/datarmnet-ext/aps:{}_aps".format(tv),
            "//vendor/qcom/opensource/datarmnet-ext/offload:{}_offload".format(tv),
            "//vendor/qcom/opensource/datarmnet-ext/shs:{}_shs".format(tv),
            "//vendor/qcom/opensource/datarmnet-ext/perf:{}_perf".format(tv),
            "//vendor/qcom/opensource/datarmnet-ext/perf_tether:{}_perf_tether".format(tv),
            "//vendor/qcom/opensource/datarmnet-ext/sch:{}_sch".format(tv),
            "//vendor/qcom/opensource/datarmnet-ext/wlan:{}_wlan".format(tv),
            "//vendor/qcom/opensource/display-drivers:{}_display_drivers".format(tv),
            "//vendor/qcom/opensource/eva-kernel:{}_modules".format(tv),
            "//vendor/qcom/opensource/video-driver:{}_video_driver_modules".format(tv),
            "//vendor/qcom/opensource/graphics-kernel:{}_msm_kgsl".format(tv),
            "//vendor/qcom/opensource/wlan/platform:{}_cnss2".format(tv),
            "//vendor/qcom/opensource/wlan/platform:{}_cnss_nl".format(tv),
            "//vendor/qcom/opensource/wlan/platform:{}_cnss_plat_ipc_qmi_svc".format(tv),
            "//vendor/qcom/opensource/wlan/platform:{}_cnss_prealloc".format(tv),
            "//vendor/qcom/opensource/wlan/platform:{}_cnss_utils".format(tv),
            "//vendor/qcom/opensource/wlan/platform:{}_icnss2".format(tv),
            "//vendor/qcom/opensource/wlan/platform:{}_wlan_firmware_service".format(tv),
            "//vendor/qcom/opensource/wlan/qcacld-3.0:{}_qca_cld_kiwi-v2".format(tv),
            "//vendor/qcom/opensource/wlan/qcacld-3.0:{}_qca_cld_qca6750".format(tv),
            "//vendor/qcom/opensource/bt-kernel:{}_modules".format(tv),
            "//vendor/qcom/opensource/camera-kernel:{}_camera".format(tv),
            "//vendor/nxp/opensource/driver:{}_nxp-nci".format(tv),
            "//vendor/qcom/opensource/touch-drivers:{}_touch_drivers".format(tv),
        ],
    )

    return group_name
