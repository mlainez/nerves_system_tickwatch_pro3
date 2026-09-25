################################################################################
#
# android-gps-compat
#
################################################################################

ANDROID_GPS_COMPAT_VERSION = 1.0
ANDROID_GPS_COMPAT_SITE = $(NERVES_DEFCONFIG_DIR)/packages/android-gps-compat
ANDROID_GPS_COMPAT_SITE_METHOD = local
ANDROID_GPS_COMPAT_LICENSE = Apache-2.0
ANDROID_GPS_COMPAT_LICENSE_FILES = LICENSE gps_hal_bridge.c

define ANDROID_GPS_COMPAT_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -Wall -Wextra -o $(@D)/android-socket-exec \
		$(@D)/android_socket_exec.c
endef

define ANDROID_GPS_COMPAT_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/gpsctl $(TARGET_DIR)/usr/bin/gpsctl
	$(INSTALL) -D -m 0755 $(@D)/android-socket-exec \
		$(TARGET_DIR)/usr/libexec/android-socket-exec
	mkdir -p $(TARGET_DIR)/usr/libexec
	gzip -n -c $(@D)/gps-hal-bridge32 > $(TARGET_DIR)/usr/libexec/gps-hal-bridge32.gz
	mkdir -p $(TARGET_DIR)/mnt/android-system $(TARGET_DIR)/system \
		$(TARGET_DIR)/vendor $(TARGET_DIR)/data $(TARGET_DIR)/dev/socket
endef

$(eval $(generic-package))
