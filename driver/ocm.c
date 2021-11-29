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

#include "knacs.h"

#include <linux/genalloc.h>
#include <linux/of_device.h>
#include <linux/slab.h>

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

struct ocm_buf {
    void *virt_addr;
    size_t sz;
    refcount_t refcnt;
};

// Open and close implementation borrowed from `drivers/char/mspec.c`
static void ocm_vm_open(struct vm_area_struct *vma)
{
    struct ocm_buf *ocm_buf = vma->vm_private_data;
    refcount_inc(&ocm_buf->refcnt);
}

static void ocm_vm_close(struct vm_area_struct *vma)
{
    struct ocm_buf *ocm_buf = vma->vm_private_data;
    if (!refcount_dec_and_test(&ocm_buf->refcnt))
        return;

    gen_pool_free(ocmc_pool, (unsigned long)ocm_buf->virt_addr, ocm_buf->sz);
    kfree(ocm_buf);
}

static const struct vm_operations_struct ocm_vm_ops = {
    .open = ocm_vm_open,
    .close = ocm_vm_close,
};

int knacs_ocm_mmap(struct file *file, struct vm_area_struct *vma)
{
    if ((vma->vm_flags & (VM_SHARED | VM_MAYSHARE)) == 0)
        return -EINVAL;

    unsigned long sz = vma->vm_end - vma->vm_start;
    if (sz == 0)
        return -EINVAL;

    // Allocation logic modified from `arch/arm/mach-zynq/pm.c`
    dma_addr_t dma_addr = 0;
    void *virt_addr = gen_pool_dma_alloc_align(ocmc_pool, sz, &dma_addr, PAGE_SIZE);
    if (!virt_addr) {
        pr_debug("Unable to allocate OCM buffer\n");
        return -ENOMEM;
    }

    int ret = -ENOMEM;
    if (dma_addr == (dma_addr_t)-1) {
        pr_alert("Unable to find physical address of OCM buffer\n");
        goto failed;
    }

    struct ocm_buf *ocm_buf = kzalloc(sizeof(struct ocm_buf), GFP_KERNEL);
    if (!ocm_buf) {
        pr_alert("kalloc failed for ocm_buf\n");
        goto failed;
    }
    ocm_buf->virt_addr = virt_addr;
    ocm_buf->sz = sz;
    refcount_set(&ocm_buf->refcnt, 1);

    // mapping implementation borrowed from `drivers/char/mem.c`
    vma->vm_private_data = ocm_buf;
    vma->vm_ops = &ocm_vm_ops;
    memset(virt_addr, 0, sz);

    pr_debug("Allocated OCM buffer of size %lu @ 0x%lx\n",
             (unsigned long)sz, (unsigned long)dma_addr);

    return remap_pfn_range(vma, vma->vm_start, dma_addr >> PAGE_SHIFT,
                           sz, vma->vm_page_prot);

failed:
    gen_pool_free(ocmc_pool, (unsigned long)virt_addr, sz);
    return ret;
}
