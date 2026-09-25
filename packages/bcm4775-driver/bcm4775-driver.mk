################################################################################
#
# bcm4775-driver
#
################################################################################

BCM4775_DRIVER_VERSION = 1.0
BCM4775_DRIVER_SITE = $(NERVES_DEFCONFIG_DIR)/packages/bcm4775-driver
BCM4775_DRIVER_SITE_METHOD = local
BCM4775_DRIVER_LICENSE = GPL-2.0-only
BCM4775_DRIVER_LICENSE_FILES = src/bbd.c src/bcm_gps_spi.c
BCM4775_DRIVER_MODULE_SUBDIRS = src

$(eval $(kernel-module))
$(eval $(generic-package))

