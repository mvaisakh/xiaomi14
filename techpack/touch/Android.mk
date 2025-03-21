LOCAL_PATH := $(call my-dir)
ifneq (, $(filter $(call get-miodm-device-name), haotian))
$(info "touch driver build for $(call get-miodm-device-name)")
include $(LOCAL_PATH)/o2/Android.mk
endif

ifneq (, $(filter $(call get-miodm-device-name), dada))
$(info "touch driver build for $(call get-miodm-device-name)")
include $(LOCAL_PATH)/o2/Android.mk
endif