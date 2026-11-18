# The Sii6400 MHL/WiHD transmitter is a standalone driver. The vendor ships
# alternative host HALs; the SPI one (bbxm) is the one its kernel build uses.

ccflags-y += -DSII6400_COMBINED_FIRMWARE
ccflags-y += -DSII6400_DRIVER_VERSION=\"4.1.0\"
ccflags-y += -DSII6400_CONFIGDATA_FILE=\"/persist/sii6400.dat\"
ccflags-y += -DCONFIG_LIMIT_ON_HDMI
ccflags-y += -DENABLE_MHL_UEVENT
ccflags-y += -DOLD_KEYMAP_TABLE

sii6400drv-objs := sii6400.o sii6400_wihd.o sii6400_mhl.o isr.o
sii6400drv-objs += osal_linux_kernel.o hal_bbxm.o device.o struct_bitfield.o
sii6400drv-objs += host_msg.o state_machine.o wihd_sm.o mhl_sm.o diag_sm.o
sii6400drv-objs += rcp_inputdev.o remote_fw_update.o

obj-$(CONFIG_FB_MSM_MDSS_HDMI_MHL_SII6400) += sii6400drv.o
