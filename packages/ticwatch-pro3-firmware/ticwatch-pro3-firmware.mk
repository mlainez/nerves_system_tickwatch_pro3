################################################################################
#
# ticwatch-pro3-firmware
#
################################################################################

TICWATCH_PRO3_FIRMWARE_VERSION = 1.0
TICWATCH_PRO3_FIRMWARE_SITE = $(NERVES_DEFCONFIG_DIR)/packages/ticwatch-pro3-firmware
TICWATCH_PRO3_FIRMWARE_SITE_METHOD = local
TICWATCH_PRO3_FIRMWARE_LICENSE = Apache-2.0
TICWATCH_PRO3_FIRMWARE_DEPENDENCIES = linux-firmware

define TICWATCH_PRO3_FIRMWARE_INSTALL_TARGET_CMDS
	# brcmfmac asks for brcm/brcmfmac43430-sdio.bin. linux-firmware moved
	# the 43430 blobs under cypress/ and left a symlink behind; Buildroot
	# copies named files rather than the tree, so the symlink is lost.
	# Recreate it relative, so it resolves inside the squashfs.
	mkdir -p $(TARGET_DIR)/lib/firmware/brcm
	ln -sf ../cypress/cyfmac43430-sdio.bin \
		$(TARGET_DIR)/lib/firmware/brcm/brcmfmac43430-sdio.bin
	# The regulatory (CLM) blob is requested separately and is optional —
	# brcmfmac logs a warning and carries on without it, with whatever
	# regulatory defaults are compiled into the firmware.
	ln -sf ../cypress/cyfmac43430-sdio.clm_blob \
		$(TARGET_DIR)/lib/firmware/brcm/brcmfmac43430-sdio.clm_blob

	$(INSTALL) -D -m 0755 $(@D)/ticwatch-bringup \
		$(TARGET_DIR)/usr/sbin/ticwatch-bringup
endef

$(eval $(generic-package))
