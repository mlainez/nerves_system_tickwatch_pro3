################################################################################
#
# nanohub-driver
#
################################################################################

NANOHUB_DRIVER_VERSION = 1.0
NANOHUB_DRIVER_SITE = $(NERVES_DEFCONFIG_DIR)/packages/nanohub-driver
NANOHUB_DRIVER_SITE_METHOD = local
NANOHUB_DRIVER_LICENSE = GPL-2.0-only AND Apache-2.0
NANOHUB_DRIVER_LICENSE_FILES = src/main.c nanohubctl.c
NANOHUB_DRIVER_MODULE_SUBDIRS = src

define NANOHUB_DRIVER_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -std=c11 -Wall -Wextra \
		-o $(@D)/nanohubctl $(@D)/nanohubctl.c
endef

define NANOHUB_DRIVER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/nanohubctl \
		$(TARGET_DIR)/usr/bin/nanohubctl
endef

$(eval $(kernel-module))
$(eval $(generic-package))
