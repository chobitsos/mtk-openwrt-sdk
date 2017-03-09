#
# Copyright (C) 2010-2014 hua.shao@mediatek.com
#
# Ralink Property Software.
#

include $(TOPDIR)/rules.mk

PKG_NAME:=nvram
PKG_RELEASE:=1

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)
PKG_CONFIG:=NVRAM_MTD_NAME
PKG_CONFIG_DEPENDS:=$(foreach c, $(PKG_KCONFIG),$(if $(CONFIG_$c),CONFIG_$(c)))


include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/kernel.mk

define Package/nvram
  SECTION:=Ralink Properties
  CATEGORY:=Ralink Properties
  TITLE:=nvram tool
  SUBMENU:=Applications
  DEPENDS:=@TARGET_ramips +libc
  MENU:=1
endef

define Package/nvram/description
  nvram tool
endef

define Package/nvram/config
	source "$(SOURCE)/config.in"
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

TARGET_CFLAGS += \
	-I$(LINUX_DIR)/arch/mips/include\
	-I$(LINUX_DIR)/drivers/char\
	-I$(LINUX_DIR)/include\
	$(foreach c, $(PKG_KCONFIG),$(if $(CONFIG_$c),-DCONFIG_$(c)))
MAKE_FLAGS += \
        CFLAGS="$(TARGET_CFLAGS)"


define Package/nvram/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/nvram $(1)/usr/bin
endef


$(eval $(call BuildPackage,nvram))

