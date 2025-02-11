/*
 * Copyright (C) 2010-2014 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/**
 * @file mali_osk_mali.c
 * Implementation of the OS abstraction layer which is specific for the Mali kernel device driver
 */
#include <linux/kernel.h>
#include <asm/uaccess.h>
#include <linux/platform_device.h>
#include <linux/mali/mali_utgard.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include "mali_osk_mali.h"
#include "mali_kernel_common.h" /* MALI_xxx macros */
#include "mali_osk.h"           /* kernel side OS functions */
#include "mali_kernel_linux.h"


#define MALI_OSK_INVALID_RESOURCE_ADDRESS 0xFFFFFFFF

/**
 * Define the max number of resource we could have.
 */
#define MALI_OSK_MAX_RESOURCE_NUMBER 27

/**
 * Define the max number of resource with interrupts, and they are
 * the first 20 elements in array mali_osk_resource_bank.
 */
#define MALI_OSK_RESOURCE_WITH_IRQ_NUMBER 20

/**
 * pp core start and end location in mali_osk_resource_bank array.
 */
#define MALI_OSK_RESOURCE_PP_LOCATION_START 2
#define MALI_OSK_RESOURCE_PP_LOCATION_END 17

/**
 * L2 cache start and end location in mali_osk_resource_bank array.
 */
#define MALI_OSK_RESOURCE_L2_LOCATION_START 20
#define MALI_OSK_RESOURCE_l2_LOCATION_END 22

static _mali_osk_resource_t mali_osk_resource_bank[MALI_OSK_MAX_RESOURCE_NUMBER] = {
{.description = "Mali_GP", .base = MALI_OFFSET_GP, .irq_name = "gp",},
{.description = "Mali_GP_MMU", .base = MALI_OFFSET_GP_MMU, .irq_name = "gpmmu",},
{.description = "Mali_PP0", .base = MALI_OFFSET_PP0, .irq_name = "pp0",},
{.description = "Mali_PP0_MMU", .base = MALI_OFFSET_PP0_MMU, .irq_name = "ppmmu0",},
{.description = "Mali_PP1", .base = MALI_OFFSET_PP1, .irq_name = "pp1",},
{.description = "Mali_PP1_MMU", .base = MALI_OFFSET_PP1_MMU, .irq_name = "ppmmu1",},
{.description = "Mali_PP2", .base = MALI_OFFSET_PP2, .irq_name = "pp2",},
{.description = "Mali_PP2_MMU", .base = MALI_OFFSET_PP2_MMU, .irq_name = "ppmmu2",},
{.description = "Mali_PP3", .base = MALI_OFFSET_PP3, .irq_name = "pp3",},
{.description = "Mali_PP3_MMU", .base = MALI_OFFSET_PP3_MMU, .irq_name = "ppmmu3",},
{.description = "Mali_PP4", .base = MALI_OFFSET_PP4, .irq_name = "pp4",},
{.description = "Mali_PP4_MMU", .base = MALI_OFFSET_PP4_MMU, .irq_name = "ppmmu4",},
{.description = "Mali_PP5", .base = MALI_OFFSET_PP5, .irq_name = "pp5",},
{.description = "Mali_PP5_MMU", .base = MALI_OFFSET_PP5_MMU, .irq_name = "ppmmu05",},
{.description = "Mali_PP6", .base = MALI_OFFSET_PP6, .irq_name = "pp6",},
{.description = "Mali_PP6_MMU", .base = MALI_OFFSET_PP6_MMU, .irq_name = "ppmmu6",},
{.description = "Mali_PP7", .base = MALI_OFFSET_PP7, .irq_name = "pp7",},
{.description = "Mali_PP7_MMU", .base = MALI_OFFSET_PP7_MMU, .irq_name = "ppmmu",},
{.description = "Mali_PP_Broadcast", .base = MALI_OFFSET_PP_BCAST, .irq_name = "pp",},
{.description = "Mali_PMU", .base = MALI_OFFSET_PMU, .irq_name = "pmu",},
{.description = "Mali_L2", .base = MALI_OFFSET_L2_RESOURCE0,},
{.description = "Mali_L2", .base = MALI_OFFSET_L2_RESOURCE1,},
{.description = "Mali_L2", .base = MALI_OFFSET_L2_RESOURCE2,},
{.description = "Mali_PP_MMU_Broadcast", .base = MALI_OFFSET_PP_BCAST_MMU,},
{.description = "Mali_Broadcast", .base = MALI_OFFSET_BCAST,},
{.description = "Mali_DLBU", .base = MALI_OFFSET_DLBU,},
{.description = "Mali_DMA", .base = MALI_OFFSET_DMA,},
};

_mali_osk_errcode_t _mali_osk_resource_initialize(void)
{
	mali_bool mali_is_450 = MALI_FALSE;
	int i, pp_core_num = 0, l2_core_num = 0;
	struct resource *res;

	for (i = 0; i < MALI_OSK_RESOURCE_WITH_IRQ_NUMBER; i++) {
		res = platform_get_resource_byname(mali_platform_device, IORESOURCE_IRQ, mali_osk_resource_bank[i].irq_name);
		if (res) {
			mali_osk_resource_bank[i].irq = res->start;
			if (0 == strncmp("Mali_PP_Broadcast", mali_osk_resource_bank[i].description,
					strlen(mali_osk_resource_bank[i].description))) {
				mali_is_450 = MALI_TRUE;
			}
		} else {
			mali_osk_resource_bank[i].base = MALI_OSK_INVALID_RESOURCE_ADDRESS;
		}
	}

	for (i = MALI_OSK_RESOURCE_PP_LOCATION_START; i <= MALI_OSK_RESOURCE_PP_LOCATION_END; i++) {
		if (MALI_OSK_INVALID_RESOURCE_ADDRESS != mali_osk_resource_bank[i].base) {
			pp_core_num++;
		}
	}

	/* We have to divide by 2, because we caculate twice for only one pp(pp_core and pp_mmu_core). */
	if (0 != pp_core_num % 2) {
		MALI_DEBUG_PRINT(2, "The value of pp core number isn't normal.");
		return _MALI_OSK_ERR_FAULT;
	}

	pp_core_num /= 2;

	/**
	 * we can caculate the number of l2 cache core according the number of pp core number
	 * and device type(mali400/mali450).
	 */
	if (mali_is_450 && 4 < pp_core_num) {
		l2_core_num = 3;
	} else if (mali_is_450 && 4 >= pp_core_num) {
		l2_core_num = 2;
	} else {
		l2_core_num = 1;
	}

	for (i = MALI_OSK_RESOURCE_l2_LOCATION_END; i > MALI_OSK_RESOURCE_L2_LOCATION_START + l2_core_num - 1; i--) {
		mali_osk_resource_bank[i].base = MALI_OSK_INVALID_RESOURCE_ADDRESS;
	}

	/* If device is not mali-450 type, we have to remove related resource from resource bank. */
	if (!mali_is_450) {
		for (i = MALI_OSK_RESOURCE_l2_LOCATION_END + 1; i < MALI_OSK_MAX_RESOURCE_NUMBER; i++) {
			mali_osk_resource_bank[i].base = MALI_OSK_INVALID_RESOURCE_ADDRESS;
		}
	}

	return _MALI_OSK_ERR_OK;
}

_mali_osk_errcode_t _mali_osk_resource_find(u32 addr, _mali_osk_resource_t *res)
{
	int i;

	if (NULL == mali_platform_device) {
		return _MALI_OSK_ERR_ITEM_NOT_FOUND;
	}

	/* Traverse all of resources in resources bank to find the matching one. */
	for (i = 0; i < MALI_OSK_MAX_RESOURCE_NUMBER; i++) {
		if (mali_osk_resource_bank[i].base == addr) {
			if (NULL != res) {
				res->base = addr + _mali_osk_resource_base_address();
				res->description = mali_osk_resource_bank[i].description;
				res->irq = mali_osk_resource_bank[i].irq;
			}
			return _MALI_OSK_ERR_OK;
		}
	}

	return _MALI_OSK_ERR_ITEM_NOT_FOUND;
}

uintptr_t _mali_osk_resource_base_address(void)
{
	struct resource *reg_res = NULL;
	uintptr_t ret = 0;

	reg_res = platform_get_resource(mali_platform_device, IORESOURCE_MEM, 0);

	if (NULL != reg_res) {
		ret = reg_res->start;
	}

	return ret;
}

void _mali_osk_device_data_pmu_config_get(u16 *domain_config_array, int array_size)
{
	struct device_node *node = mali_platform_device->dev.of_node;
	struct property *prop;
	const __be32 *p;
	int length = 0, i = 0;
	u32 u;

	MALI_DEBUG_ASSERT(NULL != node);

	if (!of_get_property(node, "pmu_domain_config", &length)) {
		return;
	}

	if (array_size != length/sizeof(u32)) {
		MALI_PRINT_ERROR("Wrong pmu domain config in device tree.");
		return;
	}

	of_property_for_each_u32(node, "pmu_domain_config", prop, p, u) {
		domain_config_array[i] = (u16)u;
		i++;
	}

	return;
}

u32 _mali_osk_get_pmu_switch_delay(void)
{
	struct device_node *node = mali_platform_device->dev.of_node;
	u32 switch_delay;

	MALI_DEBUG_ASSERT(NULL != node);

	if (0 == of_property_read_u32(node, "pmu_switch_delay", &switch_delay)) {
		return switch_delay;
	} else {
		MALI_DEBUG_PRINT(2, "Couldn't find pmu_switch_delay in device tree configuration.\n");
	}

	return 0;
}

_mali_osk_errcode_t _mali_osk_device_data_get(_mali_osk_device_data *data)
{
	MALI_DEBUG_ASSERT_POINTER(data);

	if (NULL != mali_platform_device) {
		struct mali_gpu_device_data *os_data = NULL;

		os_data = (struct mali_gpu_device_data *)mali_platform_device->dev.platform_data;
		if (NULL != os_data) {
			/* Copy data from OS dependant struct to Mali neutral struct (identical!) */
			BUILD_BUG_ON(sizeof(*os_data) != sizeof(*data));
			memcpy(data, os_data, sizeof(*os_data));

			return _MALI_OSK_ERR_OK;
		}
	}

	return _MALI_OSK_ERR_ITEM_NOT_FOUND;
}

u32 _mali_osk_l2_resource_count(void)
{
	u32 l2_core_num = 0;

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(MALI_OFFSET_L2_RESOURCE0, NULL))
		l2_core_num++;

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(MALI_OFFSET_L2_RESOURCE1, NULL))
		l2_core_num++;

	if (_MALI_OSK_ERR_OK == _mali_osk_resource_find(MALI_OFFSET_L2_RESOURCE2, NULL))
		l2_core_num++;

	MALI_DEBUG_ASSERT(0 < l2_core_num);

	return l2_core_num;
}

mali_bool _mali_osk_shared_interrupts(void)
{
	u32 irqs[128];
	u32 i, j, irq, num_irqs_found = 0;

	MALI_DEBUG_ASSERT_POINTER(mali_platform_device);
	MALI_DEBUG_ASSERT(128 >= mali_platform_device->num_resources);

	for (i = 0; i < mali_platform_device->num_resources; i++) {
		if (IORESOURCE_IRQ & mali_platform_device->resource[i].flags) {
			irq = mali_platform_device->resource[i].start;

			for (j = 0; j < num_irqs_found; ++j) {
				if (irq == irqs[j]) {
					return MALI_TRUE;
				}
			}

			irqs[num_irqs_found++] = irq;
		}
	}

	return MALI_FALSE;
}
