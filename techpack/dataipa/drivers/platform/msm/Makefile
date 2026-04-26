ifeq ($(KP_MODULE_ROOT),)
KP_MODULE_ROOT=$(KERNEL_SRC)/techpack/dataipa/drivers/platform/msm
endif

KBUILD_OPTIONS+=KBUILD_DTC_INCLUDE=$(KP_MODULE_ROOT)

all: modules # dtbs

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) clean

%:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) $@ $(KBUILD_OPTIONS)
