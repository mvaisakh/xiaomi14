ifeq (true, $(strip $(call is-factory-build)))
TOUCH_FACTORY_SELECT:= CONFIG_TOUCH_FACTORY_SELECT=1
endif

ifneq (, $(filter $(call get-miodm-device-name), haotian))
QTS_KBUILD_OPTIONS := CONFIG_TRUSTED_TOUCH=1
TOUCH_KBUILD_OPTIONS += CONFIG_TOUCH_THP_SUPPORT=1
TOUCH_KBUILD_OPTIONS += CONFIG_TOUCH_FOD_SUPPORT=1
TOUCH_KBUILD_OPTIONS += CONFIG_TOUCH_SENSORHUB_SUPPORT=1
TOUCH_KBUILD_OPTIONS += CONFIG_TOUCH_SPI_CS_CLK_DELAY=1
endif

ifneq (, $(filter $(call get-miodm-device-name), dada))
QTS_KBUILD_OPTIONS := CONFIG_TRUSTED_TOUCH=1
TOUCH_KBUILD_OPTIONS += CONFIG_TOUCH_THP_SUPPORT=1
TOUCH_KBUILD_OPTIONS += CONFIG_TOUCH_FOD_SUPPORT=1
TOUCH_KBUILD_OPTIONS += CONFIG_TOUCH_SPI_CS_CLK_DELAY=1
endif

ifneq (, $(filter $(call get-miodm-device-name), xuanyuan))
QTS_KBUILD_OPTIONS := CONFIG_TRUSTED_TOUCH=1
TOUCH_KBUILD_OPTIONS += CONFIG_TOUCH_THP_SUPPORT=1
TOUCH_KBUILD_OPTIONS += CONFIG_TOUCH_FOD_SUPPORT=1
TOUCH_KBUILD_OPTIONS += CONFIG_TOUCH_SENSORHUB_SUPPORT=1
TOUCH_KBUILD_OPTIONS += CONFIG_TOUCH_SPI_CS_CLK_DELAY=1
endif

# Build xiaomi_touch.ko
###########################################################
# This is set once per LOCAL_PATH, not per (kernel) module
KBUILD_OPTIONS := TOUCH_ROOT=$(TOUCH_BLD_DIR)
KBUILD_OPTIONS += BOARD_PLATFORM=$(TARGET_BOARD_PLATFORM)
KBUILD_OPTIONS += $(TOUCH_FACTORY_SELECT)
KBUILD_OPTIONS += $(TOUCH_KBUILD_OPTIONS)
KBUILD_OPTIONS += $(QTS_KBUILD_OPTIONS)

#KBUILD_OPTIONS += KBUILD_EXTRA_SYMBOLS=$(PWD)/$(call intermediates-dir-for,DLKM,msm_drm-module-symvers)/Module.symvers
###########################################################

DLKM_DIR   := device/qcom/common/dlkm

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
# For incremental compilation
LOCAL_MODULE              := xiaomi_touch.ko
LOCAL_MODULE_KBUILD_NAME  := ../xiaomi/xiaomi_touch.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_OWNER := xiaomi
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)
include $(DLKM_DIR)/Build_external_kernelmodule.mk

include $(CLEAR_VARS)
# For incremental compilation
LOCAL_MODULE              := synaptics_tcm2.ko
LOCAL_MODULE_KBUILD_NAME  := synaptics_tcm2.ko
LOCAL_MODULE_TAGS         := optional
LOCAL_MODULE_DEBUG_ENABLE := true
LOCAL_MODULE_PATH         := $(KERNEL_MODULES_OUT)
include $(DLKM_DIR)/Build_external_kernelmodule.mk
