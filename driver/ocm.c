/*************************************************************************
 *   Copyright (c) 2021 - 2021 Yichao Yu <yyc1992@gmail.com>             *
 *                                                                       *
 *   This program is free software; you can redistribute it and/or       *
 *   modify it under the terms of the GNU General Public License         *
 *   as published by the Free Software Foundation; either version 2      *
 *   of the License, or (at your option) any later version.              *
 *                                                                       *
 *   This program is distributed in the hope that it will be useful,     *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of      *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the       *
 *   GNU General Public License for more details.                        *
 *                                                                       *
 *   You should have received a copy of the GNU General Public License   *
 *   along with this program; if not, write to the Free Software         *
 *   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA       *
 *   02110-1301, USA.                                                    *
 *************************************************************************/

#define pr_fmt(fmt) "KNaCs (ocm): " fmt

/**
 * This is the driver to allocate on-chip-memory (OCM) RAM to userspace.
 * On Zynq, the information about mapping to physical address space
 * is available through the OCM controller which is handled by the zynq_ocmc driver.
 * The driver creates a gen_pool to manage the memory which is how we allocate buffers.
 * The way to find this pool is copied from the zynq pm driver which uses this memory
 * to store suspend recovery code.
 *
 * The allocation interface for userspace is to mmap the `knacs` device with page offset 1.
 * The size of the allocation is determined by the mmap size and the map must be shared.
 * Multiple mappings can be made on the same fd which will result in independent allocations.
 */

#include "ocm.h"

#include "buff_alloc.h"
#include "knacs.h"

#include <linux/genalloc.h>
#include <linux/of_device.h>

const char *const ocmc_comp = "xlnx,zynq-ocmc-1.0";
static struct device_node *ocmc_dev_node = NULL;
static struct gen_pool *ocmc_pool = NULL;

int __init knacs_ocm_init(void)
{
    // Get a hold of the OCM pool.
    // Logic copied from `zynq_pm_remap_ocm` in `arch/arm/mach-zynq/pm.c`.
    // Don't fail the module loading if this fail, we'll simply fail at allocation time instead.
    ocmc_dev_node = of_find_compatible_node(NULL, NULL, ocmc_comp);
    if (!ocmc_dev_node) {
        pr_alert("Unable to find OCM controller\n");
        goto failed;
    }

    ocmc_pool = gen_pool_get(&(of_find_device_by_node(ocmc_dev_node)->dev), NULL);
    if (!ocmc_pool) {
        pr_alert("Unable to find OCM pool\n");
        goto no_pool;
    }
    pr_info("Found OCM pool\n");

    return 0;

no_pool:
    of_node_put(ocmc_dev_node);
    ocmc_dev_node = NULL;
failed:
    return 0;
}

void __exit knacs_ocm_exit(void)
{
    ocmc_pool = NULL;
    if (ocmc_dev_node)
        of_node_put(ocmc_dev_node);
    ocmc_dev_node = NULL;
}

int knacs_ocm_mmap(struct file *file, struct vm_area_struct *vma)
{
    return knacs_buff_alloc_mmap(ocmc_pool, vma, "OCM");
}
