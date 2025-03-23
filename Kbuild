# SPDX-License-Identifier: GPL-2.0-only
TECHPACK?=y

# Headers
LINUXINCLUDE := -I$(srctree)/techpack/camera/include/uapi/camera \
               -I$(srctree)/techpack/dataipa/drivers/platform/msm/include \
               -I$(srctree)/techpack/dataipa/drivers/platform/msm/include/uapi \
               -I$(srctree)/techpack/graphics/include \
               -I$(srctree)/techpack/mm-drivers/sync_fence/include \
               $(LINUXINCLUDE)

techpack-dirs := $(shell find $(srctree)/techpack -maxdepth 1 -mindepth 1 -type d -not -name ".*")
obj-${TECHPACK} += $(addsuffix /,$(subst $(srctree)/techpack/,,$(techpack-dirs)))