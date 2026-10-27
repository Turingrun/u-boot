/*
 * SPDX-License-Identifier:     GPL-2.0+
 *
 * (C) Copyright 2020 Rockchip Electronics Co., Ltd
 */

#include <common.h>
#include <dm.h>
#include <dwc3-uboot.h>
#include <usb.h>

#include <libfdt.h>
#include <fdt_support.h>
#include <i2c.h>
#define TCS4525_DEVICE_ADDR 0x1c
#define RK8600_DEVICE_ADDR  0x40

static int find_subnode_by_address(const void *fdt, int parent_offset, uint32_t addr)
{
    int subnode_offset;
    const fdt32_t *reg_prop;
    int len;

    fdt_for_each_subnode(subnode_offset, fdt, parent_offset) {
        reg_prop = fdt_getprop(fdt, subnode_offset, "reg", &len);
        if (reg_prop && (len >= sizeof(fdt32_t))) {
            if (fdt32_to_cpu(reg_prop[0]) == addr) {
                return subnode_offset;
            }
        }
    }

    return -FDT_ERR_NOTFOUND;
}

static int forlinx_pmic_fdt_fixup(void *fdt)
{
    struct udevice *dev;
    int ret;
    uint8_t test_byte;
    int i2c_bus_offset;
    int regulator_node_offset = -1;
    uint32_t regulator_phandle = 0;
    int i;
    char cpu_path[32];

    printf("Starting Forlinx PMIC detection...\n");

    i2c_bus_offset = fdt_path_offset(fdt, "/i2c@fdd40000");
    if (i2c_bus_offset < 0) {
        printf("Error: Cannot find I2C bus node at /i2c@fdd40000!\n");
        return -1;
    }

    ret = i2c_get_chip_for_busnum(0, TCS4525_DEVICE_ADDR, 1, &dev);
    if (ret == 0 && dm_i2c_read(dev, 0x03, &test_byte, 1) == 0) {
        printf("Detected TCS4525 PMIC on I2C bus.\n");
        regulator_node_offset = find_subnode_by_address(fdt, i2c_bus_offset, TCS4525_DEVICE_ADDR);
    } else {
        ret = i2c_get_chip_for_busnum(0, RK8600_DEVICE_ADDR, 1, &dev);
        if (ret == 0 && dm_i2c_read(dev, 0x03, &test_byte, 1) == 0) {
            printf("Detected RK8600 PMIC on I2C bus.\n");
            regulator_node_offset = find_subnode_by_address(fdt, i2c_bus_offset, RK8600_DEVICE_ADDR);
        }
    }

    if (regulator_node_offset < 0) {
        printf("Error: Found PMIC on bus, but couldn't find its node in device tree!\n");
        return -1;
    }

    const void *prop_ptr;
    int prop_len;

    prop_ptr = fdt_getprop(fdt, regulator_node_offset, "phandle", &prop_len);
    if (!prop_ptr) {
        prop_ptr = fdt_getprop(fdt, regulator_node_offset, "linux,phandle", &prop_len);
    }

    if (prop_ptr && prop_len == sizeof(uint32_t)) {
        regulator_phandle = fdt32_to_cpu(*(const uint32_t *)prop_ptr);
    }

    if (regulator_phandle == 0) {
        printf("Error: Failed to get valid phandle from the detected regulator node!\n");
        return -1;
    }

    for (i = 0; i < 4; i++) {
        sprintf(cpu_path, "/cpus/cpu@%d", i);
        int cpu_node_offset = fdt_path_offset(fdt, cpu_path);
        if (cpu_node_offset >= 0) {
            fdt_setprop_u32(fdt, cpu_node_offset, "cpu-supply", regulator_phandle);
        }
    }

    int npu_node_offset = fdt_path_offset(fdt, "/bus-npu");
    if (npu_node_offset >= 0) {
        fdt_setprop_u32(fdt, npu_node_offset, "pvtm-supply", regulator_phandle);
    }

    printf("FDT fixup complete.\n");
    return 0;
}

int ft_board_setup(void *blob, bd_t *bd)
{
    forlinx_pmic_fdt_fixup(blob);
    return 0;
}

DECLARE_GLOBAL_DATA_PTR;

int rk_board_init(void)
{
#if !defined(CONFIG_SPL_BUILD) && defined(CONFIG_ROCKCHIP_DMC_FSP)
	u32 ret = 0;
	struct udevice *dev;
	ret = uclass_get_device_by_driver(UCLASS_DMC, DM_GET_DRIVER(dmc_fsp), &dev);
	if (ret) {
		printf("dmc_fsp failed, ret=%d\n", ret);
		return 0;
	}
#endif
	return 0;
}

#ifdef CONFIG_USB_DWC3
static struct dwc3_device dwc3_device_data = {
	.maximum_speed = USB_SPEED_HIGH,
	.base = 0xfcc00000,
	.dr_mode = USB_DR_MODE_PERIPHERAL,
	.index = 0,
	.dis_u2_susphy_quirk = 1,
	.usb2_phyif_utmi_width = 16,
};

int usb_gadget_handle_interrupts(void)
{
	dwc3_uboot_handle_interrupt(0);
	return 0;
}

int board_usb_init(int index, enum usb_init_type init)
{
	return dwc3_uboot_init(&dwc3_device_data);
}
#endif
