LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
#copy to bin
$(shell mkdir -p $(TARGET_OUT)/bin)
$(shell cp -a $(LOCAL_PATH)/bin/* $(TARGET_OUT)/bin/ )

#copy to ethtool
$(shell mkdir -p $(TARGET_OUT)/ethtool)
$(shell cp -a $(LOCAL_PATH)/ethtool/* $(TARGET_OUT)/ethtool/ )

#copy busybox to xbin
$(shell mkdir -p $(TARGET_OUT)/xbin)
$(shell cp -a $(LOCAL_PATH)/xbin/* $(TARGET_OUT)/xbin/ )

#copy etc ...
$(shell mkdir -p $(TARGET_OUT)/etc/permissions)
$(shell cp -a $(LOCAL_PATH)/etc/permissions/* $(TARGET_OUT)/etc/permissions/ )

#copy app ...
$(shell mkdir -p $(TARGET_OUT)/app)
$(shell cp -a $(LOCAL_PATH)/app/* $(TARGET_OUT)/app/ )

#copy usr ...
$(shell mkdir -p $(TARGET_OUT)/usr/idc)
$(shell cp -a $(LOCAL_PATH)/usr/idc/* $(TARGET_OUT)/usr/idc/ )

#copy data...
$(shell mkdir -p $(TARGET_OUT_DATA))
$(shell cp -a $(LOCAL_PATH)/data/* $(TARGET_OUT_DATA)/ )

#copy nfc
#$(shell mkdir -p $(TARGET_OUT)/etc/param/ )
#$(shell mkdir -p $(TARGET_OUT)/vendor/firmware/ )
#$(shell cp -a $(LOCAL_PATH)/nfc/libnfc-nxp.conf $(TARGET_OUT)/etc/ )
#$(shell cp -a $(LOCAL_PATH)/nfc/libnfc-brcm.conf $(TARGET_OUT)/etc/ )
#$(shell cp -a $(LOCAL_PATH)/nfc/route.xml $(TARGET_OUT)/etc/param/ )
#$(shell cp -a $(LOCAL_PATH)/nfc/libpn547.so $(TARGET_OUT)/vendor/firmware/ )

#copy framework ...
#$(shell mkdir -p $(TARGET_OUT)/framework )
#$(shell cp -a $(LOCAL_PATH)/framework/* $(TARGET_OUT)/framework/ )

#copy lib ...
#$(shell mkdir -p $(TARGET_OUT)/lib/lib )
#$(shell cp -a $(LOCAL_PATH)/lib/lib/* $(TARGET_OUT)/lib/lib/ )

#copy apk
$(shell mkdir -p $(TARGET_OUT)/install_APKs)
$(shell cp -a $(LOCAL_PATH)/install_APKs/* $(TARGET_OUT)/install_APKs/ )


#copy logo No LOGO
$(shell mkdir -p $(TARGET_OUT)/media/images; cp -a $(LOCAL_PATH)/boot_logo/No_Logo/boot_logo $(TARGET_OUT)/media/images/ )
$(shell cp -a $(LOCAL_PATH)/boot_logo/No_Logo/bootanimation.zip $(TARGET_OUT)/media/ )
$(shell cp -a $(LOCAL_PATH)/boot_logo/No_Logo/shutanimation.zip $(TARGET_OUT)/media/ )


# copy u-blox gps
$(shell cp -a $(LOCAL_PATH)/etc/gps.conf $(TARGET_OUT)/etc/ )
$(shell cp -a $(LOCAL_PATH)/etc/u-blox.conf $(TARGET_OUT)/etc/ )
#$(shell cp -a $(LOCAL_PATH)/lib/hw/gps.default-ublox.so $(TARGET_OUT)/lib/hw/ )

# copy lib (sierra file)
#$(shell cp -a $(LOCAL_PATH)/lib/* $(TARGET_OUT)/lib/ )
