/*************************************************************************
 *   Copyright (c) 2015 - 2015 Yichao Yu <yyc1992@gmail.com>             *
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

#define pr_fmt(fmt) "KNaCs (pulse-ctl): " fmt

#include "pulse_ctrl.h"

#include <linux/of_platform.h>
#include <linux/version.h>

static struct resource *pulse_ctl_regs = NULL;

static int knacs_pulse_ctl_probe(struct platform_device *pdev)
{
    if (pulse_ctl_regs) {
        pr_alert("Only one pulse controller is allowed\n");
        return -EINVAL;
    }

    pulse_ctl_regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if  (!pulse_ctl_regs ||
         !request_mem_region(pulse_ctl_regs->start,
                             resource_size(pulse_ctl_regs),
                             "knacs-pulse-controller")) {
        pr_alert("Failed to request pulse controller registers\n");
        return -EBUSY;
    }
    pr_info("pulse controller probe\n");
    pr_info("    res->start @0x%x\n", pulse_ctl_regs->start);

    return 0;
}

static int knacs_pulse_ctl_remove(struct platform_device *pdev)
{
    if (pulse_ctl_regs) {
        release_mem_region(pulse_ctl_regs->start,
                           resource_size(pulse_ctl_regs));
        pulse_ctl_regs = NULL;
    }
    return 0;
}

static int knacs_pulse_ctl_mmap_hardcode(struct file *filp, struct vm_area_struct *vma)
{
#ifdef __arm__
    // Have the hard coded address only on ARM where we didn't use to have a working
    // device tree for our pulse controller.
    const static size_t ctrler_addr = 0x73000000;

    pr_info("Mapping hard coded controller address\n");
    unsigned long requested_size = vma->vm_end - vma->vm_start;
    if (requested_size > PAGE_SIZE) {
        pr_alert("MMap size too large for pulse controller\n");
        return -EINVAL;
    }

    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
    vma->vm_flags |= VM_IO;
#else
    vm_flags_set(vma, VM_IO);
#endif

    pr_debug("mmap pulse controller\n");

    return remap_pfn_range(vma, vma->vm_start, ctrler_addr >> PAGE_SHIFT,
                           requested_size, vma->vm_page_prot);
#else
    return -EINVAL;
#endif
}

int knacs_pulse_ctl_mmap(struct file *filp, struct vm_area_struct *vma)
{
    if (!pulse_ctl_regs)
        return knacs_pulse_ctl_mmap_hardcode(filp, vma);

    unsigned long requested_size = vma->vm_end - vma->vm_start;
    if (requested_size > resource_size(pulse_ctl_regs)) {
        pr_alert("MMap size too large for pulse controller\n");
        return -EINVAL;
    }

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0)
    vma->vm_flags |= VM_IO;
#else
    vm_flags_set(vma, VM_IO);
#endif

    pr_debug("mmap pulse controller\n");

    return remap_pfn_range(vma, vma->vm_start,
                           pulse_ctl_regs->start >> PAGE_SHIFT,
                           requested_size, vma->vm_page_prot);
}

static const struct of_device_id knacs_pulse_ctl_of_ids[] = {
    { .compatible = "xlnx,pulse-controller-5",},
    {}
};

static struct platform_driver knacs_pulse_ctl_driver = {
    .driver = {
        .name = "knacs_pulse_controller",
        .owner = THIS_MODULE,
        .of_match_table = knacs_pulse_ctl_of_ids,
    },
    .probe = knacs_pulse_ctl_probe,
    .remove = knacs_pulse_ctl_remove,
};

int __init knacs_pulse_ctl_init(void)
{
    int err = platform_driver_register(&knacs_pulse_ctl_driver);
    if (err) {
        pr_alert("Failed to register pulse controller driver\n");
        goto err;
    }
    return 0;

err:
    return err;
}

void knacs_pulse_ctl_exit(void)
{
    platform_driver_unregister(&knacs_pulse_ctl_driver);
}
